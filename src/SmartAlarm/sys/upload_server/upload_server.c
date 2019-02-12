#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "connect_manager.h"
#include "upload_server.h"
#include "log.h"

#define BUFFER_SIZE 512
#define BODY_END    "\r\n--jqk123--\r\n"
#define BODY_HEAD  "--jqk123\r\nContent-Disposition: form-data; name=\"media\";filename=\"haha.amr\"\r\nContent-Type:application/octet-stream\r\n\r\n"
#define LOG_TAG		"UL"

static char  * http_request = NULL;
static bool  task_running_flag = false;
static char  * transfer_buff = NULL;
xQueueHandle upload_request_info_queue ;

int send_upload_req_Q(upload_info_t *buff)
{
    LOG_DBG("send_upload_req_q\n");

    if(task_running_flag == false){
        LOG_DBG("upload task running failed\n");
        return -1;
    }

    if(xQueueSend(upload_request_info_queue, buff, portMAX_DELAY) != pdTRUE){
        LOG_ERR("malloc upload Q failed\n");
        return -1;
    }

    return 0;
}

int file_size(FILE *fp)
{
    int size = 0;
    fseek(fp,0,SEEK_END);
    size = ftell(fp);
    fseek(fp,0,0);
    return size;
}

int assmble_http_post_req(sock_t sock,char * url, char *domain,char *http_request,FILE *fp)
{
    int total_size = file_size(fp);
	#if 0
    if(total_size < 0 || total_size > HTTP_BUFFER_SIZE){
    	LOG_ERR("error total_size = %d\n",total_size);
    	return -1;
    }
	#endif
    total_size += strlen(BODY_HEAD) + strlen(BODY_END);

    snprintf(http_request,HTTP_BUFFER_SIZE,\
            "POST %s HTTP/1.1\r\n"\
            "Host:%s\r\n"\
            "Connection: Keep-Alive\r\n"\
            "Accept: */*\r\n"\
            "Pragma: no-cache\r\n"\
            "Content-type: multipart/form-data;boundary=jqk123\r\n"\
            "Content-length:%d \r\n\r\n%s",url,domain,total_size,BODY_HEAD);

    total_size += strlen(http_request);

    LOG_INFO( "http_request = %s,size = %d,total_size = %d\n",http_request,strlen(http_request),total_size);
 	if (write(sock, http_request, strlen(http_request)) < 0) {
        LOG_ERR( "... socket send last body\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return HTTP_REQ_FAILED;
    }
    return total_size;
}

int upload_file(FILE *fp,sock_t sock,char*http_request,int filesize)
{   
	#if 0
    int  ret = 0; 
    char *p = http_request + strlen(http_request);

    while((ret = fread(transfer_buff,1,BUFFER_SIZE,fp)) > 0){
    	memcpy(p,transfer_buff,ret);
    	p += ret;
    }

    LOG_INFO("read file size = %d\n",(p - http_request - strlen(http_request)));

    memcpy(p,BODY_END,strlen(BODY_END));

    if (write(sock, http_request, filesize) < 0) {
        LOG_ERR( "... socket send last body\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return HTTP_REQ_FAILED;
    }
	#else
	int  ret = 0; 
	
	LOG_INFO("upload_file_start\n");
	while((ret = fread(transfer_buff,1,BUFFER_SIZE,fp)) > 0){
		if ((write(sock, transfer_buff, ret)) < 0) {
	        LOG_ERR( "... socket send last body\n");
	        vTaskDelay(1000 / portTICK_PERIOD_MS);
	        return HTTP_REQ_FAILED;
   		}
    	memset(transfer_buff,0,BUFFER_SIZE);
    }
	memset(transfer_buff,0,BUFFER_SIZE);
	memcpy(transfer_buff,BODY_END,strlen(BODY_END));
	if (write(sock, transfer_buff, strlen(BODY_END)) < 0) {
        LOG_ERR( "... socket send last body\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return HTTP_REQ_FAILED;
    }
	#endif
    LOG_INFO("upload_file_end\n");

    return LOAD_SUCC;
}

static void execute_upload_task(void *pvParameters)
{
    upload_info_t req_buff = {0};
    char domain[32] = {0};
    
    while(task_running_flag) {
        sock_t sock = 0;
        FILE *fp = NULL;
        int upload_error = LOAD_SUCC;
        int filesize = 0;
        char port[6] = {0};

        memset(http_request,0,HTTP_BUFFER_SIZE);
        memset(transfer_buff,0,BUFFER_SIZE);

        xQueueReceive(upload_request_info_queue, &req_buff, portMAX_DELAY);
            
        if(wifi_connect_status() == false){
            upload_error = WIFI_DISSCONNECT;
            goto UPLOAD_FAILED;
        }              

        upload_error = sock_get_server_info_plus(req_buff.upload_path, domain, port, NULL);
        	
        if(upload_error != LOAD_SUCC){
            goto UPLOAD_FAILED;
        }

        LOG_INFO("domain:%s,port:%s,url:%s filename:%s\n",domain,req_buff.port_number,req_buff.upload_path,req_buff.file_path_name);

        fp = fopen(req_buff.file_path_name,"rb");

        if(fp == NULL){
            upload_error = OPEN_FILE_FAILED;
            goto UPLOAD_FAILED;
        }
		if(req_buff.socket_upload > 0){
			sock = req_buff.socket_upload;
		}else{
			sock = sock_connect_plus(domain, req_buff.port_number, &upload_error);
		}
            
        if(upload_error != LOAD_SUCC){
            fclose(fp);
            goto UPLOAD_FAILED;
        }

        filesize = assmble_http_post_req(sock,req_buff.upload_path,domain,http_request,fp);

        if(filesize == -1){
            upload_error = FILE_SIZE_ERROR;
            fclose(fp);
			if(req_buff.socket_upload <= 0)
           		sock_close_plus(sock);
            goto UPLOAD_FAILED;
        }

        if(upload_file(fp,sock,http_request,filesize) == LOAD_SUCC){
            memset(http_request,0,HTTP_BUFFER_SIZE);
            read(sock, http_request,HTTP_BUFFER_SIZE);
            LOG_INFO("http_response %s\n",http_request);
        }

        fclose(fp);
		if(req_buff.socket_upload <= 0)
        	sock_close_plus(sock);
UPLOAD_FAILED:
        LOG_INFO("upload_error %d\n",upload_error);
        if(req_buff.callback != NULL)
            req_buff.callback(upload_error,http_request);
        else
            LOG_ERR("callback is NULL\n");
    }
}

int upload_task_init(void)
{	
    upload_request_info_queue = xQueueCreate(UPLOAD_REQUEST_QUEUE_SIZE, sizeof(upload_info_t));
	
	if(upload_request_info_queue != 0){
		//http_request = (char *)malloc(HTTP_BUFFER_SIZE);
		http_request = (char *)heap_caps_malloc(HTTP_BUFFER_SIZE,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if(http_request == NULL){
			vQueueDelete(upload_request_info_queue);
			LOG_ERR("http_request malloc failed\n");
			return -1;
		}

		//transfer_buff = (char *)malloc(BUFFER_SIZE);
		transfer_buff = (char *)heap_caps_malloc(BUFFER_SIZE,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if(transfer_buff == NULL){
			LOG_ERR("transfer_buff malloc failed\n");
			vQueueDelete(upload_request_info_queue);
			free(http_request);
			http_request = NULL;
			return -1;
		}

		return 0;
	}

    return -1;
}

void upload_task_uninit(void)
{
    task_running_flag = false;
    vQueueDelete(upload_request_info_queue);
	free(http_request);
	http_request = NULL;
	free(transfer_buff);
	transfer_buff = NULL;
}

void upload_server_init()
{
    if(upload_task_init() == -1){
        LOG_ERR("malloc upload xqueue failed or http_request failed!\n");
        return;
    }

 	task_running_flag = true;   

    if(xTaskCreate(&execute_upload_task, "execute_upload_task", 2560, NULL, 5, NULL) != pdPASS){
		task_running_flag = false;
		upload_task_uninit();
		LOG_ERR("--**--upload task startup failed\n");		
	}
}

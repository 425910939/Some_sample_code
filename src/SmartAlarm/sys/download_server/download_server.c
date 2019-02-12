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

#include "driver/timer.h"

#include "connect_manager.h"
#include "download_server.h"
#include "log.h"
#include "socket_plus.h"
#include "http_api.h"

#define DOWNLOAD_BUFFER_SIZE (512)
#define TOTAL_BUFFER_SIZE (1024)
#define LOG_TAG		"DL"

typedef struct{
    FILE * fp;
    sock_t sock;
    TimerHandle_t t_sock_rev;
    char * download_buff;
    uint32_t buff_size;
}recv_task_param_t;

typedef struct{
    int error_reason;
    uint32_t real_filesize;
    uint32_t http_include_filesize;
}download_finish_info_t;

static bool  task_running_flag = false;
xQueueHandle download_request_info_queue = NULL;
xQueueHandle download_finish_info_queue  = NULL;
static char * s_download_buff = NULL;
static TimerHandle_t sock_rev_timer;

int send_download_finish_Q(download_finish_info_t *buff)
{
	LOG_DBG("send_download_finish_Q\n");
    
    if(download_finish_info_queue == NULL){
        LOG_DBG("download finish info err");
        return -1;
    }

	if(xQueueSend(download_finish_info_queue, buff, portMAX_DELAY) != pdTRUE){
		LOG_DBG("malloc download Q failed\n");
		return -1;
	}
	return 0;
}

void sock_timer_cb_func()
{
    download_finish_info_t download_info = {0}; 
    LOG_DBG("sock_timer_cb_func\n");
    download_info.error_reason = HTTP_TIMEOUT;
    download_info.real_filesize = 0;
    send_download_finish_Q(&download_info);
}

int send_download_req_Q(download_info_t *buff)
{
	LOG_DBG("send_download_req_q\n");

    if(task_running_flag == false){
        LOG_DBG("download task running failed\n");
        return -1;
    }

	if(xQueueSend(download_request_info_queue, buff, portMAX_DELAY) != pdTRUE){
		LOG_DBG("malloc download Q failed\n");
		return -1;
	}
	return 0;
}

static void execute_download_task(void *pvParameters)
{
    recv_task_param_t *pthread_param = (recv_task_param_t *)pvParameters; 
    download_finish_info_t download_info = {0}; 
	char * pfile_head = NULL; 
	char * p = NULL; 
	uint32_t copy_sum = 0; 
	int r = 0; 
    uint16_t count = 0;
    uint32_t filesize = 0;

    xTimerReset(pthread_param->t_sock_rev, 0);

    if(pvParameters == NULL)
    {
        LOG_DBG("Create execute_download_task error\n" );
        download_info.error_reason = DOWNLOAD_TASK_PARAM_ERR;
        download_info.real_filesize = 0;
        download_info.http_include_filesize = 0;
        goto RESULT_RETRUN;
    }
    
    memset(pthread_param->download_buff,0,pthread_param->buff_size);

    LOG_INFO("\npthread_param->  sock %d,%p,%d\n",pthread_param->sock,pthread_param->download_buff,pthread_param->buff_size);

	while((r = sock_read_plus(pthread_param->sock, pthread_param->download_buff, pthread_param->buff_size - 1)) > 0  && count < 3) { 

        xTimerReset(pthread_param->t_sock_rev, 0);

        if(filesize == 0)
            filesize = http_get_download_filesize(pthread_param->download_buff);

        LOG_INFO("File size %u,count %d,r = %d\n",filesize,count,r);

        if(filesize == 0){
            count++;
            continue;
        }

		p = http_get_body(pthread_param->download_buff); 

		if(p != NULL){ 
			pfile_head = p; 
			copy_sum = r - (p - pthread_param->download_buff); 
            LOG_INFO("Find offset\n");
            download_info.http_include_filesize = filesize;
			break; 
		} 
        
		memset(pthread_param->download_buff,0,pthread_param->buff_size); 
        count++;
	} 

    LOG_INFO("\n r = %d, copy_sum = %d, offset = %d,count = %d\n",r,copy_sum,(p - pthread_param->download_buff),count);
	
	if(filesize == 0){
        download_info.error_reason = RECEVCE_FILESIZE_FAILED;
        download_info.real_filesize = 0;
        goto RESULT_RETRUN;
	}

    if(count >= 3){
        download_info.error_reason = NOT_FIND_HTTP_BODY_END;
        download_info.real_filesize = 0;
        goto RESULT_RETRUN;
    }

    xTimerReset(pthread_param->t_sock_rev, 0);
	if(fwrite(pfile_head,copy_sum,1,pthread_param->fp) != 1){
        download_info.real_filesize = copy_sum;
        LOG_ERR("first write failed %d\n",copy_sum);
        download_info.error_reason = WRITE_FILE_FAILED;
        goto RESULT_RETRUN;
	}

	LOG_INFO("recevie file  %d\n",copy_sum);
	memset(pthread_param->download_buff,0,pthread_param->buff_size);
    count = 0x401;

	while((r = sock_read_plus(pthread_param->sock, pthread_param->download_buff, pthread_param->buff_size - 1)) > 0){ 

        xTimerReset(pthread_param->t_sock_rev, 0);

        if(fwrite(pthread_param->download_buff,r,1,pthread_param->fp) != 1){
            LOG_ERR("file write failed %d\n",copy_sum);
            download_info.error_reason = WRITE_FILE_FAILED;
            download_info.real_filesize = copy_sum;
            send_download_finish_Q(&download_info);
            return ;
        }

        copy_sum += r;
       
        if(count++ >= 0x400){
            LOG_INFO("copy_sum %d r %d\n",copy_sum,r); 
            count = 0;
        }

        if(copy_sum == filesize){
            LOG_INFO("copy_sum = filesize break\n");
            break;
        }

        if(copy_sum > filesize){
            LOG_ERR("copy_sum > filesize\n");
            download_info.error_reason = DOWNLOAD_SIZE_OVERFLOW_ERR;
            download_info.real_filesize = copy_sum;
            goto RESULT_RETRUN;
        }

        memset(pthread_param->download_buff,0,pthread_param->buff_size);
	} 

    download_info.error_reason = LOAD_SUCC;

    if(copy_sum < download_info.http_include_filesize)
        download_info.error_reason = LOAD_HALF;
    
    download_info.real_filesize = copy_sum;
    LOG_ERR("End write real_filesize %d\n",download_info.real_filesize);


RESULT_RETRUN:
    send_download_finish_Q(&download_info);
    while(1){
        LOG_ERR("exc download not delete!\n");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }

}

static void mng_download_task(void *pvParameters)
{
    download_info_t req_buff = {0};
	
    while(task_running_flag) {
        char domain[48] = {0};
        char port[6] = {0};
        sock_t sock = 0;
        FILE *fp = NULL;
        download_finish_info_t sync_finish_info = {LOAD_SUCC,0,0};
        recv_task_param_t thread_param = {0}; 
        TaskHandle_t xDownTaskHandle = NULL;

        memset(s_download_buff,0,512);
        memset(&req_buff,0,sizeof(download_info_t));

    	xQueueReceive(download_request_info_queue, &req_buff, portMAX_DELAY);
        
        if(wifi_connect_status() == false){
            sync_finish_info.error_reason = WIFI_DISSCONNECT;
            goto DOWNLOAD_FAILED;
        }              

        LOG_INFO("Connected to AP\n");
                            
        sync_finish_info.error_reason = sock_get_server_info_plus(req_buff.download_path, domain, port, NULL);
		
        if(sync_finish_info.error_reason != LOAD_SUCC){
            goto DOWNLOAD_FAILED;
        }

        LOG_INFO("filepathname %s,domain %s, port %s ,parse_port %s\n",req_buff.file_path_name,domain,req_buff.port_number,port);
        LOG_INFO("url %s\n",req_buff.download_path);
        fp = fopen(req_buff.file_path_name,"wb+");
            
        if(fp == NULL){
            sync_finish_info.error_reason = OPEN_FILE_FAILED;
            LOG_INFO("Open File Failed\n");
            goto DOWNLOAD_FAILED;
        }
        sock = sock_connect_plus(domain, (req_buff.port_number[0] == 0?port:req_buff.port_number) , &(sync_finish_info.error_reason));

        if(sync_finish_info.error_reason != LOAD_SUCC){
            fclose(fp);
            goto DOWNLOAD_FAILED;
        }

        sock_set_read_timeout_plus(sock, 3, 0);
        sock_set_write_timeout_plus(sock, 3, 0);

        sync_finish_info.error_reason = http_send_req(sock, req_buff.download_path, domain, s_download_buff, TOTAL_BUFFER_SIZE);

        if(sync_finish_info.error_reason != LOAD_SUCC){
            fclose(fp);
            sock_close_plus(sock);
            goto DOWNLOAD_FAILED;        
        }

        thread_param.fp = fp;
        thread_param.sock = sock;
        thread_param.t_sock_rev = sock_rev_timer;
        thread_param.download_buff = s_download_buff;
        thread_param.buff_size = DOWNLOAD_BUFFER_SIZE;

        xTimerStart(sock_rev_timer,0);
        
        if(xTaskCreate(&execute_download_task, "execute_download_task", 3072, (void*)&thread_param, 2, &xDownTaskHandle) != pdPASS)
        {
            sync_finish_info.error_reason = DOWNLOAD_TASK_CREATE_ERR;
            LOG_ERR("--**--download task startup failed\n");
        }
        else{
            xQueueReceive(download_finish_info_queue, &sync_finish_info, portMAX_DELAY);
        }

        xTimerStop(sock_rev_timer, 0);

        if(xDownTaskHandle != NULL)
            vTaskDelete(xDownTaskHandle);

        xDownTaskHandle = NULL;

        fflush(fp);
        fclose(fp);
        sock_close_plus(sock);
DOWNLOAD_FAILED:
        LOG_INFO("--download_error %d, filesize %d\n",sync_finish_info.error_reason,sync_finish_info.real_filesize);

        if(req_buff.callback != NULL)
            req_buff.callback(sync_finish_info.error_reason,sync_finish_info.http_include_filesize,req_buff.file_path_name,sync_finish_info.real_filesize);
        else
            LOG_ERR("download callback is NULL\n");
        
        LOG_DBG("download again!\n");
    }
}

int download_task_init(void)
{	
    sock_rev_timer = xTimerCreate("sock_timer", pdMS_TO_TICKS(10000*3), pdTRUE, NULL, sock_timer_cb_func);
    if (sock_rev_timer == NULL)
        return -1;

    download_request_info_queue = xQueueCreate(DOWNLOAD_REQUEST_QUEUE_SIZE, sizeof(download_info_t));

    if(download_request_info_queue == NULL)
    {
        xTimerDelete(sock_rev_timer, 0);
        return -1;  
    }

    //s_download_buff = (char *)malloc(TOTAL_BUFFER_SIZE);
    s_download_buff = (char *)heap_caps_malloc(TOTAL_BUFFER_SIZE,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(s_download_buff == NULL){
        vQueueDelete(download_request_info_queue);
        LOG_ERR("s_download_buff malloc failed\n");
        xTimerDelete(sock_rev_timer, 0);
        return -1;
    }
     
    download_finish_info_queue = xQueueCreate(1, sizeof(download_finish_info_t));

    if(download_finish_info_queue == NULL){
        vQueueDelete(download_request_info_queue);
        xTimerDelete(sock_rev_timer, 0);
        free(s_download_buff);
        s_download_buff = NULL;
        return -1;  
    }

    LOG_INFO("download task init finish\n");
    return 0;
}

void download_task_uninit(void)
{
    task_running_flag = true;
    xTimerStop(sock_rev_timer, portMAX_DELAY);
    xTimerDelete(sock_rev_timer, 0);
    vQueueDelete(download_request_info_queue);
    vQueueDelete(download_finish_info_queue);
    free(s_download_buff);
    s_download_buff = NULL;
}

void download_server_init()
{
    if(download_task_init() == -1){
        LOG_ERR("malloc download xqueue failed!\n");
        return ;
    }
    
    task_running_flag = true;

    if(xTaskCreate(&mng_download_task, "mng_download_task", 3096, NULL, 8, NULL) != pdPASS){
        task_running_flag = false;
        download_task_uninit();
        LOG_ERR("--**--download manage task startup failed\n");
    }
}

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nv_interface.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "protocol.h"
#include "connect_manager.h"
#include "remote_command.h"
#include "download_server.h"
#include "log.h"
#include "system_manager.h"
#include "cJSON.h"
#include "version.h"
#include "alarms.h"

#ifdef SERVER_TEST
#define LOGIN_URL 	"http://172.16.0.145:9990/api/api/devlogin"
#define PORT_NUMBER 	"9990"
#else
#define LOGIN_URL 	"http://alarm.onegohome.com/api/api/devlogin"
#define PORT_NUMBER 	"80"
#endif

#define LOCAL_FILE_NAME 	"/sdcard/login"
#define DEFAULT_ID          "1234567890ABCDEF1234567890ABCDEF"

#define RECEVE_REMOTE_CMD_SIZE 10
#define BUFFER_SIZE 4096*3
#define ERROR_RETRY_COUNT 3
#define SYNC_DISCONNECT_TYPE (0xFFFF)

#define LOG_TAG	    "cmd"

typedef enum {
	REMOTE_CMD_LOGIN =0,
    REMOTE_CMD_INIT ,
    REMOTE_CMD_CONNECT,
    REMOTE_CMD_DISCONNECT,
} remote_cmd_all_status_t;

typedef struct{
    uint32_t task_code;
    uint16_t cmd_type;
} sync_recv_cmd_t;

static register_node_t s_register_server[REGISTER_TASK_SIZE] = {0};

static portMUX_TYPE remote_cmd_lock = portMUX_INITIALIZER_UNLOCKED;

static sock_t  s_sock;

uint16_t s_cmd_map[] = {0x0002,0x8205,0x8202,0x8207,0x8204,0x8203,0x8206,0x8208,0x8209,0x820a,0x820b,0x8201,0x820c};

xQueueHandle sendto_server_info_queue ;
xQueueHandle sync_to_recevie_queue ;
xQueueHandle get_login_info_queue ;

static remote_cmd_all_status_t s_remote_cmd_status = 0;
static char device_id[32] = {0};
bool get_server_connect_status()
{
    return ((s_remote_cmd_status == REMOTE_CMD_CONNECT ?  true:false));
}
void set_server_connect_status(int status)
{
	s_remote_cmd_status = status;
    remote_cmd_info_t req_buff = {0};
    req_buff.type = SYNC_DISCONNECT_TYPE;
    send_net_req_Q(&req_buff);
	return;
}
int send_net_req_Q(remote_cmd_info_t *buff)
{
	LOG_DBG("send_net_req_Q\n");
	if(sendto_server_info_queue && xQueueSend(sendto_server_info_queue, buff, 0) != pdTRUE){
		LOG_INFO("malloc download Q failed\n");
		return -1;
	}
	return 0;
}

uint32_t remote_cmd_register_query(void (*callback)(void *pdata))
{
    int i = 0;
    int ret = 0;
    
    portENTER_CRITICAL(&remote_cmd_lock);
    
    while(s_register_server[i].callback != NULL){
        if(s_register_server[i].callback == callback){
            ret =  s_register_server[i].register_cmd_map ;
            break;
        }
        i++;
    }
    
    portEXIT_CRITICAL(&remote_cmd_lock);
    
    return ret;
}

uint32_t remote_cmd_register(uint32_t cmd_bit, void (*callback)(void *pdata))
{
    int i = 0;

    portENTER_CRITICAL(&remote_cmd_lock);
    
    while((s_register_server[i].callback != NULL) && (i < REGISTER_TASK_SIZE)){
        if(s_register_server[i].callback == callback){
            break;
        }
        i++;
    }

    if(i >= REGISTER_TASK_SIZE){
        LOG_ERR("-*-*remote register task error %d\n",i);
    }
    else{
        s_register_server[i].callback = callback;
        s_register_server[i].register_cmd_map |= cmd_bit;
    }

    portEXIT_CRITICAL(&remote_cmd_lock);
    LOG_INFO("task code %d cmd_bit 0x%x\n",i,s_register_server[i].register_cmd_map);
    return i;
}

int remote_cmd_unregister(uint32_t task_code)
{
    if(task_code  <=  (REGISTER_TASK_SIZE -1)){
        portENTER_CRITICAL(&remote_cmd_lock);
        
        if(s_register_server[task_code].callback != 0){
            s_register_server[task_code].callback  = 0;
            s_register_server[task_code].register_cmd_map = 0;
            
            portEXIT_CRITICAL(&remote_cmd_lock);
            return 0;
        }
        
        portEXIT_CRITICAL(&remote_cmd_lock);
        return -1;
    }

    return -1;
}

int find_type_bit_offset(uint16_t type)
{
    for(int i = 0 ; i < sizeof(s_cmd_map)/sizeof(uint16_t); i++){
        if(s_cmd_map[i] == type)
            return i;
    }

    return -1;
}

char * get_device_sn(void)
{	
	return device_id;
}
int get_server_socket(void)
{
	return s_sock;
}
static bool notify_all_register_app(rx_data_t *p_data)
{
	int ret = -1;
	int i = 0;
	int type = 0;
	cJSON *root_item = NULL;
	cJSON *type_item = NULL;
	cJSON *msgid_item = NULL;
	char msgid[30];
	
	if (p_data->version <= DEVICE_HEARTBEAT_VERSION){
		type = p_data->version;
    	ret = find_type_bit_offset(DEVICE_HEARTBEAT_VERSION);
	}else if(p_data->version == DEVICE_DATA_VERSION){
		#if 1
		root_item = cJSON_Parse(p_data->data);
		if(root_item == NULL)
			return false;
		type_item = cJSON_GetObjectItem(root_item,"type");
		if(type_item == NULL){
			cJSON_Delete(root_item);
			root_item = NULL;
			return false;
		}
		type = type_item->valueint;
		ret = find_type_bit_offset(type);

		msgid_item = type_item = cJSON_GetObjectItem(root_item,"msgID");
		if(msgid_item){
			memset(msgid,0,sizeof(msgid));
			strncpy(msgid,msgid_item->valuestring,sizeof(msgid));
			if(ret != -1)
				assemble_remote_json(DEVICE_MSGID_VERSION,strlen(msgid),DEVICE_MSGID_VERSION,DEVICE_MSGID_VERSION,(char*)msgid);		
		}
		cJSON_Delete(root_item);
		root_item = NULL;
		type_item = NULL;
		msgid_item = NULL;
		#else
		if(strstr(p_data->data,"type\":")){
			char type_str[20] = {0};
			strncpy(type_str,strstr(p_data->data,"type\":")+6,6);
			type = strtol(type_str,NULL,0);
			ret = find_type_bit_offset(type);
		}else{
			LOG_ERR("Not find type\n");
		}
		#endif
	}else{
		LOG_ERR("version value err\n");
	}

    int offset = (1<< ret);
    if(ret == -1){
       LOG_DBG("send_to_app_server_req type error\n");
       return false;
    }

    while((i< REGISTER_TASK_SIZE) && (s_register_server[i].callback !=NULL )){
        if(s_register_server[i].register_cmd_map & offset){
            s_register_server[i].callback(p_data,type);
            LOG_INFO("send to app task code %d \n",i);
        }
        i++;
    }
    return true;
}

static void download_callback(download_upload_finish_reason_t finish_reason , uint32_t filesize, char * file_path_name, uint32_t write_in_file_size)
{
	login_cmd_info_t log_info = {0};
	FILE *fstream = NULL;
	char login_buff[256];
	cJSON*head_item =NULL;
	cJSON*data_item =NULL;
	cJSON*result_item =NULL;

	LOG_INFO("download_file result %d\n",finish_reason);
	log_info.finish_reason = finish_reason;
	if(finish_reason == 0){
		fstream = fopen(file_path_name,"r+");
		if(NULL == fstream){
			LOG_ERR("open get play log file err\n");
			log_info.result_code = -1;
			goto err_file;
		}
		fread(login_buff,sizeof(login_buff),1,fstream);
		ESP_LOGI("ONEGO","Login information %s",login_buff);
		head_item = cJSON_Parse(login_buff);
		if(head_item){
			result_item = cJSON_GetObjectItem(head_item, "resultCode"); 
			if(result_item)
				log_info.result_code = result_item->valueint;
			else
				log_info.result_code = -1;
			
			data_item = cJSON_GetObjectItem(head_item, "data");
			if(data_item){
				result_item = cJSON_GetObjectItem(data_item, "result");
				if(result_item){
					if(result_item->valueint == -2){
						fclose(fstream);
						unlink(file_path_name);
						restore_gactory_setting();
						return ;
					}
				}
				
				result_item = cJSON_GetObjectItem(data_item, "ip"); 
				if(result_item)
					strcpy(log_info.server_ip,result_item->valuestring);
				else
					log_info.result_code = -1;
			
				result_item = cJSON_GetObjectItem(data_item, "port");
				if(result_item)
					log_info.port = result_item->valueint;
				else
					log_info.result_code = -1;
				
				result_item = cJSON_GetObjectItem(data_item, "sid");
				if(result_item){
					strcpy(log_info.id,result_item->valuestring);
					strncpy(device_id,log_info.id,sizeof(device_id));
				}else
					log_info.result_code = -1;

			}
			if(strstr(login_buff,"timestamp")){
				char time[20] = {0};
				struct timeval tv = {0};
				strncpy(time,strstr(login_buff,"timestamp")+11,10);
				setenv("TZ", "CST-8", 1);
                tzset();
                tv.tv_sec = strtol(time,NULL,0);
                settimeofday(&tv, NULL); 
                fresh_alarm();
			}
			cJSON_Delete(head_item);
			head_item = NULL;
			data_item = NULL;
			result_item = NULL;
		}else{
			log_info.result_code = -1;
		}

		LOG_INFO("ip = %s port = %d ,sid = %s resultCode = %d\n",log_info.server_ip,log_info.port\
			,log_info.id,log_info.result_code);
		fclose(fstream);
		unlink(file_path_name);
	}
err_file:
	if(get_login_info_queue && xQueueSend(get_login_info_queue, &log_info, 100) != pdTRUE){
		LOG_ERR("send login info fail\n");
	}
	return;
}

static void get_login_info(void)
{
	
	download_info_t download_info;
	char mac[12] = {0};
	nv_item_t nv = {0};
	unsigned char *p = (unsigned char *)wifi_sta_mac_add();
    sprintf(mac, "%02X%02X%02X%02X%02X%02X", p[0],p[1],p[2],p[3],p[4],p[5]);
	nv.name = NV_ITEM_POWER_ON_MODE;
    get_nv_item(&nv);
	memset(&download_info, 0, sizeof(download_info_t));
	strncpy(&(download_info.file_path_name), LOCAL_FILE_NAME, strlen(LOCAL_FILE_NAME));
	unlink(&(download_info.file_path_name));//remove old file first
	strncpy(&(download_info.download_path), LOGIN_URL, strlen(LOGIN_URL));
	strncat(&(download_info.download_path), "?dev=", 5);
	strncat(&(download_info.download_path), mac, sizeof(mac));
	strncat(&(download_info.download_path), "&version=", 9);
	strncat(&(download_info.download_path), SOFTWARE_VERSION, strlen(SOFTWARE_VERSION));
	memset(mac, 0, sizeof(mac));
	sprintf(mac, "&mode=%d",(int)nv.value);
	strncat(&(download_info.download_path), mac, sizeof(mac));
	LOG_INFO("download path = %s\n",download_info.download_path);
	download_info.port_number[0] = 0;
	//strncpy(&(download_info.port_number), PORT_NUMBER,strlen(PORT_NUMBER));
	download_info.breakpoint = false;
	download_info.filesize = 0;
	download_info.type = 0;
	download_info.callback = download_callback;			
	send_download_req_Q(&download_info);
	return ;

}

static void remote_command_connect_recevie_task(void *pvParameters)
{
    sync_recv_cmd_t wait_response_buff = {0};
	
    while(1){
        switch(s_remote_cmd_status){
            case REMOTE_CMD_INIT:  
			case REMOTE_CMD_LOGIN:    
                if(sync_to_recevie_queue){
                	xQueueReceive(sync_to_recevie_queue, &wait_response_buff, pdMS_TO_TICKS(5000));
                }else{
					vTaskDelay(5000 / portTICK_PERIOD_MS); 
				}
                break;
            case REMOTE_CMD_CONNECT:{
				char recv_buff[BUFFER_SIZE] = {0}; 
                int ret = 0;
				int total_size = 0,r_size = 0;
                while((ret = read(s_sock, recv_buff,PACKET_HEADER_LEN)) > 0){    
                    memset(&wait_response_buff,0,sizeof(sync_recv_cmd_t));
                    xQueueReceive(sync_to_recevie_queue, &wait_response_buff, 0);
					rx_data_t * r_data = (rx_data_t*)recv_buff;
					r_data->length = BSWAP_32(r_data->length);
					LOG_INFO("read len = %d package version 0x%x size = %d\n",ret,r_data->version,r_data->length);
					
					total_size = ret - PACKET_HEADER_LEN;
					char*p = recv_buff+ret;
					while((total_size < r_data->length) && (r_data->length < sizeof(recv_buff))){
						if(total_size >= sizeof(recv_buff))
							break;
						if((r_size = read(s_sock,p,r_data->length-total_size))<0)
							break;
						p = p+r_size;
						total_size += r_size;
						LOG_INFO("read total size = %d read size = %d\n",total_size,r_size);
					}
					LOG_INFO("recevie data = %s\n",r_data->data);
					notify_all_register_app(r_data);
                    memset(recv_buff,0,BUFFER_SIZE);
                }
                LOG_INFO("remote read cmd error maybe socket disconnect %d\n",ret);
                vTaskDelay(5000 / portTICK_PERIOD_MS);  
                s_remote_cmd_status =  REMOTE_CMD_DISCONNECT;
                break;
            }
            case REMOTE_CMD_DISCONNECT:  
                while(wait_response_buff.cmd_type != 0){
                    memset(&wait_response_buff,0,sizeof(sync_recv_cmd_t));
                    xQueueReceive(sync_to_recevie_queue, &wait_response_buff, 0); //clear buff
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                }
				remote_cmd_info_t req_buff = {0};        
				req_buff.type = SYNC_DISCONNECT_TYPE;    
				send_net_req_Q(&req_buff);
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                LOG_INFO("\n2 recevie_status %d\n",s_remote_cmd_status);
                break;
        }
    }
}

static void remote_command_connect_send_task(void *pvParameters)
{
    remote_cmd_info_t req_buff = {0};
	login_cmd_info_t log_info = {0};
	char port[10] = {0};
	char ip[15] = {0};
	int i = 0,error_reason = 0;
	int s_ret = 0;
	fd_set rfds;
	struct timeval t_out;	
	
    while(1){

        if(wifi_connect_status() == false){
            if(s_remote_cmd_status == REMOTE_CMD_CONNECT){
                s_remote_cmd_status = REMOTE_CMD_DISCONNECT;
            }
			
	        LOG_INFO("wifi disconnect\n");
			
            if(s_remote_cmd_status == REMOTE_CMD_LOGIN){
				if(sendto_server_info_queue){
                	xQueueReceive(sendto_server_info_queue, &req_buff, pdMS_TO_TICKS(5000));
                }else{
					vTaskDelay(pdMS_TO_TICKS(5000)); 
				}
                LOG_INFO("wifi disconnect remote cmd init\n"); 
                continue;
            }
        }
		
       switch(s_remote_cmd_status){   
		case REMOTE_CMD_LOGIN:
			while(1){
				get_login_info();
				if((get_login_info_queue) && (xQueueReceive(get_login_info_queue, &log_info, portMAX_DELAY) == pdPASS)){
					if((log_info.finish_reason == 0) && (log_info.result_code == 0)){
						sprintf(port,"%d",log_info.port);
						strncpy(ip,log_info.server_ip,sizeof(ip));
						s_remote_cmd_status = REMOTE_CMD_INIT;
						break;
					}else{
						s_remote_cmd_status = REMOTE_CMD_LOGIN;
					}
				}
				if(sendto_server_info_queue){
                	xQueueReceive(sendto_server_info_queue, &req_buff,pdMS_TO_TICKS(1000*30));
                }else{
					vTaskDelay(pdMS_TO_TICKS(1000*30)); 
				}
			}
			break;
		
		case REMOTE_CMD_INIT:{
			error_reason = 0;
            while(req_buff.pdata != NULL){
                LOG_INFO("\nreq_buff.type %x\n",req_buff.type);
                free(req_buff.pdata);
                memset(&req_buff, 0 , sizeof(remote_cmd_info_t));
                xQueueReceive(sendto_server_info_queue, &req_buff, 0);
                vTaskDelay(10 / portTICK_PERIOD_MS);  
            }
			s_sock = sock_connect_plus(ip,port, &error_reason);
            LOG_INFO("remote_cmd_init to connect %d,%d\n",s_sock,error_reason);

            if(error_reason == 0){
                s_remote_cmd_status = REMOTE_CMD_CONNECT;
                force_handshake_with_server();
                LOG_INFO("remote init to connect\n");
            }else{
             	s_remote_cmd_status = REMOTE_CMD_LOGIN;
                vTaskDelay(5000 / portTICK_PERIOD_MS);  
            }
			break;
		}
		case REMOTE_CMD_CONNECT:{
			i = 0;
            LOG_INFO("REMOTE_CMD_CONNECT %d\n",s_sock);
            xQueueReceive(sendto_server_info_queue, &req_buff, portMAX_DELAY);
            if(s_remote_cmd_status == REMOTE_CMD_DISCONNECT){
                if(req_buff.type == SYNC_DISCONNECT_TYPE){
                    LOG_INFO("disconnect send\n");
                }
                LOG_INFO("lost socket\n");
                break;
            }
			if(req_buff.type != SYNC_DISCONNECT_TYPE){
				LOG_INFO("receive server_info 0x%x,%d\n",req_buff.type,req_buff.task_code);
	            while( i< ERROR_RETRY_COUNT){
	                LOG_INFO("send count %d\n", i);
					if ((write(s_sock, req_buff.pdata, req_buff.len)) < 0) {
	                    LOG_ERR("... socket send failed\n");
	                    vTaskDelay(4000 / portTICK_PERIOD_MS);  
	                    s_remote_cmd_status = REMOTE_CMD_DISCONNECT;
	                }else{
	                    break;
	                }
	                i++;
	            }
			}
			if(i >= ERROR_RETRY_COUNT){
                LOG_INFO("send count finish\n");
                s_remote_cmd_status = REMOTE_CMD_DISCONNECT;
            }else{
             	s_remote_cmd_status = REMOTE_CMD_CONNECT;
				if(req_buff.type == 0x02){
	             	sync_recv_cmd_t transfer_to_recv_task_info = {0};
	         		transfer_to_recv_task_info.cmd_type =  req_buff.type;
	                transfer_to_recv_task_info.task_code = req_buff.task_code;
	                LOG_INFO("send to recevie finish\n");
	                if(xQueueSend(sync_to_recevie_queue, &transfer_to_recv_task_info, 0) != pdTRUE){
	                    LOG_INFO("send to recv Q failed\n");
	                } 
					FD_ZERO(&rfds);
					FD_SET(s_sock,&rfds);
					t_out.tv_sec = 30;
					t_out.tv_usec = 0;
					s_ret = select(s_sock+1,&rfds,NULL,NULL,&t_out);
					LOG_INFO("socket status %s\n",s_ret==0?"time out":"ready");
					if(s_ret == 0)
						s_remote_cmd_status = REMOTE_CMD_DISCONNECT;
				}
            }    
			LOG_INFO("send  finished\n");
            free(req_buff.pdata);
			req_buff.pdata = NULL;
            memset(&req_buff, 0 , sizeof(remote_cmd_info_t));
			break;
		}	
		case REMOTE_CMD_DISCONNECT:
			s_remote_cmd_status = REMOTE_CMD_LOGIN;
            free(req_buff.pdata);
			req_buff.pdata = NULL;
            memset(&req_buff, 0 , sizeof(remote_cmd_info_t));
            sock_close_plus(s_sock);
            LOG_INFO("send REMOTE_CMD_DISCONNECT\n"); 
			break;
			
		default:
			break;
	   }
    }
}

int recv_task_init()
{
    LOG_INFO("recv_task_init\n");
    sync_to_recevie_queue = xQueueCreate(RECEVE_REMOTE_CMD_SIZE, sizeof(sync_recv_cmd_t));
    return (sync_to_recevie_queue == 0 ? -1: 0);
}

void recv_task_uninit(void)
{
    vQueueDelete(sync_to_recevie_queue);
}

int send_task_init(void)
{	
     LOG_INFO("send_task_init\n");
    sendto_server_info_queue = xQueueCreate(RECEVE_REMOTE_CMD_SIZE, sizeof(remote_cmd_info_t));
    return (sendto_server_info_queue == 0 ? -1: 0);
}

void send_task_uninit(void)
{
    vQueueDelete(sendto_server_info_queue);
}

int remote_command_init()
{
    if((recv_task_init() != -1) && (send_task_init() != -1)){
		get_login_info_queue = xQueueCreate(20, sizeof(login_cmd_info_t));
        xTaskCreate(&remote_command_connect_send_task, "remote_send", 1024*4, NULL, 8, NULL);
        xTaskCreate(&remote_command_connect_recevie_task, "remote_rec", 4096*4, NULL, 8, NULL);
    }else{
        LOG_ERR("remote_command_init failed\n");
        return -1;
    }
	
    return 0;
}


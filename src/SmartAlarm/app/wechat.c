#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "rom/queue.h"
#include "hwcrypto/sha.h"
#include "connect_manager.h"
#include "download_server.h"
#include "upload_server.h"
#include "audiomanagerservice.h"
#include "remote_command.h"
#include "keyevent.h"
#include "esp_log.h"
#include "wechat.h"
#include "log.h"
#include "protocol.h"
#include "system_manager.h"
#include "led.h"
#include "application.h"
#include "nv_interface.h"

#define TIMER_TYPE_AUDIO		(0)
#define TIMER_TYPE_DOWNLOAD		(TIMER_TYPE_AUDIO + 1)
#define TIMER_TYPE_UPLOAD		(TIMER_TYPE_DOWNLOAD + 1)

#define FILE_PATH_MAX_SIZE		(128)
#define WECHAT_QUEUE_SIZE		(50)
#define MAX_RECORD_MS			(15*1000)

#define WECHAT_HTTP_ADDRESS_MAX_SIZE		(240)//head+mediaid+token

#ifdef SERVER_TEST
#define WECHAT_UPLOAD_HEAD 	"http://172.16.0.145:9990/api/test/upload?mac="
#define WECHAT_UPLOAD_PORT 	"9990"
#else
#define WECHAT_UPLOAD_HEAD 	"http://alarm.onegohome.com/api/test/upload?mac="
#define WECHAT_UPLOAD_PORT 	"80"
#endif


#define LOG_TAG		"wechat"

typedef enum{
	/*state for send msg*/
	WECHAT_SEND_IDLE = 0x00,
	WECHAT_SEND_START_RECORD,
	WECHAT_SEND_FINISH_RECORD,
	WECHAT_SEND_START_UPLOAD,
	WECHAT_SEND_FINISH_UPLOAD,
	/*state for receive msg*/
	WECHAT_RECV_IDLE,
	WECHAT_RECV_START_DOWNLOAD,
	WECHAT_RECV_FINISH_DOWNLOAD,
	WECHAT_RECV_START_PLAY,
	WECHAT_RECV_FINISH_PLAY,
}wechat_action_state;

typedef enum{
	WECHAT_ACTION_T_SEND = 0x00,
	WECHAT_ACTION_T_RECV,
	WECHAT_ACTION_T_UNDEFINE,
}wechat_action_type;
#pragma pack(1)

typedef struct{
	wechat_action_type type;
	wechat_action_state state;
	uint32_t error_code;
	char msg_id[30];
	char local_path[FILE_PATH_MAX_SIZE];
	char remote_path[WECHAT_HTTP_ADDRESS_MAX_SIZE];
	struct wechat_action_t *next;
}wechat_action_t;

typedef struct{
	wechat_action_t *current;
	wechat_action_t *previous;
	TimerHandle_t timer;
}wechat_worker_t;

typedef struct{
	wechat_worker_t record_worker;
	wechat_worker_t play_worker;
	wechat_worker_t upload_worker;
	wechat_worker_t download_worker;
	upload_info_t	upload_info; //upload handler
	download_info_t download_info; //download handler
	uint32_t command_handler; //command handler
}wechat_governor;

#pragma pack()

static SemaphoreHandle_t task_exit;
static xQueueHandle wechat_queue;
static bool task_need_run = true;
static keycode_client_t *kclient;
static wechat_governor g_wechat_governor;
static TimerHandle_t key_timer;

static inline void add_action_head(wechat_worker_t *worker, wechat_action_t *action)
{
	if(worker->current)
		action->next = worker->current;
	else
		action->next = NULL;
	worker->current = action;
	return;
}

static inline void add_action_tail(wechat_worker_t *worker, wechat_action_t *action)
{
	wechat_action_t *node = worker->current;

	if(!worker->current){
		worker->current = action;
		action->next = NULL;
	}else{	
		while(node->next)
			node = node->next;
		node->next = action;
		action->next = NULL;
	}
	return;
}

static inline void remove_action(wechat_worker_t *worker, wechat_action_t *action)
{
	wechat_action_t *node = worker->current;

	if(worker->current == action){
		worker->current = action->next;
	}else{
		while(node->next){
			if(node->next == action){
				node->next = action->next;
				break;
			}
			node = node->next;
		}
	}
	return;
}
static inline void add_action_previous(wechat_worker_t *worker, wechat_action_t *action)
{
	wechat_action_t *node = worker->previous;
	wechat_action_t * temp =NULL;
	//ESP_LOGI("ONEGO","add previous type = %d state = %d local_path = %s",action->type,action->state,action->local_path);
	if(!worker->previous){
		worker->previous = action;
		action->next = NULL;
	}else{	
		while (node){
	        temp = node->next;
	        free(node);
	        node = temp;
	    }
		node = NULL;
		
		worker->previous = action;
		action->next = NULL;
	}
	return;
}

static inline void wechat_notify(wechat_cmd_node *node, uint32_t delay)
{
	//LOG_DBG("send node\n");
	xQueueSend(wechat_queue, node, delay);
	//return 0;
}

static void upload_callback(download_upload_finish_reason_t finish_reason)
{
	wechat_cmd_node node;
	//success state change to dload finish else change to dload error
	LOG_INFO("upload_file result %d\n",finish_reason);
	node.cmd_value = WECHAT_CMD_UPLOAD_FINISH;
	node.param = (void *)finish_reason;
	node.size = 0;
	wechat_notify(&node, 100);
	return;
}

static void download_callback(download_upload_finish_reason_t finish_reason , uint32_t filesize, char * file_path_name, uint32_t write_in_file_size)
{
	wechat_cmd_node node;
	//success state change to dload finish else change to dload error
	LOG_INFO("download_file result %d\n",finish_reason);
	node.cmd_value = WECHAT_CMD_DOWNLOAD_FINISH;
	node.param = (void *)finish_reason;
	node.size = 0;
	wechat_notify(&node, 100);
	return;
}

static void wechat_keytimer_cb(TimerHandle_t xTimer)
{
	wechat_cmd_node node;
	wechat_worker_t *r_worker = &(g_wechat_governor.record_worker);
	if(!r_worker->current){
		node.cmd_value = WECHAT_CMD_RECORD_START;
		node.param = NULL;
		node.size = 0;
		wechat_notify(&node, 100);
	}else{
		LOG_INFO("record is working, no need to restart\n");
	}
}

static keyprocess_t wechat_keyprocess(keyevent_t *event)
{
	wechat_cmd_node node;
	wechat_worker_t *r_worker = &(g_wechat_governor.record_worker);
	wechat_worker_t *p_worker = &(g_wechat_governor.play_worker);

	LOG_DBG("wechat recevice key event %d type %d\n", event->code, event->type);
	if(event->code == KEY_CODE_WECHAT){
		if(event->type == KEY_EVNET_PRESS)
		{
			if(wifi_connect_status() == false){
				play_tone_sync(NOTIFY_AUDIO_NOT_CONNECT);
			}else{
				if(xTimerIsTimerActive(key_timer) != pdFALSE){
					xTimerReset(key_timer, 0);
				}else{
					xTimerStart(key_timer, 0);
				}
			}
		}else if(event->type == KEY_EVNET_RELEASE){
			
			if(xTimerIsTimerActive(key_timer) != pdFALSE){
				xTimerStop(key_timer, 0);
			}
			if(wifi_connect_status()){
				if(r_worker->current){
					node.cmd_value = WECHAT_CMD_RECORD_FINISH;
					node.param = NULL;
					node.size = 0;
					wechat_notify(&node, 100);	
				}else if(p_worker->current){
					set_front_application(APP_WECHAT);		
					node.cmd_value = WECHAT_CMD_PLAY_START;
					node.param = NULL;				
					node.size = 0;			
					wechat_notify(&node, 100);
				}else if(p_worker->previous){
					set_front_application(APP_WECHAT);		
					node.cmd_value = WECHAT_CMD_PLAY_PREVIOUS;
					node.param = NULL;				
					node.size = 0;			
					wechat_notify(&node, 100);
				}else{	
					LOG_INFO("ignore key code\n");
				}
			}
		}else{
			LOG_INFO("unsupport key event (%d) ignore it\n", event->type);
		}
	}else{
		LOG_ERR("receive unexpect key (%d) event (%d)\n", event->code, event->type);
	}
	return KEY_PROCESS_PUBLIC;
}

static void wechat_command_cb(void *p_data,int cmd_type)
{
	rx_data_t *cmd = (rx_data_t *)p_data;
	int buff_size;
	wechat_cmd_node node;

	int type = cmd_type;
	buff_size = cmd->length+sizeof(cmd->length)+2;
	LOG_DBG("recv command type-%d len-%d\n", type, cmd->length);
	
	cmd = (rx_data_t *)heap_caps_malloc(buff_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if(cmd){
		memcpy(cmd, p_data, buff_size);
		switch(type){
			case S2C_WECHAT_MSG_PUSH://new wechat msg info
				node.cmd_value = WECHAT_CMD_RECV_MSG;
				node.param = (void *)cmd;
				node.size = buff_size;
				wechat_notify(&node, 100);
				break;
			default:
				LOG_ERR("unspport command coming, ignore it\n");
				free(cmd);
				cmd = NULL;
				break;
		}
	}else{
		LOG_ERR("malloc command buff fail\n");
	}
			
	return;
}
static int record_time = 0;
static void wechat_timer_cb(TimerHandle_t xTimer)
{
	#if 0
	int type = (int) pvTimerGetTimerID(xTimer);//type 0 - audio 1 - download 2 - upload
	wechat_cmd_node node;
	
	node.cmd_value = WECHAT_CMD_TIME_OUT;
	node.param = (void *)type;
	node.size = 0;
	wechat_notify(&node, 100);
	#else
	wechat_cmd_node node ={0};
	record_time++;
	if(record_time>=150){
		node.cmd_value = WECHAT_CMD_RECORD_TIMEOUT;
		node.param = NULL;
		node.size = 0;
		wechat_notify(&node, 100);		
	}
	#endif
}

static void wechat_audio_play_cb(NotifyStatus ret,AudioAppType type,AudioManagerMsg cmd, void *param)
{
	wechat_cmd_node node;
	
	switch((NotifyStatus)ret){
		case NOTIFY_STATUS_PLAY_FINISH:
			LOG_INFO("wechat play notify finish\n");
			node.cmd_value = WECHAT_CMD_PLAY_FINISH;
			node.param = (void *)ret;
			node.size = 0;
			wechat_notify(&node, 100);
			break;
		case NOTIFY_STATUS_PLAY_ERROR:
			LOG_INFO("wechat play notify error\n");
			node.cmd_value = WECHAT_CMD_PLAY_FINISH;
			node.param = (void *)ret;
			node.size = 0;
			wechat_notify(&node, 100);
			break;
		case NOTIFY_STATUS_PLAY_DISTURBED:
			LOG_INFO("wechat play notify disturbed\n");
			node.cmd_value = WECHAT_CMD_PLAY_ERROR;
			node.param = (void *)ret;
			node.size = 0;
			wechat_notify(&node, 100);
			break;
		default:
			LOG_INFO("wechat play notify (%d) ignore\n", ret);
			break;
	}
	return;
}

static void wechat_audio_record_cb(NotifyStatus ret,AudioAppType type,AudioManagerMsg cmd, void *param)
{
	wechat_cmd_node node;

	LOG_INFO("record callback %d\n", ret);
	node.cmd_value = WECHAT_CMD_RECORD_ERROR;
	node.param = (void *)ret;
	node.size = 0;
	wechat_notify(&node, 100);
}

static char *convert_url_to_filename(char *url, int url_length)
{
	char *pos = NULL;

	if(!url){
		LOG_ERR("url is NULL pointer\n");
		return "NULL";
	}

    do{  
        if(*url == '/')  
			pos = url;
    }while(*url++ && url_length--);  

	return ++pos;
}
static int wechat_msg_finish(char *id)
{	
	cJSON * json_item = NULL;
	cJSON *data = NULL;
	char *json_data = NULL;

	json_item = cJSON_CreateObject();
	cJSON_AddNumberToObject(json_item, "type", S2C_WECHAT_FINISH);
	cJSON_AddItemToObject(json_item, "data", data = cJSON_CreateObject());
	cJSON_AddStringToObject(data,"id",id);
	
	json_data = cJSON_Print(json_item);
	assemble_remote_json(DEVICE_DATA_VERSION,strlen(json_data),g_wechat_governor.command_handler,S2C_WECHAT_FINISH,json_data);
	cJSON_Delete(json_item);
	json_item = NULL;
	free(json_data);
	json_data = NULL;
	
	return 0;
}

static int convert_url_to_portnumber(char *url, int url_length, char *port, int port_size)
{
	bool vaild = false;
	char *start_pos = NULL;
	char *stop_pos = NULL;
	char *pos = NULL;
	int size;
	
	if(!url || !port){
		LOG_ERR("(%s) (%s) is NULL pointer\n", url?" ":"url", port?" ":"port");
		return -ESP_ERR_INVALID_ARG;
	}

	do{  
		if(*url == ':')
			break;
	}while(*url++ && url_length--);//ignore 1st ¡®:¡¯, avoid https:// & http://

	while(*url++ && url_length--){
		if(*url == ':' && !start_pos){
			start_pos = url + 1;
		}
		if(*url == '/' && start_pos){
			stop_pos = url;
			vaild = true;
			break;
		}
	}

	if(vaild){//check every char is number
		for(pos = start_pos; pos <stop_pos; pos++){
			if(*pos < '0' && *pos > '9'){
				vaild = false;
			}
		}
	}

	if(vaild){
		size = stop_pos - start_pos;
		strncpy(port, start_pos, (size>port_size)?port_size:size);
	}else{
		strncpy(port, "80", port_size);
	}
	//LOG_DBG("convert port num (%s)\n", port);
	return 0;
}

static void wechat_task(void *param)
{
	static unsigned char record_file_id = 0;
	wechat_cmd_node node;
	wechat_worker_t *r_worker = &(g_wechat_governor.record_worker);
	wechat_worker_t *p_worker = &(g_wechat_governor.play_worker);
	wechat_worker_t *u_worker = &(g_wechat_governor.upload_worker);	
	wechat_worker_t *d_worker = &(g_wechat_governor.download_worker);
	wechat_action_t *action;
	rx_data_t *cmd;
	char mac[12] = {0};
	static uint8_t upload_err_cnt =0;
	static uint8_t download_err_cnt =0;
	play_param_t play_param;
	cJSON* wechat_json = NULL;
	cJSON* data_json = NULL;
	cJSON* url_json = NULL;
	cJSON* id_json = NULL;

	xSemaphoreTake(task_exit, portMAX_DELAY);
	while(task_need_run){
		memset(&node, 0, sizeof(wechat_cmd_node));
        if(pdTRUE == xQueueReceive(wechat_queue, &node, portMAX_DELAY)){
			LOG_DBG("receive cmd %d\n", node.cmd_value);
			switch(node.cmd_value){		
					/*==============================================*/
					/*=================aduio process================*/
					/*==============================================*/
				case WECHAT_CMD_RECORD_START:
					if(!r_worker->current){
						action = (wechat_action_t *)heap_caps_malloc(sizeof(wechat_action_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
						if(action){
							add_action_head(r_worker, action);
							action->type = WECHAT_ACTION_T_SEND;
							action->state = WECHAT_SEND_START_RECORD;
							action->error_code = 0;
							snprintf(action->local_path, FILE_PATH_MAX_SIZE, "/sdcard/wechat/record%d.amr", record_file_id++);
							memset(mac,0,sizeof(mac));
							unsigned char *p = (unsigned char *)wifi_sta_mac_add();
						    sprintf(mac, "%02x%02x%02x%02x%02x%02x", p[0],p[1],p[2],p[3],p[4],p[5]);
							strncpy(action->remote_path, WECHAT_UPLOAD_HEAD, WECHAT_HTTP_ADDRESS_MAX_SIZE);
							strncat(action->remote_path,mac,12);
							if(!node.param)
								play_tone_sync(NOTIFY_AUDIO_WECHAT_STARTRECORD);
						  	record_start(AUDIO_APP_WECHAT, (void *)action->local_path, wechat_audio_record_cb, NULL);
							record_time = 0;
							xTimerStart(r_worker->timer, 0);
						}else{
							LOG_ERR("malloc action buff failed\n");
						}
					
					}else{
						LOG_INFO("record is working, ignore start command\n");
					}
					break;
				case WECHAT_CMD_RECORD_ERROR:
					action = r_worker->current;
					if(action){
						xTimerStop(r_worker->timer, 0);
						switch(action->state){
							case WECHAT_SEND_START_RECORD://record error or exceed
								if((NotifyStatus)node.param == NOTIFY_STATUS_RECORD_EXCEED){//file exceed
									action->state = WECHAT_SEND_FINISH_RECORD;
									node.param = NOTIFY_STATUS_RECORD_STOPPED;
								}else{//record error
									LOG_ERR("unfortunately record process error %d, free action\n", (int)node.param);
									remove_action(r_worker, action);
									free(action);
									action = NULL;
									node.cmd_value = WECHAT_CMD_RECORD_START;
									node.param = (void*)1;
									node.size = 1;
									wechat_notify(&node, 100);
									break;
								}
							case WECHAT_SEND_FINISH_RECORD://record stop callback
								LOG_INFO("record finish result %d\n", (int)node.param);
								remove_action(r_worker, action);
								if(NOTIFY_STATUS_RECORD_STOPPED == (NotifyStatus)(node.param)){
									add_action_tail(u_worker, action);
									node.cmd_value = WECHAT_CMD_UPLOAD_START;
									node.param = NULL;
									node.size = 0;
									wechat_notify(&node, 100);
								}else{
									LOG_ERR("unfortunately record stop error %d, free action\n", (int)node.param);
									free(action);
									action = NULL;
								}
								break;
							default:
								LOG_ERR("warning! undefine state can not reach here\n");
								break;
						}
					}else{
						LOG_INFO("%s action type is %d\n", (action?"prev":"no"), (action?action->type:(-1)));
					}
					break;
				case WECHAT_CMD_RECORD_FINISH:
					action = r_worker->current;
					if(action){
						xTimerStop(r_worker->timer, 0);
						play_stop(AUDIO_APP_NOTIFY);//if current play tone cancel it & stop record
						record_stop(AUDIO_APP_WECHAT);
						action->state = WECHAT_SEND_FINISH_RECORD;//wait record callback notify result
					}else{
						LOG_INFO("no record is working\n");
					}
					break;
				case WECHAT_CMD_RECORD_TIMEOUT:
					action = r_worker->current;
					if(action){
						xTimerStop(r_worker->timer, 0);
						record_stop(AUDIO_APP_WECHAT);
						action->state = WECHAT_SEND_FINISH_RECORD;//wait record callback notify result
					}else{
						LOG_INFO("no record is working\n");
					}
					break;
				case WECHAT_CMD_PLAY_START:
					action = p_worker->current;
					if(action){
						if(action->state == WECHAT_RECV_FINISH_DOWNLOAD){
							LOG_INFO("audio action play start, path %s\n", action->local_path);
							play_param.play_app_type = AUDIO_APP_WECHAT;
							play_param.is_local_file = true;
							play_param.uri = action->local_path;
							play_param.tone = NULL;
							play_param.cb = wechat_audio_play_cb;
							play_param.cb_param = NULL;
							play_start(&play_param);
							action->state = WECHAT_RECV_START_PLAY;
						}else{//record or perv play not finish
							LOG_INFO("prev play not finish state %d\n",  action->state);
						}
					}else{
						LOG_INFO("no audio action to play\n");
					}
					break;
				case WECHAT_CMD_PLAY_FINISH:
					action = p_worker->current;
					if(action){
						LOG_INFO("audio action play stop\n");
						//play_stop(AUDIO_APP_WECHAT);
						wechat_msg_finish(action->msg_id);
						action->state = WECHAT_RECV_FINISH_PLAY;
						remove_action(p_worker, action);
						add_action_previous(p_worker, action);
						//free(action);
						if(node.cmd_value == WECHAT_CMD_PLAY_ERROR){
							LOG_ERR("unfortunately play failed %d\n", (int)node.param);
						}
						if(p_worker->current){
							LOG_INFO("after play remain action to play\n");
							node.cmd_value = WECHAT_CMD_PLAY_START;
							node.param = NULL;
							node.size = 0;
							wechat_notify(&node, 100);
						}else{
							if(led_mode_get() == LED_MODE_GREEN_BLINK)//stop notify user incoming msg
								led_mode_set(LED_CLIENT_WECHAT, LED_MODE_ALL_OFF, NULL);
							LOG_INFO("no action need to play\n");
						}
					}else{
						LOG_INFO("no action in playing list\n");
					}
					break;
				case WECHAT_CMD_PLAY_PREVIOUS:
					action = p_worker->previous;
					if(action){
						LOG_INFO("audio action play previous, path %s\n", action->local_path);
						play_param.play_app_type = AUDIO_APP_WECHAT;
						play_param.is_local_file = true;
						play_param.uri = action->local_path;
						play_param.tone = NULL;
						play_param.cb = NULL;
						play_param.cb_param = NULL;
						play_start(&play_param);
					}
					else{
						LOG_INFO("no action in playing previous list\n");

					}
					break;
				case WECHAT_CMD_PLAY_ERROR://wechat play disturbed, wait keypress
					action = p_worker->current;
					if(action){
						LOG_INFO("audio action play disturbed, wait repeat...\n");
						action->state = WECHAT_RECV_FINISH_DOWNLOAD;
						if(led_mode_get() == LED_MODE_GREEN_BLINK)//stop notify user incoming msg
							led_mode_set(LED_CLIENT_WECHAT, LED_MODE_ALL_OFF, NULL);
					}else{
						LOG_INFO("no action in playing list\n");
					}
					break;

					/*==============================================*/
					/*================upload process================*/
					/*==============================================*/
				case WECHAT_CMD_UPLOAD_START:
					LOG_DBG("upload get audio to process\n");
					action = u_worker->current;
					if(action && ((action->state == WECHAT_SEND_FINISH_RECORD) || (action->state == WECHAT_SEND_START_UPLOAD))){//just upload record wav
						action->state = WECHAT_SEND_START_UPLOAD;
						memset(&(g_wechat_governor.upload_info), 0, sizeof(upload_info_t));
						LOG_INFO("record time = %d\n",record_time);
						if(record_time/10 == 0){
							download_upload_finish_reason_t _reason = FILE_SIZE_ERROR;
							node.cmd_value = WECHAT_CMD_UPLOAD_FINISH;
							node.param = (void *)_reason;
							node.size = 0;
							wechat_notify(&node, 100);
							break;
						}
						//g_wechat_governor.upload_info.record_time = record_time*100; 
						//g_wechat_governor.upload_info.socket_upload = get_server_socket();
						memset(mac,0,sizeof(mac));
						sprintf(mac, "&duration=%d",record_time/10);
						strncpy(&(g_wechat_governor.upload_info.file_path_name), &(action->local_path), sizeof(g_wechat_governor.upload_info.file_path_name));
						strncpy(&(g_wechat_governor.upload_info.upload_path), &(action->remote_path), sizeof(g_wechat_governor.upload_info.upload_path));
						strncat(&(g_wechat_governor.upload_info.upload_path),mac,strlen(mac));
						strncat(&(g_wechat_governor.upload_info.upload_path),"&type=0",7);
						strncpy(&(g_wechat_governor.upload_info.port_number),WECHAT_UPLOAD_PORT, sizeof(g_wechat_governor.upload_info.port_number));
						g_wechat_governor.upload_info.callback = upload_callback;
						//direct upload wav to server
						//LOG_DBG("upload param file path %s, upload path %s, port num %s\n",
						//	g_wechat_governor.upload_info.file_path_name, g_wechat_governor.upload_info.upload_path, 
						//	g_wechat_governor.upload_info.port_number);
						send_upload_req_Q(&(g_wechat_governor.upload_info));
						LOG_INFO("upload starting...\n");
					}else{
						LOG_INFO("no action or prev action is uploading, action 0x%x, state %d\n", (unsigned int)action, (action?(int)(action->state):(-1)));
					}
					break;
				case WECHAT_CMD_UPLOAD_FINISH://cb from upload manager
				case WECHAT_CMD_UPLOAD_ERROR: //cb from upload manager
					action = u_worker->current;
					if(action){//here must serial finish upload
						action->state = WECHAT_SEND_FINISH_UPLOAD;
						remove_action(u_worker, action);
						LOG_INFO("upload result %d\n", (int)node.param);
						switch((download_upload_finish_reason_t)node.param){
							case LOAD_SUCC:
								play_param.play_app_type = AUDIO_APP_WECHAT;
								play_param.is_local_file = true;
								play_param.uri = NOTIFY_AUDIO_WECHAT_SENDMSG;
								play_param.tone = NULL;
								play_param.cb = NULL;
								play_param.cb_param = NULL;
								play_start(&play_param);
								free(action);
								action = NULL;
								upload_err_cnt = 0;
								break;
							case FILE_SIZE_ERROR:
								free(action);
								action = NULL;
								break;
							default:
								LOG_INFO("upload_err_cnt = %d",upload_err_cnt);
								if(upload_err_cnt++ < 5){
									action->state =  WECHAT_SEND_FINISH_RECORD;
									add_action_tail(u_worker, action);
								}else{
									free(action);
									action = NULL;
								}
								break;
						}
						if(u_worker->current){
							LOG_INFO("remain upload work need process\n");
							node.cmd_value = WECHAT_CMD_UPLOAD_START;
							node.param = NULL;
							node.size = 0;
							wechat_notify(&node, 100);
						}else{
							LOG_INFO("no upload work left\n");
						}
					}else{
						LOG_INFO("no upload action\n");
					}
					break;

					/*==============================================*/
					/*================command process===============*/
					/*==============================================*/
				case WECHAT_CMD_RECV_MSG:
					cmd = (rx_data_t *)node.param;
					int size = cmd->length;
					wechat_json = cJSON_Parse(cmd->data);
					if(wechat_json){
						data_json = cJSON_GetObjectItem(wechat_json,"data");
						if(data_json == NULL){
							cJSON_Delete(wechat_json);
							wechat_json = NULL;
							break;
						}
						url_json = cJSON_GetObjectItem(data_json, "play_url");
						if(url_json == NULL){
							cJSON_Delete(wechat_json);
							wechat_json = NULL;
							break;
						}
						
						id_json = cJSON_GetObjectItem(data_json, "id");
						if(id_json == NULL){
							cJSON_Delete(wechat_json);
							wechat_json = NULL;
							break;
						}
						
						char *pos = convert_url_to_filename(url_json->valuestring, size);
						action = (wechat_action_t *)heap_caps_malloc(sizeof(wechat_action_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
						if(action){
							memset(action, 0, sizeof(wechat_action_t));
							action->type = WECHAT_ACTION_T_RECV;
							action->state = WECHAT_RECV_IDLE;
							action->error_code = 0;	
							if(cJSON_IsString(id_json))
								strncpy(action->msg_id,id_json->valuestring,sizeof(action->msg_id));
							snprintf(action->local_path, FILE_PATH_MAX_SIZE, "/sdcard/wechat/%s", pos);
							strncat(action->remote_path, url_json->valuestring, size);
							add_action_tail(d_worker, action);
							node.cmd_value = WECHAT_CMD_DOWNLOAD_START;
							node.param = NULL;
							node.size = 0;
							wechat_notify(&node, 100);
							LOG_INFO("start download action local %s remote %s msgid = %s\n", action->local_path, action->remote_path,action->msg_id);
						}
						cJSON_Delete(wechat_json);
						wechat_json = NULL;
						data_json = NULL;
						free(cmd);//free remote cmd buffer	
						cmd = NULL;
					}else{
						LOG_ERR("wechat json parse fail\n");
					}
					break;
					/*==============================================*/
					/*===============download process===============*/
					/*==============================================*/				
				case WECHAT_CMD_DOWNLOAD_START:
					LOG_INFO("download get action to process\n");
					action = d_worker->current;
					if(action && (action->state == WECHAT_RECV_IDLE)){//just upload record wav
						action->state = WECHAT_RECV_START_DOWNLOAD;
						memset(&(g_wechat_governor.download_info), 0, sizeof(download_info_t));
						strncpy(&(g_wechat_governor.download_info.file_path_name), &(action->local_path), sizeof(g_wechat_governor.download_info.file_path_name));
						strncpy(&(g_wechat_governor.download_info.download_path), &(action->remote_path), sizeof(g_wechat_governor.download_info.download_path));
						//strncpy(&(g_wechat_governor.download_info.port_number), "80", sizeof(g_wechat_governor.download_info.port_number));
						convert_url_to_portnumber(action->remote_path, strlen(action->remote_path), (g_wechat_governor.download_info.port_number), sizeof(g_wechat_governor.download_info.port_number));
						g_wechat_governor.download_info.breakpoint = false;
						g_wechat_governor.download_info.filesize = 0;
						g_wechat_governor.download_info.type = 0;
						g_wechat_governor.download_info.callback = download_callback;
						send_download_req_Q(&(g_wechat_governor.download_info));
						//LOG_DBG("download param file path %s, download path %s, port num %s\n",
						//	g_wechat_governor.download_info.file_path_name, g_wechat_governor.download_info.download_path, 
						//	g_wechat_governor.download_info.port_number);
						LOG_INFO("download starting...\n");
					}else{
						LOG_INFO("%s action %s is downloading\n", (action?"prev":"no"), (action?action->local_path:" "));
					}
					break;
				case WECHAT_CMD_DOWNLOAD_FINISH:
				case WECHAT_CMD_DOWNLOAD_ERROR:
					action = d_worker->current;
					if(action){//here must serial finish download
						action->state = WECHAT_RECV_FINISH_DOWNLOAD;
						remove_action(d_worker, action);
						LOG_INFO("download result %d\n", (int)node.param);
						switch((download_upload_finish_reason_t)node.param){
							case LOAD_SUCC:
								add_action_tail(p_worker, action);
								led_mode_set(LED_CLIENT_WECHAT, LED_MODE_GREEN_BLINK, NULL);//led blink to notify user
								download_err_cnt = 0;
								break;
							default:
								//free(action);//download fail free action or retry
								//action = NULL;
								LOG_INFO("download_err_cnt = %d",download_err_cnt);
								if(download_err_cnt++ < 5){
									action->state = WECHAT_RECV_IDLE;
									add_action_tail(d_worker, action);
								}else{
									free(action);
									action = NULL;
								}
								break;
						}
						if(d_worker->current){
							LOG_INFO("remain download work need process\n");
							node.cmd_value = WECHAT_CMD_DOWNLOAD_START;
							node.param = NULL;
							node.size = 0;
							wechat_notify(&node, 100);
						}else{
							LOG_INFO("no download work left\n");
						}
					}else{
						LOG_INFO("no download action\n");
					}				
					break;		
				default:
					LOG_ERR("unknown cmd %d receive, ignore it\n", node.cmd_value);
					break;
			}
		}
	}
	vTaskDelay(1);
    vTaskDelete(NULL);
    xSemaphoreGive(task_exit);
}

int wechat_init(void)
{
	uint32_t cmd_bits = 0;
	
	LOG_INFO("enter\n");
    wechat_queue = xQueueCreate(WECHAT_QUEUE_SIZE, sizeof(wechat_cmd_node));
	task_exit = xSemaphoreCreateMutex();
	kclient = keyevent_register_listener(KEY_CODE_WECHAT_MASK, wechat_keyprocess);
	if(!kclient){
		LOG_ERR("register key client fail\n");
		vQueueDelete(wechat_queue);
		vSemaphoreDelete(task_exit);
		return ESP_ERR_INVALID_RESPONSE;
	}
	key_timer = xTimerCreate("wechat_keytmr", pdMS_TO_TICKS(500), pdFALSE, NULL, wechat_keytimer_cb);
	if(!key_timer) {
		LOG_ERR("creat keypess xtimer fail\n");
		keyevent_unregister_listener(kclient);
		vQueueDelete(wechat_queue);
		vSemaphoreDelete(task_exit);
		return ESP_ERR_INVALID_RESPONSE;
	}
	memset(&g_wechat_governor, 0 , sizeof(wechat_governor));
	g_wechat_governor.record_worker.timer = xTimerCreate("wechat_retmr", pdMS_TO_TICKS(100), pdTRUE, TIMER_TYPE_AUDIO, wechat_timer_cb);
	if(!g_wechat_governor.record_worker.timer) {
		LOG_ERR("creat record worker xtimer fail\n");
		xTimerDelete(key_timer, 0);
		keyevent_unregister_listener(kclient);
		vQueueDelete(wechat_queue);
		vSemaphoreDelete(task_exit);
		return ESP_ERR_INVALID_RESPONSE;
	}
	#if 0
	g_wechat_governor.play_worker.timer = xTimerCreate("wechat_pltmr", pdMS_TO_TICKS(100), pdFALSE, TIMER_TYPE_AUDIO, wechat_timer_cb);
	if(!g_wechat_governor.play_worker.timer) {
		LOG_ERR("creat play worker xtimer fail\n");
		xTimerDelete(key_timer, 0);
		xTimerDelete(g_wechat_governor.record_worker.timer, 0);
		keyevent_unregister_listener(kclient);
		vQueueDelete(wechat_queue);
		vSemaphoreDelete(task_exit);
		return ESP_ERR_INVALID_RESPONSE;
	}
	g_wechat_governor.upload_worker.timer = xTimerCreate("wechat_uptmr", pdMS_TO_TICKS(100), pdFALSE, TIMER_TYPE_UPLOAD, wechat_timer_cb);
	if(!g_wechat_governor.upload_worker.timer) {
		LOG_ERR("creat upload worker xtimer fail\n");
		xTimerDelete(key_timer, 0);
		xTimerDelete(g_wechat_governor.play_worker.timer, 0);
		xTimerDelete(g_wechat_governor.record_worker.timer, 0);
		keyevent_unregister_listener(kclient);
		vQueueDelete(wechat_queue);
		vSemaphoreDelete(task_exit);
		return ESP_ERR_INVALID_RESPONSE;
	}
	g_wechat_governor.download_worker.timer = xTimerCreate("wechat_dntmr", pdMS_TO_TICKS(100), pdFALSE, TIMER_TYPE_DOWNLOAD, wechat_timer_cb);
	if(!g_wechat_governor.download_worker.timer) {
		LOG_ERR("creat download worker xtimer fail\n");
		xTimerDelete(key_timer, 0);
		xTimerDelete(g_wechat_governor.upload_worker.timer, 0);
		xTimerDelete(g_wechat_governor.play_worker.timer, 0);
		xTimerDelete(g_wechat_governor.record_worker.timer, 0);
		keyevent_unregister_listener(kclient);
		vQueueDelete(wechat_queue);
		vSemaphoreDelete(task_exit);
		return ESP_ERR_INVALID_RESPONSE;
	}
	#endif

	cmd_bits = 1<<find_type_bit_offset(S2C_WECHAT_MSG_PUSH);
	g_wechat_governor.command_handler = remote_cmd_register(cmd_bits, wechat_command_cb);
	task_need_run = true;
	xTaskCreate(wechat_task, "wechat_task", 4608, NULL, 5, NULL);
	LOG_INFO("exit\n");
	return 0;
}

void wechat_uninit(void)
{
	wechat_cmd_node node;

	LOG_INFO("called\n");
	remote_cmd_unregister(g_wechat_governor.command_handler);
	xTimerStop(key_timer, 100);
	xTimerDelete(key_timer, 0);
	xTimerStop(g_wechat_governor.play_worker.timer, 100);
	xTimerDelete(g_wechat_governor.play_worker.timer, 0);
	xTimerStop(g_wechat_governor.record_worker.timer, 100);
	xTimerDelete(g_wechat_governor.record_worker.timer, 0);
	xTimerStop(g_wechat_governor.upload_worker.timer, 100);
	xTimerDelete(g_wechat_governor.upload_worker.timer, 0);
	xTimerStop(g_wechat_governor.download_worker.timer, 100);
	xTimerDelete(g_wechat_governor.download_worker.timer, 0);
	task_need_run = false;
	node.cmd_value = WECHAT_CMD_UNDEFINE;
	node.param = NULL;
	node.size = 0;
	wechat_notify(&node, 100);
	xSemaphoreTake(task_exit, portMAX_DELAY);
	vSemaphoreDelete(task_exit);
	keyevent_unregister_listener(kclient);
	vQueueDelete(wechat_queue);
	return;
}

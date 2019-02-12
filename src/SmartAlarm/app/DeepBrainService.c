#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"

#include "debug_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "dcl_asr_api.h"

#include "cJSON.h"
#include "http_api.h"
#include "version.h"
#include "socket.h"
#include "lwip/netdb.h"
#include "log.h"
#include "audiomanagerservice.h"
#include "DeepBrainService.h"
#include "keyevent.h"
#include "application.h"
#include "ctypes.h"
#include "connect_manager.h"
#include "memory_interface.h"
#include "dcl_tts_api.h"
#include "dcl_device_license.h"

#define DEEPBRAIN_ERROR						"啊哦，思想开小差了,先问问我别的吧"
#define QUEUE_MAX_SIZE						5
#define MAX_TOTAL_REC_TIME					(8*1000) //8 seconds
#define START_TO_RECORD                     (0)
#define READ_TO_RECORD                      (1)
#define STOP_TO_RECORD                      (2)
#define SEND_AUDIO_DATA						(3)
#define TIME_10S_REMINED					(4)
#define TIME_30S_QUIT						(5)
#define DEEPBRAIN_QUIT						(6)

#define LOG_TAG		"DBS"

xQueueHandle xQueueDeepBrainService = NULL;
xQueueHandle xQueueRawStart = NULL;
xQueueHandle xQueueAudioBuffer = NULL;
static SemaphoreHandle_t g_lock_record_id;
static bool is_db_working = false; 
static record_ram_buffer_t record_info = {0};
static TimerHandle_t deepbrain_timer = NULL;

static ASR_SERVICE_HANDLE_t *g_asr_service_handle = NULL;

#define AUDIO_RAW_BOLOCK_SIZE 1024
#define AUDIO_UPLOAD_BLOCK_SIZE (6*AUDIO_RAW_BOLOCK_SIZE) //每次语音数据传输大小 

typedef struct 
{
	int  iLast;// 0-not last, 1-last 
	int  size;// audio size
	int  index;// frame index
	unsigned int id;// record id
	char audio_buf[AUDIO_UPLOAD_BLOCK_SIZE];
}AUDIO_NODE, *PAUDIO_NODE;

static inline void record_notify(uint8_t *node, uint32_t delay)
{
	if(xQueueRawStart == NULL){
		LOG_ERR("Q raw is null\n");
		return ;
	}
	xQueueSend(xQueueRawStart, node, delay);
}
static int notification_record_cmd(int cmd)
{
	uint8_t type = 255;

	type = cmd;
	set_front_application(APP_DEEPBRAIN);
	record_notify(&type, portMAX_DELAY);
	return 0;
}

static bool get_tts_play_url(const char* const input_text, char* const tts_url,const uint32_t url_len);
static void deepbrain_play_net_tone_cb(NotifyStatus ret,AudioAppType apptype,AudioManagerMsg cmd, void *param)
{
	
	switch(ret){
		case NOTIFY_STATUS_PLAY_FINISH:
			LOG_INFO("play_net_tone notify finish\n");
			if(is_db_working == false)
				break;
			if(xTimerIsTimerActive(deepbrain_timer) != pdFALSE){
				xTimerReset(deepbrain_timer, 0);
			}else{
				xTimerStart(deepbrain_timer, 0);
			}
			//is_db_working = true;
			notification_record_cmd(START_TO_RECORD);
			break;
		case NOTIFY_STATUS_PLAY_ERROR:
			if(is_db_working == false)
				break;
			if(xTimerIsTimerActive(deepbrain_timer) != pdFALSE){
				xTimerReset(deepbrain_timer, 0);
			}else{
				xTimerStart(deepbrain_timer, 0);
			}
			//is_db_working = true;
			notification_record_cmd(START_TO_RECORD);
			LOG_INFO("play_net_tone notify error\n");
			break;
		case NOTIFY_STATUS_PLAY_DISTURBED:
			LOG_INFO("play_net_tone notify disturbed\n");
			is_db_working = false;
			xTimerStop(deepbrain_timer, 0);
			break;
		case NOTIFY_STATUS_PLAY_STOPPED:
			LOG_INFO("play_net_tone notify stop\n");
			break;
		default:
			LOG_INFO("play_net_tone notify (%d) ignore\n", ret);
			break;
	}
	return ;
}

static int play_net_tone(char *str_tts)
{
	play_param_t param;

	if(xTimerIsTimerActive(deepbrain_timer) != pdFALSE)
		xTimerStop(deepbrain_timer, 0);
	record_info.record_timer = 0;
	
	//通过URL播放
	param.play_app_type = AUDIO_APP_VOICE_RECOGNITION;
	param.is_local_file = false;
	param.uri = str_tts;
	param.tone = NULL;
	param.cb = deepbrain_play_net_tone_cb;
	param.cb_param = NULL;
	play_start(&param);
	return 0;
}

#define TIME_OUT_10S	8
#define TIME_OUT_20S	18
#define TIME_OUT_30S	28

static void deepbrain_timer_fun(TimerHandle_t xTimer)
{
	record_info.record_timer ++;
	if(record_info.record_timer == TIME_OUT_10S){
		if(is_db_working == true){
			notification_record_cmd(TIME_10S_REMINED);
		}
	}else if(record_info.record_timer == TIME_OUT_20S){
		if(is_db_working == true){
			notification_record_cmd(TIME_10S_REMINED);
		}
	}else if(record_info.record_timer >= TIME_OUT_30S){
		if(is_db_working == true){
			record_info.record_timer = 0;
			LOG_INFO("timer send stop record \n");
			notification_record_cmd(TIME_30S_QUIT);
		}
	}
	return ;
}

static keyprocess_t deepbrain_keyprocess(keyevent_t *event)
{
	uint8_t type = 255;

	LOG_DBG("DB recevice key event %d type %d\n", event->code, event->type);

	if(event->code == KEY_CODE_VOICE_RECOGNIZE){
		if(event->type == KEY_EVNET_PRESS)
		{
			if(wifi_connect_status() == false){
				play_tone_sync(NOTIFY_AUDIO_NOT_CONNECT);
			}else{
				if(is_db_working == false){
					int get_rand = get_time_of_day()%10;
					if((get_rand >= 0) && (get_rand <= 2)){
						play_tone_sync(NOTIFY_AUDIO_HELLO);
					}else if((get_rand >=3) && (get_rand <=5)){
						play_tone_sync(NOTIFY_AUDIO_I_AM_IN);
					}else if((get_rand >=6) && (get_rand <=8)){
						play_tone_sync(NOTIFY_AUDIO_START_TO_CHAT);
					}else{
						play_tone_sync(NOTIFY_AUDIO_LET_ME_ALSO);
					}	
					record_info.record_timer = 0;
					type = START_TO_RECORD;
					set_front_application(APP_DEEPBRAIN);
					record_notify(&type, portMAX_DELAY);
					if(xTimerIsTimerActive(deepbrain_timer) != pdFALSE){
						xTimerReset(deepbrain_timer, 0);
					}else{
						xTimerStart(deepbrain_timer, 0);
					}
				}else if(is_db_working == true){
					
					xTimerStop(deepbrain_timer, 0);
					record_info.record_timer = 0;
					type = DEEPBRAIN_QUIT;
					set_front_application(APP_DEEPBRAIN);
					record_notify(&type, portMAX_DELAY);
					
				}else{
					LOG_INFO("record is working, no need to restart\n");
				}
			}
		}
		else if(event->type == KEY_EVNET_RELEASE)
		{
			//type = STOP_TO_RECORD;
			//set_front_application(APP_DEEPBRAIN);
			//record_notify(&type, portMAX_DELAY);
		}else{
			LOG_INFO("unsupport key event (%d) ignore it\n", event->type);
		}
	}else{
		LOG_ERR("receive unexpect key (%d) event (%d)\n", event->code, event->type);
	}
	return KEY_PROCESS_PUBLIC;
}

static void deepbrain_audio_record_cb(NotifyStatus ret,AudioAppType apptype,AudioManagerMsg cmd, void *param)
{
	uint8_t type = 255;

	switch((NotifyStatus)ret){
		case NOTIFY_STATUS_RECORD_DISTURBED:
			LOG_INFO("deepbrain_audio record notify disturbed\n");
			is_db_working = false;
			xTimerStop(deepbrain_timer, 0);
			break;
		case NOTIFY_STATUS_RECORD_EXCEED:
		case NOTIFY_STATUS_RECORD_ERROR:
			LOG_INFO("deepbrain_audio record notify error %d\n",ret);
			if(is_db_working == false)
				break;
			if(xTimerIsTimerActive(deepbrain_timer) != pdFALSE){
				xTimerStart(deepbrain_timer, 0);
			}
			notification_record_cmd(START_TO_RECORD);
			break;
		case NOTIFY_STATUS_RECORD_SEND:
			LOG_INFO("deepbrain_audio record data send (%d) %d\n", ret,record_info.vad_enable);
			if((record_info.vad_enable == true)){
				record_info.vad_enable = false;
				type = SEND_AUDIO_DATA;
				record_notify(&type, portMAX_DELAY);
			}
			break;
		case NOTIFY_STATUS_RECORD_READ:
			notification_record_cmd(READ_TO_RECORD);
			break;
		default:
			LOG_INFO("deepbrain_audio record notify (%d) ignore\n", ret);
			break;
	}

}
static void deepbrain_tone_play_cb(NotifyStatus ret,AudioAppType type,AudioManagerMsg cmd, void *param)
{	
	switch((NotifyStatus)ret){
		case NOTIFY_STATUS_PLAY_FINISH:
			if(is_db_working == false)
				break;
			if(xTimerIsTimerActive(deepbrain_timer) != pdFALSE){
				xTimerReset(deepbrain_timer, 0);
			}else{
				xTimerStart(deepbrain_timer, 0);
			}
			//is_db_working = true;
			notification_record_cmd(START_TO_RECORD);
			LOG_INFO("deepbrain_tone play notify finish\n");
			break;
		case NOTIFY_STATUS_PLAY_ERROR:
			LOG_INFO("deepbrain_tone play notify error\n");
			break;
		case NOTIFY_STATUS_PLAY_DISTURBED:
			LOG_INFO("deepbrain_tone play notify disturbed\n");
			is_db_working = false;
			xTimerStop(deepbrain_timer, 0);
			break;
		case NOTIFY_STATUS_PLAY_STOPPED:
			LOG_INFO("deepbrain_tone play notify stop\n");
			break;
		default:
			LOG_INFO("deepbrain_tone play notify (%d) ignore\n", ret);
			break;
	}
	return;
}
#define DeepBrain_APP_ID  	"A000000000000191"
#define DeepBrain_ROBOT_ID	"de5f70ba-f5b6-11e7-8672-801844e30cac"
#define DeepBrain_DEVICE_ID	"18055384698"
#define DeepBrain_USER_ID	"18055384698"
#define CITY_LONGITUDE		"121.48" 
#define CITY_LATITUDE		"31.22"
#define CHIP_ID 			"B300"

static bool get_dcl_auth_params(DCL_AUTH_PARAMS_t *dcl_auth_params)
{
	char mac[12] = {0};
	DCL_DEVICE_LICENSE_RESULT_t license_reslut = {0};
	if (dcl_auth_params == NULL)
	{
		return false;
	}
	
	unsigned char *p = (unsigned char *)wifi_sta_mac_add();
    snprintf(mac,sizeof(mac),"%02X%02X%02X%02X%02X%02X", p[0],p[1],p[2],p[3],p[4],p[5]);
	snprintf(dcl_auth_params->str_app_id, sizeof(dcl_auth_params->str_app_id), "%s", DeepBrain_APP_ID);
	snprintf(dcl_auth_params->str_robot_id, sizeof(dcl_auth_params->str_robot_id), "%s", DeepBrain_ROBOT_ID);
	snprintf(dcl_auth_params->str_user_id, sizeof(dcl_auth_params->str_user_id), "%s", CHIP_ID);
	snprintf(dcl_auth_params->str_device_id, sizeof(dcl_auth_params->str_device_id), " ");
	snprintf(dcl_auth_params->str_city_name, sizeof(dcl_auth_params->str_city_name), " ");
	dcl_get_device_license(dcl_auth_params,(const uint8_t*)mac,(const uint8_t*)CHIP_ID,false,&license_reslut);
	LOG_INFO("get device sn:%s,type%s,id:%s,license:%s\n",license_reslut.device_sn,license_reslut.wechat_device_id,license_reslut.wechat_device_type,license_reslut.wechat_device_license);
	snprintf(dcl_auth_params->str_device_id, sizeof(dcl_auth_params->str_device_id), "%s", license_reslut.device_sn);
	
	return true;
}

static void asr_rec_begin(ASR_SERVICE_HANDLE_t *asr_service_handle)
{
	ASR_RESULT_t *asr_result = &asr_service_handle->asr_result;
	DCL_AUTH_PARAMS_t *auth_params = &asr_service_handle->auth_params;
	memset(asr_result, 0, sizeof(ASR_RESULT_t));
	DCL_ASR_MODE_t 		asr_mode = DCL_ASR_MODE_ASR_NLP;		
	DCL_ASR_LANG_t 		asr_lang = DCL_ASR_LANG_CHINESE;
	DCL_ERROR_CODE_t ret = DCL_ERROR_CODE_OK;

	if (asr_service_handle->dcl_asr_handle != NULL)
	{
		dcl_asr_session_end(asr_service_handle->dcl_asr_handle);
		asr_service_handle->dcl_asr_handle = NULL;
	}

	ret = dcl_asr_session_begin(&asr_service_handle->dcl_asr_handle);
	if (ret != DCL_ERROR_CODE_OK)
	{
		ret = dcl_asr_session_begin(&asr_service_handle->dcl_asr_handle);
	}
	
	if (ret == DCL_ERROR_CODE_OK)
	{
		DEBUG_LOGI(LOG_TAG, "dcl_asr_session_begin success"); 
	}
	else
	{
		DEBUG_LOGE(LOG_TAG, "dcl_asr_session_begin failed errno[%d]", ret); 
		asr_result->error_code = ret;
	}

	get_dcl_auth_params(auth_params);
	dcl_asr_set_param(asr_service_handle->dcl_asr_handle, DCL_ASR_PARAMS_INDEX_LANGUAGE, &asr_lang, sizeof(DCL_ASR_LANG_t));
	dcl_asr_set_param(asr_service_handle->dcl_asr_handle, DCL_ASR_PARAMS_INDEX_MODE, &asr_mode, sizeof(DCL_ASR_MODE_t));
	dcl_asr_set_param(asr_service_handle->dcl_asr_handle, DCL_ASR_PARAMS_INDEX_AUTH_PARAMS, auth_params, sizeof(DCL_AUTH_PARAMS_t));
			
}
static void asr_rec_runing(ASR_SERVICE_HANDLE_t *asr_service_handle,PAUDIO_NODE audio_obj)
{
	int ret = 0;
	ASR_RESULT_t *asr_result = &asr_service_handle->asr_result;
	
	if (asr_service_handle->dcl_asr_handle == NULL)
	{
		return ;
	}
	//LOG_INFO("audio size = %d\n", audio_obj->size);
	ret = dcl_asr_audio_write(asr_service_handle->dcl_asr_handle, audio_obj->audio_buf, audio_obj->size);
	if (ret != DCL_ERROR_CODE_OK)
	{
		asr_result->error_code = ret;
		dcl_asr_session_end(asr_service_handle->dcl_asr_handle);
		asr_service_handle->dcl_asr_handle = NULL;
		DEBUG_LOGE(LOG_TAG, "dcl_asr_audio_write failed errno[%d]", ret); 
	}
	return ;
	
}
static void asr_rec_end(ASR_SERVICE_HANDLE_t *asr_service_handle)
{
	DCL_ERROR_CODE_t err_code = DCL_ERROR_CODE_OK;
	ASR_RESULT_t *asr_result = &asr_service_handle->asr_result;
	
	if (asr_service_handle->dcl_asr_handle == NULL)
	{
		return ;
	}
	err_code = dcl_asr_get_result(asr_service_handle->dcl_asr_handle, asr_result->str_result, sizeof(asr_result->str_result));
	if (err_code != DCL_ERROR_CODE_OK)
	{
		DEBUG_LOGE(LOG_TAG, "dcl_asr_get_result failed");
	}
	asr_result->error_code = err_code;
	dcl_asr_session_end(asr_service_handle->dcl_asr_handle);
	asr_service_handle->dcl_asr_handle = NULL;

}
static bool get_tts_play_url(const char* const input_text, char* const tts_url,const uint32_t url_len)
{
	DCL_AUTH_PARAMS_t *input_params = &g_asr_service_handle->auth_params;
	
	if (dcl_get_tts_url(input_params, input_text, tts_url, url_len) == DCL_ERROR_CODE_OK)
	{
		//DEBUG_LOGI(TAG_LOG, "ttsurl:[%s]", tts_url);
		return true;
	}
	else
	{
		//DEBUG_LOGE(TAG_LOG, "dcl_get_tts_url failed");
		return false;
	}
}

void _task_record_raw_read(void *pv)
{
	PAUDIO_NODE audio_node = (PAUDIO_NODE)memory_malloc(sizeof(AUDIO_NODE));
	if(audio_node == NULL)
	{
		LOG_ERR("audio_node is error\n");
	}
	PAUDIO_NODE audio_data = NULL;
	play_param_t play_param;
	memset(audio_node, 0, sizeof(AUDIO_NODE));
	uint8_t prv_status = 255;
	ASR_SERVICE_HANDLE_t *asr_service_handle = g_asr_service_handle;
	DeepBrainService_t npl_result = {0};
	int get_rand = 0;
	while(1){
		uint8_t type = 255;
		if (xQueueReceive(xQueueRawStart, &type, portMAX_DELAY) == pdPASS) {
			switch(type){
				case START_TO_RECORD:
					if(audio_node != NULL){
						if(asr_service_handle->dcl_asr_handle == NULL)
							asr_rec_begin(asr_service_handle);
						is_db_working = true;
						//memset(audio_node, 0, sizeof(AUDIO_NODE));
						//memset(&record_info+4, 0, sizeof(record_ram_buffer_t));
						record_info.total_size = AUDIO_UPLOAD_BLOCK_SIZE;
						record_info.buffer = audio_node->audio_buf;
						record_start(AUDIO_APP_VOICE_RECOGNITION, &record_info, deepbrain_audio_record_cb, NULL);
						prv_status = type;
					}else{
						LOG_ERR("malloc action buff failed\n");
						is_db_working = false;
					}
					break;
				case READ_TO_RECORD:
					if(is_db_working){
						audio_node->iLast = 1;
						audio_node->index = 1;
						audio_node->id = 1;
						audio_node->size = record_info.used_size;
						audio_data = (PAUDIO_NODE)memory_malloc(sizeof(AUDIO_NODE));
						if(audio_data == NULL){
							LOG_ERR("audio_node is error\n");
							break;
						}
						memcpy(audio_data,audio_node,sizeof(AUDIO_NODE));
						asr_rec_runing(asr_service_handle,audio_data);
						memory_free(audio_data);
						audio_data = NULL;
					}
					break;
				case STOP_TO_RECORD:
					if(prv_status == START_TO_RECORD || prv_status == SEND_AUDIO_DATA){
						LOG_INFO("\nDB KEY STOP\n");
						xTimerStop(deepbrain_timer, 0);
						record_stop(AUDIO_APP_VOICE_RECOGNITION);
					}
					break;
				case SEND_AUDIO_DATA:
					if(is_db_working){
						asr_rec_end(asr_service_handle);
						//LOG_INFO("asr result =%s\n",asr_service_handle->asr_result.str_result);
						npl_result.cmd_value = 1;
						npl_result.param = (void*)&asr_service_handle->asr_result;
						xQueueSend(xQueueDeepBrainService, &npl_result, 0);
					}
					break;
				case TIME_10S_REMINED:
					get_rand = get_time_of_day()%10;
					if(prv_status == START_TO_RECORD)
						record_stop(AUDIO_APP_VOICE_RECOGNITION);
					play_param.play_app_type = AUDIO_APP_VOICE_RECOGNITION;
					play_param.is_local_file = true;
					if((get_rand >= 0) && (get_rand <= 2)){
						play_param.uri = NOTIFY_AUDIO_START_TO_CHAT;
					}else if((get_rand >=3) && (get_rand <=5)){
						play_param.uri = NOTIFY_AUDIO_TALETELLING;
					}else if((get_rand >=6) && (get_rand <=8)){
						play_param.uri = NOTIFY_AUDIO_PLAY_CSONG;
					}else{
						play_param.uri = NOTIFY_AUDIO_READ_THREE;
					}
					play_param.tone = NULL;
					play_param.cb = deepbrain_tone_play_cb;
					play_param.cb_param = NULL;
					play_start(&play_param);
					break;
				case TIME_30S_QUIT:
					get_rand = get_time_of_day()%10;
					if(prv_status == START_TO_RECORD)
						record_stop(AUDIO_APP_VOICE_RECOGNITION);
					is_db_working = false;
					//asr_rec_end(asr_service_handle);
					dcl_asr_session_end(asr_service_handle->dcl_asr_handle);
					asr_service_handle->dcl_asr_handle = NULL;
					xTimerStop(deepbrain_timer, 0);
					if((get_rand >= 0) && (get_rand <= 5)){
						play_tone_sync(NOTIFY_AUDIO_TAKE_REST);
					}else{
						play_tone_sync(NOTIFY_AUDIO_LEAVING);
					}
					break;
				case DEEPBRAIN_QUIT:
					if(prv_status == START_TO_RECORD)
						record_stop(AUDIO_APP_VOICE_RECOGNITION);
					is_db_working = false;
					xTimerStop(deepbrain_timer, 0);
					play_tone_sync(NOTIFY_AUDIO_BYE_BYE);
					//asr_rec_end(asr_service_handle);
					dcl_asr_session_end(asr_service_handle->dcl_asr_handle);
					asr_service_handle->dcl_asr_handle = NULL;
					break;
				default:
					break;
			}
			vTaskDelay(10);
		}
	}
    vTaskDelete(NULL);
}

void _task_deepbrain_nlp(void *pv)
{
	bool ret 			= false;
	int nlp_ret 		= 0;
	play_param_t play_param;
	DeepBrainService_t deepbrain_result = {0};
	ASR_RESULT_t * asr_result = NULL;
	NLP_RESULT_LINKS_T *links = NULL;
	ASR_SERVICE_HANDLE_t *asr_service_handle = g_asr_service_handle;
	NLP_RESULT_T *nlp_result = &asr_service_handle->nlp_result;
	char *tts_url = asr_service_handle->tts_url;
		
    while (1) 
	{
        BaseType_t xStatus = xQueueReceive(xQueueDeepBrainService, &deepbrain_result, portMAX_DELAY);
		if (xStatus == pdPASS) 
		{	
			asr_result = (ASR_RESULT_t*)deepbrain_result.param;
			nlp_ret = 0;
			if(asr_result != NULL){
				LOG_INFO("result err code= %d\n",asr_result->error_code);
				if(asr_result->error_code != DCL_ERROR_CODE_OK){
					nlp_ret = -1;
					goto nlp_err;
				}
				memset(nlp_result, 0, sizeof(NLP_RESULT_T));
				if (dcl_nlp_result_decode(asr_result->str_result, nlp_result) != NLP_DECODE_ERRNO_OK)
				{
					nlp_ret = -1;
					goto nlp_err;
				}

				LOG_INFO("nlp type = %d chat_result.link = %s\n",nlp_result->type,nlp_result->chat_result.link);
				switch (nlp_result->type)
				{
					case NLP_RESULT_TYPE_NONE:
					case NLP_RESULT_TYPE_ERROR:
					case NLP_RESULT_TYPE_NO_ASR:
						nlp_ret = -1;
						break;
					
					case NLP_RESULT_TYPE_CHAT:
						if (strlen(nlp_result->chat_result.link) > 0)
						{
							play_net_tone(nlp_result->chat_result.link);
						}
						else
						{
							memset(tts_url,0,TTS_URL_SIZE);
							ret = get_tts_play_url(nlp_result->chat_result.text,tts_url, TTS_URL_SIZE);
							if (!ret){
								ret = get_tts_play_url(nlp_result->chat_result.text, tts_url, TTS_URL_SIZE);
							}
							LOG_INFO("tts url len  = %d\n",strlen(tts_url));
							if (ret)
								play_net_tone(tts_url);
							else
								nlp_ret = -1;
						}
						break;
					
					case NLP_RESULT_TYPE_LINK:
						links = &nlp_result->link_result;
						DEBUG_LOGI(LOG_TAG, "p_links->link_size=[%d]", links->link_size);
						if (strlen(links->link[0].link_url) > 0){
							play_net_tone(links->link[0].link_url);
						}else{
							nlp_ret = -1;
						}
						break;
					
					default:
						nlp_ret = -1;
						break;
				}
				nlp_err:
				if(nlp_ret == -1){
					record_info.record_timer = 0;
					play_param.play_app_type = AUDIO_APP_VOICE_RECOGNITION;
					play_param.is_local_file = true;
					play_param.uri = NOTIFY_AUDIO_CAN_NOT_TRANSLATE;
					play_param.tone = NULL;
					play_param.cb = deepbrain_tone_play_cb;
					play_param.cb_param = NULL;
					play_start(&play_param);
				}
			}
		}
    }
    vTaskDelete(NULL);
}

int deep_brain_resource_init()
{
	keycode_client_t *kclient = NULL;
	kclient = keyevent_register_listener(KEY_CODE_VOICE_RECOGNIZE_MASK, deepbrain_keyprocess);
	if(!kclient){
		LOG_ERR("register key client fail\n");
		return -1;
	}
	g_asr_service_handle = (ASR_SERVICE_HANDLE_t *)memory_malloc(sizeof(ASR_SERVICE_HANDLE_t));
	if (g_asr_service_handle == NULL)
	{
		LOG_ERR("g_asr_service_handle malloc failed");
		return -1;
	}
	
	xQueueDeepBrainService = xQueueCreate(QUEUE_MAX_SIZE, sizeof(DeepBrainService_t));
	if(xQueueDeepBrainService == NULL)
	{
		LOG_INFO("xQueueDeepBrainService fail\n");
		return -1;
	}
		

	xQueueRawStart = xQueueCreate(QUEUE_MAX_SIZE, sizeof(uint8_t));
	if(xQueueRawStart == NULL)
	{
		vQueueDelete(xQueueDeepBrainService);
		LOG_INFO("xQueueRawStart fail\n");
		return -1;
	}

	g_lock_record_id = xSemaphoreCreateMutex();
	LOG_ERR("deep_brain_resource_init finish\n");
	return 0;
}

void deep_brain_resource_uninit()
{
	vQueueDelete(xQueueDeepBrainService);
	vQueueDelete(xQueueRawStart);
	//vQueueDelete(xQueueAudioBuffer);
	vSemaphoreDelete(g_lock_record_id);
}

void deep_brain_service_init()
{
	if(deep_brain_resource_init() ==-1){
		LOG_ERR("deep_brain_service_init fail\n");
		return ;
	}
    if (xTaskCreate(_task_deepbrain_nlp,
                    "_task_deepbrain_nlp",
                    1024*5,
                    NULL,
                    4,
                    NULL) != pdPASS) {

        LOG_ERR("ERROR creating _task_deepbrain_nlp task! Out of memory?");
    }

	if (xTaskCreate(_task_record_raw_read,
                    "_task_record_raw_read",
                    1024*6,
                    NULL,
                    4,
                    NULL) != pdPASS) {

        LOG_ERR("ERROR creating _task_record_raw_read task! Out of memory?");
    }
					
	deepbrain_timer = xTimerCreate("deepbrain_timer",pdMS_TO_TICKS(1000),pdTRUE,NULL,deepbrain_timer_fun);
    LOG_INFO("DeepBrainActive\r\n");
}

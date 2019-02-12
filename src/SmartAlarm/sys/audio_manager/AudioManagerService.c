#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "audiomanagerservice.h"
#include "EspAudioAlloc.h"
#include "TerminalService.h"
#include "MediaHal.h"
#include "log.h"
#include "nv_interface.h"
#include "ES8388_interface.h"
#include "power_manager.h"
#include "EspAudio.h"
#include "array_music.h"
#include "system_manager.h"
#include "db_vad.h"
#include "interf_enc.h"
#include "dcl_interface.h"

#define LOG_TAG	    "ams"

#define AUDIO_SER_TAG                   "AUDIO_CTRL_SERV"
#define AUDIO_SERV_TASK_PRIORITY        (4)
#define AUDIO_SERV_TASK_STACK_SIZE     1024*6

#define RECORD_THREAD_PRIORITY           (6) //must higher than ams
#define RECORD_THREAD_STACK_SIZE         (1024 * 4)
#define RECORD_FILE_MAX_SIZE			(24*1024)
#define AMR_MAGIC_NUMBER        "#!AMR\n"
#define AMR_WB_MAGIC_NUMBER "#!AMR-WB\n"

#define TONE_FILE_PATH_SIZE				64
#define AUDIO_FILE_PATH_SIZE			2048
#define AUDIO_RECORD_DESCRIP_SIZE		32	
#define AUDIO_CLIENT_NAME_SIZE			16
#define AUDIO_VOLUME_STEP               10

#define AUDIO_MANGER_COMMAND_QUEUE_SIZE	20
#define AUDIO_ESP32_PLAYER_QUEUE_SIZE	5
#define LOCAL_FILE_PREFIX				"file://"

#define RECORD_TYPE_WECHAT				"i2s://8000:1@record.amr#raw"
#define RECORD_TYPE_VOCIE_RECOGNIZE		"i2s://16000:1@record.pcm#raw"
#define RECORD_TYPE_DEFAULT				"i2s://16000:1@record.pcm#raw"

typedef struct audio_command{
	AudioAppType app;
	AudioManagerMsg cmd;
	char record_type[AUDIO_RECORD_DESCRIP_SIZE];
	char tone_path[TONE_FILE_PATH_SIZE];
	char file_path[AUDIO_FILE_PATH_SIZE];
	record_ram_buffer_t *record_buffer;
	audio_callback callback;
	void *callback_param;
	struct audio_command *next;
}audio_command_t;

typedef enum audio_manager_state{
	AUDIO_MANAGER_ST_IDLE = 0x00,
	AUDIO_MANAGER_ST_START_PLAY,
	AUDIO_MANAGER_ST_PLAYING,
	AUDIO_MANAGER_ST_STOP_PLAY,
	#ifdef STORY_PLAY_PAUSE
	AUDIO_MANAGER_ST_PAUSE_PLAY,
	AUDIO_MANAGER_ST_RESUME_PLAY,
	#endif
	AUDIO_MANAGER_ST_START_RECORD,
	AUDIO_MANAGER_ST_RECORDING,
	AUDIO_MANAGER_ST_STOP_RECORD,
	AUDIO_MANAGER_ST_UNDEFINE,
}audio_manager_state_t;

typedef enum audio_type{
	AUDIO_TYPE_SONG = 0x00,
	AUDIO_TYPE_TONE,
	AUDIO_TYPE_UNDEFINE,
}audio_type_t;

typedef struct audio_manager{
	audio_manager_state_t state;
	audio_command_t *current;
	audio_type_t current_type;
	#ifdef STORY_PLAY_PAUSE
	player_record_cmd_node cmd;
	#endif
	audio_command_t *head;
}audio_manager_t;

typedef struct tone_command{
	AudioAppType app;
	char file_path[64];
	audio_callback callback;
	void *callback_param;
}tone_command_t;

const static char ams_msg_str[9][12] = {"vol up", "vol down", "play start", "play stop", "play paused","play resume","rec start", "rec stop", "undefine"};
const static char ams_app_str[8][8] = {"wechat", "baidu ", "alarms", "habit","english","tone","push","undef"};
#ifdef STORY_PLAY_PAUSE
const static char ams_notify_str[13][15] = {"play finish", "play disturbed", "play error","play stopped","play pause","play reset",
	"record stopped", "record disturb", "record exceed", "record error","voice send","voice read","comand ignored", "undefine"};
#else
const static char ams_notify_str[9][15] = {"play finish", "play stopped", "play disturbed", "play error", 
	"record stopped", "record disturb", "record exceed", "record error", "comand ignored", "undefine"};
#endif

static audio_manager_t g_audio_manager = {0};
static QueueSetHandle_t xQueSet = NULL;
static xQueueHandle xQueueAudioService = NULL;
static xQueueHandle xQuePlayerStatus = NULL;
static AudioManagerService *g_audio_service = NULL;
static SemaphoreHandle_t g_rthread_run;
static SemaphoreHandle_t g_rthread_stop;
static TerminalControlService *g_audio_term = NULL;
static tone_command_t tone_cmd = {0};
#ifdef CONFIG_ENABLE_POWER_MANAGER
static wake_lock_t *music_wake_lock =NULL;
#endif
static xQueueHandle code_queue=NULL;
static xQueueHandle record_queue=NULL;

static inline void insert_cmd(audio_manager_t *manager, audio_command_t *command)
{
	audio_command_t *node;

	if(!manager->head){
		manager->head = command;
		command->next = NULL;
	}else{
		node = manager->head;
		while(node->next){
			node = node->next;
		}
		node->next = command;
		command->next = NULL;
	}
}

static inline bool delete_cmd(audio_manager_t *manager, audio_command_t *command)
{
	audio_command_t *node;
	bool found = false;

	if(manager && command){
		if(manager->head == command){
			manager->head = command->next;
		}else{
			node = manager->head;
			while(node->next){
				if(node->next == command){
					node->next = command->next;	
					found = true;
					break;
				}
				node = node->next;			
			}
		}
	}

	return found;
}

static inline audio_command_t *search_cmd_by_app(audio_manager_t *manager, AudioAppType app, audio_command_t *pos)
{
	audio_command_t *node = NULL;
	audio_command_t *app_node = NULL;

	if(pos){
		node = pos->next;
	}else{
		node = manager->head;
	}

	while(node){
		if(node->app == app){
			app_node = node;
			break;
		}
		node = node->next;
	}

	return app_node;
}

static inline audio_command_t *search_last_start_cmd(audio_manager_t *manager)
{
	audio_command_t *node = NULL;
	audio_command_t *pos = NULL;

	node = manager->head;
	while(node){
		if((node->cmd == AUDIO_MANAGER_PLAY_START) || 
			(node->cmd == AUDIO_MANAGER_RECORD_START)){
			pos = node;
		}
		node = node->next;
	}

	return pos;
}

static inline audio_command_t *search_app_cmd_by_msg(audio_manager_t *manager, AudioAppType app, AudioManagerMsg msg)
{
	audio_command_t *node = NULL;
	audio_command_t *pos = NULL;

	if(app != AUDIO_APP_UNDEFINED){
		node = manager->head;
		while(node){
			if((node->cmd == msg) && (node->app == app)){
				pos = node;
				break;
			}
			node = node->next;
		}
	}else{
		node = manager->head;
		while(node){
			if(node->cmd == msg){
				pos = node;
				break;
			}
			node = node->next;
		}
	}

	return pos;
}
/*
play start support two play mode, 1- play song without tone,
2- play song with tone,must carefully, play start must have uri, 
if you just want to play a tone, take the tone as song to process,
just like play_tone_sync...
*/
int play_start(play_param_t *param)
{
	int ret = 0;
	audio_command_t cmd;

	if(!param){
		LOG_ERR("param is NULL pointer\n");
		ret = -ESP_ERR_INVALID_ARG;
		goto out;
	}

	if(param->play_app_type >= AUDIO_APP_UNDEFINED){
		LOG_ERR("app type (%d) is unknown\n", param->play_app_type);
		ret = -ESP_ERR_INVALID_ARG;
		goto out;
	}

	if(!param->uri){
		LOG_ERR("url is NULL pointer\n");
		ret = -ESP_ERR_INVALID_ARG;
		goto out;
	}

	memset(&cmd, 0, sizeof(audio_command_t));
	cmd.app = param->play_app_type;
	cmd.cmd = AUDIO_MANAGER_PLAY_START;
	if(param->is_local_file){//local file need prefix
		strncpy(cmd.file_path, LOCAL_FILE_PREFIX, strlen(LOCAL_FILE_PREFIX));
		strncat(cmd.file_path, param->uri, (AUDIO_FILE_PATH_SIZE - strlen(LOCAL_FILE_PREFIX)));
	}else{
		strncpy(cmd.file_path, param->uri, AUDIO_FILE_PATH_SIZE);

	}
	if(param->tone){
		strncpy(cmd.tone_path, LOCAL_FILE_PREFIX, strlen(LOCAL_FILE_PREFIX));
		strncat(cmd.tone_path, param->tone, (TONE_FILE_PATH_SIZE - strlen(LOCAL_FILE_PREFIX)));
	}
	cmd.callback = param->cb;
	cmd.callback_param = param->cb_param;
	cmd.next = NULL;
	if (xQueueAudioService && (pdTRUE == xQueueSend(xQueueAudioService, &cmd, 500))){		
		LOG_INFO("app (%s) play start url (%s) send success\n", ams_app_str[param->play_app_type], cmd.file_path);
	}else{
		LOG_ERR("app (%s) play start url (%s) send failed\n", ams_app_str[param->play_app_type], cmd.file_path);
	}
	
out:
	return ret;
}
#ifdef STORY_PLAY_PAUSE
int get_volume_grade(void)
{	
	nv_item_t nv;
	int value = 0;
	
	nv.name = NV_ITEM_AUDIO_VOLUME;
	if(ESP_OK == get_nv_item(&nv)){
		value = (int)nv.value;
	}else{
		value = DEFAULT_VOLUME;
	}
	if(value > 90)
		return value/9;
	
	if((value % 9) == 0){
		return value/9;
	}else{
		return value/9+1;
	}
}

int play_resume(AudioAppType play_app_type)
{
	int ret =0;	
	audio_command_t cmd;
	if(play_app_type >= AUDIO_APP_UNDEFINED){	
		LOG_ERR("app type (%d) is unknown\n", play_app_type);		
		ret = -ESP_ERR_INVALID_ARG;		
		goto out;	
	}	
	memset(&cmd, 0, sizeof(audio_command_t));
	cmd.app = play_app_type;	
	cmd.cmd = AUDIO_MANAGER_PLAY_RESUME;	
	cmd.next = NULL;	
	if (xQueueAudioService && (pdTRUE == xQueueSend(xQueueAudioService, &cmd, 0))){
		LOG_INFO("app (%s) play pause send success\n", ams_app_str[play_app_type]);	
	}else{	
		LOG_ERR("app (%s) play pause send failed\n", ams_app_str[play_app_type]);
	}
out:
	return ret ;
}

int play_pause(AudioAppType play_app_type)
{
	int ret =0;
	audio_command_t cmd;
	if(play_app_type >= AUDIO_APP_UNDEFINED){
		LOG_ERR("app type (%d) is unknown\n", play_app_type);
		ret = -ESP_ERR_INVALID_ARG;
		goto out;
	}
	memset(&cmd, 0, sizeof(audio_command_t));
	cmd.app = play_app_type;
	cmd.cmd = AUDIO_MANAGER_PLAY_PAUSE;
	cmd.next = NULL;
	if (xQueueAudioService && (pdTRUE == xQueueSend(xQueueAudioService, &cmd, 0))){		
		LOG_INFO("app (%s) play pause send success\n", ams_app_str[play_app_type]);
	}
	else{
		LOG_ERR("app (%s) play pause send failed\n", ams_app_str[play_app_type]);
	}
out:
	return ret;

}
#endif
int play_stop(AudioAppType play_app_type)
{
	int ret = 0;
	audio_command_t cmd;

	if(play_app_type >= AUDIO_APP_UNDEFINED){
		LOG_ERR("app type (%d) is unknown\n", play_app_type);
		ret = -ESP_ERR_INVALID_ARG;
		goto out;
	}
	
	memset(&cmd, 0, sizeof(audio_command_t));
	cmd.app = play_app_type;
	cmd.cmd = AUDIO_MANAGER_PLAY_STOP;
	cmd.next = NULL;
	 if (xQueueAudioService && (pdTRUE == xQueueSend(xQueueAudioService, &cmd, 500))){		
		LOG_INFO("app (%s) play stop send success\n", ams_app_str[play_app_type]);
	 }else{
		LOG_ERR("app (%s) play stop send failed\n", ams_app_str[play_app_type]);
	 }

out:
	return ret;
}


int record_start(AudioAppType record_app_type, void *uri, audio_callback cb, void *cb_param)
{
	int ret = 0;
	audio_command_t cmd;

	if(record_app_type >= AUDIO_APP_UNDEFINED){
		LOG_ERR("app type (%d) is unknown\n", record_app_type);
		ret = -ESP_ERR_INVALID_ARG;
		goto out;
	}

	if(!uri){
		LOG_ERR("url is NULL pointer\n");
		ret = -ESP_ERR_INVALID_ARG;
		goto out;

	}

	if(!cb){
		LOG_ERR("callback is NULL pointer\n");
		ret = -ESP_ERR_INVALID_ARG;
		goto out;

	}

	memset(&cmd, 0, sizeof(audio_command_t));
	cmd.app = record_app_type;
	cmd.cmd = AUDIO_MANAGER_RECORD_START;
	switch(record_app_type){
		case AUDIO_APP_WECHAT:
			strncpy(cmd.record_type, RECORD_TYPE_WECHAT, AUDIO_RECORD_DESCRIP_SIZE);
			strncpy(cmd.file_path, uri, AUDIO_FILE_PATH_SIZE);
			ES8388WriteReg(ES8388_ADCCONTROL10, 0xE2);
			break;
		case AUDIO_APP_VOICE_RECOGNITION:
			strncpy(cmd.record_type, RECORD_TYPE_VOCIE_RECOGNIZE, AUDIO_RECORD_DESCRIP_SIZE);
			cmd.record_buffer = (record_ram_buffer_t *)uri;
			ES8388WriteReg(ES8388_ADCCONTROL10, 0x02);
			break;
		default:
			strncpy(cmd.record_type, RECORD_TYPE_DEFAULT, AUDIO_RECORD_DESCRIP_SIZE);
			strncpy(cmd.file_path, uri, AUDIO_FILE_PATH_SIZE);
			break;
	}

	cmd.callback = cb;
	cmd.callback_param = cb_param;
	cmd.next = NULL;
	 if (xQueueAudioService && (pdTRUE == xQueueSend(xQueueAudioService, &cmd, 0))){		
		LOG_INFO("app (%s) record start url (%s) send success\n", ams_app_str[record_app_type], cmd.file_path);
	 }else{
		LOG_ERR("app (%s) record start url (%s) send failed\n", ams_app_str[record_app_type], cmd.file_path);
	 }

out:
	return ret;
}

int record_stop(AudioAppType record_app_type)
{
	int ret = 0;
	audio_command_t cmd;

	if(record_app_type >= AUDIO_APP_UNDEFINED){
		LOG_ERR("app type (%d) is unknown\n", record_app_type);
		ret = -ESP_ERR_INVALID_ARG;
		goto out;
	}
	
	memset(&cmd, 0, sizeof(audio_command_t));
	cmd.app = record_app_type;
	cmd.cmd = AUDIO_MANAGER_RECORD_STOP;
	cmd.next = NULL;
	 if (xQueueAudioService && (pdTRUE == xQueueSend(xQueueAudioService, &cmd, 0))){		
		LOG_INFO("app (%s) record stop send success\n", ams_app_str[record_app_type]);
	 }else{
		LOG_ERR("app (%s) record stop send failed\n", ams_app_str[record_app_type]);
	 }

out:
	return ret;
}

void volume_up(void)
{
	audio_command_t *cmd;

	cmd = (audio_command_t *)heap_caps_malloc(sizeof(audio_command_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);	
	if(cmd){
		memset(cmd, 0, sizeof(audio_command_t));
		cmd->cmd = AUDIO_MANAGER_VOL_UP;
		if (xQueueAudioService && (pdTRUE == xQueueSend(xQueueAudioService, cmd, 0))){		
			LOG_INFO("volume up send success\n");
		}else{
			LOG_INFO("volume up send failed\n");
		}
		free(cmd);
	}else{
		LOG_ERR("malloc cmd ptr failed\n");
	}
	return;
}

void volume_down(void)
{
	audio_command_t *cmd;

	cmd = (audio_command_t *)heap_caps_malloc(sizeof(audio_command_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);	
	if(cmd){
		memset(cmd, 0, sizeof(audio_command_t));
		cmd->cmd = AUDIO_MANAGER_VOL_DOWN;
		if (xQueueAudioService && (pdTRUE == xQueueSend(xQueueAudioService, cmd, 0))){		
			LOG_INFO("volume down send success\n");
		}else{
			LOG_INFO("volume down send failed\n");
		}
		free(cmd);
	}else{
		LOG_ERR("malloc cmd ptr failed\n");
	}

	return;
}

static void play_tone_sync_cb(NotifyStatus status,AudioAppType type,AudioManagerMsg cmd, void *param)
{
	xQueueHandle wait_q = (xQueueHandle)param;
	NotifyStatus notify;
	
	if(wait_q){
		notify = status;
		xQueueSend(wait_q, &notify, 0);
	}
}

int play_tone_sync(char *url)//sync interface for wait tone finish
{
	int ret = 0;
	xQueueHandle wait_q = NULL;
	NotifyStatus notify;
	play_param_t param;

	if(url){
		wait_q = xQueueCreate(1, sizeof(NotifyStatus));
		if(wait_q){
			param.play_app_type = AUDIO_APP_NOTIFY;
			param.is_local_file = true;
			param.uri = url;
			param.tone = NULL;
			param.cb = play_tone_sync_cb;
			param.cb_param = (void *)wait_q;
			play_start(&param);
			if(pdTRUE == xQueueReceive(wait_q, &notify, pdMS_TO_TICKS(10000))){
				LOG_INFO("tone play result (%d).\n", notify);
				if(notify != NOTIFY_STATUS_PLAY_FINISH)
					ret = -ESP_ERR_INVALID_STATE;
			}else{
				ret = -ESP_ERR_TIMEOUT;
				LOG_INFO("tone play timeout.\n");
			}
			vQueueDelete(wait_q);
		}else{
			LOG_ERR("create tone wait queue fail\n");
			ret = -ESP_ERR_NO_MEM;
		}
	}
	return ret;
}
int play_tone_and_restart(char *url)
{
	
	int ret = 0;
	xQueueHandle wait_q = NULL;
	NotifyStatus notify;
	play_param_t param;
	PlayerStatus status = {0};
	audio_command_t *cmd = NULL;
	if(url){
		wait_q = xQueueCreate(1, sizeof(NotifyStatus));
		if(wait_q){
			cmd = g_audio_manager.current;
			g_audio_service->Based.getPlayerStatus((MediaService *)g_audio_service, &status);
			if((status.status == 1)&&((cmd->app == AUDIO_APP_HABIT)||(cmd->app == AUDIO_APP_ENGLISH))){
				ESP_LOGI("ONEGO","app = %d	path = %s callback = %x",cmd->app,cmd->file_path,(unsigned int)cmd->callback);
				memset(&tone_cmd,0,sizeof(tone_command_t));
				tone_cmd.app = cmd->app;
				strncpy(tone_cmd.file_path,cmd->file_path+7,sizeof(tone_cmd.file_path));
				tone_cmd.callback = cmd->callback;
				tone_cmd.callback_param = cmd->callback_param;
			}
			param.play_app_type = AUDIO_APP_NOTIFY;
			param.is_local_file = true;
			param.uri = url;
			param.tone = NULL;
			param.cb = play_tone_sync_cb;
			param.cb_param = (void *)wait_q;
			play_start(&param);
			if(pdTRUE == xQueueReceive(wait_q, &notify, pdMS_TO_TICKS(10000))){
				LOG_INFO("tone play result (%d).\n", notify);
				if(notify != NOTIFY_STATUS_PLAY_FINISH)
					ret = -ESP_ERR_INVALID_STATE;
				if(notify == 0){
					if((tone_cmd.callback != NULL) && (tone_cmd.callback_param != NULL)){
						ESP_LOGI("ONEGO tone_cmd","app = %d path = %s callback = %x",tone_cmd.app,tone_cmd.file_path,(unsigned int)tone_cmd.callback);
						tone_cmd.callback(NOTIFY_STATUS_PLAY_RESTART, tone_cmd.app,AUDIO_MANAGER_PLAY_START,tone_cmd.callback_param);
						param.play_app_type = tone_cmd.app;
						param.is_local_file = true;
						param.uri = tone_cmd.file_path;
						param.tone = NULL;
						param.cb =	tone_cmd.callback;
						param.cb_param = (void *)tone_cmd.callback_param;
						play_start(&param);
						memset(&tone_cmd,0,sizeof(tone_command_t));
					}
				}
				
			}else{
				ret = -ESP_ERR_TIMEOUT;
				LOG_INFO("tone play timeout.\n");
			}
			vQueueDelete(wait_q);
		}else{
			LOG_ERR("create tone wait queue fail\n");
			ret = -ESP_ERR_NO_MEM;
		}
	}
	return ret;

}

static void PlayerStatusUpdatedToAudio(ServiceEvent *event)
{
    LOG_INFO("PlayerStatusUpdatedToAudio\n");
}

static inline void notify_app_status(audio_command_t *command, NotifyStatus status);

static bool record_exceed_limit = false;
static bool record_task_run = false;
#define AUDIO_PCM_RATE  16000
#define AUDIO_PCM_10MS_FRAME_SIZE	(AUDIO_PCM_RATE*10)/1000 
#define AUDIO_RECORD_TIME		100
#define AUDIO_RECORD_SIZE  		32*AUDIO_RECORD_TIME

void record_thread(void *param)
{
	audio_command_t *command = NULL;
    FILE *fp = NULL;
	record_ram_buffer_t *out_buffer = NULL;
	int len = 0;
	PlayerStatus pst;
	amr_code_node code_node;
	record_cmd_node record_node;
	
	while(1){
		if(pdTRUE == xQueueReceive(record_queue, &record_node, portMAX_DELAY)){
			command = (audio_command_t *)record_node.param;
			record_exceed_limit = false;
			len = 0;
			switch(record_node.cmd_value){
				case AUDIO_APP_VOICE_RECOGNITION:
					LOG_INFO("start record deepbrain voice\n");
					if(command->record_buffer){
						char *pbuff = NULL;		
						out_buffer = command->record_buffer;		
						out_buffer->is_overflow = false;			
						out_buffer->used_size = 0;
						out_buffer->vad_slient_ms = 0;
						out_buffer->vad_enable = false;
						out_buffer->talk_ms = 0;
						pbuff = out_buffer->buffer;
						record_task_run = true;
						LOG_INFO("Raw data get mutex...\n");
						xSemaphoreTake(g_rthread_stop, portMAX_DELAY);
						LOG_INFO("Raw data reading...\n");
						g_audio_term->Based.rawStart((MediaService *)g_audio_term, command->record_type);
						vTaskDelay(20);
						while(record_task_run){
							memset(pbuff, 0, AUDIO_RECORD_SIZE);
							if(g_audio_term->Based.rawRead((MediaService *)g_audio_term, pbuff,AUDIO_RECORD_SIZE, &len)){
								LOG_INFO("Raw data read error, stop now\n");
								pst.status = AUDIO_STATUS_ERROR;
								xQueueSend(xQuePlayerStatus, &pst, 0);//self notfiy ams record fail
								break;
							}
							out_buffer->used_size = len;
							notify_app_status(g_audio_manager.current,NOTIFY_STATUS_RECORD_READ);//send data to speech recognition  

							code_node.cmd_value = CODE_PROCEED;
							code_node.param = (void*)out_buffer;
							code_node.code_len = len;
						 	if(code_queue && xQueueSend(code_queue, &code_node, 10) != pdTRUE){
								LOG_INFO("code proceed fail\n");
						 	}
							
							//LOG_INFO("Raw data read len:%d, used size:%d, total size:%d...\n", len, out_buffer->used_size, out_buffer->total_size);
							if(pdPASS == xSemaphoreTake(g_rthread_run, pdMS_TO_TICKS(AUDIO_RECORD_TIME))){
								LOG_INFO("receive exit cmd, stop immediately\n");
								xSemaphoreGive(g_rthread_run);
								break;
							}
						}
					}
					if(out_buffer->vad_enable == true)
						notify_app_status(g_audio_manager.current,NOTIFY_STATUS_RECORD_SEND);//send data to speech recognition  		
					g_audio_term->Based.rawStop((MediaService *)g_audio_term, TERMINATION_TYPE_NOW);
					LOG_INFO("Raw data reach end\n");
					xSemaphoreGive(g_rthread_stop);
					break;
				case AUDIO_APP_WECHAT:
					LOG_INFO("start record wechat voice\n");
					int file_size = 0;
					char *buf = EspAudioAlloc(1, 320);
					fp = fopen(command->file_path , "w+");
					if((fp == NULL) || (buf == NULL)){
						LOG_INFO("record thread begin fail, url (%s), (%s %s)\n", command->file_path, 
							fp?" ":"open file fail", buf?" ":"malloc buffer fail");
						if(buf)
							free(buf);
						pst.status = AUDIO_STATUS_ERROR;
						xQueueSend(xQuePlayerStatus, &pst, 0);//self notfiy ams record fail
						break;
					}
					LOG_INFO("Raw data get mutex...\n");
					xSemaphoreTake(g_rthread_stop, portMAX_DELAY);
					LOG_INFO("Raw data reading...\n");
					g_audio_term->Based.rawStart((MediaService *)g_audio_term, command->record_type);
					while(true){
						if(pdPASS == xSemaphoreTake(g_rthread_run, pdMS_TO_TICKS(10))){
							LOG_INFO("receive exit cmd, stop immediately\n");
							xSemaphoreGive(g_rthread_run);
							break;
						}else{
							if(g_audio_term->Based.rawRead((MediaService *)g_audio_term, buf, 320, &len)){
								LOG_INFO("Raw data read error, stop now\n");
								break;
							}
							fwrite(buf , 1 , len , fp);
							file_size += len;
							LOG_DBG("Raw data read len:%d, write this frame, file size:%d...\n", len, file_size);
							if(file_size >= RECORD_FILE_MAX_SIZE){
								LOG_INFO("record file exceed limit, stop now\n");
								record_exceed_limit = true;
								break;
							}
						}
					}
					fclose(fp);
					free(buf);
					g_audio_term->Based.rawStop((MediaService *)g_audio_term, TERMINATION_TYPE_NOW);
					LOG_INFO("Raw data reach end\n");
					xSemaphoreGive(g_rthread_stop);
					break;
				default:
					LOG_INFO("cmd ignore\n");
					break;
			}
		}
	}
    vTaskDelete(NULL);
}

void amr_code_task(void *param)
{
	audio_command_t *command = NULL;
	record_ram_buffer_t *record_obj = NULL;
	amr_code_node code_node;
	unsigned char*pcm_data = NULL;
	int ret = 0;

	while(1){
		if(pdTRUE == xQueueReceive(code_queue, &code_node, portMAX_DELAY)){
			switch(code_node.cmd_value){
				case CODE_PROCEED:
					record_obj = (audio_command_t *)code_node.param;
					if((record_obj != NULL) && (record_task_run) &&(!record_obj->vad_enable)){
						if(record_obj->vad_handler == NULL){
							record_obj->vad_handler = DB_Vad_Create(2);
							if(record_obj->vad_handler == NULL)
								break;
						}
						if(pcm_data == NULL){
							pcm_data = EspAudioAlloc(1, AUDIO_RECORD_SIZE);
							if(pcm_data == NULL){
								DB_Vad_Free(record_obj->vad_handler);
								record_obj->vad_handler =NULL;
								break;
							}	
						}
						int frame_len = record_obj->used_size;
						int frame_start = 0;
						memcpy(pcm_data,record_obj->buffer,AUDIO_RECORD_SIZE);
						while (frame_len >= RAW_PCM_LEN_MS(10, PCM_SAMPLING_RATE_16K)){
							ret = DB_Vad_Process(record_obj->vad_handler, PCM_SAMPLING_RATE_16K, RAW_PCM_LEN_MS(10, PCM_SAMPLING_RATE_16K)/2, (int16_t*)(pcm_data+frame_start));
							if (ret == 1){		
								record_obj->talk_ms += 10;
								record_obj->vad_slient_ms = 0;
								record_obj->record_timer = 0;
							}else{
								record_obj->vad_slient_ms +=10;
							}
							//ESP_LOGI("ONEGO","vad ret = %d frame_len = %d frame_start = %d",ret,frame_len,frame_start);
							frame_len -= RAW_PCM_LEN_MS(10, PCM_SAMPLING_RATE_16K);
							frame_start += RAW_PCM_LEN_MS(10, PCM_SAMPLING_RATE_16K);
							if ((record_obj->vad_slient_ms >= 100) && (record_obj->talk_ms >= 400))
							{
								LOG_INFO("speech off\n");
								record_task_run = false;
								record_obj->record_timer = 0;
								record_obj->vad_enable = true;
								DB_Vad_Free(record_obj->vad_handler);
								record_obj->vad_handler =NULL;
								free(pcm_data);
								pcm_data = NULL;
								//notify_app_status(g_audio_manager.current,NOTIFY_STATUS_RECORD_SEND);//send data to speech recognition  	
								break;
							}
						}
					}

					break;
				case CODE_QUIT:
					LOG_INFO("exit amrwb encode and free source\n");
					break;
				default:
					break;
					
			}
			
		}
	}
	vTaskDelete(NULL);
}

static inline void notify_app_status(audio_command_t *command, NotifyStatus status)
{
	LOG_INFO("notify app (%s) cmd (%s) status (%s)\n", ams_app_str[command->app], ams_msg_str[command->cmd], ams_notify_str[status]);
	#ifdef CONFIG_ENABLE_POWER_MANAGER
	if((status >= NOTIFY_STATUS_PLAY_FINISH) && (status < NOTIFY_STATUS_RECORD_STOPPED)){
		if(music_wake_lock!=NULL)
			release_wake_lock(music_wake_lock);
	}
	#endif
	if(command->callback){
		command->callback(status, command->app,command->cmd,command->callback_param);
	}
	return;
}
#ifdef STORY_PLAY_PAUSE
static inline bool execute_pause_command(audio_manager_t *manager, audio_command_t *command)//WARNING:only called by execute_audio_command
{
	bool execute_result = true;
	if(manager->current){
		switch(manager->state){
			case AUDIO_MANAGER_ST_PLAYING:
				if(!g_audio_service->Based._blocking){
					g_audio_service->Based.mediaPause((MediaService *)g_audio_service);
					notify_app_status(manager->current, NOTIFY_STATUS_PLAY_PAUSE);
					LOG_INFO("pause current app (%s) playing\n", ams_app_str[manager->current->app]);
					manager->state = AUDIO_MANAGER_ST_PAUSE_PLAY;
				}
				else{
					execute_result = false;
				}
				break;
			case AUDIO_MANAGER_ST_IDLE:
				LOG_INFO("audio manager state idle\n");
				break;
			default:
				LOG_ERR("audio manager state (%d) not supported, ignore new cmd\n", manager->state);
				execute_result = false;
				break;
			
		}
	}
	return execute_result; 

}

static inline bool execute_resume_command(audio_manager_t *manager, audio_command_t *command)
{
	bool execute_result = true;
	if(manager->current){
		switch(manager->state){
			case AUDIO_MANAGER_ST_PAUSE_PLAY:
				if(!g_audio_service->Based._blocking){
					if(music_wake_lock!=NULL)
						acquire_wake_lock(music_wake_lock);
					g_audio_service->Based.mediaResume((MediaService *)g_audio_service);
					LOG_INFO("resume current app (%s) playing\n", ams_app_str[manager->current->app]);
					manager->state = AUDIO_MANAGER_ST_PLAYING;
				}
				else{
					execute_result = false;
				}
				break;
			case AUDIO_MANAGER_ST_IDLE:
				LOG_INFO("audio manager state idle\n");
				break;
			default:
				LOG_ERR("audio manager state (%d) not supported, ignore new cmd\n", manager->state);
				execute_result = false;
				break;
		}
	}

	return execute_result; 
}
#endif
static inline bool execute_stop_command(audio_manager_t *manager, audio_command_t *command)//WARNING:only called by execute_audio_command
{
	bool execute_result = true;
	audio_command_t *stop_cmd;

	if(manager->current){
		switch(manager->state){
			case AUDIO_MANAGER_ST_PLAYING:
			#ifdef STORY_PLAY_PAUSE
			case AUDIO_MANAGER_ST_PAUSE_PLAY:
			#endif
				if(!g_audio_service->Based._blocking){
					#ifdef STORY_PLAY_PAUSE
					uint32_t time;
					time= g_audio_service->Based.getPosByTime((MediaService *)g_audio_service);
					if((manager->current->app == AUDIO_APP_HABIT) ||(manager->current->app == AUDIO_APP_ENGLISH)){
						if(manager->current->callback_param){
							item_entry_t *entry;
							entry = (item_entry_t *)manager->current->callback_param;
							entry->time=time;
							LOG_INFO("stop play and record play time:%d ms\n",time);
						}
					}
					#endif
					PA_DISABLE();
					g_audio_service->Based.mediaStop((MediaService *)g_audio_service);
					if(command){
						free(command);
						notify_app_status(manager->current, NOTIFY_STATUS_PLAY_STOPPED);
						LOG_INFO("stop current app (%s) playing\n", ams_app_str[manager->current->app]);
					}else{
						notify_app_status(manager->current, NOTIFY_STATUS_PLAY_DISTURBED);
						LOG_INFO("disturb current app (%s) playing\n", ams_app_str[manager->current->app]);
					}					

					manager->state = AUDIO_MANAGER_ST_STOP_PLAY;
				}else{
					LOG_ERR("stop current app (%s) playing blocked, ignore new cmd\n", ams_app_str[manager->current->app]);
					execute_result = false;
				}
				break;
			case AUDIO_MANAGER_ST_RECORDING:
				xSemaphoreGive(g_rthread_run);
				xSemaphoreTake(g_rthread_stop, portMAX_DELAY);
				xSemaphoreGive(g_rthread_stop);	//wait record thread quit		
				if(command){
					free(command);
					notify_app_status(manager->current, NOTIFY_STATUS_RECORD_STOPPED);
					LOG_INFO("stop current app (%s) recording\n", ams_app_str[manager->current->app]);
				}else{
					notify_app_status(manager->current, NOTIFY_STATUS_RECORD_DISTURBED);
					LOG_INFO("disturb current app (%s) recording\n", ams_app_str[manager->current->app]);
				}
				manager->state = AUDIO_MANAGER_ST_STOP_RECORD;	
				break;
				
			case AUDIO_MANAGER_ST_IDLE:
				LOG_INFO("audio manager state idle\n");
				break;
			default:
				LOG_ERR("audio manager state (%d) not supported, ignore new cmd\n", manager->state);
				execute_result = false;
				break;
		}
	}

	return execute_result;
}

static inline bool execute_start_command(audio_manager_t *manager, audio_command_t *command)//WARNING:only called by execute_audio_command
{
	bool execute_result = true;
	struct MusicInfo info = {0};
	
	if(!manager->current){
		switch(command->cmd){
			case AUDIO_MANAGER_PLAY_START:
				if(!g_audio_service->Based._blocking){
					if(music_wake_lock!=NULL)
						acquire_wake_lock(music_wake_lock);
					
					LOG_INFO("start app (%s) execute cmd success\n", ams_app_str[command->app]);
					if(command->tone_path[0]){//play tone first
						g_audio_service->Based.setPlayMode((MediaService *)g_audio_service, MEDIA_PLAY_ONE_SONG);
						g_audio_service->Based.addUri((MediaService *)g_audio_service, command->tone_path);
						g_audio_service->Based.mediaPlay((MediaService *)g_audio_service);
						command->tone_path[0] = 0;
						manager->current = command;
						manager->current_type = AUDIO_TYPE_TONE;
						manager->state = AUDIO_MANAGER_ST_START_PLAY;//need to debug	
					}else{
						g_audio_service->Based.setPlayMode((MediaService *)g_audio_service, MEDIA_PLAY_ONE_SONG);
						g_audio_service->Based.addUri((MediaService *)g_audio_service, command->file_path);
						g_audio_service->Based.getSongInfo((MediaService *)g_audio_service,&info);
						g_audio_service->Based.mediaPlay((MediaService *)g_audio_service);
						manager->current = command;
						manager->current_type = AUDIO_TYPE_SONG;
						manager->state = AUDIO_MANAGER_ST_START_PLAY;//need to debug
					}
					PA_ENABLE();
					#ifdef STORY_PLAY_PAUSE
					if((manager->current_type == AUDIO_TYPE_SONG)&&((manager->current->app == AUDIO_APP_HABIT) ||(manager->current->app == AUDIO_APP_ENGLISH))){
						if(manager->current->callback_param){
							item_entry_t *entry;
							entry = (item_entry_t *)manager->current->callback_param;
							int time = (entry->time/1000);
							if(time > 0)
								g_audio_service->Based.seekByTime((MediaService *)g_audio_service,time);
							LOG_INFO("play start story and seetByTime = %d s\n",time);
						}
						uint32_t total_time = info.totalTime;
						LOG_INFO("total time = %d\n",total_time);
						send_device_status(false,true,false,total_time);
						
					}
					#endif	
				}else{
					LOG_ERR("start app (%s) play (%s) blocked, execute cmd fail\n", ams_app_str[command->app], command->file_path);
					execute_result = false;
				}
				break;
			case AUDIO_MANAGER_RECORD_START:
				xSemaphoreTake(g_rthread_run, portMAX_DELAY);
				record_cmd_node node;
				node.cmd_value = command->app;
				node.param = (void *)command; 
                if(record_queue && xQueueSend(record_queue, &node, 500) == pdTRUE){
					manager->current = command;
					manager->state = AUDIO_MANAGER_ST_START_RECORD;//need to debug
					LOG_INFO("start app (%s) record (%s), execute cmd success\n", ams_app_str[command->app], command->file_path);
				}else{
					LOG_ERR("start app (%s) record (%s) blocked, execute cmd fail\n", ams_app_str[command->app], command->file_path);
					xSemaphoreGive(g_rthread_run);
					execute_result = false;
				}
				break;
			default:
				LOG_ERR("unexpected command (%d) app (%s) ignore it\n", command->cmd, ams_app_str[command->app]);
				execute_result = false;
				break;
		}
	}else{
		LOG_ERR("audio manager not idle, current app (%s), command (%s)\n", ams_app_str[manager->current->app], ams_msg_str[manager->current->cmd]);
	}

	return execute_result;
}

static inline void ignore_all_command(audio_manager_t *manager)//WARNING:only called by execute_audio_command
{
	audio_command_t *node;

	node = manager->head;
	while(node){
		delete_cmd(manager, node);
		notify_app_status(node, NOTIFY_STATUS_COMMAND_IGNORED);
		//LOG_DBG("notify app (%s) cmd (%s) ignored\n", ams_app_str[node->app], ams_msg_str[node->cmd]);
		free(node);
		node = manager->head;
	}

	return;
}

static int update_audio_manager_state(audio_manager_t *manager, PlayerStatus *pst)//assign state only after receive player callback
{//only notify error & finish here, disturbed & stop notify by execute_stop_command
	audio_command_t *temp;

	if(manager){
		switch(manager->state){
			case AUDIO_MANAGER_ST_START_PLAY:
				if(AUDIO_STATUS_PLAYING == pst->status){
					//play success
					#ifdef STORY_PLAY_PAUSE
					if(((manager->current->app == AUDIO_APP_HABIT) ||(manager->current->app == AUDIO_APP_ENGLISH)) && (manager->current_type ==AUDIO_TYPE_SONG)){
						manager->cmd.cmd_value = PR_STOP;
						manager->cmd.param = (void*)g_audio_service;
						notification_record_play_time(&manager->cmd);
					}
					#endif
					manager->state = AUDIO_MANAGER_ST_PLAYING;
				}else if(AUDIO_STATUS_ERROR == pst->status){
					//play failed
					PA_DISABLE();
					notify_app_status(manager->current, NOTIFY_STATUS_PLAY_ERROR);
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
					manager->current_type = AUDIO_TYPE_UNDEFINE;
				}else{
					//unexpect
				}
				break;
			case AUDIO_MANAGER_ST_PLAYING:
				if((AUDIO_STATUS_STOP == pst->status) ||(AUDIO_STATUS_FINISHED == pst->status)){
					//play finish
					if(manager->current_type == AUDIO_TYPE_TONE){//tone finish need play file
						manager->state = AUDIO_MANAGER_ST_IDLE;
						temp = manager->current;
						manager->current = NULL;						
						execute_start_command(manager, temp);
					}else{
						PA_DISABLE();
						notify_app_status(manager->current, NOTIFY_STATUS_PLAY_FINISH);
						manager->state = AUDIO_MANAGER_ST_IDLE;
						free(manager->current);
						manager->current = NULL;
						manager->current_type = AUDIO_TYPE_UNDEFINE;
					}
				}else if(AUDIO_STATUS_ERROR == pst->status){
					//playing failed
					PA_DISABLE();
					notify_app_status(manager->current, NOTIFY_STATUS_PLAY_ERROR);
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
					manager->current_type = AUDIO_TYPE_UNDEFINE;
				}else{
					//unexpect
				}
				break;
			case AUDIO_MANAGER_ST_STOP_PLAY:
				if((AUDIO_STATUS_STOP == pst->status) ||(AUDIO_STATUS_FINISHED == pst->status)){
					//stop play
					//PA_DISABLE();
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
					manager->current_type = AUDIO_TYPE_UNDEFINE;
				}else if(AUDIO_STATUS_ERROR == pst->status){
					//PA_DISABLE();
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
					manager->current_type = AUDIO_TYPE_UNDEFINE;
				}else{
					//unexpect
				}
				break;
			#ifdef STORY_PLAY_PAUSE
			case AUDIO_MANAGER_ST_PAUSE_PLAY:
				if(AUDIO_STATUS_PAUSED == pst->status){
					//pause success
					manager->state = AUDIO_MANAGER_ST_PAUSE_PLAY;
				}else if(AUDIO_STATUS_ERROR == pst->status){
					//pause failed
					PA_DISABLE();
					notify_app_status(manager->current, NOTIFY_STATUS_PLAY_ERROR);
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
					manager->current_type = AUDIO_TYPE_UNDEFINE;
				}else{
					//unexpect
				}
				break;
			case AUDIO_MANAGER_ST_RESUME_PLAY:
				if(AUDIO_STATUS_PLAYING == pst->status){
					//play success
					manager->state = AUDIO_MANAGER_ST_PLAYING;
				}else if(AUDIO_STATUS_ERROR == pst->status){
					//play failed
					PA_DISABLE();
					notify_app_status(manager->current, NOTIFY_STATUS_PLAY_ERROR);
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
					manager->current_type = AUDIO_TYPE_UNDEFINE;
				}else{
					//unexpect
				}
				break;
			#endif
			case AUDIO_MANAGER_ST_START_RECORD:
				if(AUDIO_STATUS_PLAYING == pst->status){
					//record start success
					manager->state = AUDIO_MANAGER_ST_RECORDING;
				}else if(AUDIO_STATUS_ERROR == pst->status){
					//record start failed
					xSemaphoreGive(g_rthread_run);
					notify_app_status(manager->current, NOTIFY_STATUS_RECORD_ERROR);
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
				}else{
					//unexpect
				}
				break;
			case AUDIO_MANAGER_ST_RECORDING:
				if((AUDIO_STATUS_STOP == pst->status) ||(AUDIO_STATUS_FINISHED == pst->status)){
					//record thread read not read trigger stop
					xSemaphoreGive(g_rthread_run);
					if(record_exceed_limit){
						notify_app_status(manager->current, NOTIFY_STATUS_RECORD_EXCEED);
					}else{
						notify_app_status(manager->current, NOTIFY_STATUS_RECORD_STOPPED);
					}
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
				}else if(AUDIO_STATUS_ERROR == pst->status){
					//recording failed
					xSemaphoreGive(g_rthread_run);
					notify_app_status(manager->current, NOTIFY_STATUS_RECORD_ERROR);
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
				}else{
					//unexpect
				}
				break;
			case AUDIO_MANAGER_ST_STOP_RECORD:
				if((AUDIO_STATUS_STOP == pst->status) ||(AUDIO_STATUS_FINISHED == pst->status)){
					//stop record
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
				}else if(AUDIO_STATUS_ERROR == pst->status){
					manager->state = AUDIO_MANAGER_ST_IDLE;
					free(manager->current);
					manager->current = NULL;
				}else{
					//unexpect
				}
				break;
			default:
				LOG_ERR("unexpected reach here\n");
				break;
		}		
	}
	return 0;
}

static bool execute_audio_command(audio_manager_t *manager)
{
	bool delay_process = false;
	AudioManagerMsg search_msg = AUDIO_MANAGER_UNDEFINE;
	audio_command_t *start_cmd = NULL;
	audio_command_t *stop_cmd = NULL;
	#ifdef STORY_PLAY_PAUSE
	audio_command_t *pause_cmd = NULL;
	audio_command_t *resume_cmd = NULL;
	#endif
	audio_command_t *lastest_cmd = NULL;

	if(manager){
		switch(manager->state){
			case AUDIO_MANAGER_ST_START_PLAY:
			case AUDIO_MANAGER_ST_STOP_PLAY:
			case AUDIO_MANAGER_ST_START_RECORD:
			case AUDIO_MANAGER_ST_STOP_RECORD:
				delay_process = true;
				break;
			default:
				break;
		}

		if(delay_process){
			LOG_INFO("audio manager state (%d), delay process new cmd\n", manager->state);
		}else{
		//********************************************
		//WARNING: only 3 state can reach here
		//1- playing 2- recording 3-idle
		//other state can't reach here must delay process
		//********************************************
			if(manager->head){//command list not empty
				start_cmd = search_last_start_cmd(manager);
				if(start_cmd){//last start app occupy audio control rights, other will ignore
					lastest_cmd = search_cmd_by_app(manager, start_cmd->app, start_cmd);
					if(lastest_cmd){//occupy app have stop cmd, execute stop
						if(manager->current){//stop current first
							search_msg = (manager->state == AUDIO_MANAGER_ST_PLAYING)?AUDIO_MANAGER_PLAY_STOP:AUDIO_MANAGER_RECORD_STOP;
							stop_cmd = search_app_cmd_by_msg(manager, manager->current->app, search_msg);
							if(stop_cmd){
								delete_cmd(manager, stop_cmd);
							}
							LOG_INFO("execute_stop_command occupy app have stop cmd, execute stop \n");
							execute_stop_command(manager, stop_cmd);
						}
						ignore_all_command(manager);
					}else{//occupy app no other cmd, execute start
						delete_cmd(manager, start_cmd);
						if(manager->current){//stop current first, start after esp32 cb
							search_msg = (manager->state == AUDIO_MANAGER_ST_PLAYING)?AUDIO_MANAGER_PLAY_STOP:AUDIO_MANAGER_RECORD_STOP;
							stop_cmd = search_app_cmd_by_msg(manager, manager->current->app, search_msg);
							if(stop_cmd){
								delete_cmd(manager, stop_cmd);
							}
							LOG_INFO("execute_stop_command stop current first, start after esp32 cb \n");
							execute_stop_command(manager, stop_cmd);
							/***********************************
   							NOTE: only process one command in
   							this function, but this situation
   							need two command, 1 stop, 2 start,
   							so we execute stop first, re-insert 
   							start command to list, wait esp32 
   							player cb to continue execute
							***********************************/
							ignore_all_command(manager);
							insert_cmd(manager, start_cmd);
						}else{//no current, ams is idle, start here
							execute_start_command(manager, start_cmd);
							ignore_all_command(manager);
						}	
					}
				}else{//no start app, all stop
					if(manager->current){//stop current first
						search_msg = (manager->state == AUDIO_MANAGER_ST_PLAYING)?AUDIO_MANAGER_PLAY_STOP:AUDIO_MANAGER_RECORD_STOP;
						stop_cmd = search_app_cmd_by_msg(manager, manager->current->app, search_msg);
						if(stop_cmd){
							delete_cmd(manager, stop_cmd);
							LOG_INFO("execute_stop_command stop current first \n");
							execute_stop_command(manager, stop_cmd);
						}
						#ifdef STORY_PLAY_PAUSE
						search_msg = (manager->state == AUDIO_MANAGER_ST_PAUSE_PLAY)?AUDIO_MANAGER_ST_STOP_PLAY:AUDIO_MANAGER_UNDEFINE;
						stop_cmd = search_app_cmd_by_msg(manager, manager->current->app, search_msg);
						if(stop_cmd){
							delete_cmd(manager, stop_cmd);
							LOG_INFO("execute_stop_command stop pause first \n");
							execute_stop_command(manager, stop_cmd);
							goto out;
						}
						
						search_msg = (manager->state == AUDIO_MANAGER_ST_PLAYING)?AUDIO_MANAGER_ST_PAUSE_PLAY:AUDIO_MANAGER_UNDEFINE;
						pause_cmd = search_app_cmd_by_msg(manager, manager->current->app, search_msg);
						if(pause_cmd){
							delete_cmd(manager, pause_cmd);
							LOG_INFO("execute_pause_command pause current first \n");
							execute_pause_command(manager, pause_cmd);
							goto out;
						}
						search_msg = (manager->state == AUDIO_MANAGER_ST_PAUSE_PLAY)?AUDIO_MANAGER_ST_RESUME_PLAY:AUDIO_MANAGER_UNDEFINE;
						resume_cmd = search_app_cmd_by_msg(manager, manager->current->app, search_msg);
						if(resume_cmd){
							delete_cmd(manager, resume_cmd);
							LOG_INFO("execute_resume_command resume current first \n");
							execute_resume_command(manager, resume_cmd);
							goto out;
						}
						#endif
					}
#ifdef STORY_PLAY_PAUSE
out:
#endif					
					ignore_all_command(manager);
				}
			}
		}
	}

	return delay_process;
}

void AudioManagerServiceTask(void *pv)
{
	PlayerStatus playstatus;
	audio_command_t audiocommand;
	audio_command_t *new_command;
	nv_item_t nv_volume;
 
	g_audio_manager.current = NULL;
	g_audio_manager.head = NULL;
	g_audio_manager.state = AUDIO_MANAGER_ST_IDLE;
	nv_volume.name = NV_ITEM_AUDIO_VOLUME;

    xQueSet = xQueueCreateSet(AUDIO_ESP32_PLAYER_QUEUE_SIZE + AUDIO_MANGER_COMMAND_QUEUE_SIZE);
    configASSERT(xQueSet);
	xQuePlayerStatus = xQueueCreate(AUDIO_ESP32_PLAYER_QUEUE_SIZE, sizeof(PlayerStatus));
    configASSERT(xQuePlayerStatus);
    xQueueAudioService = xQueueCreate(AUDIO_MANGER_COMMAND_QUEUE_SIZE, sizeof(audio_command_t));
    configASSERT(xQueueAudioService);

	g_audio_service = (AudioManagerService *) pv;
    g_audio_service->Based.addListener((MediaService *)g_audio_service, xQuePlayerStatus);
    xQueueAddToSet(xQuePlayerStatus, xQueSet);
    xQueueAddToSet(xQueueAudioService, xQueSet);

	g_rthread_run = xSemaphoreCreateMutex();
	g_rthread_stop = xSemaphoreCreateMutex();

    while(1){
		memset(&audiocommand, 0, sizeof(audio_command_t));
		memset(&playstatus, 0, sizeof(PlayerStatus));
        if(xQueueAudioService == xQueueSelectFromSet(xQueSet, portMAX_DELAY)){
            if(pdPASS == xQueueReceive(xQueueAudioService, &audiocommand, 0)){				
                switch (audiocommand.cmd) {
                    case AUDIO_MANAGER_VOL_UP:
                        LOG_INFO("vol up\n");
                        if (g_audio_service->Based._blocking) {
                            int vol;
                            if (MediaHalGetVolume(&vol) == 0) {
								vol = (vol + AUDIO_VOLUME_STEP > 100) ? vol : (vol + AUDIO_VOLUME_STEP);
                                if (MediaHalSetVolume(vol) == 0) {
                                    LOG_INFO("vol up, current vol = %d\n", vol);
									nv_volume.value = (int64_t)vol;
									set_nv_item(&nv_volume);
                                }else{
									LOG_ERR("vol up failed...");
								}
                            }
                        } else {
                            if (g_audio_service->Based.setVolume) {
                                if (g_audio_service->Based.setVolume((MediaService *) g_audio_service, 1, AUDIO_VOLUME_STEP) < 0) {
                                    LOG_ERR("Cannot set volume now\n");
                                }else{
                                	if(g_audio_service->Based.getVolume){
										nv_volume.value = (int64_t)g_audio_service->Based.getVolume((MediaService *) g_audio_service);
										LOG_INFO("vol up, current vol = %lld\n", nv_volume.value);
										set_nv_item(&nv_volume);
									}
								}
                            }
                        }
						//send_device_status(false,false,true,0);
                    	break;
                    case AUDIO_MANAGER_VOL_DOWN:
                        LOG_INFO("vol down\n");
                        if (g_audio_service->Based._blocking) {
                            int vol;
                            if (MediaHalGetVolume(&vol) == 0) {
								vol = (vol - AUDIO_VOLUME_STEP < 0) ? vol : (vol - AUDIO_VOLUME_STEP);
                                if (MediaHalSetVolume(vol) == 0) {
                                    LOG_INFO("vol down, current vol = %d\n", vol);
									nv_volume.value = (int64_t)vol;
									set_nv_item(&nv_volume);
                                }else{
									LOG_ERR("vol down failed...");
								}
                            }
                        } else {
                            if (g_audio_service->Based.setVolume) {
                                if (g_audio_service->Based.setVolume((MediaService *) g_audio_service, 1, 1 - AUDIO_VOLUME_STEP) < 0) {
                                    LOG_ERR("Cannot set volume now\n");
								}else{
									if(g_audio_service->Based.getVolume){
										nv_volume.value = (int64_t)g_audio_service->Based.getVolume((MediaService *) g_audio_service);
										LOG_INFO("vol down, current vol = %lld\n", nv_volume.value);
										set_nv_item(&nv_volume);
									}
								}
                            }
                        }
						//send_device_status(false,false,true,0);
                    	break; 

                    case AUDIO_MANAGER_PLAY_START:
                    case AUDIO_MANAGER_PLAY_STOP:
                    case AUDIO_MANAGER_RECORD_START:
                    case AUDIO_MANAGER_RECORD_STOP:
					#ifdef STORY_PLAY_PAUSE
					case AUDIO_MANAGER_PLAY_RESUME:
					case AUDIO_MANAGER_PLAY_PAUSE:
					#endif
						new_command = (audio_command_t *)heap_caps_malloc(sizeof(audio_command_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
						if(new_command){
							memcpy(new_command, &audiocommand, sizeof(audio_command_t));
							insert_cmd(&g_audio_manager, new_command);
						}else{
							LOG_ERR("malloc for app (%s) cmd (%s) fail, ignore it\n", ams_app_str[audiocommand.app], ams_msg_str[audiocommand.cmd]);
						}
						execute_audio_command(&g_audio_manager);
	                    break;
                    default:
                        LOG_INFO("Not supported line=%d\n", __LINE__);
	                    break;
		        }
            }
        }else{
			if (pdPASS == xQueueReceive(xQuePlayerStatus, &playstatus, 0)) {
	            LOG_INFO("xQuePlayerStatus Status:%x,ErrMsg:%x\n", playstatus.status, playstatus.errMsg);
				update_audio_manager_state(&g_audio_manager, &playstatus);
				execute_audio_command(&g_audio_manager);
        	}
		}
    }
    vTaskDelete(NULL);
}

TerminalControlService *TerminalControlCreate(void)
{
    g_audio_term = (TerminalControlService *) EspAudioAlloc(1, sizeof(TerminalControlService));
    ESP_ERROR_CHECK(!g_audio_term);
    return g_audio_term;
}

extern xQueueHandle xQueueRawStart;

static void AudioManagerActive(MediaService *self)
{
    AudioManagerService *service = (AudioManagerService *) self;
	#ifdef CONFIG_ENABLE_POWER_MANAGER
	music_wake_lock  = wake_lock_init(WAKE_LOCK_MUSIC,(unsigned char*)"music_wake_lock");
	#endif
    if (xTaskCreate(AudioManagerServiceTask,
                    "AudioManagerServiceTask",
                    AUDIO_SERV_TASK_STACK_SIZE,
                    service,
                    AUDIO_SERV_TASK_PRIORITY,
                    NULL) != pdPASS) {

        LOG_ERR("ERROR creating AudioManagerServiceTask task! Out of memory?\n");
    }
	code_queue = xQueueCreate(10, sizeof(amr_code_node));
	record_queue = xQueueCreate(10, sizeof(record_cmd_node));
	xTaskCreate(record_thread, "RecordThread", RECORD_THREAD_STACK_SIZE,NULL, RECORD_THREAD_PRIORITY, NULL);
	xTaskCreate(amr_code_task, "amrencodeThread", 4096,NULL, 5, NULL);
    LOG_INFO("AudioManagerActive\n");
}

static void AudioManagerDeactive(MediaService *self)
{
    LOG_INFO("AudioManagerDeactive\n");
	#ifdef CONFIG_ENABLE_POWER_MANAGER
	wake_lock_destroy(music_wake_lock);
	#endif
}

AudioManagerService *AudioManagerCreate()
{
    AudioManagerService *audio = (AudioManagerService *) EspAudioAlloc(1, sizeof(AudioManagerService));
    ESP_ERROR_CHECK(!audio);
    audio->Based.playerStatusUpdated = PlayerStatusUpdatedToAudio;
    audio->Based.serviceActive = AudioManagerActive;
    audio->Based.serviceDeactive = AudioManagerDeactive;
    audio->_smartCfg = 0;
    return audio;
}

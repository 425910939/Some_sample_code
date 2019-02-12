#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "rom/queue.h"
#include "ff.h"
#include "hwcrypto/sha.h"
#include "connect_manager.h"
#include "download_server.h"
#include "upload_server.h"
#include "audiomanagerservice.h"
#include "remote_command.h"
#include "keyevent.h"
#include "wechat.h"
#include "log.h"
#include "protocol.h"
#include "system_manager.h"
#include "story_push.h"
#include "alarms.h"
#include "led.h"
#include "application.h"

#define SONG_TEST
#define STORY_PUSH_QUEUE_SIZE		(10)
#define ALARM_SOURCE_DIR			"/sdcard/alarm"
#define STORY_FILE_PREFIX			"token_"
#define STORY_FILE_TOKEN_LENGTH		(64)
#define STORY_FILE_TOKEN_FULL_PATH	(256)
#define STORY_DOWNLOAD_RETRY_MAX_COUNT	(5)

#define ALARM_MASK_KEY_MASK	(KEY_CODE_WECHAT_MASK|KEY_CODE_VOICE_RECOGNIZE_MASK|KEY_CODE_HABIT_MASK|KEY_CODE_ENGLISH_MASK|KEY_CODE_PLAY_NEXT_MASK|KEY_CODE_PLAY_PREV_MASK)

#define LOG_TAG		"alarm"

#define ALARM_USE_LOCAL_SOURCE	(0)

static SemaphoreHandle_t task_exit;
static xQueueHandle story_push_queue;
static bool task_need_run = true;

static uint32_t g_command_handler;
//static alarm_push_item_t*a_push_list_head =NULL;
//static alarm_song_info_t*alarm_song_play =NULL;
static SemaphoreHandle_t alarm_thread_run;

//static portMUX_TYPE g_curr_govnor_lock = portMUX_INITIALIZER_UNLOCKED;
static keycode_client_t *kclient;
static bool led_control_by_alarm;
static alarm_push_item_t test_alarm_head[10] = {0};
static alarm_song_info_t alarm_song_current ={0};


static void check_file_in_dir(char *dir_path)//check all pair file & token
{
	bool pair;
	DIR* dir_out;
	DIR* dir_in;
	struct dirent* de_out;
	struct dirent* de_in;
	char file_name[STORY_FILE_TOKEN_LENGTH];
	char token_name[STORY_FILE_TOKEN_LENGTH];
	char del_name[STORY_FILE_TOKEN_FULL_PATH];

	if(!dir_path){
		LOG_ERR("dir_path is NULL pointer\n");
	}else{
		dir_out = opendir(dir_path);
		if(dir_out){
    		de_out = readdir(dir_out);
            while(de_out){
				if(strncmp(de_out->d_name, STORY_FILE_PREFIX, strlen(STORY_FILE_PREFIX))){//ignore token file			
					strncpy(file_name, de_out->d_name, STORY_FILE_TOKEN_LENGTH);
					strncpy(token_name, STORY_FILE_PREFIX, STORY_FILE_TOKEN_LENGTH);
					strcat(token_name, file_name);
					//LOG_DBG("file name (%s), token name (%s) pair start\n", file_name, token_name);
					dir_in = opendir(dir_path);
					if(dir_in){
						de_in = readdir(dir_in);
						while(de_in){
							if((0 == strncmp(de_in->d_name, STORY_FILE_PREFIX, strlen(STORY_FILE_PREFIX))) && 
								(0 == strncmp(de_in->d_name, token_name, strlen(token_name)))){
								pair = true;
								break;
							}
							de_in = readdir(dir_in);
						}
						closedir(dir_in);
					}else{
						LOG_DBG("re-open dir (%s) failed\n", dir_path);
					}
					if(pair){
						LOG_DBG("file name (%s), token name (%s) pair success\n", file_name, token_name);
					}else{
						strncpy(del_name, dir_path, STORY_FILE_TOKEN_FULL_PATH);
						strcat(del_name, "/");
						strcat(del_name, file_name);
						unlink(del_name);
						LOG_DBG("file name (%s), token name (%s) pair fail, del file (%s)\n", file_name, token_name, del_name);
					}
				}
				pair = false;
				dir_in = NULL;
				de_in = NULL;
				de_out = readdir(dir_out);
			}
			closedir(dir_out);
		}else{
			LOG_ERR("open dir %s failed\n", dir_path);
		}
	}
	return;
}

static inline void story_push_notify(story_push_cmd_node *node, uint32_t delay)
{
	xQueueSend(story_push_queue, node, delay);
	return;
}

static void story_audio_play_cb(NotifyStatus ret,AudioAppType type,AudioManagerMsg cmd, void *param)
{
	story_push_cmd_node node;
	
	switch(ret){
		case NOTIFY_STATUS_PLAY_FINISH:
			LOG_INFO("alarm play notify play finish\n");
			node.cmd_value = STORY_PUSH_CMD_LESSON_RESULT_REPORT;
			node.param = (void *)true;
			node.size = 0;
			story_push_notify(&node, 100);
			break;
		case NOTIFY_STATUS_PLAY_DISTURBED:
		case NOTIFY_STATUS_PLAY_ERROR:
			LOG_INFO("alarm play notify disturbed %d\n",ret);
			node.cmd_value = STORY_PUSH_CMD_ALARM_PLAY_ERROR;
			node.param = (void *)false;
			node.size = 0;
			story_push_notify(&node, 100);
			break;
		default:
			LOG_INFO("alarm play notify (%d) ignore\n", ret);
			break;
	}
	return;
}

static void alarm_callback(int alarm_id)
{
	story_push_cmd_node node;

	LOG_INFO("alarm id (%d) expired\n", alarm_id);

	node.cmd_value = STORY_PUSH_CMD_ALARM_START;
	node.param = (void *)alarm_id;
	node.size = 0;
	story_push_notify(&node, 100);
	return;
}

static int __add_alarm_song_to_list(alarm_song_info_t add_lsit)
{

	strncpy(alarm_song_current.songID,add_lsit.songID,sizeof(alarm_song_current.songID));
	strncpy(alarm_song_current.url,add_lsit.url,sizeof(alarm_song_current.url));
	alarm_song_current.playCount = add_lsit.playCount;
	alarm_song_current.alarm_id = add_lsit.alarm_id;
	LOG_INFO("song info song id %s url %s playcount = %d alarm id = %d\n",alarm_song_current.songID,alarm_song_current.url,alarm_song_current.playCount,alarm_song_current.alarm_id);

	return 0;
}

static int add_songUidlist(songUidList_t**head,songUidList_t add_lsit)
{
	songUidList_t * node =NULL;
	songUidList_t * temp =(*head);

	node = (songUidList_t*)heap_caps_malloc(sizeof(songUidList_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if(node == NULL)
		return -1;
	
	strncpy(node->songID,add_lsit.songID,sizeof(node->songID));
	node->sate = add_lsit.sate;
	if((*head) == NULL){
		(*head) = node;
		(*head) ->next =NULL;
	}else{
		while(temp->next)
			temp = temp->next;
		temp->next = node;
		node->next =NULL;
	}
	
	return 0;
}

static int destroy_songUidlist(songUidList_t **head)
{
	if(NULL == (*head))
        return -1;
	songUidList_t *temp =NULL;
	while(NULL != (*head)->next){
		temp = (*head)->next;
		(*head)->next = temp->next;
		free(temp);
		temp = NULL;
	}
	free((*head));
	(*head) =NULL;

	return 0;
	
}
static int change_songlist_state(songUidList_t **head,int index,alarm_s_list_state state)
{
	int i = 0;
	if(NULL == (*head))
        return -1;
	songUidList_t *temp = (*head);
	
	while(temp){
		if(index == i){
			temp->sate = state; 
			return 0;
		}
		i++;
		temp = temp->next;
	}
	temp = (*head);
	temp->sate = state; 
	
	return 0;	
}

static int analysis_alarm_push(cJSON*habitList_json,alarm_push_item_t *alarm_t)
{
	cJSON *alarm_info = NULL;
	cJSON *songid_json = NULL;
	songUidList_t*head = NULL;
	songUidList_t s_list;
	int i = 0;


	alarm_info = cJSON_GetObjectItem(habitList_json, "id");
	if(alarm_info)
		alarm_t->alarm_id = alarm_info->valueint;
	else
		alarm_t->alarm_id = 0;
	
	alarm_info = cJSON_GetObjectItem(habitList_json, "daysofweek");
	if(alarm_info)
		alarm_t->daysofweek = alarm_info->valueint;
	else
		alarm_t->daysofweek = 0;
	
	
	alarm_info = cJSON_GetObjectItem(habitList_json, "enable");
	if(alarm_info)
		alarm_t->enable = alarm_info->valueint;
	else
		alarm_t->enable = 0;
	
	alarm_info = cJSON_GetObjectItem(habitList_json, "hour");
	if(alarm_info)
		alarm_t->hour = alarm_info->valueint;
	else
		alarm_t->hour = 0;
	
	alarm_info = cJSON_GetObjectItem(habitList_json, "minute");
	if(alarm_info)
		alarm_t->minute = alarm_info->valueint;
	else
		alarm_t->minute = 0;
	
	alarm_info = cJSON_GetObjectItem(habitList_json, "listenIndex");
	if(alarm_info)
		alarm_t->listenDays = alarm_info->valueint;
	else
		alarm_t->listenDays = 0;

	alarm_info = cJSON_GetObjectItem(habitList_json, "playType");
	if(alarm_info)
		alarm_t->playType = alarm_info->valueint;
	else
		alarm_t->playType = 0;

	alarm_info = cJSON_GetObjectItem(habitList_json, "songUidList");
	if(alarm_info == NULL)
		return -1;
	
	i = 0;
	while(1){
		songid_json = cJSON_GetArrayItem(alarm_info,i);
		if(songid_json == NULL){
			break;
		}
		strncpy(s_list.songID,songid_json->valuestring,sizeof(s_list.songID));
		s_list.sate = ALARM_SONG_PLAY_IDLE;

		add_songUidlist(&head,s_list);
		i++;
	}
	if(head){
		alarm_t->songList = head;
		alarm_t->listenDays = (alarm_t->listenDays)%i;
		change_songlist_state(&alarm_t->songList,alarm_t->listenDays,ALARM_SONG_PLAY_INDEX);
		LOG_INFO("alarm info id = %d enbale = %d daysofweek = %d listenDays = %d playType =%d hour =%d minute =%d\n",\
			alarm_t->alarm_id,alarm_t->enable,alarm_t->daysofweek,alarm_t->listenDays,alarm_t->playType ,alarm_t->hour,alarm_t->minute);
	}
	return 0;

}

static int  __search_item_by_alarm_id(int alarm_id)
{
	int i=0;
	for(i=0;i<10;i++){
		if(test_alarm_head[i].alarm_id == alarm_id){
			LOG_INFO("search_item_by_alarm_id = %d %d\n",i,test_alarm_head[i].alarm_id );
			return i;
		}
	}
	return -1;
}

static songUidList_t * search_song_by_state(alarm_push_item_t *alarm_t,int state)
{
	songUidList_t *node = alarm_t->songList;
	if(NULL == node)
        return NULL;

	while(node){
		if(node->sate == state){
			break;
		}
		node = node->next;
	}

	return node;
}

static songUidList_t * search_song_by_state_next(alarm_push_item_t *alarm_t,int state)
{
	songUidList_t *node = alarm_t->songList;
	if(NULL == node)
        return NULL;;
	
	while(node){
		if(node->sate == state){
			node = node->next;
			if(node == NULL)
				node = alarm_t->songList;
			return node;
		}
		node = node->next;
	}
	return node;
}

static inline void __del_alarm_from_headlist(int i)
{
	test_alarm_head[i].alarm_id = 0;
	test_alarm_head[i].daysofweek= 0; 
	test_alarm_head[i].enable = 0; 
	test_alarm_head[i].hour = 0; 
	test_alarm_head[i].minute = 0;
	test_alarm_head[i].listenDays = 0;
	test_alarm_head[i].playType = 0;
	destroy_songUidlist(&test_alarm_head[i].songList);
	return;
}

static int add_alarm_to_headlist(alarm_push_item_t add_lsit)
{
	
	int i = 0;
	while(test_alarm_head[i].alarm_id != 0 ){
		i++;
		if(i >= 10){
			LOG_INFO("cannot be saved as an alarm clock\n");
			return -1;
		}
	}
	LOG_INFO("alarm save index  = %d\n",i);
	test_alarm_head[i].alarm_id = add_lsit.alarm_id;
	test_alarm_head[i].daysofweek= add_lsit.daysofweek; 
	test_alarm_head[i].enable = add_lsit.enable; 
	test_alarm_head[i].hour = add_lsit.hour; 
	test_alarm_head[i].minute = add_lsit.minute;
	test_alarm_head[i].listenDays = add_lsit.listenDays;
	test_alarm_head[i].playType = add_lsit.playType;
	test_alarm_head[i].songList = add_lsit.songList;
	
	return 0;
}
static int traverse_all_list(songUidList_t *head)
{
	if(NULL == head)
        return -1;
	songUidList_t *temp = head;
	while(temp){
		LOG_INFO("traverse song list = %s state =%d",temp->songID,temp->sate);
		temp = temp->next;
	}
	return 0;	
}

static inline void traverse_item(void)
{
	int i=0;
	for(i=0;i<10;i++){
		if(test_alarm_head[i].alarm_id == 0){	
			return;
		}
		ESP_LOGI("traverse_item","id = %d pos = %d",test_alarm_head[i].alarm_id,i);
		traverse_all_list(test_alarm_head[i].songList);
	}
	return;
}


static void update_govnor(char*data)
{
	cJSON *alarm_json = NULL;
	cJSON *data_item = NULL;
	cJSON *list_item = NULL;
	cJSON *habitList_number = NULL;
	alarm_t app_alarm;
	int ret =-1,index=0;
	alarm_push_item_t alarm_t = {0};

	alarm_json = cJSON_Parse(data);
	if(alarm_json){
		data_item = cJSON_GetObjectItem(alarm_json, "data");
		if(data_item == NULL){
			LOG_ERR("get data ObjectItem error\n");
			goto err_data ;
		}
		list_item = cJSON_GetObjectItem(data_item, "habitList");
		if(list_item == NULL){
			LOG_ERR("get habitList ObjectItem error\n");
			goto err_habitList ;
		}
	
		index = 0;
		while(1){
			habitList_number = cJSON_GetArrayItem(list_item,index);
			if(habitList_number == NULL){
				goto err_ArrayItem ;
			}
			
			index++;
			
			analysis_alarm_push(habitList_number,&alarm_t);
			
			ret = __search_item_by_alarm_id(alarm_t.alarm_id);			
			if(ret >= 0){		
				LOG_INFO("alarm already existed remove fisrt\n");
				remove_alarm(alarm_t.alarm_id);			
				__del_alarm_from_headlist(ret);
			}
			add_alarm_to_headlist(alarm_t);
	
			app_alarm.hour = alarm_t.hour;			
			app_alarm.minute = alarm_t.minute;
			app_alarm.callback = alarm_callback;						
			app_alarm.week_map = (((alarm_t.daysofweek<<1)&0x7E)+((alarm_t.daysofweek&0x40)?1:0));	
			app_alarm.one_shot = false;					
			app_alarm.alarm_id = alarm_t.alarm_id;
			app_alarm.state = alarm_t.enable;	
			add_alarm(&app_alarm);
		
		}
	}
	habitList_number = NULL;
err_ArrayItem:
	list_item = NULL;
err_habitList:
	data_item = NULL;
err_data:
	cJSON_Delete(alarm_json);
	alarm_json = NULL;

	return;
}

static int del_alarm(char * data)
{
	cJSON *del_item = NULL;
	cJSON *data_item = NULL;
	cJSON *id_item = NULL;
	int ret = -1;
	
	del_item = cJSON_Parse((char*)data);
	if(del_item){
		data_item = cJSON_GetObjectItem(del_item, "data");
		if(data_item == NULL){
			LOG_ERR("get data ObjectItem error\n");
			goto err_data ;
		}
		id_item = cJSON_GetObjectItem(data_item, "id");
		if(id_item == NULL){
			LOG_ERR("get id ObjectItem error\n");
			goto err_id;
		}
		ret = __search_item_by_alarm_id(id_item->valueint);			
		if(ret >= 0){		
			LOG_INFO("alarm already existed remove fisrt\n");
			remove_alarm(id_item->valueint);	
			__del_alarm_from_headlist(ret);
		}
		//traverse_item();
	}
	
err_id:
	id_item = NULL;
err_data:
	cJSON_Delete(del_item);
	del_item = NULL;
	return 0;
}


static void check_https_to_http(char a[])
{
	int i=0;
	int j=0;
	if(!strncmp(a,"https",5)){
		for(i=4;i<strlen(a);i++)
		{
			j = i+1;
			a[i]=a[j];
		}
		a[j-1]='\0';
	}
}

static int alarm_url_sync(void *data,play_param_t *play_param)
{
	alarm_song_info_t song_info = {0};
	cJSON *sync_item =NULL;
	cJSON *data_item =NULL;
	cJSON *json_item =NULL;
	songUidList_t *song_id = NULL;
	songUidList_t *songid_next = NULL;
	int alarm_id = -1,ret = -1;
	char sync_songid[30] = {0};
	
	sync_item = cJSON_Parse((char*)data);
	if(sync_item){
		data_item = cJSON_GetObjectItem(sync_item, "data");
		if(data_item ==NULL){
			goto error_sync;
		}
		
		json_item = cJSON_GetObjectItem(data_item, "id");
		if(json_item == NULL){
			goto error_data;
		}
		alarm_id = json_item->valueint;
		LOG_INFO("alarm start play alarm_id = %d\n",alarm_id);
		json_item = cJSON_GetObjectItem(data_item, "songUid");
		if(json_item == NULL){
			goto error_data;
		}
		strncpy(sync_songid,json_item->valuestring,sizeof(sync_songid));
		LOG_INFO("song id = %s\n",sync_songid);
		
		json_item = cJSON_GetObjectItem(data_item, "play_url");
		if(json_item == NULL){
			goto error_data;
		}
			
		int size = strlen(json_item->valuestring);
		if(size == 0){
			goto error_url;
		}

		ret = __search_item_by_alarm_id(alarm_id);	
		if(ret >= 0){
			song_id =  search_song_by_state(&test_alarm_head[ret],ALARM_SONG_PLAY_ACTIVE);
			if(song_id){
				if(!strncmp(sync_songid,song_id->songID,strlen(sync_songid))){
					songid_next = search_song_by_state_next(&test_alarm_head[ret],ALARM_SONG_PLAY_ACTIVE);
					songid_next->sate = ALARM_SONG_PLAY_INDEX; 
					LOG_INFO("songnext id = %s\n",songid_next->songID);
					song_id->sate = ALARM_SONG_PLAY_IDLE;
					song_info.alarm_id =alarm_id; 
					if(test_alarm_head[ret].playType == 3){	
						song_info.playCount = 4;	
					}else if(test_alarm_head[ret].playType == 2){	
						song_info.playCount = 3;			
					}else{					
						song_info.playCount = test_alarm_head[ret].playType;	
					}
					strncpy(song_info.songID,sync_songid,sizeof(song_info.songID));
					strncpy(song_info.url,json_item->valuestring,size);
					__add_alarm_song_to_list(song_info);
					alarm_id = 0;
				}
			}
		}
	}
	
error_url:
	json_item = NULL;
error_data:
	data_item = NULL;
error_sync:
	cJSON_Delete(sync_item);
	sync_item = NULL;
	return alarm_id;
}


static void play_alarm_song(play_param_t *play_param)
{
	char *url =NULL;
	int ret =-1;
	if(alarm_song_current.alarm_id != -1){
		ret = __search_item_by_alarm_id(alarm_song_current.alarm_id);
		if(ret >=0){
			int url_size = strlen(alarm_song_current.url);
			url = heap_caps_malloc((url_size+1), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
			if(url){
				memset(url,0,url_size+1);
				strncpy(url,alarm_song_current.url,url_size);
				check_https_to_http(url);
				LOG_INFO("size = %d url = %s\n",url_size,url);
				play_param->is_local_file = false;
				play_param->uri = url;
				play_param->play_app_type = AUDIO_APP_ALARM;
				play_param->tone = NULL;
				play_param->cb = story_audio_play_cb;
				play_param->cb_param = NULL;
				play_stop(AUDIO_APP_ALARM);
				play_start(play_param);
				keyevent_mask_notify(APP_ALARM, ALARM_MASK_KEY_MASK);//disable other key press
				led_control_by_alarm = true;
				led_mode_set(LED_CLIENT_ALARM, LED_MODE_COLORFUL_BREATH, NULL);
				free(url);
				url =NULL;
			}	
		}
	}
}

static int play_song_repeat(play_param_t *play_param)
{
	int ret = -1;
	char *url =NULL;
	if(alarm_song_current.alarm_id != -1){
		ret = __search_item_by_alarm_id(alarm_song_current.alarm_id);
		if(ret >=0){
			if(test_alarm_head[ret].playType== 3){			
				LOG_INFO("alarm play circulation mode\n");		
			}else if(test_alarm_head[ret].playType == 2){	
				LOG_INFO("alarm play three times\n");	
				alarm_song_current.playCount --;			
			}else if(test_alarm_head[ret].playType == 1){	
				LOG_INFO("alarm play one times\n");		
				alarm_song_current.playCount --;		
			}	
			LOG_INFO("playCount = %d\n",alarm_song_current.playCount);
			if(alarm_song_current.playCount == 0){					
				alarm_song_current.alarm_id =-1;
				goto out;
			}
			int url_size = strlen(alarm_song_current.url);
			url = heap_caps_malloc((url_size+1), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
			if(url){
				memset(url,0,url_size+1);
				strncpy(url,alarm_song_current.url,url_size);
				check_https_to_http(url);
				LOG_INFO("size = %d url = %s\n",url_size,url);
				play_param->is_local_file = false;
				play_param->uri = url;
				play_param->play_app_type = AUDIO_APP_ALARM;
				play_param->tone = NULL;
				play_param->cb = story_audio_play_cb;
				play_param->cb_param = NULL;
				play_start(play_param);
				keyevent_mask_notify(APP_ALARM, ALARM_MASK_KEY_MASK);//disable other key press
				led_control_by_alarm = true;
				led_mode_set(LED_CLIENT_ALARM, LED_MODE_COLORFUL_BREATH, NULL);
				free(url);
				url =NULL;
			}
		}
	}
out:
	return 0;
}

static keyprocess_t alarm_keyprocess(keyevent_t *event)
{
	story_push_cmd_node node;

	LOG_INFO("alarm recevice key event %d type %d\n", event->code, event->type);
	if(event->code == KEY_CODE_ALARM){
		if(event->type == KEY_EVNET_PRESS){
			node.cmd_value = STORY_PUSH_CMD_ALARM_STOP;
			node.param = NULL;
			node.size = 0;
			story_push_notify(&node, 100);
		}
	}else{
		LOG_ERR("receive unexpect key (%d) event (%d)\n", event->code, event->type);
	}
	return KEY_PROCESS_PUBLIC;;
}

static cJSON * alarmrequest_cjon(char * uidstr,int alarm_id,int listenIndex)
{
	cJSON *root = NULL;
	cJSON *data = NULL;

	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root,"type",0x8206);
	cJSON_AddItemToObject(root, "data", data = cJSON_CreateObject());
	cJSON_AddNumberToObject(data,"id",alarm_id);
	cJSON_AddStringToObject(data,"songUid", uidstr);
	cJSON_AddNumberToObject(data,"listenIndex",listenIndex);
	return root;
}

static int request_alarm_song(int id)
{	

	int ret =-1;
	cJSON *root = NULL;
	char * json_data =NULL;
	songUidList_t *song_id = NULL;
	
	ret = __search_item_by_alarm_id(id);	
	if(ret >= 0){
		LOG_INFO("alarm_item is not null\n");
		song_id =  search_song_by_state(&test_alarm_head[ret],ALARM_SONG_PLAY_INDEX);	
		if(song_id){
			song_id->sate = ALARM_SONG_PLAY_ACTIVE;
			test_alarm_head[ret].listenDays ++;
			root = alarmrequest_cjon(song_id->songID,test_alarm_head[ret].alarm_id,test_alarm_head[ret].listenDays);
			json_data = cJSON_Print(root);
			cJSON_Delete(root);
			root = NULL;
			assemble_remote_json(DEVICE_DATA_VERSION,strlen(json_data),g_command_handler,S2C_STORY_LESSON_RESULT_ACK,json_data);
			free(json_data);
			json_data = NULL;
			goto out;
		}
		song_id =  search_song_by_state(&test_alarm_head[ret],ALARM_SONG_PLAY_IDLE);	
		if(song_id){
			song_id->sate = ALARM_SONG_PLAY_ACTIVE;
			test_alarm_head[ret].listenDays ++;
			root = alarmrequest_cjon(song_id->songID,test_alarm_head[ret].alarm_id,test_alarm_head[ret].listenDays);
			json_data = cJSON_Print(root);
			cJSON_Delete(root);
			root = NULL;
			assemble_remote_json(DEVICE_DATA_VERSION,strlen(json_data),g_command_handler,S2C_STORY_LESSON_RESULT_ACK,json_data);
			free(json_data);
			json_data = NULL;
			goto out;
		}
	}
	out:
	return 0;
}

static int request_next_alarm_song(int id)
{
	int ret =-1;
	cJSON *root = NULL;
	char * json_data =NULL;
	songUidList_t *song_id = NULL;
	LOG_INFO("enter\n");
	ret = __search_item_by_alarm_id(id);
	if(ret >= 0){
		song_id = search_song_by_state(&test_alarm_head[ret],ALARM_SONG_PLAY_INDEX);	
		if(song_id){
			song_id->sate = ALARM_SONG_PLAY_ACTIVE;
			test_alarm_head[ret].listenDays ++;
			root = alarmrequest_cjon(song_id->songID,test_alarm_head[ret].alarm_id,test_alarm_head[ret].listenDays);
			json_data = cJSON_Print(root);
			cJSON_Delete(root);
			root = NULL;
			assemble_remote_json(DEVICE_DATA_VERSION,strlen(json_data),g_command_handler,S2C_STORY_LESSON_SYNC,json_data);
			free(json_data);
			json_data = NULL;
			goto out;
		}

		song_id = search_song_by_state(&test_alarm_head[ret],ALARM_SONG_PLAY_IDLE);	
		if(song_id){
			song_id->sate = ALARM_SONG_PLAY_ACTIVE;
			test_alarm_head[ret].listenDays++;
			root = alarmrequest_cjon(song_id->songID,test_alarm_head[ret].alarm_id,test_alarm_head[ret].listenDays);
			json_data = cJSON_Print(root);
			cJSON_Delete(root);
			root = NULL;
			assemble_remote_json(DEVICE_DATA_VERSION,strlen(json_data),g_command_handler,S2C_STORY_LESSON_SYNC,json_data);
			free(json_data);
			json_data = NULL;
			goto out;
		}
	}
	out:
	LOG_INFO("exit\n");
	return 0;
}


static void story_push_task(void *param)
{
	rx_data_t *cmd= NULL;
	story_push_cmd_node node;
	int alarm_id;
	play_param_t play_param={0};
		
	xSemaphoreTake(task_exit, portMAX_DELAY);
	
	while(task_need_run){
		memset(&node, 0, sizeof(story_push_cmd_node));
        if(pdTRUE == xQueueReceive(story_push_queue, &node, portMAX_DELAY)){
			LOG_DBG("receive cmd %d\n", node.cmd_value);
			cmd = node.param;
			switch(node.cmd_value){
				case STORY_PUSH_CMD_LESSON_DELETE:
					LOG_INFO("delete alarm\n");
					del_alarm(cmd->data);
					break;

				case STORY_PUSH_CMD_LESSON_PUSH:
					LOG_INFO("receive lesson push from server\n");
					update_govnor(cmd->data);
					break;	
				case STORY_PUSH_CMD_ALARM_PLAY:
					alarm_id = alarm_url_sync((void *)cmd->data,&play_param);
					if(alarm_id != 0){
						LOG_ERR("sync alarm url fail alarm_id = %d\n",alarm_id);
						request_next_alarm_song(alarm_id);
					}else{
						node.cmd_value = STORY_PUSH_CMD_SONG_PLAY;
						node.param = NULL;
						node.size = 0;
						story_push_notify(&node, 100);	
					}
					break;
					
				case STORY_PUSH_CMD_ALARM_START:
					LOG_INFO("alarm id (%d) occur\n", (int)(node.param));
					request_alarm_song((int)(node.param));
					break;
				case STORY_PUSH_CMD_ALARM_STOP:
					LOG_INFO("alarm stop by user operation\n");
					play_stop(AUDIO_APP_ALARM);
					if(led_control_by_alarm){
						led_mode_set(LED_CLIENT_ALARM, LED_MODE_ALL_OFF, NULL);
						keyevent_unmask_notify(APP_ALARM, ALARM_MASK_KEY_MASK);//enable other key press
					}
					alarm_song_current.alarm_id = -1;
					break;
				case STORY_PUSH_CMD_LESSON_RESULT_REPORT:
					LOG_INFO("alarm lesson play %s...\n", (bool)node.param?"finish":"not finish");
					if((bool)node.param == true){
						play_song_repeat(&play_param);
						LOG_INFO("id = %d count = %d\n",alarm_song_current.alarm_id,alarm_song_current.playCount);
						if((led_control_by_alarm)&&(alarm_song_current.alarm_id == -1)){
							led_control_by_alarm = false;
							led_mode_set(LED_CLIENT_ALARM, LED_MODE_ALL_OFF, NULL);
							keyevent_unmask_notify(APP_ALARM, ALARM_MASK_KEY_MASK);//enable other key press
						}
					}
					break;
					
				case STORY_PUSH_CMD_ALARM_PLAY_ERROR:
					alarm_song_current.alarm_id = -1;
					if(led_control_by_alarm){
						led_control_by_alarm = false;
						led_mode_set(LED_CLIENT_ALARM, LED_MODE_ALL_OFF, NULL);
						keyevent_unmask_notify(APP_ALARM, ALARM_MASK_KEY_MASK);//enable other key press
					}
					break;
				case STORY_PUSH_CMD_SONG_PLAY:
					LOG_INFO("start play alarm\n");
					play_alarm_song(&play_param);
					break;
				default:
					LOG_ERR("unknown cmd %d receive, ignore it\n", node.cmd_value);
					break;
			}
			if(node.param && node.size){
				free(node.param);
				node.param =NULL;
			}
		}
	}
	vTaskDelay(1);
    vTaskDelete(NULL);
    xSemaphoreGive(task_exit);
}

static void  story_push_command_cb(void *p_data,int cmd_type)
{
	rx_data_t *cmd = (rx_data_t *)p_data;
	int buff_size;
	story_push_cmd_node node;
	int type = cmd_type;
	buff_size = cmd->length+6;

	LOG_DBG("recv command type-%d len-%d\n", type,cmd->length);
	cmd = (rx_data_t *)heap_caps_malloc(buff_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if(cmd){
		memcpy(cmd, p_data, buff_size);
		switch(type){
			case S2C_STORY_LESSON_DELETE:
				node.cmd_value = STORY_PUSH_CMD_LESSON_DELETE;
				node.param = (void *)cmd;
				node.size = buff_size;
				story_push_notify(&node, 100);
				break;
			case S2C_STORY_LESSON_PUSH:
				node.cmd_value = STORY_PUSH_CMD_LESSON_PUSH;
				node.param = (void *)cmd;
				node.size = buff_size;
				story_push_notify(&node, 100);				
				break;

			case S2C_STORY_LESSON_RESULT_ACK:
				node.cmd_value = STORY_PUSH_CMD_ALARM_PLAY;
				node.param = (void *)cmd;
				node.size = buff_size;
				story_push_notify(&node, 100);
				break;		
			default:
				LOG_ERR("unspport command %d coming, ignore it\n", type);
				free(cmd);
				cmd = NULL;
				break;
		}
	}else{
		LOG_ERR("malloc command buff fail\n");
	}
	return;
}

int story_push_init(void)
{
	uint32_t cmd_bits = 0;
	
	LOG_INFO("enter\n");
    story_push_queue = xQueueCreate(STORY_PUSH_QUEUE_SIZE, sizeof(story_push_cmd_node));
	task_exit = xSemaphoreCreateMutex();

	cmd_bits = 1<<find_type_bit_offset(S2C_STORY_LESSON_PUSH);
	cmd_bits |= 1<<find_type_bit_offset(S2C_STORY_LESSON_RESULT_ACK);
	cmd_bits |= 1<<find_type_bit_offset(S2C_STORY_LESSON_DELETE);
	g_command_handler = remote_cmd_register(cmd_bits, story_push_command_cb);

	check_file_in_dir(ALARM_SOURCE_DIR);

	kclient = keyevent_register_listener(KEY_CODE_ALARM_MASK, alarm_keyprocess);
	if(!kclient){
		LOG_ERR("register key client fail\n");
		remote_cmd_unregister(g_command_handler);
		vSemaphoreDelete(task_exit);
		vQueueDelete(story_push_queue);
		return ESP_ERR_INVALID_RESPONSE;
	}
	alarm_thread_run = xSemaphoreCreateMutex();
	task_need_run = true;
	xTaskCreate(story_push_task, "story_push_task", 4096*2, NULL, 5, NULL);
	LOG_INFO("exit\n");
	return 0;
}

void story_push_uninit(void)
{
	story_push_cmd_node node;

	LOG_INFO("called\n");

	keyevent_unregister_listener(kclient);
	remote_cmd_unregister(g_command_handler);
	task_need_run = false;
	node.cmd_value = STORY_PUSH_CMD_UNDEFINE;
	node.param = NULL;
	node.size = 0;
	story_push_notify(&node, 100);
	xSemaphoreTake(task_exit, portMAX_DELAY);
	vSemaphoreDelete(task_exit);
	vQueueDelete(story_push_queue);
	return;
}

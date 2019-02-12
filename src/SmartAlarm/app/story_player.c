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
#include "story_player.h"
#include "application.h"
#include "cJSON.h"


#define STORY_PLAYER_QUEUE_SIZE		(10)
#define ENGLISH_SOURCE_DIR			"/sdcard/e_story"
#define STORY_SOURCE_DIR			"/sdcard/story"
#define STORY_FILE_PREFIX			"token_"
#define STORY_FILE_TOKEN_LENGTH		(64)
#define STORY_FILE_TOKEN_FULL_PATH	(256)
#define STORY_DOWNLOAD_RETRY_MAX_COUNT	(5)
#define PUSH_NEXT		0
#define PUSH_PREV 		1
#define PUSH_NOTHING	2
#define LOG_TAG	    "story"

static SemaphoreHandle_t task_exit;
static xQueueHandle story_player_queue;
static bool task_need_run = true;

static uint32_t g_command_handler;
static story_player_t *g_story_player = NULL;
static story_player_t *g_english_player = NULL;
static story_player_t *g_current_player = NULL;
static keycode_client_t *kclient;

#ifdef STORY_PLAY_PAUSE
static TimerHandle_t xTimerUser;
static uint8_t realtime_push_song = 0; 
static uint8_t habit_key_press_cnt = 0; 
static uint8_t english_key_press_cnt = 0;
static realtime_push_t r_push_player = {0};
static story_player_t *g_english_song_player = NULL;
static story_player_t *g_story_song_player = NULL;
#define HABIT_SONG_SOURCE_DIR			"/sdcard/song"
#define ENGLISH_SONG_SOURCE_DIR			"/sdcard/e_song"
static SemaphoreHandle_t _task_exit;
static xQueueHandle play_record_queue;
static bool record_play_msg = false;
static key_sign_t s_key ={0};

#define ENGLISH_PLAY_LOG			"/sdcard/PLAYLOG/playenglishlog.txt"
#define STORY_PLAY_LOG				"/sdcard/PLAYLOG/playstorylog.txt"
#define ENGLISH_SONG_PLAY_LOG		"/sdcard/PLAYLOG/englishsonglog.txt"
#define STORY_SONG_PLAY_LOG			"/sdcard/PLAYLOG/habitsonglog.txt"
#define KEY_SIGN_LOG				"/sdcard/PLAYLOG/keysign"
static item_entry_t * search_in_play_list_by_name(play_list_t *list, char *file_name, int file_name_size);
static int save_playlog_to_file(player_name_time_t*msg)
{
	FILE *fstream = NULL;
	char *msg_buff =NULL;
	DIR * dir = NULL;	
	dir = opendir("/sdcard/PLAYLOG");
	if(dir == NULL) {
		if(mkdir("/sdcard/PLAYLOG",S_IRWXU|S_IRWXG|S_IROTH) < 0){
			LOG_ERR("mkdir play log file err\n");
			return -1;
		}
		LOG_ERR("mkdir success\n");
	}else{
		closedir(dir);
	}
	if(g_current_player == g_english_player)
		fstream = fopen(ENGLISH_PLAY_LOG,"wb+");
	else if(g_current_player == g_story_player)
		fstream = fopen(STORY_PLAY_LOG,"wb+");
	else if(g_current_player == g_english_song_player)	
		fstream = fopen(ENGLISH_SONG_PLAY_LOG,"wb+");	
	else if(g_current_player == g_story_song_player)	
		fstream = fopen(STORY_SONG_PLAY_LOG,"wb+");
	
	if(NULL == fstream){
		LOG_ERR("open save play log file err\n");
		return -1;
	}
	msg_buff = (char*)msg;
	fwrite(msg_buff,sizeof(player_name_time_t),1,fstream);
	fclose(fstream);
	//LOG_INFO("save player to file= %s time = %d\n",msg->player_name,msg->time);
	return 0;
}
static int get_filename_from_playlog(player_name_time_t*msg)
{
	FILE *fstream = NULL;
	char *msg_buff =NULL;
	if(g_current_player == g_english_player){
		fstream = fopen(ENGLISH_PLAY_LOG,"r+");
	}else if(g_current_player == g_story_player){
		fstream = fopen(STORY_PLAY_LOG,"r+");
	}else if(g_current_player == g_english_song_player){	
		fstream = fopen(ENGLISH_SONG_PLAY_LOG,"r+");	
	}else if(g_current_player == g_story_song_player){	
		fstream = fopen(STORY_SONG_PLAY_LOG,"r+");
	}
	if(NULL == fstream){
		LOG_ERR("open get play log file err\n");
		return -1;
	}
	msg_buff = (char*)msg;
	fread(msg_buff,sizeof(player_name_time_t),1,fstream);
	fclose(fstream);
	LOG_INFO("get player from file= %s time = %d ms\n",msg->player_name,msg->time);
	return 0;
}
static item_entry_t * search_play_in_playmsg_file(play_list_t *list){

	item_entry_t *pos = NULL;
	player_name_time_t play_msg = {0};
	if(get_filename_from_playlog(&play_msg) == 0){
		pos = search_in_play_list_by_name(list,play_msg.player_name,strlen(play_msg.player_name));
		if(pos)
			pos->time = play_msg.time;
	}
	return pos;
} 

static int save_keysign_to_file(key_sign_t*msg)
{
	FILE *fstream = NULL;
	char *msg_buff =NULL;

	fstream = fopen(KEY_SIGN_LOG,"wb+");
	
	if(NULL == fstream){
		LOG_ERR("open save play log file err\n");
		return -1;
	}
	msg_buff = (char*)msg;
	fwrite(msg_buff,sizeof(key_sign_t),1,fstream);
	fclose(fstream);
	LOG_INFO("save habit key = %d english key = %d\n",msg->habit_key,msg->english_key);
	return 0;
}
static int get_key_number_from_signlog(key_sign_t*msg)
{
	FILE *fstream = NULL;
	char *msg_buff =NULL;
	
	fstream = fopen(KEY_SIGN_LOG,"r+");

	if(NULL == fstream){
		LOG_ERR("open get play log file err\n");
		return -1;
	}
	msg_buff = (char*)msg;
	fread(msg_buff,sizeof(key_sign_t),1,fstream);
	fclose(fstream);
	LOG_INFO("get habit key = %d english key = %d\n",msg->habit_key,msg->english_key);
	return 0;
}

#else
static int save_keysign_to_file(void*msg){return -1;}
static int get_key_number_from_signlog(void*msg){return -1;}
static int get_filename_from_playlog(void*msg){return -1;}
static int save_playlog_to_file(void*msg){return -1;}
static item_entry_t * search_play_in_playmsg_file(play_list_t *list){return NULL;}
#endif
static play_list_t *create_play_list(char *list_name, char *folder_path)
{
	play_list_t *plist;
	
	plist = (play_list_t *)heap_caps_malloc(sizeof(play_list_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if(plist){
		memset(plist, 0, sizeof(play_list_t));
		strncpy(plist->list_name, list_name, PLAY_LIST_NAME_LENGTH);
		strncpy(plist->folder_path, folder_path, FOLDER_PATH_LENGTH);
		LOG_DBG("create play list %s, folder %s\n", plist->list_name, plist->folder_path);
	}else{
		LOG_ERR("malloc play list failed\n");
	}
	return plist;
}

static void delete_play_list(play_list_t *list)
{
	item_entry_t *pos;
	item_entry_t *safe_pos;

	if(list){
		LIST_FOREACH_SAFE(pos, &list->list_head, entries, safe_pos){
			LOG_DBG("free item %s\n", pos->file_name);
			free(pos);
			pos =NULL;
		}
		free(list);
		list =NULL;
	}else{
		LOG_ERR("play list is NULL pointer\n");
	}
	return;
}

static void insert_in_play_list(play_list_t *list, item_entry_t *item)
{
	if(list && item){
		LIST_INSERT_HEAD(&list->list_head, item, entries);
		//LOG_DBG("insert item %s\n", item->file_name);
	}else{
		LOG_ERR("%s %s is NULL pointer\n", list?"()":"list", item?"()":"item");
	}
	return;
}

static item_entry_t * remove_from_play_list(play_list_t *list, item_entry_t *item)
{	
	item_entry_t *pos = NULL;	
	bool find = false;		
	if(list && item){		
		LIST_FOREACH(pos, &list->list_head, entries){	
			if(pos == item){			
				find = true;			
				LIST_REMOVE(pos, entries);		
				LOG_DBG("remove item %s\n", pos->file_name);	
				break;			
			}		
		}		
		if(!find)
			LOG_INFO("item %s not in list %s\n", item->file_name, list->list_name);	
	}else{		
		LOG_ERR("%s %s is NULL pointer\n", list?"()":"list", item?"()":"item");
	}	
	return pos;
}

static item_entry_t *search_in_play_list_by_state(play_list_t *list, item_state_t state)//search play list base on state
{
	item_entry_t *pos = NULL;
	bool find = false;
	
	if(list){
		LIST_FOREACH(pos, &list->list_head, entries){
			if(pos->state == state){
				find = true;
				//LOG_DBG("search item %s match state %d\n", pos->file_name, state);
				break;
			}
		}
		if(!find)
			LOG_INFO("search list %s no item match state %d\n", list->list_name, state);
	}else{
		LOG_ERR("%s is NULL pointer", list?"(list)":"()");
	}
	return pos;
}

static item_entry_t * search_in_play_list_by_name(play_list_t *list, char *file_name, int file_name_size)
{
	item_entry_t *pos = NULL;
	bool find = false;
	
	if(list && file_name){
		LIST_FOREACH(pos, &list->list_head, entries){
			if(!strncmp(pos->file_name, file_name, ((file_name_size<STORY_FILE_NAME_LENGTH)?file_name_size:STORY_FILE_NAME_LENGTH))){
				find = true;
				LOG_DBG("search item %s\n", pos->file_name);
				break;
			}
		}
		if(!find)
			LOG_INFO("file %s not in list %s\n", file_name, list->list_name);
	}else{
		LOG_ERR("%s %s is NULL pointer\n", list?"()":"list", file_name?"()":"file_name");
	}
	return pos;
}

static item_entry_t *get_next_item(play_list_t *list, item_entry_t *pos)//get next item from pos
{
	item_entry_t *target_pos = NULL;
	bool find = false;
	
	if(list){
		if(!pos){
			target_pos = LIST_FIRST(&list->list_head);
			LOG_DBG("search pos is NULL, return 1st item %s\n",
				target_pos?target_pos->file_name:"NULL");
		}else{
			LIST_FOREACH(target_pos, &list->list_head, entries){
				if(target_pos == pos){
					find = true;
					target_pos = LIST_NEXT(target_pos, entries);
					if(!target_pos){//pos is tail return head
						target_pos = LIST_FIRST(&list->list_head);
					}
					LOG_DBG("search item %s next %s\n", pos->file_name,
						target_pos?target_pos->file_name:"NULL");
					break;
				}
			}
			if(!find){//not find return head
				target_pos = LIST_FIRST(&list->list_head);
				LOG_DBG("search pos not find, return 1st item %s\n",
					target_pos?target_pos->file_name:"NULL");
			}
		}
	}else{
		LOG_ERR("%s is NULL pointer", list?"(list)":"()");
	}
	return target_pos;
}

static item_entry_t *get_prev_item(play_list_t *list, item_entry_t *pos)//get next item from pos
{
	item_entry_t *target_pos = NULL;
	bool find = false;
	
	if(list){
		if(!pos){
			target_pos = LIST_FIRST(&list->list_head);
			LOG_DBG("search pos is NULL, return 1st item %s\n",
				target_pos?target_pos->file_name:"NULL");
		}else if(pos == LIST_FIRST(&list->list_head)){
			LIST_FOREACH(target_pos, &list->list_head, entries){
				if(LIST_NEXT(target_pos, entries) == NULL){
					break;
				}
			}
			LOG_DBG("search item %s is head prev is tail %s\n", pos->file_name, 
				target_pos->file_name);
		}else{
			LIST_FOREACH(target_pos, &list->list_head, entries){
				if(LIST_NEXT(target_pos, entries) == pos){
					find = true;
					LOG_DBG("search item %s prev %s\n", pos->file_name,
						target_pos?target_pos->file_name:"NULL");
					break;
				}
			}
			if(!find){
				target_pos = LIST_FIRST(&list->list_head);
				LOG_DBG("search pos not find, return 1st item %s\n",
					target_pos?target_pos->file_name:"NULL");
			}
		}
	}else{
		LOG_ERR("%s is NULL pointer", list?"(list)":"()");
	}
	return target_pos;
}
static void check_file_in_dir(char *dir_path);
static story_player_t * player_init(char *player_name, char *folder_path, 
	AudioAppType audio_type, audio_callback audio_callback)
{
	story_player_t *player;
	item_entry_t *item;
	char list_name[PLAY_LIST_NAME_LENGTH];

	player = (story_player_t *)heap_caps_malloc(sizeof(story_player_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if(player){
		LOG_DBG("create player %s, folder %s\n", player_name, folder_path);
		memset(player, 0, sizeof(story_player_t));
		player->mutex_lock = xSemaphoreCreateMutex();
		strncpy(player->player_name, player_name, PLAYER_NAME_LENGTH);
		strncpy(list_name, player_name, PLAYER_NAME_LENGTH);
		snprintf(&(list_name[strlen(list_name)]), (PLAY_LIST_NAME_LENGTH -strlen(list_name)), "_list");
		player->audio_type = audio_type;
		player->audio_callback = audio_callback;
		player->play_list = create_play_list(list_name, folder_path);
		if(player->play_list){
			LOG_INFO("create play list %s success\n", list_name);
			check_file_in_dir(folder_path);
			DIR* dir = opendir(folder_path);
			if(dir){
        		struct dirent* de = readdir(dir);
				//int count = 0;
		        while(de){
					if(strncmp(de->d_name, STORY_FILE_PREFIX, strlen(STORY_FILE_PREFIX))){
						//LOG_DBG("No.%d file name %s add in play list %s\n", ++count, de->d_name, player->play_list->list_name);
						item = (item_entry_t *)heap_caps_malloc(sizeof(item_entry_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
						if(item){
							memset(item, 0, sizeof(item_entry_t));
							item->state = ITEM_ST_IDLE;
							strncpy(item->file_name, de->d_name, STORY_FILE_NAME_LENGTH);
							insert_in_play_list(player->play_list, item);
						}else{
							LOG_ERR("malloc file %s's item failed\n", de->d_name);
						}
					}
					de = readdir(dir);
		        }
				closedir(dir);
		    }else{
		        LOG_ERR("can not open dir %s\n", folder_path);
		    }
		}else{
			LOG_ERR("create play list %s failed\n", list_name);
			free(player);
			player = NULL;
		}		
	}else{
		LOG_ERR("malloc player failed\n");
	}
	return player;
}

static int player_uninit(story_player_t *player)
{
	if(player){
		//xSemaphoreTake(player->mutex_lock, portMAX_DELAY);
		if(player->play_list)
			delete_play_list(player->play_list);
		//xSemaphoreGive(player->mutex_lock);
		vSemaphoreDelete(player->mutex_lock);
		free(player);
		player = NULL;
	}else{
		LOG_ERR("player param is NULL pointer\n");
	}
	return 0;
}

static bool player_is_playing(story_player_t *player)
{
	//item_entry_t *entry;
	bool ret = false;

	if(player){
		if(search_in_play_list_by_state(player->play_list , ITEM_ST_PLAY))
			ret = true;
	}
	return ret;
}

int get_play_status(char *local_file,char*_songid,char*_albumUid, int *local_status)
{
	item_entry_t *entry = NULL;

	strncpy(_songid, r_push_player.songUid, sizeof(r_push_player.songUid));
	strncpy(_albumUid, r_push_player.albumUid, sizeof(r_push_player.albumUid));
	entry = search_in_play_list_by_state(g_current_player->play_list , ITEM_ST_PLAY);
	if(entry){
		strncpy(local_file, entry->file_name, sizeof(entry->file_name));
		*local_status = PR_PLAY; 
		goto out;
	}

	entry = search_in_play_list_by_state(g_current_player->play_list , ITEM_ST_STOP);
	if(entry){
		strncpy(local_file, entry->file_name, sizeof(entry->file_name));
		*local_status = PR_STOP;
		goto out;
	}
	
	entry = search_in_play_list_by_state(g_current_player->play_list , ITEM_ST_PAUSE);
	if(entry){
		strncpy(local_file, entry->file_name, sizeof(entry->file_name));
		*local_status = PR_STOP;
		goto out;
	}

out:
	return 0;
}

static int player_play_start(story_player_t *player, char *tone)
{
	item_entry_t *entry;
	int ret = ESP_OK;
	char file_path[STORY_FILE_NAME_LENGTH + FOLDER_PATH_LENGTH];
	play_param_t play_param;

	if(player){
		//xSemaphoreTake(player->mutex_lock, portMAX_DELAY);
		//1st search play list for break pointer to start
		entry = search_in_play_list_by_state(player->play_list , ITEM_ST_PAUSE);
		if(entry){
			entry->state = ITEM_ST_PLAY;
			entry->time = 0;
			//xSemaphoreGive(player->mutex_lock);
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, entry->file_name);
			LOG_DBG("player [%s] play from pause file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = tone;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)entry;
			#ifdef STORY_PLAY_PAUSE
			play_resume(play_param.play_app_type);
			#else
			play_start(&play_param);
			#endif
			goto out;
		}		
		#ifdef STORY_PLAY_PAUSE
		//1st search play list for break pointer to start
		entry = search_in_play_list_by_state(player->play_list , ITEM_ST_STOP);
		if(entry){
			entry->state = ITEM_ST_PLAY;
			//xSemaphoreGive(player->mutex_lock);
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, entry->file_name);
			LOG_DBG("player [%s] play from stop file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = tone;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)entry;
			play_start(&play_param);
			goto out;
		}	
		#endif
		//2nd start from 1st idle item
		entry = search_play_in_playmsg_file(player->play_list);
		if(entry ==NULL)
			entry = search_in_play_list_by_state(player->play_list , ITEM_ST_IDLE);
		if(entry){
			entry->state = ITEM_ST_PLAY;
			//xSemaphoreGive(player->mutex_lock);
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, entry->file_name);
			LOG_DBG("player [%s] play from idle file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = tone;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)entry;	
			play_start(&play_param);
			goto out;
		}
		//xSemaphoreGive(player->mutex_lock);
		LOG_ERR("player [%s] no file need to play\n", player->player_name);
	}else{
		LOG_ERR("player is NULL pointer\n");
	}
out:
	realtime_push_song = 0;	
	return ret;
}

static int player_play_pause(story_player_t *player)
{
	item_entry_t *entry;
	int ret = ESP_OK;

	if(player){
		//xSemaphoreTake(player->mutex_lock, portMAX_DELAY);
		entry = search_in_play_list_by_state(player->play_list , ITEM_ST_PLAY);
		if(entry){
			entry->state = ITEM_ST_PAUSE;
			//xSemaphoreGive(player->mutex_lock);
			LOG_DBG("player [%s] pause file (%s)\n", player->player_name, entry->file_name);
			#ifdef STORY_PLAY_PAUSE
			play_pause(player->audio_type);
			#else
			play_stop(player->audio_type);
			#endif
			goto out;
		}
		ret = -ESP_ERR_NOT_FOUND;
		//xSemaphoreGive(player->mutex_lock);
		LOG_INFO("player [%s] no file need to pause\n", player->player_name);
	}else{
		ret = -ESP_ERR_INVALID_ARG;
		LOG_ERR("player is NULL pointer\n");
	}
out:
	return ret;
}
#ifdef STORY_PLAY_PAUSE
static int player_switch_stop(story_player_t *player)
{
	item_entry_t *entry;
	int ret = ESP_OK;

	if(player){
		//xSemaphoreTake(player->mutex_lock, portMAX_DELAY);
		entry = search_in_play_list_by_state(player->play_list , ITEM_ST_PAUSE);
		if(entry){
			entry->state = ITEM_ST_STOP;
			//xSemaphoreGive(player->mutex_lock);
			LOG_DBG("player [%s] stop pause file (%s)\n", player->player_name, entry->file_name);
			play_stop(player->audio_type);
			goto out;
		}
		
		entry = search_in_play_list_by_state(player->play_list , ITEM_ST_PLAY);
		if(entry){
			entry->state = ITEM_ST_STOP;
			//xSemaphoreGive(player->mutex_lock);
			LOG_DBG("player [%s] playing file (%s)\n", player->player_name, entry->file_name);
			play_stop(player->audio_type);
			goto out;
		}
		
		ret = -ESP_ERR_NOT_FOUND;
		//xSemaphoreGive(player->mutex_lock);
		LOG_INFO("player [%s] no file need to stop\n", player->player_name);
	}else{
		ret = -ESP_ERR_INVALID_ARG;
		LOG_ERR("player is NULL pointer\n");
	}
out:
	return ret;
}

static int player_play_stop(story_player_t *player,int cb)
{
	item_entry_t *entry;
	int ret = ESP_OK;

	if(player){
		//xSemaphoreTake(player->mutex_lock, portMAX_DELAY);
		entry = search_in_play_list_by_state(player->play_list , ITEM_ST_PAUSE);
		if(entry){
			if(cb == 1){
				entry->state = ITEM_ST_STOP;
			}
			else{
				entry->state = ITEM_ST_IDLE;
			}
			//xSemaphoreGive(player->mutex_lock);
			LOG_DBG("player [%s] stop pause file (%s)\n", player->player_name, entry->file_name);
			play_stop(player->audio_type);
			goto out;
		}
		entry = search_in_play_list_by_state(player->play_list , ITEM_ST_PLAY);
		if(entry){
			if(cb == 1){
				entry->state = ITEM_ST_STOP;
			}
			else{
				entry->state = ITEM_ST_IDLE;
			}
			//xSemaphoreGive(player->mutex_lock);
			LOG_DBG("player [%s] playing file (%s)\n", player->player_name, entry->file_name);
			play_stop(player->audio_type);
			goto out;
		}
		
		ret = -ESP_ERR_NOT_FOUND;
		//xSemaphoreGive(player->mutex_lock);
		LOG_INFO("player [%s] no file need to stop\n", player->player_name);
	}else{
		ret = -ESP_ERR_INVALID_ARG;
		LOG_ERR("player is NULL pointer\n");
	}
out:
	if(!realtime_push_song)
		send_device_status(false,true,false,0);
	return ret;
}
#else
static int player_play_stop(story_player_t *player)
{
	item_entry_t *entry;
	int ret = ESP_OK;

	if(player){
		//xSemaphoreTake(player->mutex_lock, portMAX_DELAY);
		entry = search_in_play_list_by_state(player->play_list , ITEM_ST_PLAY);
		if(entry){
			entry->state = ITEM_ST_IDLE;
			//xSemaphoreGive(player->mutex_lock);
			LOG_DBG("player [%s] stop file (%s)\n", player->player_name, entry->file_name);
			play_stop(player->audio_type);
			goto out;
		}
		ret = -ESP_ERR_NOT_FOUND;
		//xSemaphoreGive(player->mutex_lock);
		LOG_INFO("player [%s] no file need to stop\n", player->player_name);
	}else{
		ret = -ESP_ERR_INVALID_ARG;
		LOG_ERR("player is NULL pointer\n");
	}
out:
	return ret;
}
#endif

static int player_play_next(story_player_t *player)
{
	item_entry_t *entry;
	item_entry_t *next_entry;
	int ret = ESP_OK;
	char file_path[STORY_FILE_NAME_LENGTH + FOLDER_PATH_LENGTH];
	play_param_t play_param;

	if(player){
		//xSemaphoreTake(player->mutex_lock, portMAX_DELAY);
		//1st search play list for pause pointer to start
		#ifdef ALLOW_PLAY_START
		entry = search_in_play_list_by_state(player->play_list, ITEM_ST_PAUSE);
		if(entry){
			#ifdef STORY_PLAY_PAUSE
			play_stop(player->audio_type);
			#endif
			entry->state = ITEM_ST_IDLE;//update old pos state
			entry->time = 0;
			next_entry = get_next_item(player->play_list, entry);
			next_entry->state = ITEM_ST_PLAY;//update new pos state
			next_entry->time =0;
			//xSemaphoreGive(player->mutex_lock);
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, next_entry->file_name);
			LOG_DBG("player [%s] play form pause file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = NULL;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)next_entry;
			play_start(&play_param);
			goto out;
		}
		entry = search_in_play_list_by_state(player->play_list, ITEM_ST_STOP);
		if(entry){
			play_stop(player->audio_type);
			entry->state = ITEM_ST_IDLE;//update old pos state
			entry->time = 0;
			next_entry = get_next_item(player->play_list, entry);
			next_entry->state = ITEM_ST_PLAY;//update new pos state
			next_entry->time = 0;
			//xSemaphoreGive(player->mutex_lock);
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, next_entry->file_name);
			LOG_DBG("player [%s] play form stop file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = NULL;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)next_entry;
			play_start(&play_param);
			goto out;
		}
		#endif
		//2nd search play list for current play pos to start
		entry = search_in_play_list_by_state(player->play_list , ITEM_ST_PLAY);
		if(entry){
			play_stop(player->audio_type);
			entry->state = ITEM_ST_IDLE;//update old pos state
			entry->time = 0;
			next_entry = get_next_item(player->play_list, entry);
			next_entry->state = ITEM_ST_PLAY;//update new pos state
			next_entry->time = 0;
			//xSemaphoreGive(player->mutex_lock);
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, next_entry->file_name);
			LOG_DBG("player [%s] play form playing file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = NULL;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)next_entry;
			play_start(&play_param);
			goto out;
		}
		#ifdef ALLOW_PLAY_START
		//3rd start from 1st idle item
		entry = search_play_in_playmsg_file(player->play_list);
		if(entry ==NULL)
			entry = search_in_play_list_by_state(player->play_list , ITEM_ST_IDLE);
		if(entry){
			entry->state = ITEM_ST_PLAY;
			//xSemaphoreGive(player->mutex_lock);
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, entry->file_name);
			LOG_DBG("player [%s] play form idle file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = NULL;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)entry;
			play_start(&play_param);
			goto out;
		}
		#endif
		//xSemaphoreGive(player->mutex_lock);
		LOG_ERR("player [%s] no file need to play\n", player->player_name);
	}else{
		LOG_ERR("player is NULL pointer\n");
	}
out:
	realtime_push_song = 0;	
	return ret;
}

static int player_play_prev(story_player_t *player)
{
	item_entry_t *entry;
	item_entry_t *prev_entry;
	int ret = ESP_OK;
	char file_path[STORY_FILE_NAME_LENGTH + FOLDER_PATH_LENGTH];
	play_param_t play_param;

	if(player){
		//xSemaphoreTake(player->mutex_lock, portMAX_DELAY);
		#ifdef ALLOW_PLAY_START
		//1st search play list for pause pointer to start
		entry = search_in_play_list_by_state(player->play_list, ITEM_ST_PAUSE);
		if(entry){
			#ifdef STORY_PLAY_PAUSE
			play_stop(player->audio_type);
			#endif
			entry->state = ITEM_ST_IDLE;//update old pos state
			entry->time = 0;
			prev_entry = get_prev_item(player->play_list, entry);
			prev_entry->state = ITEM_ST_PLAY;//update new pos state
			prev_entry->time = 0;
			//xSemaphoreGive(player->mutex_lock);
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, prev_entry->file_name);
			LOG_DBG("player [%s] play form pause file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = NULL;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)prev_entry;	
			play_start(&play_param);
			goto out;
		}

		entry = search_in_play_list_by_state(player->play_list, ITEM_ST_STOP);
		if(entry){
			play_stop(player->audio_type);
			entry->state = ITEM_ST_IDLE;//update old pos state
			entry->time = 0;
			prev_entry = get_prev_item(player->play_list, entry);
			prev_entry->state = ITEM_ST_PLAY;//update new pos state
			prev_entry->time = 0;
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, prev_entry->file_name);
			LOG_DBG("player [%s] play form stop file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = NULL;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)prev_entry;
			play_start(&play_param);
			goto out;
		}
		#endif
		//2nd search play list for current play pos to start
		entry = search_in_play_list_by_state(player->play_list , ITEM_ST_PLAY);
		if(entry){
			play_stop(player->audio_type);
			entry->state = ITEM_ST_IDLE;//update old pos state
			entry->time = 0;
			prev_entry = get_prev_item(player->play_list, entry);
			prev_entry->state = ITEM_ST_PLAY;//update new pos state
			prev_entry->time = 0;
			//xSemaphoreGive(player->mutex_lock);
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, prev_entry->file_name);
			LOG_DBG("player [%s] play form playing file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = NULL;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)prev_entry;
			play_start(&play_param);
			goto out;
		}			
		//3rd start from 1st idle item
		#ifdef ALLOW_PLAY_START
		entry = search_play_in_playmsg_file(player->play_list);
		if(entry ==NULL)
			entry = search_in_play_list_by_state(player->play_list , ITEM_ST_IDLE);
		if(entry){
			entry->state = ITEM_ST_PLAY;
			//xSemaphoreGive(player->mutex_lock);
			strncpy(file_path, player->play_list->folder_path, sizeof(file_path));
			strcat(file_path, "/");
			strcat(file_path, entry->file_name);
			LOG_DBG("player [%s] play form idle file (%s)\n", player->player_name, file_path);
			play_param.play_app_type = player->audio_type;
			play_param.is_local_file = true;
			play_param.uri = file_path;
			play_param.tone = NULL;
			play_param.cb = player->audio_callback;
			play_param.cb_param = (void*)entry;
			play_start(&play_param);
			goto out;
		}
		#endif
		//xSemaphoreGive(player->mutex_lock);
		LOG_ERR("player [%s] no file need to play\n", player->player_name);
	}else{
		LOG_ERR("player is NULL pointer\n");
	}
out:
	realtime_push_song = 0;	
	return ret;
}


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
						//LOG_DBG("file name (%s), token name (%s) pair success\n", file_name, token_name);
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

static inline void story_player_notify(story_player_cmd_node *node, uint32_t delay)
{
	if(story_player_queue == NULL){
		LOG_INFO("story_player_queue is null\n");
		return ;
	}
	xQueueSend(story_player_queue, node, delay);
	return;
}

static void story_audio_play_cb(NotifyStatus ret,AudioAppType apptype,AudioManagerMsg cmd, void *param)//error:check file/disturb:pause player/stop:ignore/play end:next
{
	story_player_cmd_node node;
	item_entry_t *entry;

	switch(ret){
		case NOTIFY_STATUS_PLAY_FINISH:
			LOG_INFO("story player play notify finish\n");
			record_play_msg = false;
			node.cmd_value = STORY_PLAYER_CMD_PLAY_NEXT;
			node.param = (void *)ret;
			node.size = 0;
			story_player_notify(&node, 100);
			break;
		case NOTIFY_STATUS_PLAY_ERROR:
			LOG_INFO("story player play notify error\n");
			record_play_msg = false;
			if(param){
				entry = remove_from_play_list(g_current_player->play_list,(item_entry_t *)param);
				if(entry){
					free(entry);
					entry = NULL;
				}
			}
			node.cmd_value = STORY_PLAYER_CMD_PLAY_NEXT;
			node.param = (void *)ret;
			node.size = 0;
			story_player_notify(&node, 100);
			break;
		case NOTIFY_STATUS_PLAY_DISTURBED:
			record_play_msg = false;
			LOG_INFO("story player play notify disturbed\n");
			if(apptype == AUDIO_APP_HABIT){
				habit_key_press_cnt = (habit_key_press_cnt<1)?2:(habit_key_press_cnt-1);
				LOG_INFO("keep habit key status %d\n",habit_key_press_cnt);
			}else if(apptype == AUDIO_APP_ENGLISH){
				english_key_press_cnt = (english_key_press_cnt<1)?2:(english_key_press_cnt-1);
				LOG_INFO("keep english key status %d\n",english_key_press_cnt);
			}
			#ifdef STORY_PLAY_PAUSE
			node.cmd_value = STORY_PLAYER_CMD_PLAY_STOP;
			node.cb_sig = 1;
			#else
			node.cmd_value = STORY_PLAYER_CMD_PLAY_PAUSE;
			#endif
			node.param = (void *)ret;
			node.size = 0;
			story_player_notify(&node, 100);
			break;
		case NOTIFY_STATUS_PLAY_STOPPED:
			record_play_msg = false;
			LOG_INFO("story player play notify stop\n");
			if((apptype == AUDIO_APP_HABIT) &&(g_current_player->audio_type != AUDIO_APP_HABIT)){
				habit_key_press_cnt = (habit_key_press_cnt<1)?2:(habit_key_press_cnt-1);
				LOG_INFO("keep habit key status %d\n",habit_key_press_cnt);
			}else if((apptype == AUDIO_APP_ENGLISH)&&(g_current_player->audio_type != AUDIO_APP_ENGLISH)){
				english_key_press_cnt = (english_key_press_cnt<1)?2:(english_key_press_cnt-1);
				LOG_INFO("keep english key status %d\n",english_key_press_cnt);
			}
			break;
		case NOTIFY_STATUS_PLAY_RESTART:
			record_play_msg = true;
			if((apptype == AUDIO_APP_HABIT) &&(g_current_player->audio_type == AUDIO_APP_HABIT)){
				habit_key_press_cnt = (habit_key_press_cnt>1)?0:(habit_key_press_cnt+1);
				LOG_INFO("keep habit key status %d\n",habit_key_press_cnt);
			}else if((apptype == AUDIO_APP_ENGLISH)&&(g_current_player->audio_type == AUDIO_APP_ENGLISH)){
				english_key_press_cnt = (english_key_press_cnt>1)?0:(english_key_press_cnt+1);
				LOG_INFO("keep english key status %d\n",english_key_press_cnt);
			}
			if(param){
				entry = (item_entry_t *)param;
				entry->state = ITEM_ST_PLAY;
			}
			LOG_INFO("story playerre start habit key = %d english  key = %d\n",habit_key_press_cnt,english_key_press_cnt);
			break;
		default:
			LOG_INFO("story player play notify (%d) ignore\n", ret);
			break;
	}
	return;
}

static cJSON * create_next_prev_cjon(int cmd,char*songUid,char*albumUid,uint8_t mode,int status)
{
	cJSON *root = NULL;
	cJSON *data = NULL;
	
	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "type", S2C_STORY_REALTIME_PUSH);
	cJSON_AddItemToObject(root, "data", data = cJSON_CreateObject());
	cJSON_AddStringToObject(data,"albumUid",albumUid);
	cJSON_AddStringToObject(data,"songUid",songUid);
	cJSON_AddNumberToObject(data,"command", cmd);
	cJSON_AddNumberToObject(data,"mode", mode);
	cJSON_AddNumberToObject(data,"status", status);
	return root;
}

static int request_play_online_song(int command,int status)
{	
	cJSON * json_item = NULL;
	char *json_data = NULL;
	if((r_push_player.songUid[0] != 0) && (r_push_player.albumUid[0] != 0)){
		json_item = create_next_prev_cjon(command,r_push_player.songUid,r_push_player.albumUid,r_push_player.play_mode,status);
		json_data = cJSON_Print(json_item);
		assemble_remote_json(DEVICE_DATA_VERSION,strlen(json_data),g_command_handler,S2C_STORY_REALTIME_PUSH,json_data);
		memset(&r_push_player,0,sizeof(realtime_push_t));
		cJSON_Delete(json_item);
		json_item = NULL;
		free(json_data);
		json_data = NULL;
	}
	return 0;
}

static void realtime_push_play_cb(NotifyStatus ret,AudioAppType type,AudioManagerMsg cmd, void *param)//error:check file/disturb:pause player/stop:ignore/play end:next
{
	switch(ret){
		case NOTIFY_STATUS_PLAY_FINISH:
			LOG_INFO("story player play notify finish\n");
			request_play_online_song(PUSH_NOTHING,ret);
			break;
		case NOTIFY_STATUS_PLAY_ERROR:
			LOG_INFO("story player play notify error\n");
			request_play_online_song(PUSH_NOTHING,ret);
			break;
		case NOTIFY_STATUS_PLAY_DISTURBED:
			LOG_INFO("story player play notify disturbed\n");
			request_play_online_song(PUSH_NOTHING,ret);
			break;
		default:
			LOG_INFO("story player play notify (%d) ignore\n", ret);
			break;
	}	
	return;
}

static void  story_player_command_cb(void *p_data,int cmd_typ)
{
	rx_data_t *cmd = (rx_data_t *)p_data;
	int buff_size;
	story_player_cmd_node node;

	int type = cmd_typ;
	buff_size = cmd->length+6;
	LOG_INFO("type = %d len = %d\n",type,buff_size);
	cmd = (rx_data_t *)heap_caps_malloc(buff_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if(cmd){
		memcpy(cmd, p_data, buff_size);
		switch(type){
			case S2C_STORY_REALTIME_PUSH:
				node.cmd_value = STORY_PLAYER_CMD_REALTIME_PUSH;
				node.param = (void *)cmd;
				node.size = buff_size;
				story_player_notify(&node, 100);						
				break;
			case S2C_STORY_VOLUME_CONTROL:
				node.cmd_value = STORY_PLAYER_CMD_PUSH_CONTROL;
				node.param = (void *)cmd;
				node.size = buff_size;
				story_player_notify(&node, 100);
				break;	
			default:
				LOG_ERR("unspport command %d coming, ignore it\n", cmd->version);
				free(cmd);
				cmd = NULL;
				break;
		}
	}else{
		LOG_ERR("malloc command buff fail\n");
	}
	return;
}

static keyprocess_t story_keyprocess(keyevent_t *event)
{
	story_player_cmd_node node;
	
	LOG_INFO("story recevice key event %d type %d\n", event->code, event->type);
	switch(event->code){
		case KEY_CODE_HABIT:
			#ifdef STORY_PLAY_PAUSE
			if(event->type == KEY_EVNET_PRESS)			
			{		
				s_key.habit_key = habit_key_press_cnt;
				set_front_application(APP_STORY);		
				if(habit_key_press_cnt == 0){							
					node.cmd_value = STORY_PLAYER_CMD_SWITCH_SONG;		
				}else if(habit_key_press_cnt == 1){	
					node.cmd_value = STORY_PLAYER_CMD_SWITCH_STORY;	
				}else{							
					node.cmd_value = STORY_PLAYER_CMD_PLAY_PAUSE;	
				}				
				node.param = NULL;			
				node.size = 0;				
				story_player_notify(&node, 100);		
				}else if(event->type == KEY_EVNET_RELEASE){		
					//ignore release;	
					habit_key_press_cnt = (habit_key_press_cnt>1)?0:(habit_key_press_cnt+1);
			}
			#else
			if(event->type == KEY_EVNET_PRESS)
			{
				set_front_application(APP_STORY);
				node.cmd_value = STORY_PLAYER_CMD_SWITCH_STORY;
				node.param = NULL;
				node.size = 0;
				story_player_notify(&node, 100);
			}else{
				//ignore release;
			}
			#endif
			break;

		case KEY_CODE_ENGLISH:
			#ifdef STORY_PLAY_PAUSE
			if(event->type == KEY_EVNET_PRESS){
				s_key.english_key = english_key_press_cnt;
				set_front_application(APP_STORY);				
				if(english_key_press_cnt == 0){	
					node.cmd_value = STORY_PLAYER_CMD_SWITCH_E_SONG;
				}else if(english_key_press_cnt == 1){				
					node.cmd_value = STORY_PLAYER_CMD_SWITCH_ENGLISH;
				}else{
					node.cmd_value = STORY_PLAYER_CMD_PLAY_PAUSE;		
				}
				node.param = NULL;				
				node.size = 0;				
				story_player_notify(&node, 100);
			}else if(event->type == KEY_EVNET_RELEASE){
				english_key_press_cnt = (english_key_press_cnt>1)?0:(english_key_press_cnt+1);
				//ignore release;
			}
			#else
			if(event->type == KEY_EVNET_PRESS)
			{
				set_front_application(APP_STORY);
				node.cmd_value = STORY_PLAYER_CMD_SWITCH_ENGLISH;
				node.param = NULL;
				node.size = 0;
				story_player_notify(&node, 100);
			}else{
				//ignore release;
			}
			#endif
			break;

		case KEY_CODE_PLAY_NEXT:
			if(event->type == KEY_EVNET_PRESS)
			{
				set_front_application(APP_STORY);
				node.cmd_value = STORY_PLAYER_CMD_PLAY_NEXT;
				node.param = NULL;
				node.size = 0;
				story_player_notify(&node, 100);
			}else{
				//ignore release;
			}
			break;
		case KEY_CODE_PLAY_PREV:
			if(event->type == KEY_EVNET_PRESS)
			{
				set_front_application(APP_STORY);
				node.cmd_value = STORY_PLAYER_CMD_PLAY_PREV;
				node.param = NULL;
				node.size = 0;
				story_player_notify(&node, 100);
			}else{
				//ignore release;
			}
			break;
		case KEY_CODE_PAT:
			if(APP_STORY == get_front_application()){
				node.cmd_value = STORY_PLAYER_CMD_PLAY_REVERT;
				node.param = NULL;
				node.size = 0;
				story_player_notify(&node, 100);
			}	
		default:
			LOG_ERR("receive unexpect key (%d) event (%d)\n", event->code, event->type);
			break;
	}
	save_keysign_to_file(&s_key);
	return KEY_PROCESS_PUBLIC;
}

static inline void switch_player(story_player_t *new_player)
{
	if(new_player){
		if(!g_current_player){
			g_current_player = new_player;
			LOG_DBG("current player is NULL, switch to (%s)\n", new_player->player_name);
		}else{
			LOG_INFO("current player is (%s), stop & switch to (%s)\n", g_current_player->player_name, new_player->player_name);
			#ifdef STORY_PLAY_PAUSE
			player_switch_stop(g_current_player);
			#else
			player_play_pause(g_current_player);
			#endif
			g_current_player = new_player;
		}
	}else{
		LOG_ERR("new player is NULL, ignore it\n");
	}
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
static int play_realtime_push(char*data)
{
	cJSON*play_item = NULL;
	cJSON*data_item = NULL;
	cJSON*item = NULL;
	item_entry_t *now_item = NULL;
	play_param_t play_param = {0};
	int url_len = 0;
	char *url = NULL;
	
	play_item = cJSON_Parse(data);
	if(play_item){
		data_item = cJSON_GetObjectItem(play_item, "data");
		if(data_item == NULL){
			goto err_play;
		}
		
		memset(r_push_player.songUid,0,sizeof(r_push_player.songUid));
		item = cJSON_GetObjectItem(data_item,"songUid");
		if(item == NULL){
			goto err_data;
		}
		strncpy(r_push_player.songUid,item->valuestring,sizeof(r_push_player.songUid));
		
		
		item = cJSON_GetObjectItem(data_item,"albumUid");
		if(item == NULL){
			goto err_data;
		}
		strncpy(r_push_player.albumUid,item->valuestring,sizeof(r_push_player.albumUid));

		item = cJSON_GetObjectItem(data_item,"mode");
		if(item == NULL){
			r_push_player.play_mode = 2;
		}else{
			r_push_player.play_mode = item->valueint;
		}
		LOG_INFO("songUid = %s albumUid = %s mode = %d\n",r_push_player.songUid,r_push_player.albumUid,r_push_player.play_mode);
		
		item = cJSON_GetObjectItem(data_item,"play_url");
		if(item == NULL){
			memset(&r_push_player,0,sizeof(realtime_push_t));
			goto err_data;
		}

		url_len = strlen(item->valuestring);;
		url = (char *)heap_caps_malloc(url_len+1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if(url){
			memset(url,0,url_len+1);
			strncpy(url, item->valuestring, url_len);
			check_https_to_http(url);
			//player_play_stop(g_current_player,1);
			play_stop(AUDIO_APP_REALTIME_PUSH);
			play_param.play_app_type = AUDIO_APP_REALTIME_PUSH;
			play_param.is_local_file = false;
			play_param.uri = url;
			play_param.tone = NULL;
			play_param.cb = realtime_push_play_cb;
			play_param.cb_param = NULL;
			
			LOG_INFO("realtime push item (%s) url length (%d)  play from url\n", url, url_len);
			realtime_push_song = 1;
			play_start(&play_param);
			free(url);
			url = NULL;
		}else{
			LOG_ERR("malloc url buffer failed...\n");
		}
	err_data:	
		data_item = NULL;
	err_play:
		cJSON_Delete(play_item);
		play_item = NULL;
	}
	
	return 0;
}

static int push_control_cmd(char*data)
{
	cJSON*control_item = NULL;
	cJSON*data_item = NULL;
	cJSON*item = NULL;
	int cmd = -1;
	
	control_item = cJSON_Parse(data);
	if(control_item){
		data_item = cJSON_GetObjectItem(control_item, "data");
		if(data_item == NULL){
			goto err_control;
		}

		item = cJSON_GetObjectItem(data_item,"command");
		if(item == NULL){
			goto err_data;
		}
		cmd = item->valueint;
		LOG_INFO("control cmd = %d\n",cmd );
		if(cmd == 0){			
			volume_down();	
		}else if(cmd == 1){					
			volume_up();				
		}else if(cmd == 2){			
			play_pause(AUDIO_APP_REALTIME_PUSH);	
		}else if(cmd == 3){				
			play_resume(AUDIO_APP_REALTIME_PUSH);
		}else if(cmd == 4){	
			item = cJSON_GetObjectItem(data_item,"mode");
			if(item == NULL){
				goto err_data;
			}
			r_push_player.play_mode = item->valueint;
			LOG_INFO("push song info albumUid = %s ,songUid = %s,play_mode = %d\n",r_push_player.albumUid,r_push_player.songUid,r_push_player.play_mode);
			
		}else{
			LOG_INFO("ignore command = %d\n",cmd);
		}
		item = NULL;
	err_data:
		data_item = NULL;
	err_control:	
		cJSON_Delete(control_item);
		control_item = NULL;
	}
	
	return 0;
}

static void story_player_task(void *param)
{
	rx_data_t *cmd;
	story_player_cmd_node node;
	
	xSemaphoreTake(task_exit, portMAX_DELAY);
	while(task_need_run){
		memset(&node, 0, sizeof(story_player_cmd_node));
        if(pdTRUE == xQueueReceive(story_player_queue, &node, portMAX_DELAY)){
			LOG_DBG("receive cmd %d\n", node.cmd_value);
			cmd = node.param;
			switch(node.cmd_value){
				case STORY_PLAYER_CMD_REALTIME_PUSH:
					LOG_INFO("Play online songs\n");
					play_realtime_push(cmd->data);
					break;

				case STORY_PLAYER_CMD_SWITCH_STORY:
					if(g_current_player != g_story_player){
						switch_player(g_story_player);
						player_play_start(g_current_player, NOTIFY_AUDIO_HABIT);
					}else{
						player_play_start(g_current_player, NOTIFY_AUDIO_HABIT);
					}
					break;

				case STORY_PLAYER_CMD_SWITCH_ENGLISH:
					if(g_current_player != g_english_player){
						switch_player(g_english_player);
						player_play_start(g_current_player, NOTIFY_AUDIO_ENGLISH);
					}else{
						player_play_start(g_current_player, NOTIFY_AUDIO_ENGLISH);
						
					}
					break;
				#ifdef STORY_PLAY_PAUSE
				case STORY_PLAYER_CMD_SWITCH_E_SONG:			
					if(g_current_player != g_english_song_player){		
						switch_player(g_english_song_player);			
						player_play_start(g_current_player, NOTIFY_AUDIO_ENGLISH_SONG);	
					}else{			
						player_play_start(g_current_player, NOTIFY_AUDIO_ENGLISH_SONG);					
					}				
					break;

					case STORY_PLAYER_CMD_SWITCH_SONG:		
						if(g_current_player != g_story_song_player){	
							switch_player(g_story_song_player);			
							player_play_start(g_current_player, NOTIFY_AUDIO_HABIT_SONG);
						}else{		
							player_play_start(g_current_player, NOTIFY_AUDIO_HABIT_SONG);						
						}			
					break;
				#endif

				case STORY_PLAYER_CMD_PLAY_PAUSE:
					player_play_stop(g_current_player,1);
					#ifdef STORY_PLAY_PAUSE
					play_tone_sync(NOTIFY_AUDIO_STORY_PAUSE);
					#endif
					LOG_INFO("play pause\n");
					break;

				case STORY_PLAYER_CMD_PLAY_STOP:
					#ifdef STORY_PLAY_PAUSE
					player_play_stop(g_current_player,node.cb_sig);
					#else
					player_play_stop(g_current_player);
					#endif
					LOG_INFO("play stop\n");
					break;

				case STORY_PLAYER_CMD_PLAY_PREV:
					if((r_push_player.songUid[0] != 0) && (r_push_player.albumUid[0] != 0)){
						ESP_LOGI("ONEGO","request prev song");
						request_play_online_song(PUSH_PREV,NOTIFY_STATUS_PLAY_DISTURBED);
					}else{
						player_play_prev(g_current_player);
					}
					LOG_INFO("play prev\n");
					break;

				case STORY_PLAYER_CMD_PLAY_NEXT:
					if((r_push_player.songUid[0] != 0) && (r_push_player.albumUid[0] != 0)){
						ESP_LOGI("ONEGO","request next song");
						request_play_online_song(PUSH_NEXT,NOTIFY_STATUS_PLAY_DISTURBED);
					}else{
						player_play_next(g_current_player);
					}
					LOG_INFO("play next\n");
					break;
					
				case STORY_PLAYER_CMD_PUSH_CONTROL:
					LOG_INFO("control cmd\n");
					push_control_cmd(cmd->data);
					break;
					
				case STORY_PLAYER_CMD_PLAY_REVERT:
					if(player_is_playing(g_current_player)){
						player_play_pause(g_current_player);
					}else{
						player_play_start(g_current_player, NULL);
					}
					break;

				default:
					LOG_ERR("unknown cmd %d receive, ignore it\n", node.cmd_value);
					break;
			}

			if(node.param && node.size){
				free(node.param);
				node.param = NULL;
			}
		}
	}
	vTaskDelay(1);
    vTaskDelete(NULL);
    xSemaphoreGive(task_exit);
}

#ifdef STORY_PLAY_PAUSE
int notification_record_play_time(player_record_cmd_node *cmd)
{
	if(play_record_queue){
		record_play_msg = true;
		xQueueSend(play_record_queue,cmd,0);
	}
	return 0;
}

static void play_record_tick(TimerHandle_t xTimer)
{
    player_record_cmd_node cmd;
	cmd.cmd_value = PR_PLAY;
	if(play_record_queue&&record_play_msg){
		xQueueSend(play_record_queue,&cmd,0);
	}
}

static void player_record_task(void *param)
{

	player_record_cmd_node node;
	item_entry_t *entry;
	AudioManagerService *audio_service = NULL;
	player_name_time_t play_msg;
	int time;
	
	xSemaphoreTake(_task_exit, portMAX_DELAY);
	while(task_need_run)
	{
		if(pdTRUE == xQueueReceive(play_record_queue, &node, portMAX_DELAY)){
			switch(node.cmd_value){
			case PR_STOP:
				if(node.param)
					audio_service = (AudioManagerService *)node.param;
			break;
			case PR_PLAY:
				entry = search_in_play_list_by_state(g_current_player->play_list, ITEM_ST_PLAY);
				if(entry){
					if(audio_service){
						time = audio_service->Based.getPosByTime((MediaService *)audio_service);
					}else{
						time = 0;
					}
					entry->time = time;
					memset(play_msg.player_name,0,sizeof(play_msg.player_name));
					strncpy(play_msg.player_name,entry->file_name,strlen(entry->file_name));
					play_msg.time = time;
					save_playlog_to_file(&play_msg);
					//LOG_INFO("player_record_task file name = %s time =%d\n",entry->file_name,time); 
				}
				break;
			default:
				break;
			}

		}
	}
	vTaskDelay(1);
    vTaskDelete(NULL);
	xSemaphoreGive(_task_exit);
}

static void key_number_init(void){

	key_sign_t key_t = {0};
	if(get_key_number_from_signlog(&key_t) == 0){
		if(key_t.habit_key == 2){
			key_t.habit_key = 0;
		}
		if(key_t.english_key == 2){
			key_t.english_key = 0;
		}
		habit_key_press_cnt = key_t.habit_key;
		english_key_press_cnt = key_t.english_key;
	}
	return;
}
#endif
int story_player_init(void)
{
	uint32_t cmd_bits = 0;
	
	LOG_INFO("enter\n");
    story_player_queue = xQueueCreate(STORY_PLAYER_QUEUE_SIZE, sizeof(story_player_cmd_node));
	task_exit = xSemaphoreCreateMutex();
	
	kclient = keyevent_register_listener(KEY_CODE_HABIT_MASK|KEY_CODE_ENGLISH_MASK|KEY_CODE_PLAY_NEXT_MASK|KEY_CODE_PLAY_PREV_MASK, story_keyprocess);
	if(!kclient){
		LOG_ERR("register key client fail\n");
		vSemaphoreDelete(task_exit);
		vQueueDelete(story_player_queue);
		return ESP_ERR_INVALID_RESPONSE;
	}

	cmd_bits = 1<<find_type_bit_offset(S2C_STORY_REALTIME_PUSH);
	cmd_bits |= 1<<find_type_bit_offset(S2C_STORY_VOLUME_CONTROL);
	g_command_handler = remote_cmd_register(cmd_bits, story_player_command_cb);

	//g_download_list.head = NULL;

	//check_file_in_dir(STORY_SOURCE_DIR);
	//check_file_in_dir(ENGLISH_SOURCE_DIR);

	g_story_player = player_init("story", STORY_SOURCE_DIR, AUDIO_APP_HABIT, story_audio_play_cb);

	if(!g_story_player){
		LOG_ERR("story player init failed, return failed\n");
		remote_cmd_unregister(g_command_handler);
		vSemaphoreDelete(task_exit);
		vQueueDelete(story_player_queue);
		return -ESP_FAIL;
	}

	g_english_player = player_init("english", ENGLISH_SOURCE_DIR, AUDIO_APP_ENGLISH, story_audio_play_cb);
	if(!g_english_player){
		LOG_ERR("story player init failed, return failed\n");
		player_uninit(g_story_player);
		remote_cmd_unregister(g_command_handler);
		vSemaphoreDelete(task_exit);
		vQueueDelete(story_player_queue);
		return -ESP_FAIL;
	}
	
	#ifdef STORY_PLAY_PAUSE
	g_story_song_player = player_init("habitssong", HABIT_SONG_SOURCE_DIR, AUDIO_APP_HABIT, story_audio_play_cb);	
	if(!g_story_song_player){		
		LOG_ERR("habitssong player init failed, return failed\n");	
		player_uninit(g_story_player);	
		player_uninit(g_english_player);
		remote_cmd_unregister(g_command_handler);		
		vSemaphoreDelete(task_exit);	
		vQueueDelete(story_player_queue);	
		return -ESP_FAIL;	
	}

	g_english_song_player = player_init("englishsong", ENGLISH_SONG_SOURCE_DIR, AUDIO_APP_ENGLISH, story_audio_play_cb);
	if(!g_english_song_player){		
		LOG_ERR("englishsong player init failed, return failed\n");		
		player_uninit(g_story_player);		
		player_uninit(g_english_player);		
		player_uninit(g_story_song_player);		
		remote_cmd_unregister(g_command_handler);		
		vSemaphoreDelete(task_exit);		
		vQueueDelete(story_player_queue);		
		return -ESP_FAIL;	
	}
	#endif
	
	g_current_player = g_story_player;
	task_need_run = true;
	
	#ifdef STORY_PLAY_PAUSE
	xTimerUser = xTimerCreate("play_tick", pdMS_TO_TICKS(1000),pdTRUE, NULL, play_record_tick);
	if(!xTimerUser) {
		LOG_ERR("creat xtimer fail\n");
		player_uninit(g_english_song_player);
        player_uninit(g_story_player);		
		player_uninit(g_english_player);		
		player_uninit(g_story_song_player);		
		remote_cmd_unregister(g_command_handler);		
		vSemaphoreDelete(task_exit);		
		vQueueDelete(story_player_queue);		
		return ESP_ERR_INVALID_RESPONSE;
	}
	record_play_msg = false;
	xTimerStart(xTimerUser, 100);
	play_record_queue = xQueueCreate(STORY_PLAYER_QUEUE_SIZE, sizeof(player_record_cmd_node));
	_task_exit = xSemaphoreCreateMutex();
	xTaskCreate(player_record_task, "player_record_task", 3072, NULL, 6, NULL);
	key_number_init();
	#endif
	
	xTaskCreate(story_player_task, "story_player_task", 5120, NULL, 5, NULL);
	LOG_INFO("exit\n");

	return 0;
}

void story_player_uninit(void)
{
	wechat_cmd_node node;

	LOG_INFO("called\n");
	
	remote_cmd_unregister(g_command_handler);
	task_need_run = false;
	node.cmd_value = STORY_PLAYER_CMD_UNDEFINE;
	node.param = NULL;
	node.size = 0;
	story_player_notify(&node, 100);
	xSemaphoreTake(task_exit, portMAX_DELAY);
	keyevent_unregister_listener(kclient);
	vSemaphoreDelete(task_exit);
	vQueueDelete(story_player_queue);
	//play stop
	player_uninit(g_english_player);
	player_uninit(g_story_player);
	return;
}

#ifndef __STORY_PLAYER_H__
#define __STORY_PLAYER_H__

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "rom/queue.h"
#include "audiomanagerservice.h"

#define PLAYER_NAME_LENGTH          (32)
#define PLAY_LIST_NAME_LENGTH       (32)
#define FOLDER_PATH_LENGTH          (80)
#pragma pack(1)
typedef struct{
    char list_name[PLAY_LIST_NAME_LENGTH];
    char folder_path[FOLDER_PATH_LENGTH];
    //item_state_t search_state;
    //item_entry_t *search_pos;
    LIST_HEAD(item_head_, item_entry_) list_head;
}play_list_t;

typedef struct{
	char player_name[PLAYER_NAME_LENGTH];
	AudioAppType audio_type;
	audio_callback audio_callback;
	play_list_t *play_list;
	SemaphoreHandle_t mutex_lock;
}story_player_t;
#pragma pack()
typedef enum{
	STORY_PLAYER_CMD_SWITCH_STORY,
	STORY_PLAYER_CMD_SWITCH_ENGLISH,
	#ifdef STORY_PLAY_PAUSE
	STORY_PLAYER_CMD_SWITCH_SONG,
	STORY_PLAYER_CMD_SWITCH_E_SONG,
	#endif
    STORY_PLAYER_CMD_PLAY_START,
    STORY_PLAYER_CMD_PLAY_STOP,
    STORY_PLAYER_CMD_PLAY_PAUSE, 
    STORY_PLAYER_CMD_PLAY_PREV,
    STORY_PLAYER_CMD_PLAY_NEXT,
    STORY_PLAYER_CMD_PLAY_REVERT,   
    STORY_PLAYER_CMD_REALTIME_PUSH,
    STORY_PLAYER_CMD_PUSH_CONTROL,
    //STORY_PLAYER_CMD_DOWNLOAD_START,
    //STORY_PLAYER_CMD_DOWNLOAD_FINISH,
    //STORY_PLAYER_CMD_DOWNLOAD_ERROR,
	STORY_PLAYER_CMD_UNDEFINE,
}story_player_cmd_value;
#pragma pack(1)
typedef struct{
	story_player_cmd_value cmd_value;
	void *param;
	int size;
	#ifdef STORY_PLAY_PAUSE
	int cb_sig;
	#endif
}story_player_cmd_node;

typedef struct realtime_push{
	char albumUid[20];
	char songUid[20];
	unsigned char play_mode;
}realtime_push_t;

#ifdef STORY_PLAY_PAUSE
typedef struct{
	unsigned int time;
	char player_name[STORY_FILE_NAME_LENGTH];
}player_name_time_t;
typedef struct{
	unsigned char habit_key;
	unsigned char english_key;
}key_sign_t;
#pragma pack()
#endif
int story_player_init(void);
void story_player_uninit(void);
#endif
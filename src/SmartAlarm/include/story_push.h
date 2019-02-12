#ifndef __STORY_PUSH_H__
#define __STORY_PUSH_H__

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "rom/queue.h"
#include "audiomanagerservice.h"
#include "story_player.h"

//#define FILE_PATH_MAX_SIZE		                (128)
//#define FILE_NAME_MAX_SIZE						(64)
//#define STORY_PUSH_HTTP_ADDRESS_MAX_SIZE		(512)

typedef enum{
    STORY_PUSH_CMD_LESSON_DELETE,
	STORY_PUSH_CMD_LESSON_PUSH,
	STORY_PUSH_CMD_DOWNLOAD_START,
	STORY_PUSH_CMD_DOWNLOAD_FINISH,
    STORY_PUSH_CMD_DOWNLOAD_ERROR,
	STORY_PUSH_CMD_ALARM_START,
	STORY_PUSH_CMD_ALARM_PLAY,
	STORY_PUSH_CMD_ALARM_STOP,
	STORY_PUSH_CMD_SONG_PLAY,
	STORY_PUSH_CMD_LESSON_RESULT_REPORT,
	STORY_PUSH_CMD_ALARM_PLAY_ERROR,
	//STORY_PUSH_CMD_TIME_OUT,
	//STORY_PUSH_CMD_LESSON_PUSH_REQUEST,
	STORY_PUSH_CMD_UNDEFINE,
}story_push_cmd_value;

typedef struct{
	story_push_cmd_value cmd_value;
	void *param;
	int size;
}story_push_cmd_node;

typedef enum{
	ALARM_SONG_PLAY_IDLE=0,
	ALARM_SONG_PLAY_INDEX,
	ALARM_SONG_PLAY_ACTIVE,
	ALARM_SONG_PLAY_UNDEF,
}alarm_s_list_state;

#pragma pack(1)
typedef struct songUidList{
	char songID[20];
	alarm_s_list_state sate;
	struct songUidList *next;
}songUidList_t;

typedef struct alarm_push_item{
	uint32_t alarm_id;
	uint8_t daysofweek;
	uint8_t enable;
	uint8_t hour;
	uint8_t minute;
	uint32_t listenDays;
	uint8_t playType;
	songUidList_t *songList;
	//struct alarm_push_item *next;
}alarm_push_item_t;

typedef struct alarm_song_info{
	uint8_t playCount;
	uint8_t playType;
	int alarm_id;
	char songID[20];
	char url[128];
	//struct alarm_song_info *next;
}alarm_song_info_t;
#pragma pack()

int story_push_init();
void story_push_uninit(void);
#endif

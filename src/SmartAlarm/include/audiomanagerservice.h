#ifndef _AUDIO_MANAGER_SERVICE_H_
#define _AUDIO_MANAGER_SERVICE_H_
#include "MediaService.h"
#include "rom/queue.h"

#define STORY_PLAY_PAUSE
#define PA_IO_NUM						21
#define PA_ENABLE()					    do{vTaskDelay(60);gpio_set_level(PA_IO_NUM, 1);vTaskDelay(60);}while(0)
#define PA_DISABLE()					do{vTaskDelay(60);gpio_set_level(PA_IO_NUM, 0);vTaskDelay(60);}while(0)
#define DEFAULT_VOLUME		54
#define MUTE_VOLUME		    0

#define NOTIFY_AUDIO_ENGLISH				"/sdcard/notify/e_story.mp3"
#define NOTIFY_AUDIO_HABIT					"/sdcard/notify/story.mp3"
#define NOTIFY_AUDIO_ENGLISH_SONG			"/sdcard/notify/e_song.mp3"
#define NOTIFY_AUDIO_HABIT_SONG				"/sdcard/notify/song.mp3"
#define NOTIFY_AUDIO_STORY_PAUSE			"/sdcard/notify/play_pause.mp3"
#define NOTIFY_AUDIO_LOWBATTERY				"/sdcard/notify/lowbattery.mp3"
#define NOTIFY_AUDIO_RETRY					"/sdcard/notify/retry.mp3"
#define NOTIFY_AUDIO_VCR_STARTRECORD		"/sdcard/notify/vrecord.mp3"
#define NOTIFY_AUDIO_WECHAT_SENDMSG			"/sdcard/notify/sendmsg.mp3"
#define NOTIFY_AUDIO_WECHAT_STARTRECORD		"/sdcard/notify/wrecord.mp3"
#define NOTIFY_AUDIO_WELCOME				"/sdcard/notify/welcome.mp3"
#define NOTIFY_AUDIO_WIFI_CONNECT_FINISH	"/sdcard/notify/wififinish.mp3"
#define NOTIFY_AUDIO_WIFI_CONNECT_START		"/sdcard/notify/wifistart.mp3"
#define NOTIFY_AUDIO_WIFI_SET_BY_PHONE		"/sdcard/notify/wifisetbyphone.mp3"
#define NOTIFY_AUDIO_CONNECT_PLEASE			"/sdcard/notify/connectplz.mp3"
#define NOTIFY_AUDIO_CAN_NOT_TRANSLATE		"/sdcard/notify/cannotranslate.mp3"
#define NOTIFY_AUDIO_START_TO_CHAT		    "/sdcard/notify/starttochat.mp3"
#define NOTIFY_AUDIO_WIFI_CONNECT_FAIL	    "/sdcard/notify/wificonnectfail.mp3"
#define NOTIFY_AUDIO_MY_FAVORITE	   	 	"/sdcard/notify/my_favorite.mp3"
#define NOTIFY_AUDIO_LET_ME_ALSO	    	"/sdcard/notify/let_me_also.mp3"
#define NOTIFY_AUDIO_I_AM_IN	    		"/sdcard/notify/i_am_in.mp3"
#define NOTIFY_AUDIO_HELLO					"/sdcard/notify/hello.mp3"
#define NOTIFY_AUDIO_TAKE_REST				"/sdcard/notify/take_rest.mp3"
#define NOTIFY_AUDIO_BYE_BYE				"/sdcard/notify/bye_bye.mp3"
#define NOTIFY_AUDIO_LEAVING				"/sdcard/notify/i_m_leaving.mp3"
#define NOTIFY_AUDIO_READ_THREE				"/sdcard/notify/read_threecp.mp3"
#define NOTIFY_AUDIO_TALETELLING			"/sdcard/notify/taletelling.mp3"
#define NOTIFY_AUDIO_PLAY_CSONG				"/sdcard/notify/play_csong.mp3"
#define NOTIFY_AUDIO_MATCHES_FAIL			"/sdcard/notify/Matching_failure.mp3"
#define NOTIFY_AUDIO_NOT_CONNECT			"/sdcard/notify/Network_not_connected.mp3"


typedef struct{
	int record_timer;
	bool is_overflow;
	
	/* vad process handler */
	void *vad_handler;
	bool vad_enable;
	uint32_t vad_slient_ms;
	uint32_t talk_ms;	
	int total_size;
	int used_size;
	char *buffer;
}record_ram_buffer_t;

typedef struct AudioManagerService { //extern from TreeUtility
    /*relation*/
    MediaService Based;
    int _smartCfg;
    /*private*/
} AudioManagerService;

typedef enum AudioManagerMsg {
    AUDIO_MANAGER_VOL_UP = 0x00,
    AUDIO_MANAGER_VOL_DOWN,
    AUDIO_MANAGER_PLAY_START,
    AUDIO_MANAGER_PLAY_STOP,
    #ifdef STORY_PLAY_PAUSE
    AUDIO_MANAGER_PLAY_PAUSE,
    AUDIO_MANAGER_PLAY_RESUME,
    #endif
    AUDIO_MANAGER_RECORD_START,
    AUDIO_MANAGER_RECORD_STOP,
    AUDIO_MANAGER_UNDEFINE,
} AudioManagerMsg;

typedef enum AudioAppType {
	AUDIO_APP_WECHAT = 0x00,
    AUDIO_APP_VOICE_RECOGNITION,
    AUDIO_APP_ALARM,
	AUDIO_APP_HABIT,
	AUDIO_APP_ENGLISH,
	AUDIO_APP_NOTIFY,
	AUDIO_APP_REALTIME_PUSH,
	AUDIO_APP_UNDEFINED,
} AudioAppType;

typedef enum NotifyStatus {
	NOTIFY_STATUS_PLAY_FINISH = 0x00, //play finish
	NOTIFY_STATUS_PLAY_DISTURBED, //play is disturbed by other app
	NOTIFY_STATUS_PLAY_ERROR, //play meet some error	
	NOTIFY_STATUS_PLAY_STOPPED, //play is stopped by app
	NOTIFY_STATUS_PLAY_PAUSE,//play is pause by other app
	NOTIFY_STATUS_PLAY_RESTART,//play is disturbed by other tone and restart
	NOTIFY_STATUS_RECORD_STOPPED, //record is stopped by app
	NOTIFY_STATUS_RECORD_DISTURBED, //record is disturbed by other app
	NOTIFY_STATUS_RECORD_EXCEED, //record meet some error
	NOTIFY_STATUS_RECORD_ERROR, //record meet some error
	NOTIFY_STATUS_RECORD_SEND, //record stop send data
	NOTIFY_STATUS_RECORD_READ,
	NOTIFY_STATUS_COMMAND_IGNORED,//command is ignored by ams
	NOTIFY_STATUS_UNDEFINE, 
} NotifyStatus;

typedef void (*audio_callback)(NotifyStatus,AudioAppType,AudioManagerMsg, void *);

typedef struct{
	AudioAppType play_app_type;
	bool is_local_file;
	char *uri;
	char *tone; 
	audio_callback cb; 
	void *cb_param;
}play_param_t;


#define STORY_FILE_NAME_LENGTH      (48)
typedef enum{
    ITEM_ST_IDLE = 0x00,
    ITEM_ST_PLAY,
    ITEM_ST_PAUSE,
    #ifdef STORY_PLAY_PAUSE
    ITEM_ST_STOP,
    #endif
    ITEM_ST_DAMAGE,
    ITEM_ST_UNDEFINE = 0xff,
}item_state_t;

typedef struct item_entry_ {
    item_state_t state;
    char file_name[STORY_FILE_NAME_LENGTH];
    LIST_ENTRY(item_entry_) entries;
	unsigned int time;
}item_entry_t;

#ifdef STORY_PLAY_PAUSE
typedef enum{
	PR_STOP = 0,
    PR_PLAY,
    PR_UNDEFINE,
}player_record_cmd_value;

typedef struct{
	player_record_cmd_value cmd_value;
	void *param;
}player_record_cmd_node;

typedef enum CodeType{
	CODE_START = 0x00,
	CODE_PROCEED,
    CODE_QUIT,
	CODE_QUIT_UNDEFINED,
} encodetype;
typedef struct{
	encodetype cmd_value;
	int code_len;
	void*param;
}amr_code_node;

typedef struct{
	int cmd_value;
	void*param;
}record_cmd_node;
#endif

int play_start(play_param_t *param);
int play_stop(AudioAppType play_app_type);
int record_start(AudioAppType record_app_type, void* uri, audio_callback cb, void *cb_param);
int record_stop(AudioAppType record_app_type);
void volume_up(void);
void volume_down(void);
int play_tone_sync(char *url);
#ifdef STORY_PLAY_PAUSE
int notification_record_play_time(player_record_cmd_node* cmd);
int play_pause(AudioAppType play_app_type);
int play_resume(AudioAppType play_app_type);
int play_tone_and_restart(char *url);
int get_volume_grade(void);
int get_play_status(char *local_file,char*_songid,char*_albumUid, int *local_status);
#endif
AudioManagerService *AudioManagerCreate();
#endif

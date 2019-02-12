#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_system.h"
#include "log.h"
#include "keyevent.h"
#include "audiomanagerservice.h"

#define LOG_TAG	    "RESET"

#ifdef CONFIG_USE_ESP32_LYRAT_BOARD //use esp32 lyrat board
int restore_init(void){return 0;}
int restore_uninit(void){return 0;}
#else
#define RESTORE_QUEUE_SIZE   (15)
#define RESTORE_SEC          (5)
#define SAVED_FILE_PATH "/sdcard/wifi_info"
#define UPGRADE_STEP_FILE_NAME "/sdcard/upgrade_step"

typedef struct{
	uint32_t type;
	uint32_t state;
	uint32_t error_code;
	char local_path[64];
	char remote_path[12];
	struct restore_action_t *next;
}restore_action_t;

static bool task_need_run = false;
static xQueueHandle restore_queue = NULL;
static keycode_client_t *kclient = NULL;
static bool is_factory_mode = false;
static restore_action_t action = {0,1,0,"/sdcard/factory/test.amr"};

void set_factory_mode(bool mode){
    is_factory_mode = mode;
}

bool get_factory_mode(){
    return is_factory_mode;
}

static void reset_audio_record_cb(NotifyStatus ret,AudioAppType type,AudioManagerMsg cmd, void *param)
{
    play_param_t play_param = {AUDIO_APP_WECHAT,true,"/sdcard/factory/test.amr",NULL,NULL,NULL};
    LOG_INFO("reset_audio_record_cb\n");
    play_start(&play_param);
}

static void restore_task(void* arg)
{
	while(task_need_run){
        int cmd = 255;
        char tone_path[32] = {0};
        if(xQueueReceive(restore_queue, &cmd, portMAX_DELAY) == pdTRUE){
            LOG_INFO("receive reset cmd %d\n",cmd);

            if(cmd >= KEY_CODE_UNDEFINE)
                continue;

            sprintf(tone_path, "/sdcard/factory/%d.mp3",cmd);

            LOG_INFO("which key press %s\n",tone_path);

            if(cmd == KEY_CODE_RESET){
                play_tone_sync(tone_path);
                unlink("/sdcard/factory/test.amr");
                unlink(SAVED_FILE_PATH);
                unlink(UPGRADE_STEP_FILE_NAME);
                esp_restart();
            }else if(cmd == KEY_CODE_VOICE_RECOGNIZE){
                LOG_INFO("record voice and play it %s\n",action.local_path); 
                record_stop(AUDIO_APP_WECHAT);
            }else{
                play_tone_sync(tone_path);
            }
        }
    }	
    vTaskDelete(NULL);
}

static keyprocess_t system_reset_keyprocess(keyevent_t *event)
{
    static struct timeval timestamp= {0};
    long threshold = 0;
    int cmd = 255;

	LOG_DBG("receive key event %d type %d\n", event->code, event->type);
    if(event->code == KEY_CODE_RESET){
        if(KEY_EVNET_PRESS == event->type){
            memcpy(&timestamp, &event->timestamp, sizeof(struct timeval));
            if(get_factory_mode() == true)
                play_tone_sync("/sdcard/factory/test_reset.mp3");
        }else if(KEY_EVNET_RELEASE == event->type){
            struct timeval release_timestamp= {0};
            memcpy(&release_timestamp, &event->timestamp, sizeof(struct timeval));
            if(timestamp.tv_sec != 0 || timestamp.tv_usec != 0){
                threshold = release_timestamp.tv_sec-timestamp.tv_sec;
                LOG_INFO("threshold %ld\n",threshold);
                if(threshold > RESTORE_SEC){
                    cmd = event->code;
                    if(xQueueSend(restore_queue, &cmd, portMAX_DELAY) != pdTRUE){
                     LOG_DBG("malloc manage connet Q failed\n");
                        return -1;
                    }
                }
            }else{
                timestamp.tv_sec = 0;
                timestamp.tv_usec = 0;
            }
        }else{
            timestamp.tv_sec = 0;
            timestamp.tv_usec = 0;
        }
    }else{
        if(get_factory_mode() == false)
            return; 
        LOG_INFO("whick key release %d\n",event->code);
        if(KEY_EVNET_PRESS == event->type && event->code == KEY_CODE_VOICE_RECOGNIZE){
            play_tone_sync("/sdcard/factory/3.mp3");
            record_start(AUDIO_APP_WECHAT, (void *)action.local_path, reset_audio_record_cb, NULL);
        }else if(KEY_EVNET_RELEASE == event->type){
            cmd = event->code;
            if(xQueueSend(restore_queue, &cmd, portMAX_DELAY) != pdTRUE){
                LOG_DBG("malloc manage connet Q failed\n");
                return -1;
            }
        }
    }
    return KEY_PROCESS_PUBLIC;
}

int restore_uninit(void)
{
    keyevent_unregister_listener(kclient);
    task_need_run = false;
	vQueueDelete(restore_queue);
	return;
}

int restore_init(void)
{	
    if(get_factory_mode() == false){
        kclient = keyevent_register_listener(KEY_CODE_RESET_MASK, system_reset_keyprocess);
    }else{
        kclient = keyevent_register_listener(KEY_CODE_WECHAT_MASK|KEY_CODE_VOL_UP_MASK|KEY_CODE_VOL_DOWN_MASK|KEY_CODE_VOICE_RECOGNIZE_MASK|KEY_CODE_BRIGHTNESS_MASK|KEY_CODE_PLAY_NEXT_MASK|KEY_CODE_PLAY_PREV_MASK|KEY_CODE_HABIT_MASK|KEY_CODE_ENGLISH_MASK|KEY_CODE_ALARM_MASK|KEY_CODE_RESET_MASK, system_reset_keyprocess);
    }

	if(kclient == NULL){
		LOG_ERR("register key client fail\n");
        return -1;
    }

    restore_queue = xQueueCreate(RESTORE_QUEUE_SIZE, sizeof(int));

    if(restore_queue == NULL)
    {
        keyevent_unregister_listener(kclient);
        LOG_ERR("restore_queue fail\n");
        return -2;  
    }

    task_need_run = true;

    if(xTaskCreate(restore_task, "restore_task", 4096, NULL, 8, NULL) != pdPASS){
        task_need_run = false;
        restore_uninit();
        LOG_ERR("--**--restore_task startup failed\n");
        return -3;
    }
    
	return 0;
}
#endif
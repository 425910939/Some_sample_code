#ifndef __KEYEVENT_H__
#define __KEYEVENT_H__
#include <sys/time.h>
#include "freertos/timers.h"
#include "application.h"

#define SHORT_PRESS_THRESHOLD	(1500) // < 500 ms short > 500 long

#define KEY_CODE_MAP(a)			(1<<a)
#define KEY_CODE_IS_MASK(a,b)	(a&(1<<b))	

//used for keypress interrupt --- begin
/*
typedef enum{
	KEY_CODE_WECHAT = 0x00,
	KEY_CODE_VOL_UP,
	KEY_CODE_VOL_DOWN,
	KEY_CODE_VOICE_RECOGNIZE,
	KEY_CODE_PLAY_PAUSE,
	KEY_CODE_PLAY_NEXT,
	KEY_CODE_PLAY_PREV,
	KEY_CODE_UNDEFINE,
}uint32_t;
*/

#define KEY_CODE_WECHAT 			(0)
#define KEY_CODE_VOL_UP				(1)
#define KEY_CODE_VOL_DOWN           (2)
#define KEY_CODE_VOICE_RECOGNIZE    (3)
#define KEY_CODE_BRIGHTNESS         (4)
#define KEY_CODE_PLAY_NEXT          (5)
#define KEY_CODE_PLAY_PREV          (6)
#define KEY_CODE_START_ESP_TOUCH    (7)
#define KEY_CODE_HABIT    			(8)
#define KEY_CODE_ENGLISH    		(9)
#define KEY_CODE_ALARM    			(10)
#define KEY_CODE_PAT         		(11)
#define KEY_CODE_RESET         		(12)
#define KEY_CODE_UNDEFINE           (13)


#define KEY_CODE_WECHAT_MASK		    (uint32_t)((uint32_t)1U<<KEY_CODE_WECHAT)
#define KEY_CODE_VOL_UP_MASK            (uint32_t)((uint32_t)1U<<KEY_CODE_VOL_UP)
#define KEY_CODE_VOL_DOWN_MASK          (uint32_t)((uint32_t)1U<<KEY_CODE_VOL_DOWN)
#define KEY_CODE_VOICE_RECOGNIZE_MASK   (uint32_t)((uint32_t)1U<<KEY_CODE_VOICE_RECOGNIZE)
#define KEY_CODE_BRIGHTNESS_MASK        (uint32_t)((uint32_t)1U<<KEY_CODE_BRIGHTNESS)
#define KEY_CODE_PLAY_NEXT_MASK         (uint32_t)((uint32_t)1U<<KEY_CODE_PLAY_NEXT)
#define KEY_CODE_PLAY_PREV_MASK         (uint32_t)((uint32_t)1U<<KEY_CODE_PLAY_PREV)
#define KEY_CODE_START_ESP_TOUCH_MASK   (uint32_t)((uint32_t)1U<<KEY_CODE_START_ESP_TOUCH)
#define KEY_CODE_HABIT_MASK    			(uint32_t)((uint32_t)1U<<KEY_CODE_HABIT)
#define KEY_CODE_ENGLISH_MASK    		(uint32_t)((uint32_t)1U<<KEY_CODE_ENGLISH)
#define KEY_CODE_ALARM_MASK    			(uint32_t)((uint32_t)1U<<KEY_CODE_ALARM)
#define KEY_CODE_PAT_MASK          		(uint32_t)((uint32_t)1U<<KEY_CODE_PAT)
#define KEY_CODE_RESET_MASK          	(uint32_t)((uint32_t)1U<<KEY_CODE_RESET)
#define KEY_CODE_UNDEFINE_MASK          (uint32_t)((uint32_t)1U<<KEY_CODE_UNDEFINE)


#define KEYCODE_TO_KEYMASK(k)       (uint32_t)((uint32_t)1U<<k)


typedef enum{
	KEY_ACTION_PRESS = 0x00,
	KEY_ACTION_RELEASE = 0x01,
	KEY_ACTION_UNDEFINE,
}keypress_t;

typedef struct keymsg{
	uint32_t code;
	keypress_t press;
	struct timeval press_time;
}keymsg_t;
//used for keypress interrupt --- end

//used for keypress task --- begin
typedef enum{
	KEY_STATE_WAIT_PRESS,//wait press
	KEY_STATE_WAIT_RELEASE,
	KEY_STATE_WAIT_CONFIRM_SHORT,//wait release
	KEY_STATE_WAIT_CONFIRM_LONG,//wait release
	KEY_STATE_UNDEFINE,
}keystate_t;

typedef struct{
	uint32_t code;
	bool is_mask;
	keystate_t state;
	struct timeval press_time;
	struct timeval release_time;
}keyaction_t;

typedef struct{
    keyaction_t map[0];
}keyaction_map;
//used for keypress task --- end

//used for notify app --- begin
typedef enum{
	KEY_EVNET_SHORT_PRESS = 0x00,
	KEY_EVNET_LONG_PRESS = 0x01,
	KEY_EVNET_PRESS = 0x02,
	KEY_EVNET_RELEASE = 0x03,
	KEY_EVNET_UNDEFINE,
}keypress_event_t;

typedef struct{
	uint32_t code;
	keypress_event_t type;
	struct timeval timestamp;
}keyevent_t;

typedef enum{
	KEY_PROCESS_PRIVATE,
	KEY_PROCESS_PUBLIC,
}keyprocess_t;

typedef struct{
	struct keycode_client_t *next;
	uint32_t key_code_map;
	keyprocess_t (*notify)(keyevent_t *event);
}keycode_client_t;
//used for notify app --- end

int keyaction_notfiy(keymsg_t *msg);
int keyaction_notfiy_from_isr(keymsg_t *msg);
int keyevent_unregister_listener(keycode_client_t *client);
void keyevent_mask_notify(application_t app, uint32_t key_code_map);
void keyevent_unmask_notify(application_t app, uint32_t key_code_map);
keycode_client_t *keyevent_register_listener(uint32_t key_code, keyprocess_t (*callback)(keyevent_t *event));
int keyevent_dispatch_init(void);
#endif

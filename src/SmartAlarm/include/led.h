#ifndef __LED_H__
#define __LED_H__

typedef enum{
    LED_CLIENT_PRI_URGENT = 0, //red-blink
    LED_CLIENT_PRI_SYS, //wechat incoming msg
    LED_CLIENT_PRI_USR, //alarm
}led_client_pri_t;

typedef enum{
    LED_CLIENT_BATTERY = 0, //red-blink
    LED_CLIENT_WECHAT, //wechat incoming msg
    LED_CLIENT_ALARM, //alarm
    LED_CLIENT_OTHER, //other
}led_client_app_t;

typedef enum{
    LED_BREATH_COLOR_RED,
    LED_BREATH_COLOR_BLUE,
    LED_BREATH_COLOR_GREEN,
    LED_BREATH_COLOR_PURPLE,
    LED_BREATH_COLOR_CYAN,
    LED_BREATH_COLOR_YELLOW,
    LED_BREATH_COLOR_WHITE,
    LED_BREATH_COLOR_UNDEFINE,
}led_breath_color_t;

typedef enum{
    LED_MODE_ALL_OFF = 0,
    LED_MODE_NIGHT_HALF = 1,
    LED_MODE_NIGHT_ON = 2,
    LED_MODE_RED_ON = 3,
    LED_MODE_BLUE_ON = 4,
    LED_MODE_GREEN_ON = 5,
    LED_MODE_PURPLE_ON = 6,
    LED_MODE_CYAN_ON = 7,
    LED_MODE_COLORFUL_LOOP = 8,
    LED_MODE_COLORFUL_BREATH = 9,
    LED_MODE_RED_BLINK = 10, 
    LED_MODE_GREEN_BLINK = 11, 
    LED_MODE_BLUE_BLINK = 12,
    LED_MODE_AUTO = 13, 
    LED_MODE_UNDEFINE,
}led_mode_t;

typedef enum{
    LED_NOTIFY_MODE_SWITCH_IGNORED,
    LED_NOTIFY_CLIENT_OCCUPY,  
	LED_NOTIFY_UNDEFINE, 
} led_notify_t;

typedef void (*led_callback)(led_notify_t);

typedef struct{
    led_client_app_t app;
    led_client_pri_t pri;
    led_callback cb;
}led_client_t;

typedef struct{
	led_client_t client;
	led_mode_t mode;
}ledmsg_t;

int led_init(void);
void led_uninit(void);
void led_mode_set(led_client_app_t app, led_mode_t mode, led_callback cb);
led_mode_t led_mode_get(void);
#endif


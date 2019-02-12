#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#define DISPLAY_COLON_THRESHOLD	(1000) //1000-msec
#define DISPLAY_JITTER			(100)//100-msec

typedef enum{
	DISPLAY_RECT_TIME = 0x00,
	DISPLAY_RECT_WIFI,
	#ifdef CONFIG_ENABLE_POWER_MANAGER
	DISPLAY_RECT_BRIGHTNESSE,
	#endif
	DISPLAY_RECT_NONE,
}display_rect_t;

typedef struct{
	bool need_update;
	display_rect_t rect;
	union{
		struct{
			uint8_t hour;
			uint8_t minute;
			uint8_t second;
		}rect_time;
		struct{
			bool connnect_state;
		}rect_wifi;
		#ifdef CONFIG_ENABLE_POWER_MANAGER
		struct{
			uint8_t bright_level;
		}rect_brightness;
		#endif
	}buff;
}display_buff_t;

display_buff_t *alloc_display_client(const char *app_name, void (*notify)(void *result));
int free_display_client(display_buff_t *buff);
int display_update(display_buff_t *buff);
int display_task_init(void);
void display_task_uninit(void);
#endif


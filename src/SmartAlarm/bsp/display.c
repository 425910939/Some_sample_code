#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "esp_task_wdt.h"
#include "display.h"
#include "tm1628.h"
#include "log.h"

#define LOG_TAG	    "display"

#undef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#undef container_of
#define container_of(ptr, type, member) ({                      \
const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
(type *)( (char *)__mptr - offsetof(type,member) );})

#define DISPLAY_QUEUE_SIZE		(4)
#ifdef CONFIG_ENABLE_POWER_MANAGER
#define DISPLAY_CLIENT_MAX		(2)
#else
#define DISPLAY_CLIENT_MAX		(1)
#endif
typedef struct{
	uint8_t id;
	//task_name / handler to verify
	display_buff_t buffer;
	void (*notify)(void *result);
}display_client_t;

static SemaphoreHandle_t task_exit;
static portMUX_TYPE display_lock = portMUX_INITIALIZER_UNLOCKED;
static xQueueHandle display_queue;
static bool task_need_run = true;
static display_client_t g_dclient[DISPLAY_CLIENT_MAX];

static int display_time_rect(display_buff_t *buff)
{	
	tm1628_tick_process(buff);
	return 0;
}
#ifdef CONFIG_ENABLE_POWER_MANAGER
static int display_brightness_rect(display_buff_t *buff)
{
	tm1628_set_brightness(buff);
	return 0;
}
#endif
#ifdef CONFIG_USE_ESP32_LYRAT_BOARD //use esp32 lyrat board
display_buff_t *alloc_display_client(const char *app_name, void (*notify)(void *result)){return NULL;}
int free_display_client(display_buff_t *buff){return 0;}
int display_update(display_buff_t *buff){return 0;}
int display_task_init(void){return 0;}
void display_task_uninit(void){return;}

#else //use self board
display_buff_t *alloc_display_client(const char *app_name, void (*notify)(void *result))
{
	static int i = 0;
	display_buff_t *ptr = NULL;
	
	portENTER_CRITICAL(&display_lock);
	#ifndef  CONFIG_ENABLE_POWER_MANAGER
	for(i=0; i<DISPLAY_CLIENT_MAX; i++){
		if(!g_dclient[i].id){
			g_dclient[i].id = (i+1);
			g_dclient[i].notify = notify;
			ptr = &g_dclient[i].buffer;
		}
	}
	#else
	if(i < DISPLAY_CLIENT_MAX){
		g_dclient[i].id = (i+1);
		g_dclient[i].notify = notify;
		ptr = &g_dclient[i].buffer;
		i++;
	}
	#endif
	portEXIT_CRITICAL(&display_lock);
	return ptr;
}

int free_display_client(display_buff_t *buff)
{
	display_client_t *dclient;
	
	if(buff){
		portENTER_CRITICAL(&display_lock);
		dclient = container_of(buff, display_client_t, buffer);
		memset(dclient, 0, sizeof(display_client_t));	
		portEXIT_CRITICAL(&display_lock);
	}else{
		LOG_ERR("NULL pointer\n");
		return ESP_ERR_INVALID_ARG;
	}
	return 0;
}

int display_update(display_buff_t *buff)
{
	LOG_DBG("send buff\n");
	xQueueSend(display_queue, &buff, portMAX_DELAY);
	return 0;
}

static void display_task(void *param)
{
	static uint8_t	led_seq = 0;
	static uint32_t breath_count = 0;

	xSemaphoreTake(task_exit, portMAX_DELAY);
	while(task_need_run){
        display_buff_t *buff = NULL;
        if(pdTRUE == xQueueReceive(display_queue, &buff, pdMS_TO_TICKS(DISPLAY_JITTER))){
			switch(buff->rect){
				case DISPLAY_RECT_TIME:
					display_time_rect(buff);
					break;
				case DISPLAY_RECT_WIFI:
					break;
				#ifdef CONFIG_ENABLE_POWER_MANAGER
				case DISPLAY_RECT_BRIGHTNESSE:
					display_brightness_rect(buff);
					
					break;
				#endif
				default:
					LOG_ERR("unsupport rect ignore it\n");
					break;
			}
		}else{
			display_time_rect(NULL);
		}
	}
	vTaskDelay(1);
    vTaskDelete(NULL);
    xSemaphoreGive(task_exit);
}

int display_task_init(void)
{
	LOG_INFO("enter\n");
	memset(g_dclient, 0, sizeof(display_client_t)*DISPLAY_CLIENT_MAX);
    display_queue = xQueueCreate(DISPLAY_QUEUE_SIZE, sizeof(display_buff_t *));
	task_exit = xSemaphoreCreateMutex();
	task_need_run = true;
	//xTaskCreate(display_task, "diplay_task", 2048, NULL, 8, NULL);
	xTaskCreatePinnedToCore(display_task, "diplay_task", 2048, NULL, 6, NULL, 1);
	LOG_INFO("exit\n");

	return 0;
}

void display_task_uninit(void)
{
	display_buff_t buff;

	LOG_INFO("called\n");
	//unreg all display clients
	task_need_run = false;
	buff.rect = DISPLAY_RECT_NONE;
	display_update(&buff);
	xSemaphoreTake(task_exit, portMAX_DELAY);
	vSemaphoreDelete(task_exit);
	vQueueDelete(display_queue);

	return;
}
#endif
/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/


#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "esp_log.h"
#include "display.h"
#include "power_manager.h"
#include "esp_pm.h"
#include "log.h"
#include "audiomanagerservice.h"
#include "MediaHal.h"

#ifndef CONFIG_ENABLE_POWER_MANAGER
wake_lock_t *  wake_lock_init(wake_type_t type,unsigned char *name){return NULL;}
int release_wake_lock(wake_lock_t *r_lock){return 0;}
int acquire_wake_lock(wake_lock_t *a_lock){return 0;}
int wake_lock_destroy(wake_lock_t *d_lock){return 0;}
int power_manager_init(void){return 0;}
#else
#define LOG_TAG		"pm"
#define POWER_MANAGER_TIME  (60*1000)

static TimerHandle_t xTimerPower;
static wake_lock_t *wake_lock_head = NULL;
static SemaphoreHandle_t task_exit;
static xQueueHandle pm_queue=NULL;
static xTaskHandle task_pm_handle = NULL;
static bool task_need_run = true;
static display_buff_t *display_brightness = NULL;
static bool sleep_manager_staus = true;

wake_lock_t * wake_lock_init(wake_type_t type,unsigned char *name)
{
	wake_lock_t * lock_n;
	lock_n = (wake_lock_t *)heap_caps_malloc(sizeof(wake_lock_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if(lock_n == NULL){
		LOG_INFO("malloc wake_lock failed\n");
		return NULL;
	}
	
	lock_n->type = type;
	lock_n->active = 0;
	memset(lock_n->name,0,sizeof(lock_n->name));
	memcpy(lock_n->name,name,NAME_LEN);
	
	if(wake_lock_head==NULL){	
		wake_lock_head = (wake_lock_t *)heap_caps_malloc(sizeof(wake_lock_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		wake_lock_head->type = WAKE_LOCK_NONE;
		wake_lock_head->active = 0;
		memset(wake_lock_head->name,0,sizeof(wake_lock_head->name));
		memcpy(wake_lock_head->name,"none_wake_lock",NAME_LEN);
		wake_lock_head->next = lock_n;
		lock_n->next = NULL;
	}
	else{
		lock_n ->next = wake_lock_head->next;
		wake_lock_head->next = lock_n;
	}
	
    return lock_n;

}

int wake_lock_destroy(wake_lock_t *d_lock)
{
	wake_lock_t * pm_temp;

    if(NULL == wake_lock_head)
        return -1;
	
	if(wake_lock_head==d_lock){
        wake_lock_head = wake_lock_head->next;
        free(d_lock);
		return 0;
    }

	pm_temp = wake_lock_head;
    while(NULL != pm_temp->next){
		if(pm_temp->next == d_lock){
			pm_temp->next = d_lock->next;
			free(d_lock);
			break;
		}	
        pm_temp = pm_temp->next;
    }

    return 0;
}

int acquire_wake_lock(wake_lock_t *a_lock)
{
	if(pm_queue!=NULL){
		a_lock->active =1;
		//LOG_INFO("acquire_wake_lock = %s\n",a_lock->name);
		xQueueSend(pm_queue, a_lock, 100);
	}
	return 0;
}

int release_wake_lock(wake_lock_t *r_lock)
{

	r_lock->active =0;
	//LOG_INFO("release_wake_lock = %s\n",r_lock->name);
	if(xTimerPower!=NULL)
		xTimerReset(xTimerPower,100);
	return 0;
}

static int destroy_all_wake_lock(void)
{
	if(NULL == wake_lock_head)
        return -1;
	wake_lock_t *temp;
	while(NULL != wake_lock_head->next){
		temp = wake_lock_head->next;
		wake_lock_head->next = temp->next;
		free(temp);
		temp = NULL;
	}
	free(wake_lock_head);
	wake_lock_head =NULL;

	return 0;
	
}

static int search_wake_lock(void)
{
    wake_lock_t * lock_temp = NULL;
    if(NULL == wake_lock_head)
        return 0;
	
	lock_temp = wake_lock_head->next;
	do{
		if(lock_temp->active == 1){
			LOG_INFO("lock name = %s\n",lock_temp->name);	
			return 1;
		}
		lock_temp = lock_temp->next;
	}while(NULL != lock_temp);
	
    return 0;
}

static void power_manager_tick(TimerHandle_t xTimer)
{
	if((pm_queue!=NULL) && (wake_lock_head!=NULL))
		xQueueSend(pm_queue, wake_lock_head, 100);
	
}

static void power_manager_task(void *param)
{

	wake_lock_t notify;
	LOG_INFO("power_manager_task running ...\n");
	xSemaphoreTake(task_exit, portMAX_DELAY);
	while(task_need_run){
		if(pdTRUE == xQueueReceive(pm_queue, &notify, portMAX_DELAY)){
			LOG_INFO("power %d %d %s\n",notify.type,notify.active,notify.name);
			switch(notify.type){
				case WAKE_LOCK_NONE:
					if(search_wake_lock() == 0){
						if(sleep_manager_staus){
							sleep_manager_staus = false;
							display_brightness->rect = DISPLAY_RECT_BRIGHTNESSE;
							display_brightness->buff.rect_brightness.bright_level = 0x88;
							display_update(display_brightness);
							MediaHalStop(CODEC_MODE_DECODE_ENCODE);
							xTimerStop(xTimerPower, 100);
						}else{
							LOG_INFO("The device is already sleep\n");
						}
					}
					else{
						LOG_INFO("wake lock not release\n");
					}
					break;
				default:
					if(notify.active == 1){	
						if(!sleep_manager_staus){
							LOG_INFO("The device wake up\n");
							sleep_manager_staus = true;
							display_brightness->rect = DISPLAY_RECT_BRIGHTNESSE;
							display_brightness->buff.rect_brightness.bright_level = 0x8f;
							display_update(display_brightness);
							MediaHalStart(CODEC_MODE_DECODE_ENCODE);
							xTimerStart(xTimerPower, 100);
						}
					}
					break;
			}
		}
	}
	vTaskDelay(1);
	vTaskDelete(NULL);
	xSemaphoreGive(task_exit);

}

int power_manager_init(void)
{

	display_brightness = alloc_display_client("brightness", NULL);
	if(display_brightness){
		task_exit = xSemaphoreCreateMutex();
		pm_queue = xQueueCreate(10, sizeof(wake_lock_t));
	    xTimerPower = xTimerCreate("power_manager_tick", pdMS_TO_TICKS(POWER_MANAGER_TIME), pdTRUE, NULL, power_manager_tick);
		if(!xTimerPower) {
			LOG_ERR("creat xtimer fail\n");
	        vSemaphoreDelete(task_exit);
	        vQueueDelete(pm_queue);
			return ESP_ERR_INVALID_RESPONSE;
		}
		xTaskCreate(power_manager_task, "pm_task", 2048, NULL, 5, &task_pm_handle);
		xTimerStart(xTimerPower, 100);
	
	}
	else{
		LOG_ERR("alloc display buff fail\n");
	}
	
	return 0;
}


int power_manager_uninit(void)
{

	destroy_all_wake_lock();
	
	xTimerStop(xTimerPower, 100);
	task_need_run = false;
    xSemaphoreTake(task_exit, portMAX_DELAY);
    vSemaphoreDelete(task_exit);
	return 0;
}
#endif



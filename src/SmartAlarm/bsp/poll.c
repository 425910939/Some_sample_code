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
#include "poll.h"
#include "log.h"

#define time_before_eq(a, b) \
		((a).tv_sec < (b).tv_sec || \
		 ((a).tv_sec == (b).tv_sec && (a).tv_usec <= (b).tv_usec))

#define LOG_TAG	    "poll"

static SemaphoreHandle_t task_exit;
static bool task_need_run = true;
static xQueueHandle timer_queue;
static portMUX_TYPE poll_lock = portMUX_INITIALIZER_UNLOCKED;
#ifdef USE_XTIMER
static TimerHandle_t xTimerUser;
#endif

static LIST_HEAD(poll_func_entries_head, poll_func_entry) s_poll_func_entries_head =
    LIST_HEAD_INITIALIZER(s_poll_func_entries_head);

#ifdef USE_XTIMER
static void poll_tick_isr(TimerHandle_t xTimer)
{
    poll_event_t evt;

	xQueueSend(timer_queue, &evt, 100);
}
#else
static void IRAM_ATTR poll_tick_isr(void* varg) {
    portBASE_TYPE high_priority_task_awoken = 0;
    poll_event_t evt;

    TIMERG0.int_clr_timers.t0 = 1;

    xQueueSendFromISR(timer_queue, &evt, &high_priority_task_awoken);
    if (high_priority_task_awoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}
#endif

static inline void insert_poll_list(poll_func_entry_t *new_entry)
{
    poll_func_entry_t *after_entry = NULL;
    poll_func_entry_t *before_entry = NULL;

	if(LIST_EMPTY(&s_poll_func_entries_head)){
        LIST_INSERT_HEAD(&s_poll_func_entries_head, new_entry, entries);
		goto out;
	}else{
		for (after_entry = LIST_FIRST(&s_poll_func_entries_head); after_entry != NULL; 
			before_entry = after_entry, after_entry = LIST_NEXT(after_entry, entries)) {
			if(time_before_eq(new_entry->expire, after_entry->expire)){
		        LIST_INSERT_BEFORE(after_entry, new_entry, entries);
				goto out;
			}
	    }
		LIST_INSERT_AFTER(before_entry, new_entry, entries);
	}

out:
	return;
}


static inline void timer_config(long long duration)//us
{
#ifdef USE_XTIMER
#define TICK_TIMER_US	(1000000/configTICK_RATE_HZ)
	long long tick;
	//xTimerStop(xTimerUser, portMAX_DELAY);
	tick = (duration + TICK_TIMER_US - 1)/TICK_TIMER_US;
	tick = (tick<=0)?1:tick;//add for sometime tick = 0 panic
	//LOG_DBG("HZ %d, xtimer duration %lld, tick %lld\n", configTICK_RATE_HZ, duration, tick);
	xTimerChangePeriod(xTimerUser, tick, portMAX_DELAY);
#else
	 timer_config_t config = {
            .alarm_en = false,
            .counter_en = false,
            .intr_type = TIMER_INTR_LEVEL,
            .counter_dir = TIMER_COUNT_UP,
            .auto_reload = false,
            .divider = 80
    };
    timer_pause(TIMER_GROUP_0, TIMER_0);
    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, duration);
    timer_set_alarm(TIMER_GROUP_0, TIMER_0, TIMER_ALARM_EN);
    timer_start(TIMER_GROUP_0, TIMER_0);
    LOG_DBG("alarm set value %lld\n", duration);
    //timer_get_alarm_value(TIMER_GROUP_0, TIMER_0, &duration);
    //LOG_DBG("alarm get value %lld\n", duration);
#endif
}


static void poll_task(void* varg) {
    struct timeval now;
    poll_func_entry_t *entry;
	long long sec, msec, duration;

	xSemaphoreTake(task_exit, portMAX_DELAY);
    while(task_need_run){
        poll_event_t evt;
        if(pdTRUE == xQueueReceive(timer_queue, &evt, portMAX_DELAY)){
			//portENTER_CRITICAL(&poll_lock);
			entry = LIST_FIRST(&s_poll_func_entries_head);
			if(!entry){
				LOG_DBG("no entry continue\n");
				//portEXIT_CRITICAL(&poll_lock);
				continue;
			}

			do{
				gettimeofday(&now, NULL);
				if(time_before_eq(entry->expire, now)){	
					entry->used = true;
					//portEXIT_CRITICAL(&poll_lock);
					//LOG_DBG("one poll func %x called - %lds%ldms\n", (uint32_t)entry->poll_callback, now.tv_sec, now.tv_usec/1000);
					entry->poll_callback(entry->param);
					//portENTER_CRITICAL(&poll_lock);
					entry->used = false;
					LIST_REMOVE(entry, entries);
					gettimeofday(&now, NULL);
					sec = entry->period / 10;
					msec = (entry->period - sec * 10) *100;
					entry->expire.tv_sec = sec + now.tv_sec;
					entry->expire.tv_sec += (now.tv_usec / 1000 + msec) / 1000;
					entry->expire.tv_usec = ((now.tv_usec  + (msec - 1) * 1000) % 1000000);
					insert_poll_list(entry);
					entry = LIST_FIRST(&s_poll_func_entries_head);
				}else{
					entry = NULL;
				}
				vTaskDelay(10);
			}while(entry);
			entry = LIST_FIRST(&s_poll_func_entries_head);
			if(entry){
				gettimeofday(&now, NULL);
				duration = (entry->expire.tv_sec - now.tv_sec) * 1000000;
				duration += (entry->expire.tv_usec - now.tv_usec);
				timer_config(duration);
			}
			//portEXIT_CRITICAL(&poll_lock);
		}
    }
    vTaskDelay(1);
    vTaskDelete(NULL);
    xSemaphoreGive(task_exit);
}

poll_func_entry_t *register_poll_func(poll_func_config_t *config)
{
	long long sec, msec, duration;
    poll_func_entry_t *new_entry;
    struct timeval now;


	LOG_INFO("called\n");
	if(!config || !config->poll_callback){
		LOG_ERR("invaild params\n");
		return NULL;
	}

    new_entry = (poll_func_entry_t *) calloc(1, sizeof(poll_func_entry_t));
    if (new_entry == NULL) {
    	LOG_ERR("calloc failed\n");
        return NULL;
    }

	portENTER_CRITICAL(&poll_lock);
	gettimeofday(&now, NULL);
	sec = config->period / 10;
	msec = (config->period - sec * 10) *100;
	new_entry->expire.tv_sec = sec + now.tv_sec;
	new_entry->expire.tv_sec += (now.tv_usec / 1000 + msec) / 1000;
	new_entry->expire.tv_usec = ((now.tv_usec  + (msec - 1) * 1000) % 1000000);
	new_entry->period = config->period;
	new_entry->poll_callback = config->poll_callback;
	new_entry->param = config->param;
	new_entry->used = false;
	LOG_DBG("time: %ld.%ld\n",now.tv_sec, now.tv_usec);
	insert_poll_list(new_entry);
	if(new_entry == LIST_FIRST(&s_poll_func_entries_head)){
		duration = (new_entry->expire.tv_sec - now.tv_sec) * 1000000;
		duration += (new_entry->expire.tv_usec - now.tv_usec);
		timer_config(duration);
    }
	portEXIT_CRITICAL(&poll_lock);
	return new_entry;
}

int unregister_poll_func(poll_func_entry_t *entry)//禁止在回调或中断里调用
{
    struct timeval now;
	poll_func_entry_t *next_entry;
	long long duration;

	LOG_INFO("called\n");
	if(!entry || entry->used){
		LOG_ERR("invaild params\n");
		return ESP_ERR_INVALID_ARG;
	}
	portENTER_CRITICAL(&poll_lock);
	gettimeofday(&now, NULL);
	LOG_DBG("time: %ld.%ld\n",now.tv_sec, now.tv_usec);
	if(entry == LIST_FIRST(&s_poll_func_entries_head)){
		next_entry = LIST_NEXT(entry, entries);
		duration = (next_entry->expire.tv_sec - now.tv_sec) * 1000000;
		duration += (next_entry->expire.tv_usec - now.tv_usec);
		timer_config(duration);
    }
	LIST_REMOVE(entry, entries);
	free(entry);
	entry = NULL;
	portEXIT_CRITICAL(&poll_lock);
	return 0;
}

int poll_task_init(void)
{
	LOG_INFO("enter\n");
    timer_queue = xQueueCreate(10, sizeof(poll_event_t));
	task_exit = xSemaphoreCreateMutex();
#ifdef USE_XTIMER
	xTimerUser = xTimerCreate("poll_isr", pdMS_TO_TICKS(100), pdFALSE, NULL, poll_tick_isr);
	if(!xTimerUser) {
		LOG_ERR("creat xtimer fail\n");
		return ESP_ERR_INVALID_RESPONSE;
	}
#else
	timer_isr_register(TIMER_GROUP_0, TIMER_0, &poll_tick_isr, NULL, 0, NULL);
#endif
    task_need_run = true;
    xTaskCreate(poll_task, "poll_task", 3072, NULL, 3, NULL);
	LOG_INFO("exit\n");
    return 0;
}

int poll_task_uninit(void)
{
	poll_event_t evt;

    task_need_run = false;
#ifdef USE_XTIMER
    xTimerStop(xTimerUser, portMAX_DELAY);
#endif
    xQueueSend(timer_queue, &evt, 100);
    xSemaphoreTake(task_exit, portMAX_DELAY);
#ifdef USE_XTIMER
   	xTimerDelete(xTimerUser, 0);
#else
	timer_pause(TIMER_GROUP_0, TIMER_0);
    timer_disable_intr(TIMER_GROUP_0, TIMER_0);
#endif
    vQueueDelete(timer_queue);
    vSemaphoreDelete(task_exit);
    return 0;
}

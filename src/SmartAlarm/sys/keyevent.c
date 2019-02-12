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
#include "poll.h"
#include "log.h"
#include "keyevent.h"
#include "power_manager.h"

#define LOG_TAG	    "keyevt"

static SemaphoreHandle_t task_exit;
static xQueueHandle key_queue = NULL;
static portMUX_TYPE key_lock = portMUX_INITIALIZER_UNLOCKED;
static bool task_need_run = true;
static keyaction_map *g_keyactmap = NULL;
static keycode_client_t *g_keyclient = NULL;
#ifdef CONFIG_ENABLE_POWER_MANAGER
static wake_lock_t *  key_wake_lock =NULL;
#endif
static const char *keycode_name[KEY_CODE_UNDEFINE] = {
	"wechat", "volum_up", "volum_down",
	"voice recognize", "brightness", 
	"play_next", "play_prev", "esp_touch",
	"habit", "english", "alarm", "pat","reset"
};

static int time_threshold(struct timeval press_time,struct timeval last_time){
	long a = (press_time.tv_sec*1000) + (press_time.tv_usec/1000);
	long b = (last_time.tv_sec*1000) + (last_time.tv_usec/1000);

	if(a > b){
		return (a - b);
	}else if (a < b){
		return 1100;
	}else{
		return 0;
	}
}

static int notify_keyevent(keyevent_t *event)
{
    keycode_client_t *node;
	keyprocess_t process;

	LOG_INFO("notify app key (%s), event (%s), timestamp %ld.%ld\n", 
		(event->code<KEY_CODE_UNDEFINE?keycode_name[event->code]:"undefine"), 
		(event->type==KEY_EVNET_PRESS?"PRESS":"RELEASE"), event->timestamp.tv_sec, event->timestamp.tv_usec);

    if (g_keyclient==NULL)
    {
        LOG_ERR("NULL keyevent\n");
        return 0;
    }
	
	#ifdef CONFIG_ENABLE_POWER_MANAGER
	if((event->type==KEY_EVNET_PRESS) && (key_wake_lock!=NULL)){
		acquire_wake_lock(key_wake_lock);
	}
	if((event->type==KEY_EVNET_RELEASE) && (key_wake_lock!=NULL)){
		release_wake_lock(key_wake_lock);
	}
	#endif
    //portENTER_CRITICAL(&key_lock);
    for(node=g_keyclient; node!=NULL; )
    {
        if(0!=(node->key_code_map & KEYCODE_TO_KEYMASK(event->code)))
        {
            process = node->notify(event);
			if(process == KEY_PROCESS_PRIVATE)//single process break out
				break;
        }
        node = node->next;
    }
   // portEXIT_CRITICAL(&key_lock);

    return 0;
}

static void key_task(void* varg) {	
	keyevent_t event;
	long long msec, duration, tick;
	struct timeval now;
	keyaction_t *action;

	xSemaphoreTake(task_exit, portMAX_DELAY);
    while(task_need_run){
		keymsg_t msg;
        if(pdTRUE == xQueueReceive(key_queue, &msg, portMAX_DELAY)){
			//LOG_DBG("receive Key %d Action %d\n", msg.code, msg.press);
			if(msg.code >= KEY_CODE_UNDEFINE){
				LOG_ERR("invaild Key %d, ignore it\n", msg.code);
				continue;
			}
			if((msg.code == KEY_CODE_PAT) && 
				(time_threshold(msg.press_time, g_keyactmap->map[msg.code].press_time) > 1000)){//ignore global key map state machine
				memcpy(&(g_keyactmap->map[msg.code].press_time), &(msg.press_time), sizeof(struct timeval));
				if(g_keyactmap->map[msg.code].is_mask == false){
					event.code = msg.code;
					event.type = KEY_EVNET_PRESS;
					memcpy(&(event.timestamp), &(g_keyactmap->map[msg.code].press_time), sizeof(struct timeval));
					notify_keyevent(&event);//notify client event
				}	
			}else{
				switch(g_keyactmap->map[msg.code].state){
					case KEY_STATE_WAIT_PRESS:
						if(msg.press == KEY_ACTION_PRESS){
							g_keyactmap->map[msg.code].state = KEY_STATE_WAIT_RELEASE;
							memcpy(&(g_keyactmap->map[msg.code].press_time), &(msg.press_time), sizeof(struct timeval));
							if(g_keyactmap->map[msg.code].is_mask == false){
								event.code = msg.code;
								event.type = KEY_EVNET_PRESS;
								memcpy(&(event.timestamp), &(g_keyactmap->map[msg.code].press_time), sizeof(struct timeval));
								notify_keyevent(&event);//notify client event
							}
						}else{//ignore release msg
							continue;
						}
						break;
					case KEY_STATE_WAIT_RELEASE:
						if(msg.press == KEY_ACTION_RELEASE){
							g_keyactmap->map[msg.code].state = KEY_STATE_WAIT_PRESS;
							memcpy(&(g_keyactmap->map[msg.code].release_time), &(msg.press_time), sizeof(struct timeval));
							if(g_keyactmap->map[msg.code].is_mask == false){
								event.code = msg.code;
								event.type = KEY_EVNET_RELEASE;
								memcpy(&(event.timestamp), &(g_keyactmap->map[msg.code].release_time), sizeof(struct timeval));
								notify_keyevent(&event);//notify client event
							}
						}else{//ignore press msg
							continue;
						}
						break;
					default:
						break;
				}
			}	
		}
    }
    vTaskDelay(1);
    vTaskDelete(NULL);
    xSemaphoreGive(task_exit);
}

int keyevent_unregister_listener(keycode_client_t *client)
{
    int ret = 0;
    keycode_client_t *cur;

    //portENTER_CRITICAL(&key_lock);
    if(g_keyclient==NULL)
    {
        LOG_ERR("inv keyclient unreg\n");
        ret = -1;
        goto out;
    }

    if(g_keyclient==client) //delete head if head is expacted unregister node
    {
        g_keyclient = g_keyclient->next;
        free(client);
        ret = 0;
        goto out;
    }

    for(cur=g_keyclient; cur->next!=NULL; cur = cur->next)
    {
        if(cur->next==client)
        {
            cur->next = client->next;
            free(client);
            ret = 0;
            goto out;
        }
    }

out:
    //portEXIT_CRITICAL(&key_lock);
    LOG_ERR("keyclient unreg res:%d\n", ret);

    return ret;
}

keycode_client_t *keyevent_register_listener(uint32_t key_code, keyprocess_t (*callback)(keyevent_t *event))
{
    keycode_client_t *node;
    keycode_client_t *client;

	
	//client = (keycode_client_t *)malloc(sizeof(keycode_client_t));
	client = (keycode_client_t *)heap_caps_malloc(sizeof(keycode_client_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if(client == NULL)
	{
		LOG_ERR("malloc keyclient failed\n");
		return NULL;
	}
	client->key_code_map = key_code;
	client->notify = callback;
	client->next = NULL;

    //portENTER_CRITICAL(&key_lock);
    if(g_keyclient==NULL)//empty
    {
		g_keyclient = client;
		goto out;
    }

	node=g_keyclient;
	while(node->next)
		node=node->next;

	node->next = client;

 out:
   // portEXIT_CRITICAL(&key_lock);
    return client;
}

void keyevent_mask_notify(application_t app, uint32_t key_code_map)
{
	int key_code = 0;

	for(; key_code < KEY_CODE_UNDEFINE; key_code++)
    {
        if((0 != (key_code_map & KEYCODE_TO_KEYMASK(key_code)))
			&& (g_keyactmap->map[key_code].is_mask == false))
        {
            g_keyactmap->map[key_code].is_mask = true;
			LOG_INFO("app (%d) mask key (%d) notify...\n", app, key_code);
        }
    }
	return;
}

void keyevent_unmask_notify(application_t app, uint32_t key_code_map)
{
	int key_code = 0;

	for(; key_code < KEY_CODE_UNDEFINE; key_code++)
    {
        if((0 != (key_code_map & KEYCODE_TO_KEYMASK(key_code)))
			&& (g_keyactmap->map[key_code].is_mask == true))
        {
            g_keyactmap->map[key_code].is_mask = false;
			LOG_INFO("app (%d) unmask key (%d) notify...\n", app, key_code);
        }
    }
	return;
}

int keyaction_notfiy(keymsg_t *msg)
{
	if(msg && key_queue){
		//LOG_DBG("notify Key %d Action %d\n", msg->code, msg->press);
		gettimeofday(&msg->press_time, NULL);
		xQueueSend(key_queue, msg, 100);
	}
	return 0;
}

int keyaction_notfiy_from_isr(keymsg_t *msg)
{
	if(msg && key_queue){
		gettimeofday(&msg->press_time, NULL);
		xQueueSendToBackFromISR(key_queue, msg, 0);
	}
	return 0;
}

int keyevent_dispatch_init(void)
{
	int i = 0;

	LOG_INFO("enter\n");
	task_exit = xSemaphoreCreateMutex();
	key_queue = xQueueCreate(10, sizeof(keymsg_t));
	if(NULL==key_queue)
	{
		LOG_ERR("creat xtimer fail\n");
		return ESP_ERR_INVALID_RESPONSE;
	}
	//g_keyactmap = (keyaction_map *)malloc(sizeof(keyaction_map) + sizeof(keyaction_t) * KEY_CODE_UNDEFINE);
	g_keyactmap = (keyaction_map *)heap_caps_malloc((sizeof(keyaction_map) + sizeof(keyaction_t) * KEY_CODE_UNDEFINE), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if(!g_keyactmap){
		LOG_ERR("malloc keymap failde\n");
		return ESP_ERR_NO_MEM;
	}
	for(i=0; i<KEY_CODE_UNDEFINE; i++){
		g_keyactmap->map[i].code = i;
		g_keyactmap->map[i].state = KEY_STATE_WAIT_PRESS;
		g_keyactmap->map[i].is_mask = false;
	}

	task_need_run = true;
	if(pdPASS != xTaskCreate(key_task, "key_task", 4608, NULL, 5, NULL))
	{
		LOG_ERR("creat task fail\n");
		return ESP_ERR_INVALID_RESPONSE;
	}
	#ifdef CONFIG_ENABLE_POWER_MANAGER
	key_wake_lock  = wake_lock_init(WAKE_LOCK_KEY,(unsigned char*)"key_wake_lock");
	#endif
	LOG_INFO("exit\n");
	return 0;
}

int keyevent_dispatch_uninit(void)
{
	LOG_INFO("called\n");
	keymsg_t msg;

	msg.code = KEY_CODE_UNDEFINE;
    msg.press = KEY_ACTION_RELEASE;
	task_need_run = false;
    xQueueSend(key_queue, &msg, 100);
    xSemaphoreTake(task_exit, portMAX_DELAY);
    vQueueDelete(key_queue);
    free(g_keyactmap);
    vSemaphoreDelete(task_exit);
	#ifdef CONFIG_ENABLE_POWER_MANAGER
	wake_lock_destroy(key_wake_lock);
	#endif
    return 0;
}

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
#include <sys/time.h>
#include "soc/timer_group_struct.h"
#include "rom/ets_sys.h"
#include "soc/rtc.h"
#include "soc/timer_group_reg.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "driver/gpio.h"
#include "esp_task_wdt.h"
#include "log.h"
#include "display.h"
#include "alarms.h"
#include "apps/sntp/sntp.h"
#include "connect_manager.h"

#define LOG_TAG		"clock"
#define ALARM_TASK_TICK     (10*1000) //10-seconds
#define ALARM_QUEUE_SIZE		(10)

typedef struct{
    alarm_t node;
    struct alarm_node_t *next;
}alarm_node_t;

typedef struct{
    alarm_node_t *parent;
    struct alarm_scan_node_t *next;
}alarm_scan_node_t;

typedef struct{
    bool vaild;
	int	wday;
    int year;
    int month;
    int day;
    int hour;
    int minute;
}alarm_scan_data_t;

typedef enum{
	ALARM_CMD_ADD_ALARM = 0,
	ALARM_CMD_DEL_ALARM,
    ALARM_CMD_FRESH_ALL,
	ALARM_CMD_UNDEFINE,
}alarm_cmd_t;

typedef struct{
	alarm_cmd_t command;
	void *param;
	int size;
}alarm_cmd_node_t;

static alarm_node_t *alarm_list_head = NULL;
static alarm_scan_node_t *scan_list_head = NULL;
static alarm_scan_data_t g_scan_date;
//static portMUX_TYPE list_spinlock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t task_exit;
static xQueueHandle alarm_queue;
static bool task_need_run = true;
static display_buff_t *display_clock_buffer = NULL;

static inline int add_alarm_list(alarm_node_t *node)
{
    alarm_node_t *ptr = alarm_list_head;
    int ret = 0;

    if(node){
        //portENTER_CRITICAL(&list_spinlock);
        if(alarm_list_head){
            while(ptr->next)
                ptr = ptr->next;
            ptr->next = node;
            node->next = NULL;
        }else{
            alarm_list_head = node;
            node->next = NULL;
        }
        //portEXIT_CRITICAL(&list_spinlock);
    }else{
        ret = -ESP_ERR_NOT_FOUND;
        LOG_ERR("alarm node is NULL...\n");
    }
    return ret;
}

static inline alarm_node_t * seek_alarm_list(int alarm_id)
{
    alarm_node_t *current = NULL;
    alarm_node_t *node = NULL;

    //portENTER_CRITICAL(&list_spinlock);
    current = alarm_list_head;
    while(current){
        if(current->node.alarm_id == alarm_id){
            node = current;
            break;
        }
        current = current->next;
    }
    //portEXIT_CRITICAL(&list_spinlock);
    return node;
}

static inline alarm_node_t * del_alarm_list(int alarm_id)
{
    alarm_node_t *current = NULL;
    alarm_node_t *prev = NULL;
    alarm_node_t *node = NULL;

    if(alarm_list_head){
        //portENTER_CRITICAL(&list_spinlock);
        current = alarm_list_head;
        while(current){
            if(current->node.alarm_id == alarm_id){
                node = current;
                if(prev){//not head node
                    prev->next = current->next;
                }else{//head node
                    alarm_list_head = alarm_list_head->next;
                }
                break;
            }
            prev = current;
            current = current->next;
        }
        //portEXIT_CRITICAL(&list_spinlock);
    }else{
        LOG_ERR("alarm list is empty...\n");
    }
    return node;
}

static inline int add_scan_list(alarm_scan_node_t *node)
{
    alarm_scan_node_t *ptr = scan_list_head;
    int ret = 0;

    if(node){
        //portENTER_CRITICAL(&list_spinlock);
        if(scan_list_head){
            while(ptr->next)
                ptr = ptr->next;
            ptr->next = node;
            node->next = NULL;
        }else{
            scan_list_head = node;
            node->next = NULL;
        }
        //portEXIT_CRITICAL(&list_spinlock);
    }else{
        ret = -ESP_ERR_NOT_FOUND;
        LOG_ERR("scan node is NULL...\n");
    }
    return ret;
}

static inline alarm_scan_node_t * del_scan_list(int alarm_id)
{
    alarm_scan_node_t *current = NULL;
    alarm_scan_node_t *prev = NULL;
    alarm_scan_node_t *node = NULL;

    if(scan_list_head){
        //portENTER_CRITICAL(&list_spinlock);
        current = scan_list_head;
        while(current){
            if(current->parent->node.alarm_id == alarm_id){
                node = current;
                if(prev){//not head node
                    prev->next = current->next;
                }else{//head node
                    scan_list_head = scan_list_head->next;
                }
                break;
            }
            prev = current;
            current = current->next;
        }
        //portEXIT_CRITICAL(&list_spinlock);
    }else{
        LOG_ERR("scan list is empty...\n");
    }
    return node;
}

int add_alarm(alarm_t *app_alarm)
{
    alarm_cmd_node_t node;

    if(app_alarm){
        node.size = sizeof(alarm_t);
        node.param = heap_caps_malloc(node.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	    if(node.param){
            memcpy(node.param, app_alarm, node.size);
            node.command = ALARM_CMD_ADD_ALARM;
            xQueueSend(alarm_queue, &node, 0);
        }else{
            LOG_ERR("alloc alarm parameter failed\n");
        }
    }else{
        LOG_ERR("alarm parameter is NULL\n");
    }
	return 0;
}

void remove_alarm(int alarm_id)
{
    alarm_cmd_node_t node;

    node.command = ALARM_CMD_DEL_ALARM;
    node.param = (void *)alarm_id;
    node.size = 0;
    xQueueSend(alarm_queue, &node, 0);
    
    return;
}

void fresh_alarm(void)
{
    alarm_cmd_node_t node;

    node.command = ALARM_CMD_FRESH_ALL;
    node.param = NULL;
    node.size = 0;
    xQueueSend(alarm_queue, &node, 0);
    return;
}
static int time_equal(alarm_scan_data_t now_time,alarm_node_t * node)
{
	if(NULL == node)
		return 0;
	
	if((now_time.hour == node->node.hour) && (now_time.minute == node->node.minute))
		return 1;
	else
		return 0;
}

static void alarm_task(void *param)
{
	alarm_cmd_node_t node;
    time_t now;
    struct tm timeinfo;
    alarm_t *alarm = NULL;
    alarm_node_t *alarm_node = NULL;
	alarm_node_t *alarm_temp = NULL;
    alarm_scan_node_t *add_pos = NULL;
	alarm_scan_node_t *fresh_pos = NULL;
	alarm_scan_node_t *pos = NULL;
    alarm_scan_node_t *temp = NULL;

	xSemaphoreTake(task_exit, portMAX_DELAY);
	while(task_need_run){
		memset(&node, 0, sizeof(alarm_cmd_node_t));
        if(pdTRUE == xQueueReceive(alarm_queue, &node, pdMS_TO_TICKS(ALARM_TASK_TICK))){
			LOG_DBG("alarm receive cmd %d...\n", node.command);
			switch(node.command){
				case ALARM_CMD_ADD_ALARM:
                    alarm = (alarm_t *)node.param;
                    LOG_DBG("new alarm id (%d), expire time (%d:%d), cycle (0x%x),state =%d callback (0x%x)...\n",
                        alarm->alarm_id, alarm->hour, alarm->minute, 
                        alarm->week_map,alarm->state ,(unsigned int)alarm->callback);
					alarm_node = seek_alarm_list(alarm->alarm_id);
					if(alarm_node){
						LOG_DBG("new alarm is exist id = %d\n",alarm->alarm_id);
						del_alarm_list(alarm_node->node.alarm_id);
						free(alarm_node);
						alarm_node = NULL;
					}
					alarm_node = (alarm_node_t *)heap_caps_malloc(sizeof(alarm_node_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
					if(alarm_node){
						alarm_node->next = NULL;
						memcpy(&(alarm_node->node), alarm, sizeof(alarm_t));
						//alarm_node->node.state = ALARM_ST_ON;
						add_alarm_list(alarm_node);
						if(alarm_node->node.week_map & (1<<g_scan_date.wday)){//add alarm to scan list immediately
							 add_pos = (alarm_scan_node_t *)heap_caps_malloc(sizeof(alarm_scan_node_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
							 if(add_pos){
							 	add_pos->parent = alarm_node;
								add_pos->next = NULL;
							 	add_scan_list(add_pos);
								LOG_DBG("new alarm id %d node expire time (%d:%d) add to alarm and scan list success...\n",
									alarm_node->node.alarm_id, alarm_node->node.hour, alarm_node->node.minute);
							 }else{
								LOG_ERR("new alarm id %d node expire time (%d:%d) just add to alarm list for scan node malloc fail...\n",
									alarm_node->node.alarm_id, alarm_node->node.hour, alarm_node->node.minute);
							 }
						}else{
							LOG_DBG("new alarm id %d node expire time (%d:%d) just add to alarm list for today (%d) not set...\n",
								alarm_node->node.alarm_id, alarm_node->node.hour, alarm_node->node.minute, g_scan_date.wday);
						}
					}else{
						LOG_ERR("malloc alarm node buffer failed...\n");
					}
					break;
				case ALARM_CMD_DEL_ALARM:
					alarm_node = seek_alarm_list((int)node.param);
					if(alarm_node){
						alarm_node->node.state = ALARM_ST_OFF;
                    	LOG_DBG("off alarm id (%d), expire time (%d:%d), cycle (0x%x), callback (0x%x)...\n",
                        	alarm_node->node.alarm_id, alarm_node->node.hour, alarm_node->node.minute, 
                            alarm_node->node.week_map, (unsigned int)alarm_node->node.callback);
					}else{
						LOG_INFO("off alarm id (%d) fail, not found in alarm list...\n", (int)node.param);
					}
                    break;
                case ALARM_CMD_FRESH_ALL:
                    //step 1. update scan date
                    time(&now);
                    localtime_r(&now, &timeinfo);
                    g_scan_date.year = timeinfo.tm_year + 1900;
                    g_scan_date.month = timeinfo.tm_mon + 1;
                    g_scan_date.day = timeinfo.tm_mday;
					g_scan_date.wday = timeinfo.tm_wday;
                    g_scan_date.vaild = true;
                    LOG_DBG("update scan date to %d/%d/%d week day %d....\n", g_scan_date.year,
                        g_scan_date.month, g_scan_date.day,g_scan_date.wday);

					//step 2. remove delted alarm node & rebuild today scan list
                    fresh_pos = scan_list_head;
                    while(fresh_pos){//delete prev scan list
                        temp = fresh_pos->next;
                        del_scan_list(fresh_pos->parent->node.alarm_id);
                        free(fresh_pos);
						fresh_pos = NULL;
                        fresh_pos = temp;
                    }
                    alarm_node = alarm_list_head;
                    while(alarm_node){//check alarm list each node
                    	vTaskDelay(1);
                    	alarm_temp = alarm_node->next;
                    	if(alarm_node->node.state == ALARM_ST_OFF){//delete garbage alarm node
							LOG_DBG("delete alarm id (%d) node expire time (%d:%d) ...\n",
								alarm_node->node.alarm_id, alarm_node->node.hour, alarm_node->node.minute);							
							del_alarm_list(alarm_node->node.alarm_id);
							free(alarm_node);
							alarm_node = NULL;
						}else if(alarm_node->node.week_map & (1<<g_scan_date.wday)){//today this alarm match week day
							 fresh_pos = (alarm_scan_node_t *)heap_caps_malloc(sizeof(alarm_scan_node_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
							 if(fresh_pos){
							 	fresh_pos->parent = alarm_node;
								fresh_pos->next = NULL;
							 	add_scan_list(fresh_pos);
								LOG_DBG("alarm id %d node expire time (%d:%d) son scan node active success...\n",
									alarm_node->node.alarm_id, alarm_node->node.hour, alarm_node->node.minute);
							 }else{
								LOG_ERR("alarm id %d node expire time (%d:%d) son scan node malloc failed...\n",
									alarm_node->node.alarm_id, alarm_node->node.hour, alarm_node->node.minute);
							 }
						}else{//today this alarm not match week day
							LOG_DBG("alarm id %d node expire time (%d:%d) today not active...\n",
								alarm_node->node.alarm_id, alarm_node->node.hour, alarm_node->node.minute);
						}
						alarm_node = alarm_temp;
                    }
                    break;
				default:
					LOG_ERR("unknown cmd %d receive, ignore it...\n", node.command);
					break;
			}
            if(node.param && node.size){
                free(node.param);
				node.param = NULL;
            }
		}

        if(g_scan_date.vaild){
            //check scan list
            time(&now);
            localtime_r(&now, &timeinfo);
            //step 1. check whether need fresh scan list
            if(timeinfo.tm_mday != g_scan_date.day){//need update scan list now
				fresh_alarm();
            }else{
                if(g_scan_date.hour != timeinfo.tm_hour){
                    g_scan_date.hour = timeinfo.tm_hour;
                    display_clock_buffer->buff.rect_time.hour = g_scan_date.hour;
                    display_clock_buffer->need_update = true;
                }
                if(g_scan_date.minute != timeinfo.tm_min){
                    g_scan_date.minute = timeinfo.tm_min;
                    display_clock_buffer->buff.rect_time.minute = g_scan_date.minute;
                    display_clock_buffer->need_update = true;
                }
                if(display_clock_buffer->need_update){
                    display_clock_buffer->rect = DISPLAY_RECT_TIME;
                    display_update(display_clock_buffer);
                    display_clock_buffer->need_update = false;
                }

                //step 2. process expire scan node
                pos = scan_list_head;
                while(pos && pos->parent){
					//vTaskDelay(1);
                    if(time_equal(g_scan_date, pos->parent) && 
						(pos->parent->node.state == ALARM_ST_ON)){
                        //execute callback
                        if(pos->parent->node.callback){
                            LOG_DBG("current (%d:%d) alarm (%d) expire, callback (0x%x) execute start...\n",
                                g_scan_date.hour, g_scan_date.minute, pos->parent->node.alarm_id, (unsigned int)pos->parent->node.callback);
                            (void)pos->parent->node.callback(pos->parent->node.alarm_id);
                            LOG_DBG("alarm (%d) expire, callback (0x%x) execute stop...\n",
                                pos->parent->node.alarm_id, (unsigned int)pos->parent->node.callback);
                        }else{
                            LOG_DBG("current (%d:%d) alarm (%d) expire, callback is NULL...\n",
                                g_scan_date.hour, g_scan_date.minute, pos->parent->node.alarm_id);
                        }
                        temp = pos->next;
                        del_scan_list(pos->parent->node.alarm_id);
						if(pos->parent->node.one_shot)//only once, turn off parent node
							pos->parent->node.state = ALARM_ST_OFF;
                        free(pos);
						pos = NULL;
                        pos = temp;
                    }else{
                        pos = pos->next;
                    }
                }
            }
        }
	}
	vTaskDelay(1);
    vTaskDelete(NULL);
    xSemaphoreGive(task_exit);
}

int alarm_init(void)
{	
	LOG_INFO("enter\n");
    display_clock_buffer = alloc_display_client("alarms", NULL);
    if(display_clock_buffer){
        memset(display_clock_buffer, 0 , sizeof(display_buff_t));
        alarm_queue = xQueueCreate(ALARM_QUEUE_SIZE, sizeof(alarm_cmd_node_t));
        task_exit = xSemaphoreCreateMutex();
        memset(&g_scan_date, 0 , sizeof(alarm_scan_data_t));
        g_scan_date.vaild = false;
        task_need_run = true;
        xTaskCreate(alarm_task, "alarms_task", 4608, NULL, 6, NULL);
    }else{
        LOG_ERR("alloc display buffer failed\n");
    }
	LOG_INFO("exit\n");
	return 0;
}

void alarm_uninit(void)
{
	alarm_cmd_node_t node;

	LOG_INFO("called\n");
	task_need_run = false;
	node.command = ALARM_CMD_UNDEFINE;
	node.param = NULL;
	node.size = 0;
	xQueueSend(alarm_queue, &node, 0);
	xSemaphoreTake(task_exit, portMAX_DELAY);
	vSemaphoreDelete(task_exit);
    vQueueDelete(alarm_queue);
    free_display_client(display_clock_buffer);
	//...........
	//...........
	//free all alarm list node & scan list node
	//...........
	// code not finish need to fix
	//...........
	//...........
	return;
}
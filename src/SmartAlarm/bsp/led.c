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
#include "extern_gpio.h"
#include "led.h"
#include "log.h"
#include "keyevent.h"
#include "sc7a20.h"

#define LOG_TAG	    "led"

#define LED_FRESH_JITTER    (100)
#define LED_BLINK_JITTER    (800)
#define LED_QUEUE_SIZE		(5)

static SemaphoreHandle_t task_exit;
static xQueueHandle led_queue;
static keycode_client_t *kclient;
static bool task_need_run = true;
static led_mode_t led_work_mode =  LED_MODE_ALL_OFF;
static uint8_t  led_breath_step = 0;
static led_breath_color_t  led_breath_color = 0;
static uint8_t  led_blink_step = 0;
static uint8_t  led_blink_onoff = 0;
static const uint8_t led_step_value[] = {
    0x01, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 
    0x10, 0x14, 0x18, 0x1C, 0x20, 0x28, 0x30, 0x38, 
    0x40, 0x50, 0x60, 0x70, 0x80, 0xA0, 0xC0, 0xE0, 
    0xFF, 0xE0, 0xC0, 0xA0, 0x80, 0x70, 0x60, 0x50, 
    0x40, 0x38, 0x30, 0x28, 0x20, 0x1C, 0x18, 0x14, 
    0x10, 0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02,
};

static void led_work_mode_switch(ledmsg_t *msg)
{
    static led_client_t current_client = {LED_CLIENT_OTHER, LED_CLIENT_PRI_USR, NULL};
    bool need_update_mode = false;

    if(msg->client.pri <= current_client.pri){
        LOG_INFO("new app (%d) instaed prev app (%d) become led msater\n", msg->client.app, current_client.app);
        if(current_client.cb)
            current_client.cb(LED_NOTIFY_CLIENT_OCCUPY);//notify app lose led-controller
        memcpy(&current_client, &msg->client, sizeof(led_client_t));
        need_update_mode = true;
    }else{
        LOG_INFO("prev app (%d) have higher pri than new app (%d)\n", current_client.app, msg->client.app);     
        if(msg->client.cb)
            msg->client.cb(LED_NOTIFY_MODE_SWITCH_IGNORED);//notify app mode switch failed
    }

    if(need_update_mode){
        if(msg->mode == LED_MODE_AUTO){//pat use auto mode
            if(led_work_mode <= LED_MODE_RED_BLINK){
                led_work_mode = (led_mode_t)(((uint8_t)(led_work_mode + 1))%LED_MODE_RED_BLINK);
				if(led_work_mode == LED_MODE_COLORFUL_LOOP)//ignore colorful loop method
					led_work_mode = LED_MODE_COLORFUL_BREATH;
			}else{
                led_work_mode = LED_MODE_NIGHT_HALF;
			}
        }else if(msg->mode == LED_MODE_ALL_OFF){//app turn off means release led-controller
            led_work_mode = msg->mode;
            current_client.app = LED_CLIENT_OTHER;
            current_client.pri = LED_CLIENT_PRI_USR;
            current_client.cb = NULL;
        }else{//wechat,battery,alarm use specify mode
            led_work_mode = msg->mode;
        }

        switch(led_work_mode){
            case LED_MODE_ALL_OFF:
            case LED_MODE_RED_BLINK:
            case LED_MODE_GREEN_BLINK:
            case LED_MODE_BLUE_BLINK:
            case LED_MODE_COLORFUL_BREATH:
            case LED_MODE_COLORFUL_LOOP:
                led_breath_color = LED_BREATH_COLOR_RED;
                led_breath_step = 0;
                led_blink_step = 0;
                led_blink_onoff = 1;
                rgb_display_reset();
                night_led_onoff(NIGHT_LIGHT_OFF);
                break;
            case LED_MODE_NIGHT_HALF:
                rgb_display_reset();
                night_led_onoff(NIGHT_LIGHT_HALF);
                break;
            case LED_MODE_NIGHT_ON:
                rgb_display_reset();
                night_led_onoff(NIGHT_LIGHT_ON);
                break;
            case LED_MODE_RED_ON:
                rgb_display_reset();
                rgb_display_red(RGB_LIGHT_ON);
                night_led_onoff(NIGHT_LIGHT_OFF);
                break;
            case LED_MODE_BLUE_ON:
                rgb_display_reset();
                rgb_display_blue(RGB_LIGHT_ON);
                night_led_onoff(NIGHT_LIGHT_OFF);
                break;
            case LED_MODE_GREEN_ON:
                rgb_display_reset();
                rgb_display_green(RGB_LIGHT_ON);
                night_led_onoff(NIGHT_LIGHT_OFF);
                break;
            case LED_MODE_PURPLE_ON:
                rgb_display_reset();
                rgb_display_purple(RGB_LIGHT_ON);
                night_led_onoff(NIGHT_LIGHT_OFF);
                break;
            case LED_MODE_CYAN_ON:
                rgb_display_reset();
                rgb_display_cyan(RGB_LIGHT_ON);
                night_led_onoff(NIGHT_LIGHT_OFF);
                break;
            default:
                LOG_ERR("undefine work mode %d, ignore it\n", led_work_mode);
                led_work_mode = LED_MODE_ALL_OFF;
                break;
        }
        LOG_INFO("led work mode switch to (%d)...\n", led_work_mode);
    }
    return;
}

static inline void led_breath_blink_simulate(void)
{
    switch(led_work_mode){
        case LED_MODE_COLORFUL_BREATH:
            switch(led_breath_color){
                case LED_BREATH_COLOR_RED:
                    rgb_display_red(led_step_value[led_breath_step]);
                    break;
                case LED_BREATH_COLOR_BLUE:
                    rgb_display_blue(led_step_value[led_breath_step]);
                    break;
                case LED_BREATH_COLOR_GREEN:
                    rgb_display_green(led_step_value[led_breath_step]);
                    break;
                case LED_BREATH_COLOR_PURPLE:
                    rgb_display_purple(led_step_value[led_breath_step]);
                    break;
                case LED_BREATH_COLOR_CYAN:
                    rgb_display_cyan(led_step_value[led_breath_step]);
                    break;
                case LED_BREATH_COLOR_YELLOW:
                    rgb_display_yellow(led_step_value[led_breath_step]);
                    break;
                case LED_BREATH_COLOR_WHITE:
                    rgb_display_white(led_step_value[led_breath_step]);
                    break;
                default:
                    break;
            }
            led_breath_step = (uint8_t)(((led_breath_step + 1)%(sizeof(led_step_value))));
            if(led_breath_step == 0){
                led_breath_color = (uint8_t)(((led_breath_color + 1)%LED_BREATH_COLOR_UNDEFINE));
            }          
            break;
        case LED_MODE_COLORFUL_LOOP:
            if(0 == led_blink_step){
                if(led_blink_onoff){//on
                    switch(led_breath_color){
                        case LED_BREATH_COLOR_RED:
                            rgb_display_red(RGB_LIGHT_ON);
                            break;
                        case LED_BREATH_COLOR_BLUE:
                            rgb_display_blue(RGB_LIGHT_ON);
                            break;
                        case LED_BREATH_COLOR_GREEN:
                            rgb_display_green(RGB_LIGHT_ON);
                            break;
                        case LED_BREATH_COLOR_PURPLE:
                            rgb_display_purple(RGB_LIGHT_ON);
                            break;
                        case LED_BREATH_COLOR_CYAN:
                            rgb_display_cyan(RGB_LIGHT_ON);
                            break;
                        case LED_BREATH_COLOR_YELLOW:
                            rgb_display_yellow(RGB_LIGHT_ON);
                            break;
                        case LED_BREATH_COLOR_WHITE:
                            rgb_display_white(RGB_LIGHT_ON);
                            break;
                        default:
                            break;
                    }   
                }else{//off
                    switch(led_breath_color){
                        case LED_BREATH_COLOR_RED:
                            rgb_display_red(RGB_LIGHT_OFF);
                            break;
                        case LED_BREATH_COLOR_BLUE:
                            rgb_display_blue(RGB_LIGHT_OFF);
                            break;
                        case LED_BREATH_COLOR_GREEN:
                            rgb_display_green(RGB_LIGHT_OFF);
                            break;
                        case LED_BREATH_COLOR_PURPLE:
                            rgb_display_purple(RGB_LIGHT_OFF);
                            break;
                        case LED_BREATH_COLOR_CYAN:
                            rgb_display_cyan(RGB_LIGHT_OFF);
                            break;
                        case LED_BREATH_COLOR_YELLOW:
                            rgb_display_yellow(RGB_LIGHT_OFF);
                            break;
                        case LED_BREATH_COLOR_WHITE:
                            rgb_display_white(RGB_LIGHT_OFF);
                            break;
                        default:
                            break;
                    }
                    led_breath_color = (uint8_t)(((led_breath_color + 1)%LED_BREATH_COLOR_UNDEFINE));                  
                }
                led_blink_onoff = 1 - led_blink_onoff;
            }
            led_blink_step = (uint8_t)(((led_blink_step + 1)%(LED_BLINK_JITTER/LED_FRESH_JITTER)));
            break;
        case LED_MODE_RED_BLINK:
            if(0 == led_blink_step){
                if(led_blink_onoff)
                    rgb_display_red(RGB_LIGHT_ON);
                else
                    rgb_display_red(RGB_LIGHT_OFF);
                led_blink_onoff = 1 - led_blink_onoff;
            }
            led_blink_step = (uint8_t)(((led_blink_step + 1)%(LED_BLINK_JITTER/LED_FRESH_JITTER)));
            break;
        case LED_MODE_GREEN_BLINK:
            if(0 == led_blink_step){
                if(led_blink_onoff)
                    rgb_display_green(RGB_LIGHT_ON);
                else
                    rgb_display_green(RGB_LIGHT_OFF);
                led_blink_onoff = 1 - led_blink_onoff;
            }
            led_blink_step = (uint8_t)(((led_blink_step + 1)%(LED_BLINK_JITTER/LED_FRESH_JITTER)));
            break;
        case LED_MODE_BLUE_BLINK:
            if(0 == led_blink_step){
                if(led_blink_onoff)
                    rgb_display_blue(RGB_LIGHT_ON);
                else
                    rgb_display_blue(RGB_LIGHT_OFF);
                led_blink_onoff = 1 - led_blink_onoff;
            }
            led_blink_step = (uint8_t)(((led_blink_step + 1)%(LED_BLINK_JITTER/LED_FRESH_JITTER)));
            break;
        default:
            break;        
    }
}

static void led_task(void *param)
{
    ledmsg_t msg;

	xSemaphoreTake(task_exit, portMAX_DELAY);
	while(task_need_run){
        if(pdTRUE == xQueueReceive(led_queue, &msg, pdMS_TO_TICKS(LED_FRESH_JITTER))){
            led_work_mode_switch(&msg);
        }else{//timeout
            led_breath_blink_simulate();
        }
	}
	vTaskDelay(1);
    vTaskDelete(NULL);
    xSemaphoreGive(task_exit);
}

static keyprocess_t led_keyprocess(keyevent_t *event)
{
	ledmsg_t msg;
    static bool led_enable = false;
	
	//LOG_DBG("led recevice key event %d type %d\n", event->code, event->type);
	switch(event->code){
        case KEY_CODE_BRIGHTNESS:
            if(event->type == KEY_EVNET_PRESS){
                if(led_enable){
                    led_enable = false;
                    LOG_DBG("disable led tap function.\n");
                    keyevent_mask_notify(APP_UNDEFINE, KEY_CODE_PAT_MASK);
					//acc_disable();
                    if(led_mode_get() != LED_MODE_ALL_OFF){
                        led_mode_set(LED_CLIENT_OTHER, LED_MODE_ALL_OFF, NULL);                 
                    }
                }else{
                    led_enable = true;
                    LOG_DBG("enable led tap function.\n");
                    keyevent_unmask_notify(APP_UNDEFINE, KEY_CODE_PAT_MASK);
					//acc_enable();
                    led_mode_set(LED_CLIENT_OTHER, LED_MODE_NIGHT_HALF, NULL);    
                }
            }
			break;
        case KEY_CODE_PAT:
            if(led_enable){
                if(event->type == KEY_EVNET_PRESS)
                    led_mode_set(LED_CLIENT_OTHER, LED_MODE_AUTO, NULL);
            }
            break;
		default:
			break;
	}
    return KEY_PROCESS_PUBLIC;
}

#ifdef CONFIG_USE_ESP32_LYRAT_BOARD //use esp32 lyrat board
int led_init(void){return 0;}
void led_uninit(void){return;}
void led_mode_set(led_client_app_t app, led_mode_t mode, led_callback cb){return;}
led_mode_t led_mode_get(void){return 0;}
#else //self board
void led_mode_set(led_client_app_t app, led_mode_t mode, led_callback cb)
{
    ledmsg_t msg;

    switch(app){
        case LED_CLIENT_BATTERY:
            msg.client.pri = LED_CLIENT_PRI_URGENT;
            break;
        case LED_CLIENT_WECHAT:
            msg.client.pri = LED_CLIENT_PRI_SYS;
            break;  
        case LED_CLIENT_ALARM:
        case LED_CLIENT_OTHER:
            msg.client.pri = LED_CLIENT_PRI_USR;
            break;
        default:
            LOG_ERR("unknown app type (%d)...\n", app);
            return;        
    }

    msg.client.app = app;
    msg.client.cb = cb;
    msg.mode = mode;
    xQueueSend(led_queue, &msg, portMAX_DELAY);
}

led_mode_t led_mode_get(void)
{
    return led_work_mode;
}

int led_init(void)
{
    LOG_INFO("enter\n");

    kclient = keyevent_register_listener(KEY_CODE_BRIGHTNESS_MASK|KEY_CODE_PAT_MASK, led_keyprocess);
	if(!kclient){
		LOG_ERR("register key client fail\n");
    }
    keyevent_mask_notify(APP_UNDEFINE, KEY_CODE_PAT_MASK);
    led_queue = xQueueCreate(LED_QUEUE_SIZE, sizeof(ledmsg_t));
	task_exit = xSemaphoreCreateMutex();
	task_need_run = true;
	xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);
	LOG_INFO("exit\n");
	return 0;
}

void led_uninit(void)
{
	ledmsg_t msg;

	LOG_INFO("called\n");
	task_need_run = false;
    led_mode_set(LED_CLIENT_OTHER, LED_MODE_ALL_OFF, NULL);
    xSemaphoreTake(task_exit, portMAX_DELAY);
    keyevent_unregister_listener(kclient);
	vSemaphoreDelete(task_exit);
	vQueueDelete(led_queue);
	return;
}
#endif
/*
0. 使用灯效按键打开
1. 白灯半亮   --- 普通灯效
2. 白灯全亮   --- 普通灯效
3. 红灯全亮   --- 普通灯效
4. 蓝灯全亮   --- 普通灯效
5. 绿灯全亮   --- 普通灯效
6. 紫色全亮   --- 普通灯效
7. 青色全亮   --- 普通灯效
8. 七彩色循环 --- 呼吸灯效
9. 七彩亮灭   --- 呼吸灯效
10. 关闭灯效

a.1~10 切换为拍打，拍打变换顺序也是1~10
b.同时10也支持灯效按键关闭
以上为拍打灯效



其他使用场景（非拍打）
a.低电量红灯闪烁 ，此时优先级最高，可以屏蔽拍打变化
b.只要是闹铃响就出第9种灯效，闹钟推送内容播放完毕，灯效自动关闭，也可以拍打关闭
c.收到微聊的时候绿灯闪，此时屏蔽拍打灯效，知道按了微聊键之后播完完毕，关闭绿灯闪烁，也可拍打播放
以上为闪烁灯效
*/
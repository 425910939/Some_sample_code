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
#include "log.h"
#include "poll.h"
#include "led.h"
#include "driver/adc.h"
#include "audiomanagerservice.h"
#include "esp_system.h"

#define LOG_TAG		"bat"
#define EMPIRICAL_VALUE (250)

static uint32_t s_voltage_value = 4000;

uint32_t get_voltage_value()
{
    return s_voltage_value;
}

static int lowbattery_count = 5;//2-min

void period_get_voltage_cb(void *param)
{
	play_param_t play_param;
    static uint8_t count = 0;

    s_voltage_value = adc1_get_voltage(ADC1_CHANNEL_3) + EMPIRICAL_VALUE;

    if(s_voltage_value < 3600 && lowbattery_count > 0 && s_voltage_value > 3500){
        lowbattery_count--;
        count = 0;
        if(0 == lowbattery_count){
			#if 0
			play_param.play_app_type = AUDIO_APP_NOTIFY;
			play_param.is_local_file = true;
			play_param.uri = NOTIFY_AUDIO_LOWBATTERY;
			play_param.tone = NULL;
			play_param.cb = NULL;
			play_param.cb_param = NULL;
			play_start(&play_param);
			#endif
            led_mode_set(LED_CLIENT_BATTERY, LED_MODE_RED_BLINK, NULL);
        }
        LOG_INFO(" 3600 ~~ 3500 lowbattery %d, count %d\n",lowbattery_count,count);
    }else if(s_voltage_value > 3700){
        count = 0;
        lowbattery_count = 5;
        if(led_mode_get() == LED_MODE_RED_BLINK)
            led_mode_set(LED_CLIENT_BATTERY, LED_MODE_ALL_OFF, NULL);

        LOG_INFO(" > 3700 lowbattery %d, count %d\n",lowbattery_count,count);
    }else if(s_voltage_value < 3500){
        count++;
        if(count > 3){
            esp_restart();
        }
        LOG_INFO(" < 3500 lowbattery %d, count %d\n",lowbattery_count,count);
    }else{
        //nothing to do
        LOG_INFO(" s_voltage_value %d lowbattery %d, count %d\n",s_voltage_value,lowbattery_count,count);
    }

    LOG_DBG("show voltag_vaule %d\n",s_voltage_value);
	ESP_LOGI("ONEGO2","internal (%d)  spiram (%d)", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), 
		heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

poll_func_config_t voltage_config = {PCLOCK_SOURCE_RTC,200,period_get_voltage_cb,NULL};

void get_voltage_init()
{
    uint8_t count = 0;
    adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_0db);
    adc1_config_width(ADC_WIDTH_12Bit);
    
    while(1){
        vTaskDelay(200);
        s_voltage_value = adc1_get_voltage(ADC1_CHANNEL_3) + EMPIRICAL_VALUE;
        s_voltage_value > 3550 ? (count++) : (count = 0) ;
        LOG_INFO("startup curren voltage %d,count %d\n",s_voltage_value,count);
        if(count > 2)
            break;
    }

    register_poll_func(&voltage_config);
}



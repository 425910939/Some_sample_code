#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "log.h"
#include "keyevent.h"
#include "keypad.h"
#include "boardconfig.h"
#include "extern_gpio.h"
#include "touchpad.h"

#define LOG_TAG	    "keypad"

#ifdef CONFIG_USE_ESP32_LYRAT_BOARD //use esp32 lyrat board

#define GPIO_BOOT			 GPIO_NUM_2

#define MAX_NAME_SIZE				(12)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define	BUTTON_ACTIVE_LEVEL_LOW    		(0)
#define	BUTTON_ACTIVE_LEVEL_HIGH		(1)

#define TOUCHPAD_THRESHOLD                      400
#define TOUCHPAD_FILTER_VALUE                   150

typedef struct button{
	char name[MAX_NAME_SIZE];
	unsigned char gpio_num;
	int active_level;
	unsigned char jitter_threshold;
	uint32_t report_code;
	keypress_t press_state;
	TimerHandle_t xtimer;
}button_t;

static button_t keypad[] = {
	{ 
		.name = "record",
		.gpio_num = GPIO_REC,
		.active_level = BUTTON_ACTIVE_LEVEL_LOW,
		.jitter_threshold = 25,
		.report_code = KEY_CODE_ENGLISH,
		.press_state = KEY_ACTION_RELEASE,
	},
	{
		.name = "mode",
		.gpio_num = GPIO_MODE,
		.active_level = BUTTON_ACTIVE_LEVEL_LOW,
		.jitter_threshold = 25,
		.report_code = KEY_CODE_HABIT,
		.press_state = KEY_ACTION_RELEASE,
	},
};

static void keypad_thalf_handler(void* arg)
{
    button_t* btn = (button_t*) arg;
    portBASE_TYPE HPTaskAwoken = pdFALSE;
 
	if (btn->xtimer) {
		xTimerStopFromISR(btn->xtimer, &HPTaskAwoken);
		xTimerResetFromISR(btn->xtimer, &HPTaskAwoken);
	}
}

static void keypad_bhalf_handler(TimerHandle_t tmr)
{ 
    button_t* btn = (button_t*) pvTimerGetTimerID(tmr); 
    keymsg_t msg; 
    
    if(!btn)
    { 
        LOG_ERR("NULL ptr in keypad bhalf\n"); 
    }
    else
    { 
        msg.code = btn->report_code; 
        if(gpio_get_level(btn->gpio_num) == btn->active_level){//press
			if(btn->press_state != KEY_ACTION_PRESS){
				btn->press_state = KEY_ACTION_PRESS;
				msg.press = KEY_ACTION_PRESS; 
				keyaction_notfiy(&msg); 
			}else{
				//ignore
			}
        }else{//release
			if(btn->press_state != KEY_ACTION_RELEASE){
				btn->press_state = KEY_ACTION_RELEASE;
				msg.press = KEY_ACTION_RELEASE; 
				keyaction_notfiy(&msg); 
			}else{
				//ignore
			}
        } 
    } 

    return; 
}

int keypad_init(void)
{
	int i = 0;
	gpio_config_t gpio_conf;
	
	for(i = 0; i < ARRAY_SIZE(keypad); i++){
		keypad[i].xtimer = xTimerCreate(keypad[i].name, pdMS_TO_TICKS(keypad[i].jitter_threshold), pdFALSE, &keypad[i], keypad_bhalf_handler);
		if(!keypad[i].xtimer) {
			LOG_ERR("creat key[%d]-%s xtimer fail\n", i , keypad[i].name);
			//unregister isr handler
			continue;
		}
		gpio_install_isr_service(0);
		gpio_conf.intr_type = GPIO_INTR_ANYEDGE;
		gpio_conf.mode = GPIO_MODE_INPUT;
		gpio_conf.pin_bit_mask = (uint64_t)(((uint64_t)1)<<keypad[i].gpio_num);
		gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
		gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
		gpio_config(&gpio_conf);
		gpio_isr_handler_add(keypad[i].gpio_num, keypad_thalf_handler, &keypad[i]);		
	}

	touchpad_create(TOUCH_PAD_NUM4, TOUCHPAD_THRESHOLD, TOUCHPAD_FILTER_VALUE, TOUCHPAD_SINGLE_TRIGGER, 2);
    touchpad_create(TOUCH_PAD_NUM7, TOUCHPAD_THRESHOLD, TOUCHPAD_FILTER_VALUE, TOUCHPAD_SINGLE_TRIGGER, 2);
    touchpad_create(TOUCH_PAD_NUM8, TOUCHPAD_THRESHOLD, TOUCHPAD_FILTER_VALUE, TOUCHPAD_SINGLE_TRIGGER, 2);
    touchpad_create(TOUCH_PAD_NUM9, TOUCHPAD_THRESHOLD, TOUCHPAD_FILTER_VALUE, TOUCHPAD_SINGLE_TRIGGER, 2);

    return 0;
}

int keypad_uninit(void)
{
	return 0;
}

#else //use self board

#if 0
#define EXT_GPIO_INT_PIN		(38)
#define ISR_JITTER_THRESHOLD	(200)
#define COL_NUM					(3)
#define ROW_NUM					(4)

static TimerHandle_t xtimer;

static const uint32_t  key_code_array[COL_NUM][ROW_NUM] = {
	{KEY_CODE_VOICE_RECOGNIZE, KEY_CODE_BRIGHTNESS, KEY_CODE_VOL_UP, KEY_CODE_VOL_DOWN},
	{KEY_CODE_PLAY_NEXT, KEY_CODE_ALARM, KEY_CODE_HABIT, KEY_CODE_UNDEFINE},
	{KEY_CODE_PLAY_PREV, KEY_CODE_WECHAT, KEY_CODE_ENGLISH, KEY_CODE_UNDEFINE},
};

static keypress_t key_state[COL_NUM][ROW_NUM] = {
	{KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE},
	{KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE},
	{KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE},
};

static void keypad_thalf_handler(void* arg)
{
	portBASE_TYPE HPTaskAwoken = pdFALSE;

	if (xtimer) {
		xTimerStopFromISR(xtimer, &HPTaskAwoken);
		xTimerResetFromISR(xtimer, &HPTaskAwoken);
	}
	gpio_intr_disable(EXT_GPIO_INT_PIN);
}

static inline int keypad_row_detect(int col_num)
{
	ext_gpio_num_t num = (ext_gpio_num_t)col_num;
	ext_gpio_in_t val;
	int row_num = -1;

	ext_gpio_output(AW9523B_P1_0, EXT_GPIO_OUT_HIGH);
	if((0 == ext_gpio_input(num, &val)) && (val == EXT_GPIO_IN_HIGH)){
		row_num = 0;
		goto out_0;
	}

	ext_gpio_output(AW9523B_P1_1, EXT_GPIO_OUT_HIGH);
	if((0 == ext_gpio_input(num, &val)) && (val == EXT_GPIO_IN_HIGH)){
		row_num = 1;
		goto out_1;
	}

	ext_gpio_output(AW9523B_P1_2, EXT_GPIO_OUT_HIGH);
	if((0 == ext_gpio_input(num, &val)) && (val == EXT_GPIO_IN_HIGH)){
		row_num = 2;
		goto out_2;
	}

	ext_gpio_output(AW9523B_P1_3, EXT_GPIO_OUT_HIGH);
	if((0 == ext_gpio_input(num, &val)) && (val == EXT_GPIO_IN_HIGH)){
		row_num = 3;
	}


	ext_gpio_output(AW9523B_P1_3, EXT_GPIO_OUT_LOW);
out_2:
	ext_gpio_output(AW9523B_P1_2, EXT_GPIO_OUT_LOW);
out_1:
	ext_gpio_output(AW9523B_P1_1, EXT_GPIO_OUT_LOW);
out_0:
	ext_gpio_output(AW9523B_P1_0, EXT_GPIO_OUT_LOW);

	return row_num;
}

static int report_keycode(int col_num, int row_num, keypress_t press)
{
	int col, row;
	keymsg_t msg;

	if((col_num < 0) || (col_num > COL_NUM) || (row_num < 0) || (row_num > ROW_NUM)){
		LOG_ERR("invaild parameter col (%d) row(%d)\n", col_num, row_num);
		return -ESP_ERR_INVALID_ARG;
	}
	//LOG_DBG("report col (%d) row(%d) press (%d)\n", col_num, row_num, press);
	if((col_num == COL_NUM) && (row_num == ROW_NUM)){//all release
		for(col=0; col<COL_NUM; col++){
			for(row=0; row<ROW_NUM; row++){
				if(key_state[col][row] != KEY_ACTION_RELEASE){
					key_state[col][row] = KEY_ACTION_RELEASE;
					msg.press = KEY_ACTION_RELEASE;
					msg.code = key_code_array[col][row];
					keyaction_notfiy(&msg);
				}
			}
		}
	}else if((col_num < COL_NUM) && (row_num < ROW_NUM)){//report one-key press or release
		if(key_state[col_num][row_num] != press){
			key_state[col_num][row_num] = press;
			msg.press = press;
			msg.code = key_code_array[col_num][row_num];
			keyaction_notfiy(&msg);
		}
	}else if(row_num == ROW_NUM){//report one-col-key release
		for(row=0; row<ROW_NUM; row++){
			if(key_state[col_num][row] != KEY_ACTION_RELEASE){
				key_state[col_num][row] = KEY_ACTION_RELEASE;
				msg.press = KEY_ACTION_RELEASE;
				msg.code = key_code_array[col_num][row];
				keyaction_notfiy(&msg);
			}
		}
	}else{
		LOG_DBG("col equal COL MAX ignore, col (%d) row(%d)\n", col_num, row_num);
	}

	return 0;
}

static void keypad_bhalf_handler(TimerHandle_t tmr)
{ 
	keymsg_t msg;
	uint8_t val;
	static int col_num = 1;
	static int row_num = 3;//use undefine keycode init
	
	//LOG_DBG("keypad isr detected\n");

	led_display_disable();
	ext_gpio_isr_disable();

	val = ext_gpio_isr_clear();
	LOG_INFO("keypad isr value (0x%x)\n", val);

	if(val == 0x07){//release all key
		report_keycode(COL_NUM, ROW_NUM, KEY_ACTION_RELEASE);
	}else{
		col_num = 0;
		if(0 == (val & 0x01)){
			row_num = keypad_row_detect(col_num);
			//LOG_DBG("keypad detect col %d, row %d\n", col_num, row_num);
			if(row_num >= 0){
				report_keycode(col_num, row_num, KEY_ACTION_PRESS);
			}
		}else{
			report_keycode(col_num, ROW_NUM, KEY_ACTION_RELEASE);
		}

		col_num = 1;
		if(0 == (val & 0x02)){
			row_num = keypad_row_detect(col_num);
			//LOG_DBG("keypad detect col %d, row %d\n", col_num, row_num);
			if(row_num >= 0){
				report_keycode(col_num, row_num, KEY_ACTION_PRESS);
			}
		}else{
			report_keycode(col_num, ROW_NUM, KEY_ACTION_RELEASE);
		}

		col_num = 2;
		if(0 == (val & 0x04)){
			row_num = keypad_row_detect(col_num);
			//LOG_DBG("keypad detect col %d, row %d\n", col_num, row_num);
			if(row_num >= 0){
				report_keycode(col_num, row_num, KEY_ACTION_PRESS);
			}
		}else{
			report_keycode(col_num, ROW_NUM, KEY_ACTION_RELEASE);
		}
	}

	ext_gpio_isr_enable();
	led_display_enable();
	gpio_intr_enable(EXT_GPIO_INT_PIN);
    return; 
}
#endif
int keypad_init(void)
{
#if 0
	int i = 0;
	gpio_config_t gpio_conf;

	xtimer = xTimerCreate("aw9523_isr", pdMS_TO_TICKS(ISR_JITTER_THRESHOLD), pdFALSE, NULL, keypad_bhalf_handler);
	if(xtimer) {
		//config keypad matrix
		ext_gpio_output(AW9523B_P1_0, EXT_GPIO_OUT_LOW);
		ext_gpio_output(AW9523B_P1_1, EXT_GPIO_OUT_LOW);
		ext_gpio_output(AW9523B_P1_2, EXT_GPIO_OUT_LOW);
		ext_gpio_output(AW9523B_P1_3, EXT_GPIO_OUT_LOW);
		ext_gpio_isr_clear();

		gpio_install_isr_service(0);
		gpio_conf.intr_type = GPIO_INTR_LOW_LEVEL;
		gpio_conf.mode = GPIO_MODE_INPUT;
		gpio_conf.pin_bit_mask = (uint64_t)(((uint64_t)1)<<EXT_GPIO_INT_PIN);
		gpio_conf.pull_down_en = 0;
		gpio_conf.pull_up_en = 0;
		gpio_config(&gpio_conf);
		gpio_isr_handler_add(EXT_GPIO_INT_PIN, keypad_thalf_handler, NULL);
	}
	else{
		LOG_ERR("creat aw9523_isr xtimer fail\n");
		//unregister isr handler
	}
#endif
    return 0;
}

int keypad_uninit(void)
{
	return 0;
}
#endif

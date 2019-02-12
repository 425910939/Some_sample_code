#include <stdio.h>
#include <stdlib.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "log.h"
#include "tm1628.h"
#include "keyevent.h"
#include "boardconfig.h"
#include "restore.h"

#define LOG_TAG	    "tm1628"

#define TM1628_STB    32
#define TM1628_CLK    33
#define TM1628_DIO    27
#define TM1628_KEY_REG_MAX			(5)
#define TM1628_DEFAULT_BRIGHTNESS	(0x8F)
#define TM1628_PINS  ((uint64_t)(((uint64_t)1)<<TM1628_STB) | (uint64_t)(((uint64_t)1)<<TM1628_CLK))

#define COL_NUM					(2)
#define ROW_NUM					(5)
#define KS1_K1_BIT(val)			(0x01 & val)	
#define KS1_K2_BIT(val)			(0x02 & val)	
#define KS2_K1_BIT(val)			(0x08 & val)	
#define KS2_K2_BIT(val)			(0x10 & val)
#define KS3_K1_BIT(val)			KS1_K1_BIT(val)		
#define KS3_K2_BIT(val)			KS1_K2_BIT(val)		
#define KS4_K1_BIT(val)			KS2_K1_BIT(val)		
#define KS4_K2_BIT(val)			KS2_K2_BIT(val)		
#define KS5_K1_BIT(val)			KS1_K1_BIT(val)			
#define KS5_K2_BIT(val)			KS1_K2_BIT(val)			

typedef enum{
    TM1628_GRID_1 = 0,
    TM1628_GRID_2 = 1,
    TM1628_GRID_3 = 2,
    TM1628_GRID_4 = 3,
    TM1628_GRID_5 = 4,
    TM1628_GRID_6 = 5,
    TM1628_GRID_7 = 6,
    TM1628_GRID_MAX = 7,
}tm1628_grid_t;

typedef struct{
    uint8_t seg1 : 1; 
    uint8_t seg2 : 1;
    uint8_t seg3 : 1;
    uint8_t seg4 : 1;
    uint8_t seg5 : 1;
    uint8_t seg6 : 1;
    uint8_t seg7 : 1;
    uint8_t seg8 : 1;
}tm1628_seg_low_t;

typedef struct{
    uint8_t seg9 : 1; 
    uint8_t seg10 : 1;
    uint8_t segx : 1;
    uint8_t seg12 : 1;
    uint8_t seg13 : 1;
    uint8_t seg14 : 1;
    uint8_t segy : 1;
    uint8_t segz : 1;
}tm1628_seg_high_t;

typedef struct{
    tm1628_seg_low_t seg_low;
    tm1628_seg_high_t seg_high;
}tm1628_seg_t;

const uint8_t leddata[]={ 
	0x3F,  //"0"
	0x18,  //"1"
	0x6D,  //"2"
	0x7C,  //"3"
	0x5A,  //"4"
	0x76,  //"5"
	0x77,  //"6"
	0x1C,  //"7"
	0x7F,  //"8"
	0x7E,  //"9"
};

static const uint32_t  key_code_array[COL_NUM][ROW_NUM] = {
	{KEY_CODE_VOICE_RECOGNIZE, KEY_CODE_PLAY_NEXT, KEY_CODE_PLAY_PREV, KEY_CODE_VOL_DOWN, KEY_CODE_ENGLISH},//K1
	{KEY_CODE_BRIGHTNESS, KEY_CODE_ALARM, KEY_CODE_WECHAT, KEY_CODE_HABIT, KEY_CODE_VOL_UP},//K2
};
static keypress_t key_state[COL_NUM][ROW_NUM] = {
	{KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE},//K1
	{KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE, KEY_ACTION_RELEASE},//K2
};
static tm1628_seg_t tm1628_display_reg[TM1628_GRID_MAX];//GRID 1-7, SEG 1 - 14
static uint8_t tm1628_key_reg[TM1628_KEY_REG_MAX] = {0};

static inline void tm1628_dio_input(void)
{
    gpio_set_direction(TM1628_DIO, GPIO_MODE_INPUT);
}

static inline void tm1628_dio_output(void)
{
    gpio_set_direction(TM1628_DIO, GPIO_MODE_OUTPUT);
}

static int tm1628_send_command(uint8_t cmd)
{
    uint8_t i;

    tm1628_dio_output();
    gpio_set_level(TM1628_STB, 0);
    for(i=0; i<8; i++)
    {
        gpio_set_level(TM1628_CLK, 0);
        ets_delay_us(1);
        if(cmd&0x01)
            gpio_set_level(TM1628_DIO, 1);
        else
            gpio_set_level(TM1628_DIO, 0);
        gpio_set_level(TM1628_CLK, 1);
        ets_delay_us(1);
        cmd >>= 1;
    }
    gpio_set_level(TM1628_STB, 1);
    return 0;
}

static int tm1628_write_multi_data(uint8_t cmd, uint8_t* data, uint8_t data_size)//send display data
{
    uint8_t i, j;

    tm1628_dio_output();
    gpio_set_level(TM1628_STB, 0);
    for(i=0; i<8; i++)
    {
        gpio_set_level(TM1628_CLK, 0);
        ets_delay_us(1);
        if(cmd&0x01)
            gpio_set_level(TM1628_DIO, 1);
        else
            gpio_set_level(TM1628_DIO, 0);
        gpio_set_level(TM1628_CLK, 1);
        ets_delay_us(1);
        cmd >>= 1;
    }

    for(j=0; j<data_size; j++){
        for(i=0; i<8; i++)
        {
            gpio_set_level(TM1628_CLK, 0);
            ets_delay_us(1);
            if(data[j]&0x01)
                gpio_set_level(TM1628_DIO, 1);
            else
                gpio_set_level(TM1628_DIO, 0);
            gpio_set_level(TM1628_CLK, 1);
            ets_delay_us(1);
            data[j] >>= 1;
        }
    }
    gpio_set_level(TM1628_STB, 1);
    return 0;
}

static int tm1628_read_multi_data(uint8_t cmd, uint8_t* data, uint8_t data_size)//read keypad data
{
    uint8_t i, j, mask;

    tm1628_dio_output();
    gpio_set_level(TM1628_STB, 0);
    for(i=0; i<8; i++)
    {
        gpio_set_level(TM1628_CLK, 0);
        ets_delay_us(1);
        if(cmd&0x01)
            gpio_set_level(TM1628_DIO, 1);
        else
            gpio_set_level(TM1628_DIO, 0);
        gpio_set_level(TM1628_CLK, 1);
        ets_delay_us(1);
        cmd >>= 1;
    }

    tm1628_dio_input();
    for(j=0; j<data_size; j++){
        mask = 0x01;
        for(i=0; i<8; i++)
        {
            gpio_set_level(TM1628_CLK, 0);
            ets_delay_us(1);
            gpio_set_level(TM1628_CLK, 1);
            ets_delay_us(1);
            if(gpio_get_level(TM1628_DIO))
                data[j] |= mask;
            else
                data[j] &= (~mask);
            mask = mask<<1;
        }
    }
    gpio_set_level(TM1628_STB, 1);
    return 0;
}

static inline void tm1628_update_time(uint8_t hours, uint8_t minutes)
{    
    uint8_t grid_index;

    for(grid_index=0; grid_index<TM1628_GRID_MAX; grid_index++){
        if(leddata[hours/10]&(1<<grid_index))//SEG-6
            tm1628_display_reg[grid_index].seg_low.seg6 = 1;
        else
            tm1628_display_reg[grid_index].seg_low.seg6 = 0;
        if(leddata[hours%10]&(1<<grid_index))//SEG-7
            tm1628_display_reg[grid_index].seg_low.seg7 = 1;
        else
            tm1628_display_reg[grid_index].seg_low.seg7 = 0;
        if(leddata[minutes/10]&(1<<grid_index))//SEG-8
            tm1628_display_reg[grid_index].seg_low.seg8 = 1;
        else
            tm1628_display_reg[grid_index].seg_low.seg8 = 0;
        if(leddata[minutes%10]&(1<<grid_index))//SEG-9
            tm1628_display_reg[grid_index].seg_high.seg9 = 1;
        else
            tm1628_display_reg[grid_index].seg_high.seg9 = 0;	
    } 
    return;
}

static inline void tm1628_update_colon(bool colon_on)
{
    if(colon_on){
        tm1628_display_reg[TM1628_GRID_1].seg_high.seg10 = 1;
        tm1628_display_reg[TM1628_GRID_2].seg_high.seg10 = 1;
    }else{
        tm1628_display_reg[TM1628_GRID_1].seg_high.seg10 = 0;
        tm1628_display_reg[TM1628_GRID_2].seg_high.seg10 = 0;
    }
    return;
}

static inline int report_keycode(int col_num, int row_num, keypress_t press)
{
	keymsg_t msg;

	if((col_num < 0) || (col_num > COL_NUM) || (row_num < 0) || (row_num > ROW_NUM)){
		LOG_ERR("invaild parameter col (%d) row(%d)\n", col_num, row_num);
		return -ESP_ERR_INVALID_ARG;
	}

	if(key_state[col_num][row_num] != press){
		key_state[col_num][row_num] = press;
		msg.press = press;
		msg.code = key_code_array[col_num][row_num];
		keyaction_notfiy(&msg);
	}

	return 0;
}

static void check_reset_status(){
    keymsg_t msg = {KEY_CODE_RESET};
    static int before_status = KEY_ACTION_RELEASE;
	msg.press = (gpio_get_level(GPIO_RESET) == 1) ? KEY_ACTION_RELEASE : KEY_ACTION_PRESS;
    if(before_status == msg.press)
        return ;
    before_status = msg.press;
    keyaction_notfiy(&msg);
}

#ifdef CONFIG_USE_ESP32_LYRAT_BOARD //use esp32 lyrat board
int tm1628_tick_process(display_buff_t *buff){return 0;}
int tm1628_init(void){return 0;}
#else
int tm1628_tick_process(display_buff_t *buff)
{
    static bool colon_on = true;
	static uint8_t count = 0;
    static uint8_t hour = 0;
    static uint8_t minute = 0;
    bool need_update = false;

	if(buff){
        hour = buff->buff.rect_time.hour;
        minute = buff->buff.rect_time.minute;
        need_update = true;
        LOG_DBG("tm1628 display time (%d:%d)\n", hour, minute);
	}

	if(count++ == (DISPLAY_COLON_THRESHOLD/DISPLAY_JITTER)){	
		colon_on = !colon_on;
		count = 0;
        need_update = true;
	}

    if(get_factory_mode() == true){
        need_update = false;
    }

    if(need_update){
        tm1628_update_time(hour, minute);
        tm1628_update_colon(colon_on);
        tm1628_write_multi_data(0xC0, (uint8_t *)tm1628_display_reg, sizeof(tm1628_display_reg));	
    }
    memset(tm1628_key_reg, 0, sizeof(tm1628_key_reg));
	tm1628_read_multi_data(0x42, tm1628_key_reg, sizeof(tm1628_key_reg));

	if(KS1_K1_BIT(tm1628_key_reg[0]))
		report_keycode(0, 0, KEY_ACTION_PRESS);
	else
		report_keycode(0, 0, KEY_ACTION_RELEASE);
	if(KS2_K1_BIT(tm1628_key_reg[0]))
		report_keycode(0, 1, KEY_ACTION_PRESS);
	else
		report_keycode(0, 1, KEY_ACTION_RELEASE);
	if(KS1_K2_BIT(tm1628_key_reg[0]))
		report_keycode(1, 0, KEY_ACTION_PRESS);
	else
		report_keycode(1, 0, KEY_ACTION_RELEASE);
	if(KS2_K2_BIT(tm1628_key_reg[0]))
		report_keycode(1, 1, KEY_ACTION_PRESS);
	else
		report_keycode(1, 1, KEY_ACTION_RELEASE);

	if(KS3_K1_BIT(tm1628_key_reg[1]))
		report_keycode(0, 2, KEY_ACTION_PRESS);
	else
		report_keycode(0, 2, KEY_ACTION_RELEASE);		
	if(KS3_K2_BIT(tm1628_key_reg[1]))
		report_keycode(1, 2, KEY_ACTION_PRESS);
	else
		report_keycode(1, 2, KEY_ACTION_RELEASE);	
	if(KS4_K1_BIT(tm1628_key_reg[1]))
		report_keycode(0, 3, KEY_ACTION_PRESS);
	else
		report_keycode(0, 3, KEY_ACTION_RELEASE);	
	if(KS4_K2_BIT(tm1628_key_reg[1]))
		report_keycode(1, 3, KEY_ACTION_PRESS);
	else
		report_keycode(1, 3, KEY_ACTION_RELEASE);
	
	if(KS5_K1_BIT(tm1628_key_reg[2]))
		report_keycode(0, 4, KEY_ACTION_PRESS);
	else
		report_keycode(0, 4, KEY_ACTION_RELEASE);		
	if(KS5_K2_BIT(tm1628_key_reg[2]))
		report_keycode(1, 4, KEY_ACTION_PRESS);
	else
		report_keycode(1, 4, KEY_ACTION_RELEASE);	
    check_reset_status();
	return 0;
}

#ifdef CONFIG_ENABLE_POWER_MANAGER
int tm1628_set_brightness(display_buff_t *buff)
{
	LOG_DBG("tm1628 set brightness 0x%x\n",buff->buff.rect_brightness.bright_level);
	tm1628_send_command(buff->buff.rect_brightness.bright_level);
	return 0;
}
#endif
static void init_reset_key(){
    gpio_config_t io_conf;

    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (uint64_t)((uint64_t)1<<GPIO_RESET);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
}

int tm1628_init(void)
{
    gpio_config_t io_conf;

    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = TM1628_PINS;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
    io_conf.pin_bit_mask = (uint64_t)(((uint64_t)1)<<TM1628_DIO);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    memset(tm1628_display_reg, 0, sizeof(tm1628_display_reg));

    init_reset_key();
 
    //init time display 00:00
    if(get_factory_mode() == true){
        tm1628_update_time(88, 88);
        tm1628_update_colon(true);
    }
    /*
    if(get_factory_mode() == true){
        tm1628_update_time(88, 88);
    }else{
        tm1628_update_time(0, 0);
    }

    tm1628_update_colon(true);*/
	tm1628_send_command(0x03);//set display mode，7 bit 10 segment 
	tm1628_send_command(0x40);//set address auto increase
	tm1628_write_multi_data(0xC0, (uint8_t *)tm1628_display_reg, sizeof(tm1628_display_reg));
	tm1628_send_command(TM1628_DEFAULT_BRIGHTNESS);//turn off display

    return 0;
}
#endif
#ifndef __BOARD_CONFIG_H__
#define __BOARD_CONFIG_H__


#ifdef CONFIG_WROAR_BOARD
#define IIS_SCLK 5
#define IIS_DSIN 26

/* LED related */
#define LED_INDICATOR_SYS    19  //red
#define LED_INDICATOR_NET    22  //green
#define GPIO_SEL_LED_RED         GPIO_SEL_19  //red
#define GPIO_SEL_LED_GREEN       GPIO_SEL_22  //green

/**/
#define GPIO_PA_EN           GPIO_NUM_21
#define GPIO_SEL_PA_EN       GPIO_SEL_21

#define IIC_CLK 23
#define IIC_DATA 18
#else
#define IIC_CLK 19
#define IIC_DATA 21

#define IIS_SCLK 26
#define IIS_DSIN 22

#define LED_INDICATOR_SYS    18  //red
#define LED_INDICATOR_NET    5  //green

#define GPIO_SEL_LED_RED         GPIO_SEL_18  //red
#define GPIO_SEL_LED_GREEN       GPIO_SEL_5  //green
#endif

/* Press button related */
#define GPIO_RESET           GPIO_NUM_34
#define GPIO_SEL_REC         GPIO_SEL_36    //SENSOR_VP
#define GPIO_SEL_MODE        GPIO_SEL_39    //SENSOR_VN
#define GPIO_REC             GPIO_NUM_36
#define GPIO_MODE            GPIO_NUM_39

/* AUX-IN related */
#define GPIO_SEL_AUX         GPIO_SEL_12    //Aux in
#define GPIO_AUX             GPIO_NUM_12

/* SD card relateed */
#define SD_CARD_INTR_GPIO           GPIO_NUM_34
#define SD_CARD_INTR_SEL            GPIO_SEL_34

/* Flash playlist related */
#define FLASH_PLAYLIST_BASE_ADDR    0x3c0000     //XXX: make sure it does not corrupt flash tone
#define FLASH_PLAYLIST_SIZE         0x1000     //0x1000 = 4K

#endif /* __BOARD_CONFIG_H__ */

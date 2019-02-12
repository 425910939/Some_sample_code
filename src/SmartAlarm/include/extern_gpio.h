#ifndef __EXTERN_GPIO_H__
#define __EXTERN_GPIO_H__

#define RGB_LIGHT_ON        (0xFF)
#define RGB_LIGHT_OFF       (0x00)
#define NIGHT_LIGHT_ON      (2)
#define NIGHT_LIGHT_HALF    (1)
#define NIGHT_LIGHT_OFF     (0)

typedef enum{
    EXT_GPIO_DIR_IN,
    EXT_GPIO_DIR_OUT,
}ext_gpio_dir_t;

typedef enum{
    EXT_GPIO_PULL_UP,
    EXT_GPIO_PULL_DOWN,
    EXT_GPIO_HIGH_Z,
}ext_gpio_pushpull_t;

typedef enum{
    EXT_GPIO_OUT_LOW,
    EXT_GPIO_OUT_HIGH,
}ext_gpio_out_t;

typedef enum{
    EXT_GPIO_IN_LOW,
    EXT_GPIO_IN_HIGH,
}ext_gpio_in_t;

typedef enum{
    EXT_GPIO_CHIP_AW9523B,
    EXT_GPIO_CHIP_AW9110B,
}ext_gpio_chip_t;

typedef enum{
    AW9523B_P0_0 = 0,
    AW9523B_P0_1,
    AW9523B_P0_2,
    AW9523B_P0_3,
    AW9523B_P0_4,
    AW9523B_P0_5,
    AW9523B_P0_6,
    AW9523B_P0_7,
    AW9523B_P1_0,
    AW9523B_P1_1,
    AW9523B_P1_2,
    AW9523B_P1_3,
    AW9523B_P1_4,
    AW9523B_P1_5,
    AW9523B_P1_6,
    AW9523B_P1_7,
    AW9110B_0,
    AW9110B_1,
    AW9110B_2,
    AW9110B_3,
    AW9110B_4,
    AW9110B_5,
    AW9110B_6,
    AW9110B_7,
}ext_gpio_num_t;

esp_err_t i2c_write_reg(uint8_t slave_addr, uint8_t reg_addr, uint8_t* data_wr, size_t size);
esp_err_t i2c_read_reg(uint8_t slave_addr, uint8_t reg_addr, uint8_t* data_rd, size_t size);

int ext_gpio_output(ext_gpio_num_t num, ext_gpio_out_t val);
int ext_gpio_input(ext_gpio_num_t num, ext_gpio_in_t *val);
uint8_t ext_gpio_isr_clear(void);
void ext_gpio_isr_enable(void);
void ext_gpio_isr_disable(void);

int rgb_display_red(uint8_t val);
int rgb_display_green(uint8_t val);
int rgb_display_blue(uint8_t val);
int rgb_display_cyan(uint8_t val);
int rgb_display_purple(uint8_t val);
int rgb_display_yellow(uint8_t val);
int rgb_display_white(uint8_t val);
int rgb_display_reset(void);
int night_led_onoff(uint8_t val);

int ext_gpio_init(void);
#endif
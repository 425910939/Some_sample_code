#include <stdio.h>
#include <stdlib.h>
#include "driver/i2c.h"
#include "esp_system.h"
#include "log.h"
#include "extern_gpio.h"

#define LOG_TAG	    "egpio"

#define ACK_VAL    0x0         /*!< I2C ack value */
#define NACK_VAL   0x1         /*!< I2C nack value */

#define GPIO_NIGHT_LED_CS_1 (19)
#define GPIO_NIGHT_LED_CS_2 (22)
#define GPIO_OUTPUT_PIN_SEL  ((uint64_t)(((uint64_t)1)<<GPIO_NIGHT_LED_CS_1) | (uint64_t)(((uint64_t)1)<<GPIO_NIGHT_LED_CS_2))

//AW9523B chip register map begin
#define AW9523B_ADDR        0x5A
#define INPUT_PORT0         0x00 //r
#define INPUT_PORT1         0x01 //r
#define OUTPUT_PORT0        0x02 //rw
#define OUTPUT_PORT1        0x03 //rw
#define CONFIG_PORT0        0x04 //rw
#define CONFIG_PORT1        0x05 //rw
#define INT_PORT0           0x06 //rw
#define INT_PORT1           0x07 //rw
#define ID_REG              0x10 //r
#define CTRL_REG            0x11 //rw
#define LED_MODE_SWITCH_0   0x12//rw
#define LED_MODE_SWITCH_1   0x13//rw
#define LED_DIM_P1_0        0x20//w
#define LED_DIM_P1_1        0x21//w
#define LED_DIM_P1_2        0x22//w
#define LED_DIM_P1_3        0x23//w
#define LED_DIM_P0_0        0x24//w
#define LED_DIM_P0_1        0x25//w
#define LED_DIM_P0_2        0x26//w
#define LED_DIM_P0_3        0x27//w
#define LED_DIM_P0_4        0x28//w
#define LED_DIM_P0_5        0x29//w
#define LED_DIM_P0_6        0x2A//w
#define LED_DIM_P0_7        0x2B//w
#define LED_DIM_P1_4        0x2C//w
#define LED_DIM_P1_5        0x2D//w
#define LED_DIM_P1_6        0x2E//w
#define LED_DIM_P1_7        0x2F//w
#define SW_RSTN             0x7F//w
//AW9523B chip register map end

//AW9110B chip register map begin
#define AW9110B_ADDR        0x5B
#define GPIO_INPUT_A        0x00//r
#define GPIO_INPUT_B        0x01//r
#define GPIO_OUTPUT_A       0x02//rw
#define GPIO_OUTPUT_B       0x03//rw
#define GPIO_CFG_A          0x04//rw
#define GPIO_CFG_B          0x05//rw
#define GPIO_INTN_A         0x06//rw
#define GPIO_INTN_B         0x07//rw
#define CTRL                0x11//rw
#define GPMD_A              0x12//rw
#define GPMD_B              0x13//rw
#define EN_BRE              0x14//rw
#define FADE_TMR            0x15//rw
#define FULL_TMR            0x16//rw
#define DLY0_BRE            0x17//rw
#define DLY1_BRE            0x18//rw
#define DLY2_BRE            0x19//rw
#define DLY3_BRE            0x1A//rw
#define DLY4_BRE            0x1B//rw
#define DLY5_BRE            0x1C//rw
#define BLUE_1_DIM          0x20//w
#define GREEN_1_DIM         0x21//w
#define RED_1_DIM           0x22//w
#define BLUE_2_DIM          0x23//w
#define GREEN_2_DIM         0x24//w
#define RED_2_DIM           0x25//w
#define BLUE_3_DIM          0x26//w
#define GREEN_3_DIM         0x27//w
#define RED_3_DIM           0x28//w
#define DIM9                0x29//w
#define RESET               0x7F//w
//AW9110B chip register map end

#define CLOCK_LED_BRIGHTNESS    0xFF
#define OUTPUT_PORT_REG 0x02
#define POLARITY_INVERT_REG 0x04
#define CONFIG_PORT_REG 0x06

#define BIT_0   (0)
#define BIT_1   (1)
#define BIT_2   (2)
#define BIT_3   (3)
#define BIT_4   (4)
#define BIT_5   (5)
#define BIT_6   (6)
#define BIT_7   (7)
#define BIT_ONOFF(val, bit)    (((uint8_t)(val&(uint8_t)1<<bit))?CLOCK_LED_BRIGHTNESS:0)

static xSemaphoreHandle mutex_lock; 

static void i2c_init()
{
    int i2c_master_port = I2C_NUM_0;
    i2c_config_t conf;

    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = 18;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = 23;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000;
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0);
    mutex_lock = xSemaphoreCreateMutex();
}

#ifdef CONFIG_USE_ESP32_LYRAT_BOARD //use esp32 lyrat board
esp_err_t i2c_write_reg(uint8_t slave_addr, uint8_t reg_addr, uint8_t* data_wr, size_t size){return 0;}
esp_err_t i2c_read_reg(uint8_t slave_addr, uint8_t reg_addr, uint8_t* data_rd, size_t size){return 0;}
int ext_gpio_output(ext_gpio_num_t num, ext_gpio_out_t val){return 0;}
int ext_gpio_input(ext_gpio_num_t num, ext_gpio_in_t *val){return 0;}
uint8_t ext_gpio_isr_clear(void){return 0;}
void ext_gpio_isr_enable(void){return;}
void ext_gpio_isr_disable(void){return;}
int ext_gpio_init(void){i2c_init(); return 0;}
int rgb_display_red(uint8_t val){return 0;}
int rgb_display_green(uint8_t val){return 0;}
int rgb_display_blue(uint8_t val){return 0;}
int rgb_display_cyan(uint8_t val){return 0;}
int rgb_display_purple(uint8_t val){return 0;}
int rgb_display_yellow(uint8_t val){return 0;}
int rgb_display_white(uint8_t val){return 0;}
int rgb_display_reset(void){return 0;}
int night_led_onoff(uint8_t val){return 0;}
#else//self board
esp_err_t i2c_write_reg(uint8_t slave_addr, uint8_t reg_addr, uint8_t* data_wr, size_t size)
{
    esp_err_t ret;
    i2c_cmd_handle_t cmd;

    portBASE_TYPE res = xSemaphoreTake(mutex_lock, pdMS_TO_TICKS(5));
    if (res == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_WRITE, 1);
    i2c_master_write_byte(cmd, reg_addr, 1);
    i2c_master_write(cmd, data_wr, size, 1);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    xSemaphoreGive(mutex_lock);
    //LOG_DBG("i2c write 0x%x val 0x%x\n", reg_addr, *data_wr);
    if (ret != ESP_OK) {
        LOG_DBG("I2C Err:%d\n", ret);
    }
    return ret;
}

esp_err_t i2c_read_reg(uint8_t slave_addr, uint8_t reg_addr, uint8_t* data_rd, size_t size)
{
    esp_err_t ret;
    i2c_cmd_handle_t cmd;

    portBASE_TYPE res = xSemaphoreTake(mutex_lock, pdMS_TO_TICKS(5));
    if (res == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_WRITE, 1);
    i2c_master_write_byte(cmd, reg_addr, 1);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
  
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_READ, 1);
    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    xSemaphoreGive(mutex_lock);

    //LOG_DBG("i2c read 0x%x val 0x%x\n", reg_addr, *data_rd);
    if (ret != ESP_OK) {
        LOG_DBG("I2C Err:%d\n", ret);
    }
    return ret;
}

static void ext_gpio_chip_reset(ext_gpio_chip_t chip)
{
    return;
}

int ext_gpio_output(ext_gpio_num_t num, ext_gpio_out_t val)
{
    int ret = 0;
    volatile uint8_t reg_in;
    volatile uint8_t reg_out;  
   
    if(num <= AW9523B_P1_7){//chip 9523
        if(num <= AW9523B_P0_7){//p0
            i2c_read_reg(AW9523B_ADDR, OUTPUT_PORT0, &reg_in, 1);
            if(val == EXT_GPIO_OUT_LOW){//output low
                reg_out = reg_in & (~(1<<(num%8)));
                i2c_write_reg(AW9523B_ADDR, OUTPUT_PORT0, &reg_out, 1);
            }else{//output high
                reg_out = reg_in | (1<<(num%8));
                i2c_write_reg(AW9523B_ADDR, OUTPUT_PORT0, &reg_out, 1);
            }
        }else{//p1
            i2c_read_reg(AW9523B_ADDR, OUTPUT_PORT1, &reg_in, 1);
            if(val == EXT_GPIO_OUT_LOW){//output low
                reg_out = reg_in & (~(1<<(num%8)));
                i2c_write_reg(AW9523B_ADDR, OUTPUT_PORT1, &reg_out, 1);
            }else{//output high
                reg_out = reg_in | (1<<(num%8));
                i2c_write_reg(AW9523B_ADDR, OUTPUT_PORT1, &reg_out, 1);
            }        
        }
        //LOG_DBG("extern gpio (%d) output (%d), old reg (0x%x) new reg (0x%x)\n",
        //    num, val, reg_in, reg_out);
    }else if(num <= AW9110B_7){//chip 9110

    }else{
        ret = -ESP_ERR_INVALID_ARG;
        LOG_ERR("undefine extern gpio num (%d)\n", num);
    }
    
    return ret;
}

int ext_gpio_input(ext_gpio_num_t num, ext_gpio_in_t *val)
{
    int ret = 0;
    volatile uint8_t reg_in;

    if(num <= AW9523B_P1_7){//chip 9523
        if(num <= AW9523B_P0_7){//p0
            i2c_read_reg(AW9523B_ADDR, INPUT_PORT0, &reg_in, 1);
            *val = (reg_in & (1<<(num%8)))?EXT_GPIO_OUT_HIGH:EXT_GPIO_IN_LOW;
        }else{//p1
            i2c_read_reg(AW9523B_ADDR, INPUT_PORT1, &reg_in, 1);
            *val = (reg_in & (1<<(num%8)))?EXT_GPIO_OUT_HIGH:EXT_GPIO_IN_LOW;     
        }
        //LOG_DBG("extern gpio (%d) input (%d), reg value (0x%x)\n",
        //    num, *val, reg_in);
    }else if(num <= AW9110B_7){//chip 9110

    }else{
        ret = -ESP_ERR_INVALID_ARG;
        LOG_ERR("undefine extern gpio num (%d)\n", num);
    }

    return ret;
}

uint8_t ext_gpio_isr_clear(void)
{
    uint8_t val;

    i2c_read_reg(AW9523B_ADDR, INPUT_PORT0, &val, 1);
    val = val & 0x07;//just care p0_0~p0_2
    //LOG_DBG("keypad isr detected 0x%x\n", val);

    return val;
}

void ext_gpio_isr_enable(void)
{
    uint8_t val;

    val = 0xF8;
    i2c_write_reg(AW9523B_ADDR, INT_PORT0, &val, 1);
}

void ext_gpio_isr_disable(void)
{
    uint8_t val;

    val = 0xFF;
    i2c_write_reg(AW9523B_ADDR, INT_PORT0, &val, 1);
}

static int chip_aw9110b_init(void)
{
    uint8_t val;

    val = 0x00;
    i2c_write_reg(AW9110B_ADDR, RESET, &val, 1);
    ets_delay_us(5);

    i2c_write_reg(AW9110B_ADDR, CTRL, &val, 1);
    i2c_write_reg(AW9110B_ADDR, GPMD_A, &val, 1);
    i2c_write_reg(AW9110B_ADDR, GPMD_B, &val, 1);

    i2c_write_reg(AW9110B_ADDR, BLUE_1_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, GREEN_1_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, RED_1_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, BLUE_2_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, GREEN_2_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, RED_2_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, BLUE_3_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, GREEN_3_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, RED_3_DIM, &val, 1);

    return 0;
}

static int chip_aw9523b_init(void)
{
    uint8_t val;

    i2c_read_reg(AW9523B_ADDR, ID_REG, &val, 1);
    LOG_DBG("chip id (0x%x)\n", val);

    /*=== gpio config begin ===*/
    val = 0x0F;
    i2c_write_reg(AW9523B_ADDR, LED_MODE_SWITCH_0, &val, 1);
    val = 0x0F;
    i2c_write_reg(AW9523B_ADDR, LED_MODE_SWITCH_1, &val, 1);
    val = 0x10;
    i2c_write_reg(AW9523B_ADDR, CTRL_REG, &val, 1);
    val = 0x07;
    i2c_write_reg(AW9523B_ADDR, CONFIG_PORT0, &val, 1);
    val = 0x00;
    i2c_write_reg(AW9523B_ADDR, CONFIG_PORT1, &val, 1);
    val = 0x08;
    i2c_write_reg(AW9523B_ADDR, OUTPUT_PORT0, &val, 1);
    val = 0x00;
    i2c_write_reg(AW9523B_ADDR, OUTPUT_PORT1, &val, 1);
    val = 0xF8;
    i2c_write_reg(AW9523B_ADDR, INT_PORT0, &val, 1);
    val = 0xFF;
    i2c_write_reg(AW9523B_ADDR, INT_PORT1, &val, 1);
  
    //clear pending interrupt
    i2c_read_reg(AW9523B_ADDR, INPUT_PORT0, &val, 1);
    LOG_DBG("extern gpio 9523 p0 state (0x%x)\n", val);
    i2c_read_reg(AW9523B_ADDR, INPUT_PORT1, &val, 1);
    LOG_DBG("extern gpio 9523 p1 state (0x%x)\n", val);
    /*=== gpio config end ===*/

    /*=== led config begin ===*/
    val = CLOCK_LED_BRIGHTNESS;
    i2c_write_reg(AW9523B_ADDR, LED_DIM_P0_4, &val, 1);
    i2c_write_reg(AW9523B_ADDR, LED_DIM_P0_5, &val, 1);
    i2c_write_reg(AW9523B_ADDR, LED_DIM_P0_6, &val, 1);
    i2c_write_reg(AW9523B_ADDR, LED_DIM_P0_7, &val, 1);
    i2c_write_reg(AW9523B_ADDR, LED_DIM_P1_4, &val, 1);
    i2c_write_reg(AW9523B_ADDR, LED_DIM_P1_5, &val, 1);
    i2c_write_reg(AW9523B_ADDR, LED_DIM_P1_6, &val, 1);
    val = 0;
    i2c_write_reg(AW9523B_ADDR, LED_DIM_P1_7, &val, 1);
    /*=== led config end ===*/
    return 0;
}

int ext_gpio_init(void)
{
    gpio_config_t io_conf;

    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);//config led cs signal

    i2c_init();
    chip_aw9110b_init();
    //chip_aw9523b_init();

    gpio_set_level(GPIO_NIGHT_LED_CS_1, 0);//turn off night led
    gpio_set_level(GPIO_NIGHT_LED_CS_2, 0);//turn off night led

    return 0;
}

int rgb_display_red(uint8_t val)
{
    i2c_write_reg(AW9110B_ADDR, RED_1_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, RED_2_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, RED_3_DIM, &val, 1);
    return 0;
}

int rgb_display_green(uint8_t val)
{
    i2c_write_reg(AW9110B_ADDR, GREEN_1_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, GREEN_2_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, GREEN_3_DIM, &val, 1);
    return 0;
}

int rgb_display_blue(uint8_t val)
{
    i2c_write_reg(AW9110B_ADDR, BLUE_1_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, BLUE_2_DIM, &val, 1);
    i2c_write_reg(AW9110B_ADDR, BLUE_3_DIM, &val, 1);
    return 0;
}

int rgb_display_cyan(uint8_t val)
{
    rgb_display_green(val);
    rgb_display_blue(val);
    return 0;
}

int rgb_display_purple(uint8_t val)
{
    rgb_display_red(val);
    rgb_display_blue(val);
    return 0;
}

int rgb_display_yellow(uint8_t val)
{
    rgb_display_red(val);
    rgb_display_green(val);
    return 0;
}

int rgb_display_white(uint8_t val)
{
    rgb_display_red(val);
    rgb_display_green(val);
    rgb_display_blue(val);
    return 0;
}

int rgb_display_reset(void)
{
    rgb_display_red(0);
    rgb_display_green(0);
    rgb_display_blue(0);
    return 0;
}

int night_led_onoff(uint8_t val)
{
    switch(val){
        case NIGHT_LIGHT_ON:
            gpio_set_level(GPIO_NIGHT_LED_CS_1, 0);
            gpio_set_level(GPIO_NIGHT_LED_CS_2, 1);
            break; 
        case NIGHT_LIGHT_HALF:
            gpio_set_level(GPIO_NIGHT_LED_CS_1, 1);
            gpio_set_level(GPIO_NIGHT_LED_CS_2, 0);
            break;
        case NIGHT_LIGHT_OFF:
            gpio_set_level(GPIO_NIGHT_LED_CS_1, 0);
            gpio_set_level(GPIO_NIGHT_LED_CS_2, 0);
            break;
        default:
            break;
    }
    return 0;
}
#endif
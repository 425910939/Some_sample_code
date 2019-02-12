#include <stdio.h>
#include <stdlib.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "log.h"
#include "keyevent.h"
#include "sc7a20.h"

#define LOG_TAG	    "accel"

#ifdef CONFIG_USE_ESP32_LYRAT_BOARD //use esp32 lyrat board
void acc_enable(void){return;}
void acc_disable(void){return;}
int acc_init(void){return 0;}
int acc_uninit(void){return 0;}
#else
#define SC7A20_INT_0        GPIO_NUM_36
#define SC7A20_INT_1        GPIO_NUM_37
#define ACC_QUEUE_SIZE      (10)
#define ACC_SLAVE_ADDR      (0x19)
#define SC7A20_ID	                0x11	 /*      Expctd content for WAI  */

static SemaphoreHandle_t task_exit;
static bool task_need_run = false;
static xQueueHandle acc_queue;
static bool is_use_sensor = false;

static void IRAM_ATTR acc_isr(void *arg)
{
    acc_cmd_node_t cmd;
    int higher_priority_task_awoken = pdFALSE;

    //gpio_intr_disable(SC7A20_INT_0);
    if(task_need_run == false)
        return;

    cmd.cmd = ACC_CMD_INT_OCCUR;
    cmd.param = NULL;
    cmd.param_size = 0;
    xQueueSendFromISR(acc_queue, &cmd, &higher_priority_task_awoken);
    if(higher_priority_task_awoken == pdTRUE){
        portYIELD_FROM_ISR();
    }
    return;
}

static void acc_task(void* arg)
{
    acc_cmd_node_t cmd;
    uint8_t val;
	keymsg_t msg;

	xSemaphoreTake(task_exit, portMAX_DELAY);
	while(task_need_run){
        if(pdTRUE == xQueueReceive(acc_queue, &cmd, portMAX_DELAY)){
            switch(cmd.cmd){
                case ACC_CMD_INT_OCCUR:
                    if(is_use_sensor == true){
                        i2c_read_reg(ACC_SLAVE_ADDR, 0x31, &val, 1);
                    }else{
                        val = 1;
                    }
                    if(val){
                        //LOG_ERR("acc interrupt status (0x%x)...\n", val);
                        msg.press = KEY_ACTION_PRESS;
                        msg.code = KEY_CODE_PAT;
                        keyaction_notfiy(&msg);
                    }
                    //vTaskDelay(1500);
                    //gpio_intr_enable(SC7A20_INT_1);
                    break;
                default:
                    break;
            }
        }
    }	
	vTaskDelay(1);
    vTaskDelete(NULL);
    xSemaphoreGive(task_exit);
}

void acc_enable(void)
{
    uint8_t val;

	i2c_read_reg(ACC_SLAVE_ADDR, 0x31, &val, 1);
	gpio_intr_enable(SC7A20_INT_1);
	LOG_INFO("acc enable...\n");
	return;
}

void acc_disable(void)
{
    uint8_t val;

	gpio_intr_disable(SC7A20_INT_0);
	i2c_read_reg(ACC_SLAVE_ADDR, 0x31, &val, 1);
	LOG_INFO("acc disable...\n");
	return;
}

int acc_init(void)
{	
    uint8_t val = 0;
	gpio_config_t io_conf;

	LOG_INFO("enter\n");
    i2c_read_reg(ACC_SLAVE_ADDR, 0x0F, &val, 1);
    if(val == SC7A20_ID){
        val = 0x47;//50HZ
        i2c_write_reg(ACC_SLAVE_ADDR, 0x20, &val, 1);
        val = 0x11;
        i2c_write_reg(ACC_SLAVE_ADDR, 0x21, &val, 1);
        val = 0x40;
        i2c_write_reg(ACC_SLAVE_ADDR, 0x22, &val, 1);   
        val = 0xC8;
        i2c_write_reg(ACC_SLAVE_ADDR, 0x23, &val, 1);
        val = 0x02;
        i2c_write_reg(ACC_SLAVE_ADDR, 0x25, &val, 1);
       
        val = 0x7F;
        i2c_write_reg(ACC_SLAVE_ADDR, 0x30, &val, 1);     
        val = 0x02;
        i2c_write_reg(ACC_SLAVE_ADDR, 0x32, &val, 1);
        //val = 0x02;//irq keep time
        //i2c_write_reg(ACC_SLAVE_ADDR, 0x33, &val, 1);
        is_use_sensor = true;

        gpio_install_isr_service(0);
        io_conf.intr_type = GPIO_INTR_NEGEDGE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (uint64_t)((uint64_t)1<<SC7A20_INT_0);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
        gpio_isr_handler_add(SC7A20_INT_0, acc_isr, NULL);
		//acc_disable();
        LOG_INFO("USE SENSOR\n");
    }else{
        gpio_install_isr_service(0);
        io_conf.intr_type = GPIO_INTR_NEGEDGE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (uint64_t)((uint64_t)1<<SC7A20_INT_1);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
        gpio_isr_handler_add(SC7A20_INT_1, acc_isr, NULL);
        LOG_INFO("USE SPRING\n");
        is_use_sensor = false;
    }

    acc_queue = xQueueCreate(ACC_QUEUE_SIZE, sizeof(acc_cmd_node_t));
    task_exit = xSemaphoreCreateMutex();
    
    if(xTaskCreate(acc_task, "acc_task", 2048, NULL, 8, NULL) != pdPASS){
        LOG_ERR("sensor task error\n");
        return -1;
    }
    task_need_run = true;
    
	LOG_INFO("exit\n");
	return 0;
}

int acc_uninit(void)
{
    acc_cmd_node_t cmd;

	LOG_INFO("called\n");

    gpio_intr_disable(SC7A20_INT_0);
    gpio_isr_handler_remove(SC7A20_INT_0);
	task_need_run = false;
    cmd.cmd = ACC_CMD_UNDEFINE;
    cmd.param = NULL;
    cmd.param_size = 0;
    xQueueSend(acc_queue, &cmd, portMAX_DELAY);
	xSemaphoreTake(task_exit, portMAX_DELAY);
	vSemaphoreDelete(task_exit);
	vQueueDelete(acc_queue);
    is_use_sensor = false;
	return;
}
#endif
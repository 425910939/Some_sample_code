#ifndef __LOG_H__
#define __LOG_H__
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/xtensa_api.h"
#include "freertos/portmacro.h"

#undef LOG_TAG
#define LOG_TAG		("undef")


#define LOG_BUFFER_SIZE             (1024*10) //64K LOG CACHE
#define LOG_BUFFER_WATER_MARK       (0) //4K LOG WATRER MARK

typedef struct log_buffer{
    int buffer_total_size;
    int buffer_water_mark;
    int buffer_used_size;
	unsigned long file_used_size;
    char buffer[LOG_BUFFER_SIZE];
}log_buffer_t;

extern bool log_flag_debug;
extern bool log_flag_info;
extern bool log_flag_error;
extern bool offline_log_enable;
extern log_buffer_t *current_buffer;
extern portMUX_TYPE offline_log_spinlock;

void print_buffer(void *buff, int size);
int offline_log_printf(const char *fmt, ...);
int offline_log_init(void);

#define LOG_ERR(x,...)		do{\
								if(log_flag_error){\
									uint32_t cpu_id = xPortGetCoreID();\
									uint32_t timestamp = esp_log_timestamp();\
									if(offline_log_enable && current_buffer&&(current_buffer->buffer_used_size < current_buffer->buffer_total_size)){\
										portENTER_CRITICAL(&offline_log_spinlock);\
										current_buffer->buffer_used_size += snprintf(&current_buffer->buffer[current_buffer->buffer_used_size], \
										current_buffer->buffer_total_size-current_buffer->buffer_used_size,\
											"(%d)[%u][%.8s][%.24s]"x, cpu_id, timestamp, LOG_TAG, __func__,##__VA_ARGS__);\
										portEXIT_CRITICAL(&offline_log_spinlock);\
									}\
									printf("(%d)[%u][%.8s][%.24s]"x, cpu_id, timestamp, LOG_TAG, __func__,##__VA_ARGS__);\
								}\
							}while(0)\

#define LOG_INFO(x,...)		do{\
								if(log_flag_info){\
									uint32_t cpu_id = xPortGetCoreID();\
									uint32_t timestamp = esp_log_timestamp();\
									if(offline_log_enable && current_buffer&&(current_buffer->buffer_used_size < current_buffer->buffer_total_size)){\
										portENTER_CRITICAL(&offline_log_spinlock);\
										current_buffer->buffer_used_size += snprintf(&current_buffer->buffer[current_buffer->buffer_used_size], \
										current_buffer->buffer_total_size-current_buffer->buffer_used_size,\
											"(%d)[%u][%.8s][%.24s]"x, cpu_id, timestamp, LOG_TAG, __func__,##__VA_ARGS__);\
										portEXIT_CRITICAL(&offline_log_spinlock);\
									}\
									printf("(%d)[%u][%.8s][%.24s]"x, cpu_id, timestamp, LOG_TAG, __func__,##__VA_ARGS__);\
								}\
							}while(0)\

#define LOG_DBG(x,...)		do{\
								if(log_flag_debug){\
									uint32_t cpu_id = xPortGetCoreID();\
									uint32_t timestamp = esp_log_timestamp();\
									if(offline_log_enable && current_buffer&&(current_buffer->buffer_used_size < current_buffer->buffer_total_size)){\
										portENTER_CRITICAL(&offline_log_spinlock);\
										current_buffer->buffer_used_size += snprintf(&current_buffer->buffer[current_buffer->buffer_used_size], \
										current_buffer->buffer_total_size-current_buffer->buffer_used_size,\
											"(%d)[%u][%.8s][%.24s]"x, cpu_id, timestamp, LOG_TAG, __func__,##__VA_ARGS__);\
										portEXIT_CRITICAL(&offline_log_spinlock);\
									}\
									printf("(%d)[%u][%.8s][%.24s]"x, cpu_id, timestamp, LOG_TAG, __func__,##__VA_ARGS__);\
								}\
							}while(0)\

#endif
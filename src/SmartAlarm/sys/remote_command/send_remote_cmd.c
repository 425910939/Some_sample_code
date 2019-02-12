#include <stdio.h>                                                                                                                                   
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "display.h"
#include "log.h"
#include "poll.h"
#include "keyevent.h"
#include "connect_manager.h"
#include "download_server.h"
#include "audio_manager.h"
#include "remote_command.h"
#include "log.h"
#include "cJSON.h"

#define LOG_TAG	    "cmd"
          
int assemble_remote_json (uint16_t version,uint32_t length,uint32_t task_code,uint16_t type,char*data)
{
    static uint32_t temp_id = 0;
    
    if(get_server_connect_status() == false){
        LOG_ERR("send remote not connect\n");
        return -1;
    }
	tx_data_t * pjson_data = (tx_data_t*)malloc(sizeof(tx_data_t)+length);
    if(pjson_data == NULL){
        LOG_DBG("pcmd_data malloc failed\n");
        return -1;
    }
	
	pjson_data->version = version;
	pjson_data->magic = 7;
	pjson_data->length = BSWAP_32(length);
	memcpy(pjson_data->data,data,length);

    remote_cmd_info_t buff = {0};
 	
	buff.pdata = (char *)pjson_data;
	buff.len = length + PACKET_HEADER_LEN;
    buff.task_code = task_code;
    buff.type = type;
	
	#if 0
    for(int i = 0 ; i< length + PACKET_HEADER_LEN ; i++){
        printf("%d-0x%x ",i,((unsigned char*)pjson_data)[i]);
    }
	if(version == 0x02){
		pjson_data->version = 0x35;
		buff.len = 1;
	}
	ESP_LOGI("ONEGO","send data = %s",data);
    #endif
	
	LOG_INFO("send data len = %d\n",buff.len);
    if(xQueueSend(sendto_server_info_queue, &buff, portMAX_DELAY) != pdTRUE){
        LOG_DBG("send remote cmd  Q failed\n");
        temp_id--;
        return -1;
    }

    return 0;       
}


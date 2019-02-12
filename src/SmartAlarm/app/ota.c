/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "ff.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "protocol.h"
#include "remote_command.h"
#include "log.h"
#include "download_server.h"
#include "ota.h"
#include "system_manager.h"

#define GET_OTA_VERSION             (0)
#define FINISH_OTA_FILE_DOWNLOAD    (1)
#define FINISH_OTA_UPGRADE          (2)
#define OTA_BUFFER_SIZE             (1024)
#define LOG_TAG		                "OTA"
#define NORMAL_STARTUP              (0)
#define DOWNLOAD_BIN_FINISH         (1)
#define WRITE_IN_FLASH              (2)
#define MAX_RETRY                   (3)

#define NOT_START_UPGRADE           (0)
#define START_UPGRADE               (1)        
#define UPGRADE_FAILED              (2)
#define UPGRAFE_SUCCESS             (3)
#define REQUEST_DOWNLOAD_FAIL       (128)
#define DEVICE_OTA_STATUS_ERROR     (129)
#define UPGRADE_REASON_SUCCESS      (0)

typedef struct{
    uint8_t cmd_type;
    void * params;
}ota_remote_info_t;

typedef struct{
    char file_path[56];
    download_upload_finish_reason_t finish_reason;
    uint32_t real_filesize;
}load_finish_info_t;

static uint32_t g_register_cmd_lable = 255;
xQueueHandle ota_update_req_queue  = NULL;
static bool task_running_flag = false;

int start_to_upgrade(char * file_path,uint32_t file_size)
{
    esp_err_t err;
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();
    char ota_write_data[OTA_BUFFER_SIZE + 1] = {0};
    FILE *fp = NULL;
    int ret = 0;
    uint32_t read_sum = 0;

    fp = fopen(file_path,"rb+");

    if(fp == NULL){
        LOG_INFO("open file failed!\n");
        return -1;
    }

    assert(configured == running);
    LOG_INFO("Running partition type %d subtype %d (offset 0x%08x)\n",configured->type, configured->subtype, configured->address);
    update_partition = esp_ota_get_next_update_partition(NULL);
    LOG_INFO("Writing to partition subtype %d at offset 0x%x\n",update_partition->subtype, update_partition->address);
    assert(update_partition != NULL);

    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        LOG_ERR("esp_ota_begin failed, error=%d\n", err);
        return -2;
    }
    LOG_INFO("esp_ota_begin succeeded\n");
    
    while(1){
        memset(ota_write_data, 0, OTA_BUFFER_SIZE);
        if((ret = fread(ota_write_data,1,OTA_BUFFER_SIZE,fp)) > 0){
    	    err = esp_ota_write(update_handle, (const void *)ota_write_data, ret);
            if (err != ESP_OK) {
                LOG_ERR("Error: esp_ota_write failed! err=0x%x\n", err);
                return -3;
            }
            read_sum += ret;
            LOG_INFO("read total %d, ret %d\n",read_sum,ret);
        }else{
            break;
        }
    }

    if(read_sum != file_size){
        LOG_INFO("read file size error\n");
        return -4;
    }

    LOG_INFO("Total Write binary data length : %d\n", read_sum);

    if (esp_ota_end(update_handle) != ESP_OK) {
        LOG_ERR("esp_ota_end failed!\n");
        return -5;
    }

    LOG_INFO("Prepare to restart system!\n");
    return 0;
}

int send_ota_request_Q(ota_remote_info_t *buff)
{
	LOG_DBG("send_ota_request_Q\n");
    
    if(ota_update_req_queue == NULL){
        LOG_DBG("download finish info err\n");
        return -1;
    }

	if(xQueueSend(ota_update_req_queue, buff, portMAX_DELAY) != pdTRUE){
		LOG_DBG("malloc download Q failed\n");
		return -1;
	}
	return 0;
}

static void ota_download_callback(download_upload_finish_reason_t finish_reason , uint32_t filesize, char * file_path_name, uint32_t write_in_file_size)
{
    ota_remote_info_t cmder = {0};
    load_finish_info_t * p = (load_finish_info_t *)heap_caps_malloc(sizeof(load_finish_info_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    p->finish_reason = finish_reason;
    p->real_filesize = write_in_file_size;
    strncpy(p->file_path,file_path_name,sizeof(p->file_path) - 1);
    p->file_path[sizeof(p->file_path) - 1] = '\0';
    cmder.cmd_type = FINISH_OTA_FILE_DOWNLOAD;
    cmder.params = (void *)p;

    LOG_INFO("\nfilesize %d,write_in_file_size %d\n",filesize,write_in_file_size);

    send_ota_request_Q(&cmder);
	return;
}

static void  ota_command_recevie(void *p_data,int cmd_type)
{
    rx_data_t *p = (rx_data_t *)p_data;
    int len = 0,type = 0;
    ota_remote_info_t cmder = {0};
	
	type = cmd_type;
	len = p->length+6;
	LOG_INFO("recive type = 0x%x len = %d\n",type,len);
	p = (rx_data_t *)heap_caps_malloc(len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if(p){
		memcpy(p, p_data, len);
		switch(type){
			case S2C_OTA_UPDATE_PUSH:
				cmder.cmd_type = GET_OTA_VERSION;
				cmder.params = (void *)p;
				send_ota_request_Q(&cmder);						
				break;
			default:
				LOG_ERR("unspport command %d coming, ignore it\n", type);
				free(p);
				p = NULL;
				break;
		}
	}else{
		LOG_ERR("malloc command buff fail\n");
	}
	return;
}

void running_partition(){
    const esp_partition_t *update_partition = NULL;
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if(configured != running)
        LOG_INFO("configured != running\n");

    LOG_INFO("Running partition type %d subtype %d (offset 0x%08x)\n",configured->type, configured->subtype, configured->address);
    update_partition = esp_ota_get_next_update_partition(NULL);
    LOG_INFO("Writing to partition subtype %d at offset 0x%x\n",update_partition->subtype, update_partition->address);
}

int get_upgrade_step(upgrade_step_info_t * p_step_info){
    int ret = 0;

    if(p_step_info == NULL){
        LOG_DBG("p_step_info null\n");
        return -1;
    }

    FILE *fp = fopen(UPGRADE_STEP_FILE_NAME,"rb+");

    if(fp == NULL){
        LOG_DBG("Can not upgrade step file file,maybe file not exist\n");
        p_step_info->upgrade_step = NORMAL_STARTUP;
        p_step_info->retry = 0;
        p_step_info->before_step_status = NORMAL_STARTUP;
        memset(p_step_info->upgrade_file_name,0,sizeof(p_step_info->upgrade_file_name)); 
        p_step_info->upgrade_file_size = 0;
        return -2;
    }

    if((ret = fread(p_step_info,sizeof(upgrade_step_info_t),1,fp)) != 1){
        LOG_DBG("upgrade step failed\n");
        fclose(fp);
        return -3;
    }
    LOG_DBG("upgrade step read %d\n",ret);
    fclose(fp);
    return 0;
}

int set_upgrade_file(upgrade_step_info_t * p_step_info){
    int ret = 0;
    if(p_step_info == NULL){
        LOG_DBG("p_step_info null\n");
        return -1;
    }

    FILE *fp = fopen(UPGRADE_STEP_FILE_NAME,"wb+");

    if(fp == NULL){
        LOG_DBG("Can not upgrade step file file\n");
        return -2;
    }

    if((ret = fwrite(p_step_info,sizeof(upgrade_step_info_t),1,fp)) != 1){
        LOG_DBG("upgrade step failed\n");
        fclose(fp);
        return -3;
    }

    LOG_DBG("upgrade step write %d\n",ret);

    fflush(fp);
    fclose(fp);
    return 0;
}

static int check_net_resouce_is_right(uint16_t is_need_crc,uint32_t crc_value,char * p_file_path)
{
    if(is_need_crc == 0){
        LOG_INFO("No need to crc verify to check file\n");
        return 0;
    }

    FILE *fp = fopen(UPGRADE_STEP_FILE_NAME,"rb+");

    if(fp == NULL){
        LOG_INFO("File open error\n");
        return -1;
    }

    LOG_INFO("Now start ot crc check\n");

    fclose(fp);
    return 0;
}

int check_write_in_flase_error(char * file_name,uint32_t file_size){
    FILE *fp = NULL;
    char file_buff[OTA_BUFFER_SIZE + 1] = {0};
    char flash_buff[OTA_BUFFER_SIZE + 1] = {0};
    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);//esp_ota_get_running_partition();//
    uint32_t offset = 0;
    uint32_t read_size = 0;
    esp_err_t err = ESP_OK;

    fp = fopen(file_name,"rb+");

    if(fp == NULL){
        LOG_INFO("fopen failed \n");
        return -1;
    }

    if (partition == NULL) {
        LOG_INFO("PHY data partition not found\n");
        fclose(fp);
        return -2;
    }

    LOG_INFO("Writing to partition subtype %d at offset 0x%x\n",partition->subtype, partition->address);

    while(file_size > 0){
        
        LOG_INFO("file_size %d,offset %d\n",file_size,offset);
        if(file_size > OTA_BUFFER_SIZE){
            read_size = OTA_BUFFER_SIZE;
            file_size -= OTA_BUFFER_SIZE;
        }else{
            read_size = file_size;
            file_size = 0;
        }

        err = esp_partition_read(partition, offset, flash_buff, read_size);

        if (err != ESP_OK) {
            LOG_INFO("failed to read PHY data partition (0x%x)\n", err);
            fclose(fp);
            return -3;
        }
        
        if(fread(file_buff,1,read_size,fp) != read_size){
            LOG_INFO("file read error\n");
            fclose(fp);
            return -4;
        }

        if(strncmp(flash_buff,file_buff,read_size) != 0){
            LOG_INFO("Compare error\n");
            fclose(fp);
            return -5;
        }

        offset += read_size;
    }

    return 0;
}

void delete_all_upgrade_file(char * file_name)
{
    unlink(file_name);
    unlink(UPGRADE_STEP_FILE_NAME);
}

int change_boot_partition()
{
    esp_err_t err;
    const esp_partition_t *update_partition = NULL;
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if(configured != running){
        LOG_INFO("configured != running\n");
        return -1;
    }
    LOG_INFO("Running partition type %d subtype %d (offset 0x%08x)\n",configured->type, configured->subtype, configured->address);
    update_partition = esp_ota_get_next_update_partition(NULL);
    LOG_INFO("Writing to partition subtype %d at offset 0x%x\n",update_partition->subtype, update_partition->address);

    if(update_partition == NULL){
        LOG_INFO("update_partition is NULL\n");
        return -2;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        LOG_ERR("esp_ota_set_boot_partition failed! err=0x%x\n", err);
        return -3;
    }

    return 0;
}

void doing_ota_upgrade(upgrade_step_info_t * p_step_info){
    int ret = 0;

    switch(p_step_info->upgrade_step)
	{
		case NORMAL_STARTUP:
			if(p_step_info->before_step_status == CHECK_FLASH_IS_RIGHT){
				LOG_INFO("upgrade is succese\n");
				unlink(p_step_info->upgrade_file_name);
			}
			break;
		
		case DOWNLOAD_BIN_FINISH:
            if(p_step_info->retry >= MAX_RETRY){
                p_step_info->upgrade_step = NORMAL_STARTUP;
                set_upgrade_file(p_step_info);
                LOG_INFO("0---ota case %d,ret %d\n",p_step_info->upgrade_step,ret);
                break;
			}else{
                p_step_info->retry ++;
                set_upgrade_file(p_step_info);
                LOG_INFO("1---ota case %d,ret %d\n",p_step_info->upgrade_step,ret);
            }
            vTaskDelay(500 / portTICK_PERIOD_MS);
            ret = start_to_upgrade(p_step_info->upgrade_file_name,p_step_info->upgrade_file_size);
			if(ret == 0){
				p_step_info->before_step_status = p_step_info->upgrade_step;
				p_step_info->upgrade_step = ALREADY_WRITE_IN_FLASH;
				p_step_info->retry = 0;
			}
            LOG_INFO("2---ota case %d,ret %d\n",p_step_info->upgrade_step,ret);
			set_upgrade_file(p_step_info);
			vTaskDelay(2000 / portTICK_PERIOD_MS);
			esp_restart();
			break;
		
		case ALREADY_WRITE_IN_FLASH:
            ret = check_write_in_flase_error(p_step_info->upgrade_file_name,p_step_info->upgrade_file_size);
			if(ret == 0){
				p_step_info->before_step_status = p_step_info->upgrade_step;
				p_step_info->upgrade_step = CHECK_FLASH_IS_RIGHT;
				p_step_info->retry = 0;
			}else{
				if(p_step_info->retry >= MAX_RETRY){
					p_step_info->upgrade_step = NORMAL_STARTUP;
				}
				p_step_info->retry ++;
			}
            LOG_INFO("ota case %d,ret %d\n",p_step_info->upgrade_step,ret);
			set_upgrade_file(p_step_info);
			vTaskDelay(2000 / portTICK_PERIOD_MS);
			esp_restart();
			break;
		
		case CHECK_FLASH_IS_RIGHT:
            ret = change_boot_partition();
			if(ret == 0){
				p_step_info->before_step_status = p_step_info->upgrade_step;
				p_step_info->upgrade_step = NORMAL_STARTUP;
				p_step_info->retry = 0;
			}else{
				if(p_step_info->retry >= MAX_RETRY){
					p_step_info->upgrade_step = NORMAL_STARTUP;
				}
				p_step_info->retry ++;
			}
            LOG_INFO("ota case %d,ret %d\n",p_step_info->upgrade_step,ret);
			set_upgrade_file(p_step_info);
			vTaskDelay(2000 / portTICK_PERIOD_MS);
			esp_restart();
			break;
		
		default:
			unlink(UPGRADE_STEP_FILE_NAME);
			vTaskDelay(2000 / portTICK_PERIOD_MS);
			esp_restart();
			break;
	}
}

cJSON * create_ota_cjson(download_upload_finish_reason_t reason)
{
	cJSON *root = NULL;
	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "reason", reason);
	return root;
}

static void ota_update_task(void *pvParameter)
{
    static bool is_upgrade = false;
    uint16_t is_need_crc = 0;
    uint32_t crc_value = 0;
	cJSON * ota_json =NULL;
	cJSON * data_json =NULL;
	cJSON * item_json =NULL;
    while(task_running_flag)
    {
        rx_data_t * ota_request = NULL;
        server_ota_push_t * ota_info = NULL;
        load_finish_info_t * load_info = NULL;
        uint32_t url_size = 0;
        download_info_t download_info = {BIN_FILE_PATH};
        ota_remote_info_t cmder = {0};
        upgrade_step_info_t step_info = {0};

        xQueueReceive(ota_update_req_queue, &cmder, portMAX_DELAY);

		switch(cmder.cmd_type){
            case GET_OTA_VERSION:{
				ota_request = (rx_data_t *)cmder.params;
				ota_json = cJSON_Parse(ota_request->data);
				if(ota_json){
					data_json = cJSON_GetObjectItem(ota_json,"data");
					if(data_json == NULL){
						cJSON_Delete(ota_json);
						ota_json = NULL;
						break;
					}
					
					item_json = cJSON_GetObjectItem(data_json,"ota_url");
					if(item_json == NULL){
						cJSON_Delete(ota_json);
						ota_json = NULL;
						data_json =NULL;
						break;
					}
					url_size = strlen(item_json->valuestring);
					ota_info =  (server_ota_push_t *)heap_caps_malloc(sizeof(server_ota_push_t)+url_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
					if(ota_info){
						strncpy(ota_info->url,item_json->valuestring,url_size);
						
						item_json = cJSON_GetObjectItem(data_json,"soft_version");
						if(item_json == NULL){
							cJSON_Delete(ota_json);
							ota_json = NULL;
							data_json =NULL;
							free(ota_info);
							ota_info = NULL;
							break;
						}
						strncpy(ota_info->version,item_json->valuestring,sizeof(ota_info->version));
						
						if(is_upgrade == true){
	                   		LOG_INFO("NOT IS UPDATING version %s\n",ota_info->version);
	                   		break;
	               		}
						is_need_crc =0;
						LOG_INFO("\nis need crc %d,version %s\n",is_need_crc,ota_info->version);

						strncat(download_info.file_path_name, ota_info->version,sizeof(ota_info->version));
		                unlink(download_info.file_path_name);
		                strncpy(download_info.download_path, ota_info->url, url_size);
		                LOG_INFO("\nota Url %s download path %s url size %d\n",ota_info->url,download_info.download_path,url_size);
		                download_info.breakpoint = false;
		                download_info.filesize = 0;
		                download_info.type = 0;
		                download_info.port_number[0] = 0;
		                download_info.callback = ota_download_callback;

						if(send_download_req_Q(&download_info) == 0){
	                    	is_upgrade = true;
	                    	LOG_INFO("Now start to download and upgrade\n");
	                	}
						free(ota_info);
						ota_info = NULL;
						cJSON_Delete(ota_json);
						ota_json = NULL;
						data_json = NULL;
						item_json = NULL;
					}	
				}
            }
				break;
            case FINISH_OTA_FILE_DOWNLOAD:{
                load_info = (load_finish_info_t *)cmder.params;
               
                LOG_INFO("Load filepath %s reason %d,size %d\n",load_info->file_path,load_info->finish_reason,load_info->real_filesize);
            
                if(load_info->finish_reason == 0 && load_info->real_filesize > 1024){
                    LOG_INFO("start to write bin to flash\n");
                    if(check_net_resouce_is_right(is_need_crc,crc_value,load_info->file_path) == 0){
                        uint8_t cp_size = 0;

                        step_info.upgrade_step = DOWNLOAD_BIN_FINISH;
                        step_info.retry = 0;
                        step_info.before_step_status = NORMAL_STARTUP;
                        cp_size = strlen(load_info->file_path);
                        cp_size = cp_size > sizeof(step_info.upgrade_file_name)? sizeof(step_info.upgrade_file_name):cp_size;
                        strncpy(step_info.upgrade_file_name,load_info->file_path,cp_size);
                        step_info.upgrade_file_size = load_info->real_filesize;

                        if(set_upgrade_file(&step_info) == 0){

                            vTaskDelay(5000);
                            esp_restart();
                        }
                        LOG_INFO("set_upgrade_file\n");
                    }
                }

                delete_all_upgrade_file(load_info->file_path);
                is_upgrade = false;
                is_need_crc = 0;
                crc_value = 0;
				#if 0
				ota_json = create_ota_cjson(load_info->finish_reason);
				char *buf = cJSON_Print(ota_json);
				cJSON_Delete(ota_json);
				ota_json = NULL;
				assemble_remote_json(DEVICE_DATA_VERSION,strlen(buf),g_register_cmd_lable,C2S_OTA_PROGRESS_NOTIFY,buf);
				#endif
            }
                break;
            default:
                LOG_INFO("Not have the type %d\n",cmder.cmd_type);
                break;
        }

        if(cmder.params != NULL)
            free(cmder.params);
        
        LOG_INFO("ota task status %d, cmder.cmd_type %d\n",is_upgrade,cmder.cmd_type);
    }
}

int ota_task_init(void)
{	
    uint32_t cmd_bits = 0;

    cmd_bits = 1<<find_type_bit_offset(S2C_OTA_UPDATE_PUSH);
    g_register_cmd_lable = remote_cmd_register(cmd_bits, ota_command_recevie);

    LOG_INFO("cmd_bit %d,register_cmd_lable %d\n",cmd_bits,g_register_cmd_lable);

    ota_update_req_queue = xQueueCreate(2, sizeof(ota_remote_info_t));

    if(ota_update_req_queue == NULL){
        return -1;  
    }

    LOG_INFO("ota task init finish\n");

    return 0;
}

void ota_task_uninit(void)
{
    task_running_flag = true;
    remote_cmd_unregister(g_register_cmd_lable);
    vQueueDelete(ota_update_req_queue);
}

void ota_net_monitor_init()
{
    if(ota_task_init() == -1){
        LOG_INFO("ota resource failed \n");
        return ;
    }

    task_running_flag = true;

    if(xTaskCreate(&ota_update_task, "ota_update_task", 4096, NULL, 5, NULL) != pdPASS){
        task_running_flag = false;
        ota_task_uninit();
        LOG_ERR("--**--ota update task startup failed\n");
    }
}

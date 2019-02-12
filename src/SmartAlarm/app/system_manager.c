#include <stdlib.h>                                                                                                                               
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include <sys/unistd.h>
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "rom/queue.h"
#include "connect_manager.h"
#include "download_server.h"
#include "esp_log.h"
#include "remote_command.h"
#include "system_manager.h"
#include "wechat.h"
#include "log.h"
#include "protocol.h"
#include "get_voltage.h"
#include "version.h"
#include "time.h"
#include "alarms.h"
#include "cJSON.h"
#include "audiomanagerservice.h"
#include "gc.h"
#include "nv_interface.h"
#include "esp_wifi.h"

#define HEART_BEAT_THRESHOLD        (4*60*1000) //4minutes

#define LOG_TAG	    "system"

static uint32_t command_handler; 
static xQueueHandle system_queue;
static TimerHandle_t xTimerUser;
static SemaphoreHandle_t task_exit;
static xTaskHandle task_handle = NULL;
static bool task_need_run = true;
static bool connected_server = false;

int send_device_status(bool _first,bool _song,bool _volume,uint32_t _song_time)
{
	
	system_notify_t notify;
	device_notify_t *dev_notify = NULL;
	
	LOG_INFO("send_device_status\n");
	if(get_server_connect_status() == false)
		return 0;
	
	dev_notify = (device_notify_t *)heap_caps_malloc(sizeof(device_notify_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if(dev_notify){
		dev_notify->first = _first;
		dev_notify->song = _song;
		dev_notify->volume = _volume;
		dev_notify->song_time = _song_time;
		notify.command = SYS_CMD_DEVICE_STATUS;   
		notify.param = (void*)dev_notify;
		notify.size = sizeof(device_notify_t);
		if(xQueueSend(system_queue, &notify,portMAX_DELAY) != pdTRUE){
			LOG_INFO("send_device_status failed\n");
			free(dev_notify);
			dev_notify = NULL;
			return -1;
		}
	}
	
	return 0;
}

static void delete_file(const char *path)
{
    DIR *dir = NULL;
	char g_file_path[256];
	
    dir = opendir(path);
	if(dir){
	    struct dirent* de = readdir(dir);
	    if(de){
	        while(true){
	            if(!de)
	               break;
				memset(g_file_path, 0, 256);
				strncpy(g_file_path, path, 256);
				strcat(g_file_path, "/");
				strcat(g_file_path, de->d_name);
				ESP_LOGI("ONEGO","file path = %s",g_file_path);
				unlink(g_file_path);
	            de = readdir(dir);
	        }
		}else{
	        LOG_ERR("can not read sdcard dir (%s)...\n",path);            
		}
		closedir(dir);
	}
	return ;
}

int restore_gactory_setting(void)
{
	nv_item_t nv_volume;
	delete_file("/sdcard/wechat");
	delete_file("/sdcard/PLAYLOG");
	unlink("/sdcard/wifi_info");
	unlink("/sdcard/offlinelog.txt");
	nv_volume.name = NV_ITEM_POWER_ON_MODE;
	nv_volume.value = (int64_t)0;
	set_nv_item(&nv_volume);
	esp_wifi_disconnect();
	esp_wifi_restore();
	vTaskDelay(2000 / portTICK_PERIOD_MS);
	esp_restart();
	return 0;
}

static cJSON*get_device_status(bool first,bool volume,bool song,uint32_t song_time)
{
	cJSON *root = NULL;
	cJSON *data = NULL;
	int status = 0;
	char file[48] = {0};
	char _songid[20] = {0};
	char _albumUid[20] = {0};
	uint32_t use_space = 0;
	uint32_t total_space = 0;
	
	LOG_INFO("first = %d volumecontrol=%d songcontrol = %d song_time = %d\n",first,volume,song,song_time);
	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "type", S2C_DEVICE_STATUS);
	cJSON_AddItemToObject(root, "data", data = cJSON_CreateObject());
	
	if(first){
		total_space = get_sd_free_space(&use_space);
		memset(file,0,sizeof(file));
		snprintf(file,sizeof(file),"%dKB",use_space);
		cJSON_AddStringToObject(data,"used_space",file);
		memset(file,0,sizeof(file));
		snprintf(file,sizeof(file),"%dKB",total_space);
		cJSON_AddStringToObject(data,"total_space",file);
	} 
	
	if(volume){
		cJSON_AddNumberToObject(data,"volume",get_volume_grade());
	}
	
	if(song){
		get_play_status(file,_songid,_albumUid,&status);
		cJSON_AddStringToObject(data,"name",file);
		cJSON_AddNumberToObject(data,"status",status);
		cJSON_AddNumberToObject(data,"mseconds",song_time);
		cJSON_AddStringToObject(data,"songUid",_songid);
		cJSON_AddStringToObject(data,"albumUid",_albumUid);
	}
	
	return root;
}

static long long_vaule_cmp(long a, long b)
{
    if(a > b){
        return (a-b);
    }else{
        return (b-a);
    }
}

static void  system_command_cb(void *p_data,int cmd_type)
{
	struct timeval tv = {0};
	struct timeval new_time ={0};		
	rx_data_t *cmd = (rx_data_t *)p_data;
	int type = cmd_type;
 	system_notify_t notify;
	nv_item_t nv_volume;
	
	LOG_INFO("recv version  = %d len = %d\n",cmd->version,cmd->length);
    switch(type){
        case S2C_DEVICE_HEARTBEAT_ACK:
			new_time.tv_sec= strtol(cmd->data,NULL,0);
            gettimeofday(&tv, NULL);
            LOG_INFO("recv heartbeat network time %ld, local time %ld\n", new_time.tv_sec,tv.tv_sec);
            if(long_vaule_cmp(tv.tv_sec, new_time.tv_sec) > 3){
                setenv("TZ", "CST-8", 1);
                tzset();
                tv.tv_sec = new_time.tv_sec + 1;
                settimeofday(&tv, NULL); 
                fresh_alarm();
            }
            break;
        case LOGIN_VERSION_FAIL:
			LOG_INFO("login fail reaseon %s\n",cmd->data);
			connected_server = false;
			notify.command = SYS_CMD_POWERON_HANDSHAKE;           
			xQueueSend(system_queue, &notify, 100);
			break;
		case LOGIN_VERSION_SUCCEED:
			LOG_INFO("login success\n");
			connected_server = true;
			nv_volume.name = NV_ITEM_POWER_ON_MODE;
			nv_volume.value = (int64_t)1;
			set_nv_item(&nv_volume);
			//notify.command = SYS_CMD_DEVICE_STATUS_FIRST;  
			//xQueueSend(system_queue, &notify, 100);
			break;
		case S2C_DEVICE_STATUS:
			notify.command = SYS_CMD_DEVICE_STATUS;   
			xQueueSend(system_queue, &notify, 100);
			break;
		case S2C_FACTORY_RESET:
			notify.command = SYS_CMD_FACTORY_RESET;   
			xQueueSend(system_queue, &notify, 100);
			break;
		default:
            break;
    }

	return;
}

static void system_task(void *param)
{
    system_notify_t notify;
	char buf[10] = {0};
	cJSON *json_item = NULL;
	char*json_data = NULL;
	device_notify_t *dev_notify = NULL;
		
    xSemaphoreTake(task_exit, portMAX_DELAY);
    while(task_need_run){
        if(pdTRUE == xQueueReceive(system_queue, &notify, portMAX_DELAY)){
            LOG_DBG("receive command %d\n", notify.command);
            switch(notify.command){ 
                case SYS_CMD_HEARTBEAT_HANDSHAKE:
					snprintf(buf,sizeof(buf),"%d",get_voltage_value());
					assemble_remote_json(DEVICE_HEARTBEAT_VERSION,strlen(buf),command_handler,S2C_DEVICE_HEARTBEAT_ACK,(char*)buf);
					break;
				case SYS_CMD_POWERON_HANDSHAKE:
					assemble_remote_json(LOGIN_VERSION_SUCCEED,32,command_handler,0,get_device_sn());
					break;
				case SYS_CMD_DEVICE_STATUS:
					dev_notify = (device_notify_t*)notify.param;
					json_item = get_device_status(dev_notify->first,dev_notify->volume,dev_notify->song,dev_notify->song_time);
					if(notify.param && notify.size){
						free(dev_notify);
						dev_notify =NULL;
					}
					json_data = cJSON_Print(json_item);
					assemble_remote_json(DEVICE_DATA_VERSION,strlen(json_data),command_handler,S2C_DEVICE_STATUS,(char*)json_data);
					cJSON_Delete(json_item);
					json_item = NULL;
					free(json_data);
					json_data = NULL;
					break;
				case SYS_CMD_DEVICE_STATUS_FIRST:
					json_item = get_device_status(true,true,false,0);
					json_data = cJSON_Print(json_item);
					assemble_remote_json(DEVICE_DATA_VERSION,strlen(json_data),command_handler,S2C_DEVICE_STATUS,(char*)json_data);
					cJSON_Delete(json_item);
					json_item = NULL;
					free(json_data);
					json_data = NULL;
					break;
				case SYS_CMD_FACTORY_RESET:
					restore_gactory_setting();
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

static void system_manager_tick(TimerHandle_t xTimer)
{
    system_notify_t notify;

    LOG_DBG("system tick connect status %d\n", connected_server);
    if(get_server_connect_status()){
        if(connected_server){
            notify.command = SYS_CMD_HEARTBEAT_HANDSHAKE;
        }else{
            notify.command = SYS_CMD_POWERON_HANDSHAKE;
        }
        xQueueSend(system_queue, &notify, 100);
    }else{
        LOG_INFO("remote command server not connect\n");
    }
	LOG_ERR("system memory internal (%d)  spiram (%d)\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), 
		heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void force_handshake_with_server(void)
{
	if(task_handle){
		LOG_INFO("force handshake with server...\n");
		connected_server = false;
		system_manager_tick(NULL);
	}else{
		LOG_INFO("system manager task uninit, ignore it...\n");
	}
}

int system_manager_init()
{
    //1. heart beat
    uint32_t cmd_bits = 0;
    struct timeval tv = {0};

    LOG_INFO("enter\n");
    settimeofday(&tv, NULL); 
    cmd_bits = 1<<find_type_bit_offset(S2C_DEVICE_HEARTBEAT_ACK);
	cmd_bits |= 1<<find_type_bit_offset(S2C_DEVICE_STATUS);
	cmd_bits |= 1<<find_type_bit_offset(S2C_FACTORY_RESET);
	command_handler = remote_cmd_register(cmd_bits, system_command_cb);

    system_queue = xQueueCreate(10, sizeof(system_notify_t));
    task_exit = xSemaphoreCreateMutex();

    xTimerUser = xTimerCreate("sys_tick", pdMS_TO_TICKS(HEART_BEAT_THRESHOLD), pdTRUE, NULL, system_manager_tick);
	if(!xTimerUser) {
		LOG_ERR("creat xtimer fail\n");
        vSemaphoreDelete(task_exit);
        vQueueDelete(system_queue);
        remote_cmd_unregister(command_handler);
		return ESP_ERR_INVALID_RESPONSE;
	}

    xTaskCreate(system_task, "system_task", 3072, NULL, 5, &task_handle);
    xTimerStart(xTimerUser, 100);
    LOG_INFO("exit\n");
    return 0;
}

int system_manager_uninit(void)
{
    system_notify_t notify;

    xTimerStop(xTimerUser, 100);

    task_need_run = false;
    notify.command = SYS_CMD_UNDEFINE;
    xQueueSend(system_queue, &notify, 100);
    xSemaphoreTake(task_exit, portMAX_DELAY);

    vSemaphoreDelete(task_exit);
    return 0;
}

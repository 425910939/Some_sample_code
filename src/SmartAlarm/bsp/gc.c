#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "ff.h"
#include "sdmmc_cmd.h"
#include "DeviceCommon.h"
#include "display.h"
#include "log.h"
#include "poll.h"
#include "MediaControl.h"
#include "PlaylistManager.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "gc.h"
#include "userconfig.h"
#include <errno.h>
#define SD_CARD_CHECK_PRINT		(0)
#define MOUNT_SD_PERIOD         (30*1000) //30-sec
#define GC_TASK_TICK            (60*60*1000) //60-min
#define SD_MOUNT_RETRY_MAX      (1)
#define SD_FILE_PATH_MAX_SIZE	(256)
#define USER_TASK
#define LOG_TAG	    "gc"
#ifdef USER_TASK
static SemaphoreHandle_t task_exit;
static bool task_need_run = true;
static xQueueHandle timer_queue;
static TimerHandle_t xTimerUser;
#else
void gc_check_state_cb(void *param);
poll_func_config_t gc_config = {PCLOCK_SOURCE_SYS,300,gc_check_state_cb,NULL};
static uint8_t remount_count = 0;
static int period_time = 0;
static int call_count = 0;
#endif
static void *notify_fd = NULL;
static sdcard_state_t g_sd_state = SD_CARD_ST_UNMOUNT;
static char g_file_path[SD_FILE_PATH_MAX_SIZE] = {0};
static uint32_t sd_total_size = 0;
static int sd_card_mount(void)
{
	LOG_INFO("mount\n");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
#ifdef CONFIG_USE_ESP32_LYRAT_BOARD //
    host.flags = SDMMC_HOST_FLAG_1BIT;
#endif
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = SD_CARD_OPEN_FILE_NUM_MAX
    };

    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            LOG_ERR("Failed to mount filesystem.\n");
        } else {
            LOG_ERR("Failed to initialize the card (%d).\n", ret);
        }
        return ret;
    }
	sd_total_size = (((uint64_t) card->csd.capacity) * card->csd.sector_size)/1024;
    LOG_INFO("SD Card Init Success, Name: %s, Type: %s, Speed: %s, Size: %lluMB, CSD: ver=%d, sector_size=%d, capacity=%d, read_bl_len=%d, SCR: sd_spec=%d, bus_width=%d\n",
        card->cid.name, (card->ocr & SD_OCR_SDHC_CAP)?"SDHC/SDXC":"SDSC", 
        (card->csd.tr_speed > 25000000)?"high speed":"default speed",
        ((uint64_t) card->csd.capacity) * card->csd.sector_size / (1024 * 1024),
        card->csd.csd_ver, card->csd.sector_size, card->csd.capacity, card->csd.read_block_len,
        card->scr.sd_spec, card->scr.bus_width);
	return 0;
}


static int sd_card_unmount(void)
{
	LOG_INFO("unmount\n");
	esp_vfs_fat_sdmmc_unmount();
	return 0;
}

static void notify_media_sd_state(MediaControl *ctrl)
{  
    DeviceNotification note;
    DeviceNotifySDCardMsg msg;

    if(!ctrl){
        LOG_ERR("ctrl ptr is NULL\n");
        return;
    }

    if (g_sd_state == SD_CARD_ST_MOUNTED)
        msg = DEVICE_NOTIFY_SD_MOUNTED;
    else
        msg = DEVICE_NOTIFY_SD_UNMOUNTED;

    memset(&note, 0x00, sizeof(DeviceNotification));
    note.type = DEVICE_NOTIFY_TYPE_SDCARD;
    note.data = &msg;
    note.len = sizeof(DeviceNotifySDCardMsg);
    LOG_INFO("type %d, msg %d, len %d\n", note.type, msg, note.len);

    PlaylistManager *playlistManager = ctrl->playlistManager;
    if(playlistManager){
        note.receiver = (PlaylistManager *) playlistManager;
        playlistManager->deviceEvtNotified(&note);
    }
    return;
}
#ifdef USER_TASK
static int check_sdcard_state(void)
{
    int ret = 0;
    DIR* dir = opendir("/sdcard");
	
    if(dir){
        struct dirent* de = readdir(dir);
        //LOG_INFO("open sdcard dir success\n");
        if(de){
#if SD_CARD_CHECK_PRINT
            while(true){
                if (!de) {
                    break;
                }
                LOG_INFO("file name %s\n", de->d_name);
                de = readdir(dir);
            }
#endif
            LOG_INFO("read sdcard dir success\n");
        }else{
            ret = -1;
            LOG_ERR("can not read sdcard dir\n");            
        }
		closedir(dir);
    }else{
        ret = -1;
        LOG_ERR("can not open sdcard dir\n");
    }

    return ret;
}
#else
static int check_sdcard_state(void)
{
    int ret = 0;
    DIR* dir = opendir("/sdcard/notify");
    if(dir){
		LOG_INFO("open sdcard dir success\n");
		closedir(dir);
    }else{
        ret = -1;
        LOG_ERR("can not open sdcard dir\n");
    }
    return ret;
}
#endif
static uint32_t gc_one_folder(char *in_dir, uint32_t folder_max_size)
{
    int ret = 0;
    DIR* dir;
	uint64_t total_size = 0;
	FILE *inputfile = NULL;
	struct stat statbuf;
	time_t now;
	struct tm timeinfo, timefile;

    if(!in_dir){
        LOG_ERR("in_dir parameter is NULL.\n");
        return -ESP_ERR_INVALID_ARG;
    }

    dir = opendir(in_dir);
    if(dir){
        struct dirent* de = readdir(dir);
        if(de){
            while(true){
                if (!de) {
                    break;
                }
				memset(g_file_path, 0, SD_FILE_PATH_MAX_SIZE);
				strncpy(g_file_path, in_dir, SD_FILE_PATH_MAX_SIZE);
				strcat(g_file_path, "/");
				strcat(g_file_path, de->d_name);

				inputfile = fopen(g_file_path, "r");
				if(inputfile){
					if(0 == fstat(fileno(inputfile), &statbuf)){
						total_size += statbuf.st_size;
					}else{
						LOG_ERR("fstat file (%s) failed...\n",g_file_path);
					}
					fclose(inputfile);
				}else{
					LOG_ERR("file (%s) open file failed...\n", g_file_path);
				}
                de = readdir(dir);
            }
        }else{
            ret = -ESP_ERR_INVALID_RESPONSE;
            LOG_ERR("can not read sdcard dir (%s)...\n", in_dir);            
        }
		closedir(dir);
		total_size = total_size/1024;
		LOG_INFO("file total size (%lld), folder limit (%d)...\n", total_size, folder_max_size);

		if(total_size >= folder_max_size){
			dir = opendir(in_dir);
			folder_max_size = total_size - folder_max_size/2;
            total_size = 0;
            while(true){
				de = readdir(dir);
                if (!de) {
                    break;
                }
				memset(g_file_path, 0, SD_FILE_PATH_MAX_SIZE);
				strncpy(g_file_path, in_dir, SD_FILE_PATH_MAX_SIZE);
				strcat(g_file_path, "/");
				strcat(g_file_path, de->d_name);

				inputfile = fopen(g_file_path, "r");
				if(inputfile){
					if(0 == fstat(fileno(inputfile), &statbuf)){
						fclose(inputfile);
						time(&now);
						localtime_r(&now, &timeinfo);
						localtime_r(&statbuf.st_ctime, &timefile);
						//LOG_DBG("file (%s), now: %d/%d/%d, ctime: %d/%d/%d...\n", 
                        //    g_file_path, 1900+timeinfo.tm_year, timeinfo.tm_mon, timeinfo.tm_mday,
						//	1900+timefile.tm_year, timefile.tm_mon, timefile.tm_mday);
						if((timeinfo.tm_year == timefile.tm_year) && 
							(timeinfo.tm_mon == timefile.tm_mon) &&
							(timeinfo.tm_mday == timefile.tm_mday)){
								//today's data not delete
						}else{
							total_size += statbuf.st_size;
							unlink(g_file_path);
							if(total_size >= folder_max_size){
								break;
							}
						}
					}else{
						fclose(inputfile);
						LOG_ERR("fstat file (%s) failed...\n", g_file_path);
					}
				}else{
					LOG_ERR("file (%s) open file failed...\n", g_file_path);
				}
            }
            LOG_INFO("delete total size (%lld), target size (%d)...\n", total_size, folder_max_size);
			closedir(dir);
		}
    }else{
        ret = -ESP_ERR_INVALID_RESPONSE;
        LOG_ERR("can not open sdcard dir (%s)\n", in_dir);
    }

    return (int)total_size;
}

uint32_t get_sd_free_space(uint32_t *use_space)
{
    DIR* dir;
	uint32_t _use_space = 0;
	FILE *inputfile = NULL;
	struct stat statbuf;
	char file_name[128];
	dir = opendir("/sdcard");
    if(dir){
        struct dirent* de = readdir(dir);
        if(de){
            while(true){
                if (!de) {
                    break;
                }
				memset(file_name, 0, sizeof(file_name));
				strncpy(file_name, "/sdcard", sizeof(file_name));
				strcat(file_name, "/");
				strcat(file_name, de->d_name);
				stat(file_name,&statbuf);
				if (S_ISDIR(statbuf.st_mode)){
					//LOG_ERR("file (%s) is directory\n", file_name);
					_use_space +=gc_one_folder(file_name,sd_total_size);
				}else{
					//LOG_ERR("file (%s) is file\n", file_name);
					inputfile = fopen(file_name, "r");
					if(inputfile){
						if(0 == fstat(fileno(inputfile), &statbuf)){
							//LOG_INFO("file_size = %d\n",(int)statbuf.st_size/1024);
							_use_space += (int)statbuf.st_size/1024+1;
						}else{
							LOG_ERR("fstat file (%s) failed...\n",file_name);
						}
						fclose(inputfile);
					}else{
						LOG_ERR("file (%s) open file failed...\n", file_name);
					}
				}
				
                de = readdir(dir);
            }
        }
		closedir(dir);
    }
	*use_space = _use_space;
	LOG_INFO("sd use space = %d total space = %d\n",_use_space,sd_total_size);
	return sd_total_size;

}

static void sd_card_state_update(sdcard_state_t new)
{
    if(g_sd_state == new){
        LOG_DBG("sd card state remain %d\n", new);
    }else{
        LOG_DBG("sd card state update prev %d new %d\n", g_sd_state, new);
        g_sd_state = new;
        notify_media_sd_state(notify_fd);
    }
}
static void gc_process(void)
{
    //process garbage follow config
    gc_one_folder("/sdcard/wechat", 512*1024);//wechat folder max size 512MB
}

#ifdef USER_TASK
static void gc_tick_cb(TimerHandle_t xTimer)
{
    gc_command_t cmd = GC_CMD_TIMEOUT;
	xQueueSend(timer_queue, &cmd, 100);
}
static void gc_task(void* varg)
{
    static int retry_count = 0;

	xSemaphoreTake(task_exit, portMAX_DELAY);
    while(task_need_run){
        gc_command_t cmd;
        if(pdTRUE == xQueueReceive(timer_queue, &cmd, portMAX_DELAY)){
            LOG_DBG("receive cmd %d, sd card state %d\n", cmd, g_sd_state);
            switch(g_sd_state){
                case SD_CARD_ST_UNMOUNT:
                    if(retry_count++ < SD_MOUNT_RETRY_MAX){
                        if(0 == sd_card_mount()){
                            sd_card_state_update(SD_CARD_ST_MOUNTED);
                            LOG_INFO("mount sd card success\n");
                        }else{   
                            LOG_ERR("mount sd card failed, retry\n"); 
                        }
                        xTimerChangePeriod(xTimerUser, pdMS_TO_TICKS(MOUNT_SD_PERIOD), 100);
                    }else{
                        LOG_ERR("mount sd card exceed limit\n");
                    }
                    break;
                case SD_CARD_ST_MOUNTED:
                    if(check_sdcard_state()){
                        LOG_ERR("check sd card failed, remount it\n");
                        sd_card_state_update(SD_CARD_ST_UNMOUNT);
                        sd_card_unmount();
                        retry_count = 0;
                        xTimerChangePeriod(xTimerUser, pdMS_TO_TICKS(MOUNT_SD_PERIOD), 100);
                    }else{
                        LOG_INFO("check sd card success\n");
                        gc_process();
                        xTimerChangePeriod(xTimerUser, pdMS_TO_TICKS(MOUNT_SD_PERIOD), 100);
                    }
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
#else
void gc_check_state_cb(void *param)
{
	LOG_DBG(" sd card state %d call_count %d peroid %d\n", g_sd_state,call_count,period_time);
	
	if(call_count++ != period_time)
		return ;
	switch(g_sd_state){
        case SD_CARD_ST_UNMOUNT:
            if(remount_count++ < SD_MOUNT_RETRY_MAX){
                if(0 == sd_card_mount()){
                    sd_card_state_update(SD_CARD_ST_MOUNTED);
                    LOG_INFO("mount sd card success\n");
                }else{   
                    LOG_ERR("mount sd card failed, retry\n"); 
                }
				period_time = 0;
				call_count = 0;
            }else{
                LOG_ERR("mount sd card exceed limit\n");
            }
            break;
        case SD_CARD_ST_MOUNTED:
            if(check_sdcard_state()){
                LOG_ERR("check sd card failed, remount it\n");
                sd_card_state_update(SD_CARD_ST_UNMOUNT);
                sd_card_unmount();
                remount_count = 0;
				period_time = 0;
				call_count = 0;
            }else{
                LOG_INFO("check sd card success\n");
                gc_process(); 
				period_time = 20;
				call_count = 0;
            }
            break;
        default:
            break;
    }
	return 0;
}
#endif
int register_sdcard_notify(void *fd)
{
    if(fd){//first register cb, notify it sdcard status
        notify_fd = fd;
        notify_media_sd_state((MediaControl *)notify_fd);
    }else{
        LOG_ERR("fd is NULL\n");
    }
    return 0;
}
void garbage_cleanup_init(void)
{
    LOG_INFO("enter\n");

    if(0 == sd_card_mount())   
        g_sd_state = SD_CARD_ST_MOUNTED;
	#ifdef USER_TASK
    timer_queue = xQueueCreate(10, sizeof(gc_command_t));
    task_exit = xSemaphoreCreateMutex();
    xTimerUser = xTimerCreate("gc_tick", pdMS_TO_TICKS(MOUNT_SD_PERIOD), pdFALSE, NULL, gc_tick_cb);
	if(!xTimerUser) {
        vSemaphoreDelete(task_exit);
        vQueueDelete(timer_queue);
        sd_card_unmount();
		LOG_ERR("creat xtimer fail\n");
		return ESP_ERR_INVALID_RESPONSE;
    }

    task_need_run = true;
    xTaskCreate(gc_task, "gc_task", 3072, NULL, 2, NULL);
    xTimerStart(xTimerUser, 100);
	#else
	register_poll_func(&gc_config);
	#endif
	LOG_INFO("exit\n");
}

void garbage_cleanup_uninit(void)
{ 
	#ifdef USER_TASK
    gc_command_t cmd = GC_CMD_UNDEFINE;

    LOG_INFO("enter\n");
	
    task_need_run = false;
    xTimerStop(xTimerUser, portMAX_DELAY);
    xQueueSend(timer_queue, &cmd, 100);
    xSemaphoreTake(task_exit, portMAX_DELAY);
    sd_card_unmount();
    vQueueDelete(timer_queue);
    vSemaphoreDelete(task_exit);
	#else
	unregister_poll_func(&gc_config);
	#endif
    LOG_INFO("exit\n");
    return 0;
}

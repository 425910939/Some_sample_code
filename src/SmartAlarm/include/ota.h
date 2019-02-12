#ifndef __OTA_H__
#define __OTA_H__
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"

#define NORMAL_STARTUP          (0)
#define DOWNLOAD_BIN_FINISH     (1)
#define ALREADY_WRITE_IN_FLASH  (2)
#define CHECK_FLASH_IS_RIGHT    (4)
#define FACTORY_MODE_STARTUP    (5)

#define MAX_RETRY (3)
#define BIN_FILE_PATH "/sdcard/"
#define UPGRADE_STEP_FILE_NAME "/sdcard/upgrade_step"

typedef struct{
    uint8_t upgrade_step;//0 is startup normal ; 1 is ota.bin download and crc pass; 2 is write in flash
    uint8_t retry;
    uint8_t before_step_status;
    char upgrade_file_name[50];
    uint32_t upgrade_file_size;
}upgrade_step_info_t;

void ota_net_monitor_init();
int start_to_upgrade(char * file_path,uint32_t file_size);
int get_upgrade_step(upgrade_step_info_t * p_step_info);
int set_upgrade_file(upgrade_step_info_t * p_step_info);
int check_write_in_flase_error();
int change_boot_partition();
void delete_all_upgrade_file();
void doing_ota_upgrade(upgrade_step_info_t * p_step_info);
void running_partition();
#endif

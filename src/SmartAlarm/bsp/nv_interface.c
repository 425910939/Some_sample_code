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
#include "nv_interface.h"
#include "log.h"

#define LOG_TAG		"nvitem"
#define NV_NAMESPACE "littleP"

int get_nv_item(nv_item_t *item)
{
    nvs_handle my_handle;
    int err = 0;

    if(!item){
        LOG_ERR("item is NULL pointer\n");
        return -ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NV_NAMESPACE, NVS_READWRITE, &my_handle);
	if(err != ESP_OK){
	  LOG_ERR("open nv space failed.\n");
	  return -ESP_ERR_INVALID_RESPONSE;
  	}

    switch(item->name){
        case NV_ITEM_AUDIO_VOLUME:
            err = nvs_get_i64(my_handle, "audio_volume", &(item->value));
            break;
        case NV_ITEM_SYSTEM_PANIC_COUNT:
            err = nvs_get_i64(my_handle, "panic_count", &(item->value));
            break;
		case NV_ITEM_POWER_ON_MODE:
			err = nvs_get_i64(my_handle, "power_on", &(item->value));
			break;	
        default:
            LOG_ERR("unsupport nv type %d...\n", item->name);
            err = -ESP_ERR_INVALID_ARG;
            goto out;      
    }

out:
    nvs_close(my_handle);
    return err;
}

int set_nv_item(nv_item_t *item)
{
    nvs_handle my_handle;
    esp_err_t err;

    if(!item){
        LOG_ERR("item is NULL pointer\n");
        return -ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NV_NAMESPACE, NVS_READWRITE, &my_handle);
    if(err != ESP_OK){
		LOG_ERR("open nv space failed.\n");
		return -ESP_ERR_INVALID_RESPONSE;
	}

    switch(item->name){
        case NV_ITEM_AUDIO_VOLUME:
            err = nvs_set_i64(my_handle, "audio_volume", item->value);
            break;
        case NV_ITEM_SYSTEM_PANIC_COUNT:
            err = nvs_set_i64(my_handle, "panic_count", item->value);
            break;
		case NV_ITEM_POWER_ON_MODE:
			err = nvs_set_i64(my_handle, "power_on", item->value);
            break;
        default:
            LOG_ERR("unsupport nv type %d...\n", item->name);
            err = -ESP_ERR_INVALID_ARG;
            goto out;      
    }

    if(err != ESP_OK){
		LOG_ERR("set nv item %d failed.\n", item->name);
		goto out;
	}

    err = nvs_commit(my_handle);
    if(err != ESP_OK)
		LOG_ERR("commit nv item %d failed.\n", item->name);

out:
    nvs_close(my_handle);
    return err;
}

int nv_init(){
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // If this happens, we erase NVS partition and initialize NVS again.
        const esp_partition_t* nvs_partition = esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
        assert(nvs_partition && "partition table must have an NVS partition");
        ESP_ERROR_CHECK( esp_partition_erase_range(nvs_partition, 0, nvs_partition->size) );
        esp_restart();
        return -1;
    }
    ESP_ERROR_CHECK( err );
    return 0;
 }
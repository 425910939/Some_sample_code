/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_smartconfig.h"
#include "log.h"
#include "keyevent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "audiomanagerservice.h"
#include "restore.h"
#include "esp_pm.h"
#include "remote_command.h"

#pragma pack(1)
typedef struct {
    bool    sta_connected;
    uint8_t sta_bssid[6];
    uint8_t sta_ssid[48];
    int     sta_ssid_len;
} wifi_current_status_t;

typedef struct {
    uint8_t sta_ssid[48];
    uint8_t sta_pswd[32];
} wifi_saved_ssid_pswd_t;

typedef struct {
    uint8_t sta_ssid[48];
    uint8_t sta_pswd[32];
    uint8_t phone_ip[4];
    uint8_t self_ip[4];
} wifi_temp_config_info_t;

typedef enum WIFI_MANAGE_STATUS
{
	WIFI_STATUS_NONE = 0,
	WIFI_STATUS_DISCONNECT,
	WIFI_STATUS_STARTCONNECT,
	WIFI_STATUS_PLEASECONNECT,
	WIFI_STATUS_CONNECT,
	WIFI_STATUS_SMARTCONFIGFAIL,
	WIFI_STATUS_SMARTCONFIG,
}WIFI_MANAGE_STATUS_T;

typedef struct{
    uint8_t start_esp_touch;
	WIFI_MANAGE_STATUS_T wifi_status;
}manage_connect_info_t;
#pragma pack()

#define SAVED_FILE_PATH "/sdcard/wifi_info"
#define SSID_TAG "ssid="
#define PSWD_TAG "password="
#define LOG_TAG		"CM"
#define RETRY_COUNT    (10)
#define ERROR_PASSWORD (2)
#define LOST_SOFTAP    (3)
#define NOT_FOUND_AP   (201)
#define SMARTCONFIG_TIME_OUT  (90*1000)	//1.5minutes
/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;
const int ESPTOUCH_DONE_BIT = BIT1;
static wifi_current_status_t wifi_sta_current_status = {0};
static wifi_saved_ssid_pswd_t wifi_saved_info = {0};
static wifi_temp_config_info_t temp_config_info ={0};
static keycode_client_t *kclient = NULL;
static bool start_esp_touch = false;
xQueueHandle manage_connect_info_queue;
static uint8_t error_reason_count = 0; 

void smartconfig_start_task(void * parm);
void get_storage_wifi_ssid_pswd();

int8_t get_connect_rssi()
{
    wifi_ap_record_t ap_info = {0};
    
    if(wifi_sta_current_status.sta_connected == false)
        return -128;
    
    if(esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK)
        return -128;

    return ap_info.rssi;
}
static int wifi_reconnection(void)
{
	esp_err_t err_ret = 0;
	wifi_config_t wifi_config = {0};
	get_storage_wifi_ssid_pswd();
	memcpy(wifi_config.sta.ssid,wifi_saved_info.sta_ssid,strlen((char *)(wifi_saved_info.sta_ssid)));
	memcpy(wifi_config.sta.password,wifi_saved_info.sta_pswd,strlen((char *)(wifi_saved_info.sta_pswd)));
	err_ret = esp_wifi_disconnect();
	err_ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
	err_ret = esp_wifi_connect();
	return err_ret;
}

static esp_err_t wifi_net_event_handler(void *ctx, system_event_t *event)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
	manage_connect_info_t req_buff = {0};
	
    LOG_DBG("wifi_net_event_handeler %d\n",event->event_id);

    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:{
            LOG_DBG("STA START\n");

            if((strlen((char *)(wifi_saved_info.sta_ssid)) > 0) && start_esp_touch == false){
                wifi_config_t wifi_config = {0};

                memcpy(wifi_config.sta.ssid,wifi_saved_info.sta_ssid,strlen((char *)(wifi_saved_info.sta_ssid)));
                memcpy(wifi_config.sta.password,wifi_saved_info.sta_pswd,strlen((char *)(wifi_saved_info.sta_pswd)));

                ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
				esp_wifi_connect();
				req_buff.start_esp_touch = 0;
				req_buff.wifi_status = WIFI_STATUS_STARTCONNECT;
				if(xQueueSend(manage_connect_info_queue, &req_buff, portMAX_DELAY) != pdTRUE){
					LOG_DBG("malloc manage connet Q failed\n");
					break;
				}
            }/*else if(start_esp_touch == true){
                LOG_DBG("use esp touch connect\n");
				req_buff.start_esp_touch = 0;
				req_buff.wifi_status = WIFI_STATUS_SMARTCONFIG;
				if(xQueueSend(manage_connect_info_queue, &req_buff, portMAX_DELAY) != pdTRUE){
					LOG_DBG("malloc manage connet Q failed\n");
					break;
				}
                xTaskCreate(smartconfig_start_task, "sc_connect", 4096, NULL, 3, NULL);
            }*/else{
            	if((start_esp_touch == false) && (strlen((char *)(wifi_saved_info.sta_ssid)) == 0)){
	            	req_buff.start_esp_touch = 0;
					req_buff.wifi_status = WIFI_STATUS_PLEASECONNECT;
					if(xQueueSend(manage_connect_info_queue, &req_buff, portMAX_DELAY) != pdTRUE){
						LOG_DBG("malloc manage connet Q failed\n");
						break;
					}
	                LOG_DBG("maybe first start\n");
            	}
            }
            error_reason_count = 0; 
            break;
        }
        case SYSTEM_EVENT_STA_GOT_IP:
			wifi_sta_current_status.sta_connected = true;
			xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            LOG_DBG("wifi_net Got IP\n");
			req_buff.start_esp_touch = 0;
			req_buff.wifi_status = WIFI_STATUS_CONNECT;
			if(xQueueSend(manage_connect_info_queue, &req_buff, portMAX_DELAY) != pdTRUE){
				LOG_DBG("malloc manage connet Q failed\n");
				break;
			}
			set_server_connect_status(0);
            error_reason_count = 0; 
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            memcpy(wifi_sta_current_status.sta_bssid, event->event_info.connected.bssid, 6);
            memcpy(wifi_sta_current_status.sta_ssid, event->event_info.connected.ssid, event->event_info.connected.ssid_len);
            wifi_sta_current_status.sta_ssid_len = event->event_info.connected.ssid_len;
            error_reason_count = 0; 
            break; 
        case SYSTEM_EVENT_STA_DISCONNECTED:
			if(wifi_sta_current_status.sta_connected == true)
				set_server_connect_status(0);
            wifi_sta_current_status.sta_connected = false;
            memset(wifi_sta_current_status.sta_ssid, 0, 32);
            memset(wifi_sta_current_status.sta_bssid, 0, 6);
            wifi_sta_current_status.sta_ssid_len = 0;
            LOG_INFO("disconnect error reason %d\n\n",event->event_info.disconnected.reason);
            if(error_reason_count == RETRY_COUNT){
                LOG_INFO("retry count is many time\n"); 
                if(start_esp_touch == true){
                    xEventGroupSetBits(wifi_event_group, ESPTOUCH_DONE_BIT);
                    LOG_INFO("releas smart config\n");
                }
				req_buff.start_esp_touch = 0;
				req_buff.wifi_status = WIFI_STATUS_DISCONNECT;
				if(xQueueSend(manage_connect_info_queue, &req_buff, portMAX_DELAY) != pdTRUE){
					LOG_DBG("malloc manage connet Q failed\n");
					break;
				}
            }else{
                if(event->event_info.disconnected.reason == ERROR_PASSWORD){
                    LOG_INFO("error password\n");
                }else if(event->event_info.disconnected.reason == LOST_SOFTAP){
                    LOG_INFO("disconnect ap \n");
                }else if(event->event_info.disconnected.reason == NOT_FOUND_AP){
                    LOG_INFO("not found ap\n");
                }else{
                    LOG_INFO("you network error\n");
                } 
            }
			error_reason_count ++;
			if(start_esp_touch != true)
				wifi_reconnection();
			xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_STOP:
            wifi_sta_current_status.sta_connected = false;
            memset(wifi_sta_current_status.sta_ssid, 0, 32);
            memset(wifi_sta_current_status.sta_bssid, 0, 6);
            wifi_sta_current_status.sta_ssid_len = 0;
            memset(&wifi_saved_info, 0, sizeof(wifi_saved_ssid_pswd_t));
            //memset(&temp_config_info, 0, sizeof(wifi_temp_config_info_t));
            error_reason_count = 0; 
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

bool wifi_connect_status()
{
    return wifi_sta_current_status.sta_connected;
}

static void initialise_wifi(void)
{
    LOG_DBG("initialise_wifi\n");

	#if CONFIG_PM_ENABLE
    // Configure dynamic frequency scaling: maximum frequency is set in sdkconfig,
    // minimum frequency is XTAL.
    rtc_cpu_freq_t max_freq;
    rtc_clk_cpu_freq_from_mhz(CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ, &max_freq);
    esp_pm_config_esp32_t pm_config = {
            .max_cpu_freq = max_freq,
            .min_cpu_freq = RTC_CPU_FREQ_XTAL
    };
    ESP_ERROR_CHECK( esp_pm_configure(&pm_config) );
	#endif // CONFIG_PM_ENABLE

    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(wifi_net_event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_FLASH) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    esp_wifi_set_ps(WIFI_PS_MODEM);
}

static void sc_callback(smartconfig_status_t status, void *pdata)
{
    switch (status) {
        case SC_STATUS_WAIT:
            LOG_DBG("SC_STATUS_WAIT\n");
            break;
        case SC_STATUS_FIND_CHANNEL:
            LOG_DBG("SC_STATUS_FINDING_CHANNEL\n");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            LOG_DBG("SC_STATUS_GETTING_SSID_PSWD\n");
            break;
        case SC_STATUS_LINK:
            LOG_DBG("SC_STATUS_LINK");
            wifi_config_t *wifi_config = pdata;
            LOG_DBG("SSID:%s\n", wifi_config->sta.ssid);
            LOG_DBG("PASSWORD:%s\n", wifi_config->sta.password);
			memset(&temp_config_info, 0, sizeof(wifi_temp_config_info_t));
            memcpy(temp_config_info.sta_ssid,wifi_config->sta.ssid,strlen((char *)(wifi_config->sta.ssid)));
            temp_config_info.sta_ssid[strlen((char *)(wifi_config->sta.ssid))] = 0;
            memcpy(temp_config_info.sta_pswd,wifi_config->sta.password,strlen((char *)(wifi_config->sta.password)));
            temp_config_info.sta_pswd[strlen((char *)(wifi_config->sta.password))] = 0;
            ESP_ERROR_CHECK( esp_wifi_disconnect() );
			esp_wifi_restore();
			ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
			esp_wifi_start();
		    esp_wifi_set_ps(WIFI_PS_MODEM);
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_connect() );
            break;
        case SC_STATUS_LINK_OVER:
            LOG_DBG("SC_STATUS_LINK_OVER");
            if (pdata != NULL) {
                uint8_t phone_ip[4] = { 0 };
                memcpy(phone_ip, (uint8_t* )pdata, 4);
                memcpy(temp_config_info.phone_ip,phone_ip,4);
                LOG_DBG("Phone ip: %d.%d.%d.%d\n", phone_ip[0], phone_ip[1], phone_ip[2], phone_ip[3]);
            }
            xEventGroupSetBits(wifi_event_group, ESPTOUCH_DONE_BIT);
            break;
        default:
            break;
    }
}

void save_wifi_ssid_pswd()
{
    FILE *fp = fopen(SAVED_FILE_PATH,"wb+");
    char write_buff[128]={0};

    if(fp == NULL){
        LOG_DBG("Can not open wifi ssid password file\n");
        return ;
    }

    sprintf(write_buff, SSID_TAG"%s\r\n"PSWD_TAG"%s\r\n",temp_config_info.sta_ssid,temp_config_info.sta_pswd);
	LOG_DBG("save_wifi_ssid_pswd = %s",write_buff);
    if(fwrite(write_buff,strlen(write_buff),1,fp) != 1){
        LOG_DBG("save ssid & pswd failed");
    }

    fclose(fp);
}

void smartconfig_start_task(void * parm)
{
    EventBits_t uxBits;
	ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) ); 
	ESP_ERROR_CHECK(esp_smartconfig_start(sc_callback) );
    while (1) {
        uxBits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, pdMS_TO_TICKS(SMARTCONFIG_TIME_OUT)); 
		if(uxBits & CONNECTED_BIT) {
            LOG_DBG("WiFi Connected to ap\n");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            LOG_DBG("smartconfig over\n");
            esp_smartconfig_stop();
            if(wifi_sta_current_status.sta_connected == true){
                save_wifi_ssid_pswd();
                LOG_INFO("smart config finish\n");
            }
            start_esp_touch = false;
            vTaskDelete(NULL);
        }
		if(uxBits == 0){	
			LOG_INFO("smartconfig time out\n");
			esp_smartconfig_stop();
			manage_connect_info_t req_buff = {0};
			req_buff.start_esp_touch = 0;
			req_buff.wifi_status = WIFI_STATUS_SMARTCONFIGFAIL;
			if(xQueueSend(manage_connect_info_queue, &req_buff, portMAX_DELAY) != pdTRUE){	
				LOG_DBG("malloc manage connet Q failed\n");
			}
			start_esp_touch = false;
			error_reason_count = 0;
			vTaskDelay(6000 / portTICK_PERIOD_MS);
			esp_wifi_restore();
			ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
			esp_wifi_start();
		    esp_wifi_set_ps(WIFI_PS_MODEM);
			if(wifi_reconnection() != ESP_OK){
				LOG_INFO("wifi reconnect fail\n");
			}
       		vTaskDelete(NULL);
		}
    }
}

int8_t get_storage_value(char *tag, FILE *fp, uint8_t* set_value,uint8_t limit_cpy_len)
{
    int  ret = 0;
    char *p_start = NULL;
    char *p_end = NULL;
    char read_buff[128] = {0};

    if((ret = fgets(read_buff,sizeof(read_buff),fp)) > 0){
        p_start = strstr(read_buff,tag);
        if (p_start != NULL){ 
            p_start += strlen(tag); 
            p_end = strstr(p_start,"\r\n");
            if(p_end != NULL){
                if((p_end - p_start) < limit_cpy_len){
                    memcpy(set_value,p_start,(p_end - p_start));
                    return 0;
                }else{
                    LOG_DBG("%s %d is so long\n",tag,(p_end - p_start));
                    return -1;
                }
            }else{
                LOG_DBG("%s not find end tag \n",tag);
                return -1;
            }
        }else{
            LOG_DBG("%s not find start tag \n",tag);
            return -1;
        } 
    }
    
    LOG_DBG("%s read file failure \n",tag);

    return -1;
}

void get_storage_wifi_ssid_pswd()
{
    int count = 2;
    FILE *fp = NULL;
 
    do{
        fp = fopen(SAVED_FILE_PATH,"rb+");
        if(fp)
            break;
        LOG_DBG("open wifi ssid password file retry count %d...\n", count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }while(--count > 0);
     
    
    if(fp == NULL){
        LOG_DBG("Can not open wifi ssid password file\n");
        return ;
    }

    if(get_storage_value(SSID_TAG,fp,wifi_saved_info.sta_ssid,sizeof(wifi_saved_info.sta_ssid)) == -1){
        LOG_DBG("Get SSID Failed\n");
        fclose(fp);
        return ;
    }
        
    if(get_storage_value(PSWD_TAG,fp,wifi_saved_info.sta_pswd,sizeof(wifi_saved_info.sta_pswd)) == -1){
        LOG_DBG("Get PSWD Failed\n");
        memset(&wifi_saved_info,0,sizeof(wifi_saved_info));
        fclose(fp);
        return ;
    }

    LOG_DBG("Get SSID %s & PASSWORD %s\n",wifi_saved_info.sta_ssid,wifi_saved_info.sta_pswd);

    fclose(fp);
}

static char sta_mac_addr[6] = {0};

char * wifi_sta_mac_add(void)
{
    if(((int *)sta_mac_addr) == 0)
        return  NULL;

    return sta_mac_addr;
}

static void get_wifi_sta_addr(void)
{
    esp_read_mac((uint8_t *)sta_mac_addr,ESP_MAC_WIFI_STA);
    LOG_DBG("Get mac addr %x,%x,%x,%x,%x,%x\n",sta_mac_addr[0],sta_mac_addr[1],sta_mac_addr[2],sta_mac_addr[3],sta_mac_addr[4],sta_mac_addr[5]);
}

static long esp_touch_abs(long a, long b)
{
    if(a > b){
        return (a-b);
    }else{
        return (b-a);
    }
}

#ifdef CONFIG_USE_ESP32_LYRAT_BOARD //use esp32 lyrat board
static keyprocess_t esp_touch_keyprocess(keyevent_t *event)
{
  	switch(event->code){
		case KEY_CODE_START_ESP_TOUCH:
			if(KEY_EVNET_PRESS == event->type)
			{
                if(start_esp_touch == false){
                    manage_connect_info_t req_buff = {0};
                    req_buff.start_esp_touch = 1;
                    start_esp_touch = true;
                    if(xQueueSend(manage_connect_info_queue, &req_buff, portMAX_DELAY) != pdTRUE){
                        LOG_DBG("malloc manage connet Q failed\n");
                    }else{
                        LOG_DBG("smart config start...\n");
                    }
                }
            }
            break;
		default:
			break;
	}
    return KEY_PROCESS_PUBLIC;
}
#else//use self board
static keyprocess_t esp_touch_keyprocess(keyevent_t *event)
{
    static struct timeval timestamp[2] = {{0,0}, {0,0}};
    long threshold;

	LOG_DBG("receive key event %d type %d\n", event->code, event->type);

    if(get_factory_mode() == true){
        LOG_INFO("factory mode\n");
        return KEY_PROCESS_PUBLIC;
    }
    
	switch(event->code){
		case KEY_CODE_VOL_UP:
			if(KEY_EVNET_PRESS == event->type)
			{
                memcpy(&timestamp[0], &event->timestamp, sizeof(struct timeval));
                threshold = esp_touch_abs((long)(timestamp[0].tv_sec*1000+timestamp[0].tv_usec/1000),
                    (long)(timestamp[1].tv_sec*1000+timestamp[1].tv_usec/1000));
                if((threshold < 200) && (start_esp_touch == false)){
                    manage_connect_info_t req_buff = {0};
                    req_buff.start_esp_touch = 1;
                    start_esp_touch = true;
                    if(xQueueSend(manage_connect_info_queue, &req_buff, portMAX_DELAY) != pdTRUE){
                        LOG_DBG("malloc manage connet Q failed\n");
                        return -1;
                    }
                    LOG_DBG("smart_connect_init key long press!\n");
                }
            }else{
                timestamp[0].tv_sec = 0;
                timestamp[0].tv_usec = 0;
            }
            break;
        case KEY_CODE_VOL_DOWN:
            if(KEY_EVNET_PRESS == event->type) 
            {
                memcpy(&timestamp[1], &event->timestamp, sizeof(struct timeval));
                threshold = esp_touch_abs((long)(timestamp[0].tv_sec*1000+timestamp[0].tv_usec/1000),
                    (long)(timestamp[1].tv_sec*1000+timestamp[1].tv_usec/1000));
				if((threshold < 200) && (start_esp_touch == false)){
                    manage_connect_info_t req_buff = {0};
                    req_buff.start_esp_touch = 1;
                    start_esp_touch = true;
                    if(xQueueSend(manage_connect_info_queue, &req_buff, portMAX_DELAY) != pdTRUE){
                        LOG_DBG("malloc manage connet Q failed\n");
                        return -1;
                    }
                    LOG_DBG("smart_connect_init key long press!\n");
                }
            }else{
                timestamp[1].tv_sec = 0;
                timestamp[1].tv_usec = 0;
            }
            break;
		default:
			break;
	}
    return KEY_PROCESS_PUBLIC;
}
#endif

static void manager_connect_task(void *pvParameters)
{
    while(1)
    {
        manage_connect_info_t req_buff = {0};//portMAX_DELAY
        if(xQueueReceive(manage_connect_info_queue, &req_buff, portMAX_DELAY) == pdPASS){
			switch(req_buff.wifi_status){
				case WIFI_STATUS_DISCONNECT:
					play_tone_and_restart(NOTIFY_AUDIO_WIFI_CONNECT_FAIL);
					break;
				case WIFI_STATUS_CONNECT:
					play_tone_and_restart(NOTIFY_AUDIO_WIFI_CONNECT_FINISH);
					break;
				case WIFI_STATUS_SMARTCONFIG:
					play_tone_sync(NOTIFY_AUDIO_WIFI_SET_BY_PHONE);
					break;
				case WIFI_STATUS_STARTCONNECT:
					play_tone_and_restart(NOTIFY_AUDIO_WIFI_CONNECT_START);
					break;
				case WIFI_STATUS_PLEASECONNECT:
					play_tone_and_restart(NOTIFY_AUDIO_CONNECT_PLEASE);
					break;
				case WIFI_STATUS_SMARTCONFIGFAIL:
					play_tone_and_restart(NOTIFY_AUDIO_MATCHES_FAIL);
					break;
				default:
					#if 0
					if(req_buff.start_esp_touch == 1){
						wifi_sta_current_status.sta_connected = false;
						esp_wifi_disconnect();
						vTaskDelay(1000/portTICK_PERIOD_MS);
						esp_wifi_stop();
					}
					vTaskDelay(1000 / portTICK_PERIOD_MS);
					esp_wifi_start();
					esp_wifi_set_ps(WIFI_PS_MODEM);
					#else
					if(req_buff.start_esp_touch == 1){
						wifi_sta_current_status.sta_connected = false;
						esp_wifi_disconnect();
						LOG_DBG("use esp touch connect\n");
						req_buff.start_esp_touch = 0;
						req_buff.wifi_status = WIFI_STATUS_SMARTCONFIG;
						if(xQueueSend(manage_connect_info_queue, &req_buff, portMAX_DELAY) != pdTRUE){
							LOG_DBG("malloc manage connet Q failed\n");
							break;
						}
		                xTaskCreate(smartconfig_start_task, "sc_connect", 4096, NULL, 3, NULL);
					}
					#endif
					break;
			}
		}
    }
}

int manager_connect_task_init(void)
{	
    manage_connect_info_queue = xQueueCreate(5, sizeof(manage_connect_info_t));

    if(manage_connect_info_queue == 0){
        LOG_DBG("manage_connect_info_queue error\n");
        return -1;
    }

    return 0;
}

void manager_connect_task_uninit(void)
{
    vQueueDelete(manage_connect_info_queue);
}

void smart_connect_unint()
{
    if(keyevent_unregister_listener(kclient) != 0)
        LOG_DBG("release key event error\n");
}

void smart_connect_init()
{

#ifdef CONFIG_USE_ESP32_LYRAT_BOARD //use esp32 lyrat board
    kclient = keyevent_register_listener(KEY_CODE_START_ESP_TOUCH_MASK, esp_touch_keyprocess);
	if(!kclient){
		LOG_ERR("register key client fail\n");
    }
#else //use self board
    kclient = keyevent_register_listener(KEY_CODE_VOL_UP_MASK|KEY_CODE_VOL_DOWN_MASK, esp_touch_keyprocess);
	if(!kclient){
		LOG_ERR("register key client fail\n");
    }
#endif

    get_wifi_sta_addr();

    if(get_factory_mode() == true){
        memcpy(wifi_saved_info.sta_ssid,FACTORY_WIFI_SSID,sizeof(FACTORY_WIFI_SSID));
        memcpy(wifi_saved_info.sta_pswd,FACTORY_WIFI_PSWD,sizeof(FACTORY_WIFI_PSWD));
        LOG_INFO("In factory mode and ssid: %s ... password %s\n",wifi_saved_info.sta_ssid,wifi_saved_info.sta_pswd);
    }else{
        get_storage_wifi_ssid_pswd();
    }
    
    manager_connect_task_init();

    if(xTaskCreate(&manager_connect_task, "manager_con_task", 4608, NULL, 5, NULL) != pdPASS){
        manager_connect_task_uninit();
        LOG_ERR("--**--download task startup failed\n");
    }

    initialise_wifi();
}

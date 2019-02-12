#ifndef __CONNECT_MANAGER_H__
#define __CONNECT_MANAGER_H__
#include "freertos/event_groups.h"

extern EventGroupHandle_t wifi_event_group;
extern int CONNECTED_BIT;

/*此函数用于返回当前WiFi的作为STA模式与AP的连接状态
False: 未连接/断开
true:  已经连接成功
*/
#if defined(CONFIG_AIRKISS_TO_CONNECT_WIFI) || defined(CONFIG_BT_TO_CONNECT_WIFI) || defined(CONFIG_ESP_TOUCH_TO_CONNECT_WIFI)
bool wifi_connect_status();
#else
#define wifi_connect_status()  (false)
#endif

/*
此函数用于WiFi BT连接设置后台管理任务
*/
#ifdef CONFIG_BT_TO_CONNECT_WIFI
void connect_manager_init();
#endif

#ifdef CONFIG_AIRKISS_TO_CONNECT_WIFI
void airkiss_init_main();
#endif

#ifdef CONFIG_ESP_TOUCH_TO_CONNECT_WIFI
void smart_connect_init();
char * wifi_sta_mac_add(void);
int8_t get_connect_rssi();
#endif

#endif

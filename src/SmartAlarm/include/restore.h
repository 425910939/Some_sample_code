#ifndef __RESTORE_H_
#define __RESTORE_H_

#define FACTORY_WIFI_SSID  "onego_factory_ssid"
#define FACTORY_WIFI_PSWD  "12345678"

bool get_factory_mode();
void set_factory_mode(bool mode);

int restore_init(void);
int restore_uninit(void);
#endif
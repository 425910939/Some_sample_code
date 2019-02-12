#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_shell.h"

#include "terminalcontrolservice.h"
#include "connect_manager.h"
#include "version.h"

#include "log.h"


static void test(void *ref, int argc, char *argv[])
{
    LOG_ERR("test ok!\n");
}

static void reboot(void *ref, int argc, char *argv[])
{
    system_restart();
}

static void getsoftversion(void *ref, int argc, char *argv[])
{
    LOG_ERR("current_software_version: %s\n",SOFTWARE_VERSION);
}

static void getwifimac(void *ref, int argc, char *argv[]){

    unsigned char *p = (unsigned char *)wifi_sta_mac_add();
    char mac[18] = {0};

    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", p[0],p[1],p[2],p[3],p[4],p[5]);

    LOG_INFO("current_wifi_mac: %s\n",mac);
}

const ShellCommand command[] = {
    {"test", test},
    {"reboot", reboot},
    {"softversion",getsoftversion},
    {"wifimac",getwifimac},
    {NULL, NULL}
};

void terminalCtlActive()
{
    shell_init(command, NULL);
}

void terminalCtlDeactive()
{
    shell_stop();
    LOG_ERR("Terminal Stop\n");
}

void start_terminalctl_srv()
{
    terminalCtlActive();
    LOG_ERR("Terminal Starts\n");
}


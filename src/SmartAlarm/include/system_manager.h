#ifndef __SYSTEM_MANAGER_H__
#define __SYSTEM_MANAGER_H__
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

typedef enum{
    SYS_CMD_POWERON_HANDSHAKE = 0x01,
    SYS_CMD_HEARTBEAT_HANDSHAKE,
    SYS_CMD_DEVICE_STATUS_FIRST,
    SYS_CMD_DEVICE_STATUS,
    SYS_CMD_FACTORY_RESET,
    SYS_CMD_UNDEFINE,
}system_cmd_t;

typedef struct{
    system_cmd_t command;
    uint16_t size;
    void *param;
}system_notify_t;

typedef struct{
    bool first;
    bool volume;
    bool song;
	uint32_t song_time;
}device_notify_t;
int restore_gactory_setting(void);
int send_device_status(bool _first,bool _song,bool _volume,uint32_t _song_time);
void force_handshake_with_server(void);
int system_manager_init(void);
int system_manager_uninit(void);
#endif


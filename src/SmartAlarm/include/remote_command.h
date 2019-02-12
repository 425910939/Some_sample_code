#ifndef __REMOTE_COMMAND_H__                                                                                                                        
#define __REMOTE_COMMAND_H__
#include "cJSON.h"

#define REGISTER_TASK_SIZE 10
#define DEVICE_SN_LEN (15)
#define CRC_LEN (0)
#define CMD_HEADER_LEN (19 + CRC_LEN) //SN 15 REQUEST_ID 4 CRC 1
#define PACKET_HEADER_LEN (6) //TYPE LEN 2 CMD LEN 2

//#define SERVER_TEST


#define BSWAP_32(x) \
    (uint32_t)((((uint32_t)(x) & 0xff000000) >> 24) | \
              (((uint32_t)(x) & 0x00ff0000) >> 8) | \
              (((uint32_t)(x) & 0x0000ff00) << 8) | \
              (((uint32_t)(x) & 0x000000ff) << 24)) 
              

typedef struct{
	uint32_t register_cmd_map;
	void (*callback)(void * data,int cmd_type);
} register_node_t;

typedef struct{
    uint32_t task_code;
    char * pdata;
    uint32_t len;
    uint16_t type;
} remote_cmd_info_t;

#pragma pack(1)
typedef struct{
    uint8_t version;
    uint8_t magic;
	uint32_t length;
    char data[];
} tx_data_t;

typedef struct{
    uint8_t version;
    uint8_t magic;
	uint32_t length;
    char data[];
} rx_data_t;

typedef struct{
	char server_ip[15];
	uint32_t port;
	char id[32];
	uint32_t result_code;
    uint32_t finish_reason;
} login_cmd_info_t;


#pragma pack()


extern xQueueHandle sendto_server_info_queue ;
char *get_device_sn(void);
int get_server_socket(void);
void set_server_connect_status(int status);
//发送remote cmd数据到服务器，此接口app task不用调用，开发给其他封装好的函数
int send_net_req_Q(remote_cmd_info_t *buff);
//根据type得到注册的bit位置
int find_type_bit_offset(uint16_t type);
//注册cmd的bit位和回调函数，回调函数中不能做过多的操作，建议只发event或者queue，因为做了临界区的保护
uint32_t remote_cmd_register(uint32_t cmd_bit, void (*callback)(void *pdata));
//注销函数，根据注册函数的返回值，注销
int remote_cmd_unregister(uint32_t task_code);
//得到服务器连接的状态，建议在APP task中判断使用
bool get_server_connect_status();
//整个remote cmd的初始化接口，在init中调用
int remote_command_init();
//用来发送组装发送命令
int assemble_remote_json (uint16_t version,uint32_t length,uint32_t task_code,uint16_t type,char*data);
#endif


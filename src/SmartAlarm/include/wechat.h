#ifndef __WECHAT_H__
#define __WECHAT_H__
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

typedef enum{
	/*send msg private command*/
	WECHAT_CMD_RECORD_START= 0x00,
	WECHAT_CMD_RECORD_FINISH,
	WECHAT_CMD_RECORD_TIMEOUT,
	WECHAT_CMD_RECORD_ERROR,
	WECHAT_CMD_UPLOAD_START,
	WECHAT_CMD_UPLOAD_FINISH,
	WECHAT_CMD_UPLOAD_ERROR,
	/*receive msg private command*/
	WECHAT_CMD_RECV_MSG,
	WECHAT_CMD_DOWNLOAD_START,
	WECHAT_CMD_DOWNLOAD_FINISH,
	WECHAT_CMD_DOWNLOAD_ERROR,
	WECHAT_CMD_PLAY_START,
	WECHAT_CMD_PLAY_FINISH,
	WECHAT_CMD_PLAY_PREVIOUS,
	WECHAT_CMD_PLAY_ERROR,
	WECHAT_CMD_UNDEFINE,
}wechat_cmd_value;

typedef struct{
	wechat_cmd_value cmd_value;
	void *param;
	int size;
}wechat_cmd_node;

void wechat_token_update(char *new);
int wechat_init(void);
void wechat_uninit(void);
#endif


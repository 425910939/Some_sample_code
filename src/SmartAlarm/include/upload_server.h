#ifndef __UPLOAD_SERVER_H__
#define __UPLOAD_SERVER_H__

#define UPLOAD_REQUEST_QUEUE_SIZE 5

#include "download_server.h"

#define HTTP_BUFFER_SIZE (1024*1)

void upload_server_init();

typedef struct{
	int socket_upload;
    char file_path_name[64];  //文件需要保存的完整路径如/sdcard/xx.mp3
    char port_number[6];      //需要访问的端口号，如http的tcp端口号为80
    char upload_path[320];  //下载的完成路径
    void (*callback)(download_upload_finish_reason_t finish_reason,char * http_response);//文件下载完成后的回调函数，用于通知调用下载的task，下载完成情况
} upload_info_t;

int send_upload_req_Q(upload_info_t *buff);
#endif


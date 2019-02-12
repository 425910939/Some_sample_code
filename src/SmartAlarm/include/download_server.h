#ifndef __DOWNLOAD_SERVER_H__
#define __DOWNLOAD_SERVER_H__

#include "socket_plus.h"
#define DOWNLOAD_REQUEST_QUEUE_SIZE 5

extern xQueueHandle download_request_info_queue;

/*
此函数用于下载相应连接任务
*/
void download_server_init();

typedef struct{
    char file_path_name[64];  //文件需要保存的完整路径如/sdcard/xx.mp3
    char port_number[6];      //需要访问的端口号，如http的tcp端口号为80
    char download_path[320];  //下载的完成路径
    bool breakpoint;          //是否需要断电续传，暂时不支持
    uint32_t filesize;        //文件的实际大小
    uint8_t type;             //文件类型，如http的大文件，为0 ，暂时设置的为0，后续一遍扩展
    void (*callback)(download_upload_finish_reason_t finish_reason , uint32_t filesize,char * file_path_name, uint32_t write_in_file_size);//文件下载完成后的回调函数，用于通知调用下载的task，下载完成情况
}download_info_t;

int send_download_req_Q(download_info_t *buff);
#endif

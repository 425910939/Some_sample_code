#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "ff.h"
#include "log.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "poll.h"
#include "zlib.h"
#include "protocol.h"
#include "upload_server.h"
#include "remote_command.h"
#include "connect_manager.h"

#define LOG_TAG		"offline"
#define USER_OFFLINE_TASK
#define OFFLINE_LOG_QUEUE_SIZE      (10)
#define OFFLINE_LOG_SYNC_THRESHOLD  (20*1000)//60-second
#define OFFLINE_LOG_FILE_PATH       ("/sdcard/offlinelog.txt")
#define OFFLINE_LOG_COMPERSS_PATH   ("/sdcard/offlinelog.zip")
#define OFFLINE_LOG_FILE_MAX_SIZE   (8*1024*1024)//8M LOG FILE
#define OFFLINE_LOG_FILE_HEAD       ("\r\nOFFLINE LOG BEGINNING >>>>>>\r\n")

typedef enum{
    OFFLINE_LOG_CMD_SYNC,
	OFFLINE_LOG_CMD_UPLOAD,
	OFFLINE_LOG_CMD_UNDEFINE,
}offline_log_cmd_t;

#ifdef CONFIG_USE_OFFLINE_LOG
bool offline_log_enable = true;
#else
bool offline_log_enable = false;
#endif
#ifdef SERVER_TEST
#define LOG_UPLOAD_HEAD 	"http://172.16.0.145:9990/api/test/upload?mac="
#define LOG_UPLOAD_PORT 	"9990"
#else
#define LOG_UPLOAD_HEAD 	"http://alarm.onegohome.com/api/test/upload?mac="
#define LOG_UPLOAD_PORT 	"80"
#endif
static bool first_write_log = true;
static log_buffer_t *ping_buffer = NULL;
static log_buffer_t *pong_buffer = NULL;
log_buffer_t *current_buffer = NULL;
//static uint32_t command_handler; 
static upload_info_t upload_info = {0}; //upload handler
#ifdef USER_OFFLINE_TASK
static bool task_need_run = true;
static xQueueHandle offline_log_queue;
static TimerHandle_t xTimerUser;
#else
void offline_log_task(void *param);
poll_func_config_t log_poll_config = {PCLOCK_SOURCE_SYS,100,offline_log_task,NULL};
#endif
portMUX_TYPE offline_log_spinlock = portMUX_INITIALIZER_UNLOCKED;

static int gzcompress(Bytef *data, uLong dlen, Bytef *zdata, uLong *zdlen)
{
    int err = 0;
    z_stream c_stream;
    
    //LOG_INFO("data=%p, dlen=%ld, zdata=%p, *zdlen=%ld\n",data, dlen, zdata, *zdlen);
    
    if(data && dlen > 0){
        c_stream.zalloc = NULL;
        c_stream.zfree = NULL;
        c_stream.opaque = NULL;
        //use parm 'MAX_WBITS+16' so that gzip headers are contained
        if(deflateInit2(&c_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK){
            LOG_INFO("deflateinit2 failed!\n");
            return -1;
        }
        c_stream.next_in  = data;
        c_stream.avail_in  = dlen;
        c_stream.next_out = zdata;
        c_stream.avail_out  = *zdlen;
        while(c_stream.avail_in != 0 && c_stream.total_out < *zdlen){
            if(deflate(&c_stream, Z_NO_FLUSH) != Z_OK) {
                LOG_INFO("deflate failed!\n");
                return -1;
            }
        }
        if(c_stream.avail_in != 0){
            LOG_INFO("avail_in not zero!\n");
            return c_stream.avail_in;
        }
        for(;;){
            if((err = deflate(&c_stream, Z_FINISH)) == Z_STREAM_END)
                break;
            if(err != Z_OK) {
                LOG_INFO("deflate finish fail: %d\n", err);
                return -1;
            }
        }
        if(deflateEnd(&c_stream) != Z_OK) {
            LOG_INFO("deflate end failed!\n");
            return -1;
        }
        *zdlen = c_stream.total_out;
        return 0;
    }
    return -1;
}

#define GZ_BUFFER_SIZE	1024
static int compress_file_to_gzip(char * input_name, char * output_name)
{
    FILE *fp = NULL,*fp2 = NULL;
    uLong flen, clen;
	int ret = 0;
    unsigned char * fbuf = NULL;
    unsigned char * cbuf = NULL; 

    if((fp = fopen(input_name, "rb")) == NULL){
        LOG_INFO("can not open %s!\n", input_name);
        return -1;
    }
    
    /*load file content to buffer*/
    fbuf = (unsigned char *)heap_caps_malloc(GZ_BUFFER_SIZE,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(NULL == fbuf){
        LOG_INFO("no enough memory!\n");
        goto __error;
    }
	
	if((fp2 = fopen(output_name, "wb")) == NULL){
        LOG_INFO("can not open %s!\n", output_name);
        goto __error;
    }
	
	cbuf = (unsigned char *)heap_caps_malloc(GZ_BUFFER_SIZE*4,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(NULL == cbuf) {
        LOG_INFO("no enough memory!\n");
        goto __error;
    }
	
	memset(fbuf,0,GZ_BUFFER_SIZE);
	memset(cbuf,0,GZ_BUFFER_SIZE*2);
	
   	while((ret = fread(fbuf,1,GZ_BUFFER_SIZE,fp)) > 0){
		flen = (uLong)ret;
		clen = compressBound(flen);
		LOG_INFO("clen = %ld ret = %d flen = %ld\n",clen,ret,flen);
		if(gzcompress(fbuf, flen, cbuf, &clen)){
	        LOG_INFO("compress %s failed!\n", input_name);
	        goto __error;
	    }
		fwrite(cbuf, sizeof(unsigned char), clen, fp2);
    	memset(fbuf,0,GZ_BUFFER_SIZE);
		memset(cbuf,0,GZ_BUFFER_SIZE);
    }
    
    fclose(fp);
	fclose(fp2);
    free(fbuf);
    free(cbuf);
    return 0;

__error:
    if(fp) fclose(fp);
    if(fbuf) free(fbuf);
    if(cbuf) free(cbuf);
    return -1;
}

static void log_command_cb(void *p_data,int cmd_type)
{	
	int type = cmd_type;	
	offline_log_cmd_t log_cmd;	
	LOG_INFO("command type = 0x%x\n",type);	
	switch(type){		
		case S2C_LOG_UPLOAD:			
			log_cmd = OFFLINE_LOG_CMD_UPLOAD;			
			xQueueSend(offline_log_queue, &log_cmd, 0);			
		break;	
		
		default:			
			LOG_INFO("type is ignore\n");			
		break;	
	}	
	return;
}

static void log_upload_callback(download_upload_finish_reason_t finish_reason)
{	
	
	return;
}

void print_buffer(void *buff, int size)
{
#define PRINT_ALIGN_SIZE	(8)
	uint8_t *ptr = (uint8_t *)buff;
	for(int i = 0; i < size; i++, ptr++){
		if(0 == (i%PRINT_ALIGN_SIZE))
			printf("\n");
		printf("%d:0x%x ",i, *ptr);
	}
	printf("\n");
}

static inline log_buffer_t *switch_buffer(void)
{
    log_buffer_t * prev_buffer;

    portENTER_CRITICAL(&offline_log_spinlock);
    prev_buffer = current_buffer;
    if(current_buffer == ping_buffer){
        current_buffer = pong_buffer;
    }else{
        current_buffer = ping_buffer;
    }
    current_buffer->buffer_used_size = 0;
    portEXIT_CRITICAL(&offline_log_spinlock);

    return prev_buffer;
}

static inline bool check_buffer(log_buffer_t * buffer)
{
    bool need_writeback = false;

    if(buffer && (buffer->buffer_used_size >= buffer->buffer_water_mark)){
        need_writeback = true;
    }

    return need_writeback;
}

static inline int write_back_to_file(log_buffer_t * buffer)
{
    bool from_head_write;
    FILE *fp = NULL;

	LOG_DBG("offline log file size (%ld)...\n",buffer->file_used_size);
	if(buffer->file_used_size >= OFFLINE_LOG_FILE_MAX_SIZE){
		from_head_write = true;
		buffer->file_used_size = 0;
		unlink(OFFLINE_LOG_FILE_PATH);
	}else{
		from_head_write = false;
	}

    fp = fopen(OFFLINE_LOG_FILE_PATH, "at+");
    if(fp){
        if(from_head_write){
            fseek(fp, 0, SEEK_SET);
        }else{
            fseek(fp, 0, SEEK_END);
        }

        if(first_write_log){
            fwrite(OFFLINE_LOG_FILE_HEAD, strlen(OFFLINE_LOG_FILE_HEAD), 1, fp);
            first_write_log = false;
        }
        fwrite(buffer->buffer, buffer->buffer_used_size, 1, fp);
		buffer->file_used_size += buffer->buffer_used_size;
        fclose(fp);
    }else{
        LOG_ERR("can't open offline log file\n");
    }

    return 0;
}
#ifdef USER_OFFLINE_TASK
static void offline_log_task(void *param)
{
	offline_log_cmd_t cmd;
    log_buffer_t * prev_buffer;
	char mac[12] = {0};
	
	while(task_need_run){
        if(pdTRUE == xQueueReceive(offline_log_queue, &cmd, portMAX_DELAY)){
            switch(cmd){
                case OFFLINE_LOG_CMD_SYNC:
                    if(check_buffer(current_buffer)){
                        LOG_DBG("switch offline log buffer and writeback.\n");
                        prev_buffer = switch_buffer();
                        write_back_to_file(prev_buffer);
                    }else{
                        LOG_DBG("no need switch offline log buffer.\n");
                    }
                    break;
				case OFFLINE_LOG_CMD_UPLOAD:
					if(compress_file_to_gzip(OFFLINE_LOG_FILE_PATH,OFFLINE_LOG_COMPERSS_PATH) == 0){
						memset(&(upload_info), 0, sizeof(upload_info_t));		
						strncpy(&(upload_info.file_path_name),OFFLINE_LOG_COMPERSS_PATH, sizeof(upload_info.file_path_name));	
						strncpy(&(upload_info.upload_path), LOG_UPLOAD_HEAD, sizeof(upload_info.upload_path));			
						unsigned char *p = (unsigned char *)wifi_sta_mac_add();					   	
						sprintf(mac, "%02x%02x%02x%02x%02x%02x", p[0],p[1],p[2],p[3],p[4],p[5]);		
						strncat(&(upload_info.upload_path),mac,strlen(mac));	
						strncat(&(upload_info.upload_path),"&duration=0",11);	
						strncat(&(upload_info.upload_path),"&type=1",7);					
						strncpy(&(upload_info.port_number),LOG_UPLOAD_PORT, sizeof(upload_info.port_number));	
						LOG_DBG("upload param file path %s, upload path %s, port num %s\n",						
							upload_info.file_path_name, upload_info.upload_path,upload_info.port_number);				
						upload_info.callback = log_upload_callback;						
						send_upload_req_Q(&upload_info);
					}
					break;
                default:
                    break;
            }
        }
	}
	vTaskDelay(1);
    vTaskDelete(NULL);
}

static void offline_log_tick(TimerHandle_t xTimer)
{
	offline_log_cmd_t cmd = OFFLINE_LOG_CMD_SYNC;

	xQueueSend(offline_log_queue, &cmd, 0);
}
#else
void offline_log_task(void *param)
{
	log_buffer_t * prev_buffer;
	if(check_buffer(current_buffer)){
		LOG_DBG("switch offline log buffer and writeback.\n");
		prev_buffer = switch_buffer();
		write_back_to_file(prev_buffer);
	}else{
		LOG_DBG("no need switch offline log buffer.\n");
	}
}
#endif
#ifdef CONFIG_USE_OFFLINE_LOG
int offline_log_init(void)
{	
	struct stat st = {0};
	uint32_t cmd_bits = 0;
	LOG_INFO("enter\n");
	
    ping_buffer = (log_buffer_t *)heap_caps_malloc(sizeof(log_buffer_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    pong_buffer = (log_buffer_t *)heap_caps_malloc(sizeof(log_buffer_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!ping_buffer || !pong_buffer){
		LOG_ERR("malloc offline log buffer fail\n");
		return -ESP_ERR_NO_MEM;
    }else{
    	stat(OFFLINE_LOG_FILE_PATH, &st); 
        memset(ping_buffer, 0, sizeof(log_buffer_t));
        memset(pong_buffer, 0, sizeof(log_buffer_t));
        ping_buffer->buffer_total_size = LOG_BUFFER_SIZE;
        ping_buffer->buffer_water_mark = LOG_BUFFER_WATER_MARK;
		ping_buffer->file_used_size = st.st_size;
        pong_buffer->buffer_total_size = LOG_BUFFER_SIZE;
        pong_buffer->buffer_water_mark = LOG_BUFFER_WATER_MARK;
		pong_buffer->file_used_size = st.st_size;
        current_buffer = ping_buffer;
    }
	LOG_INFO("log file size = %ld",ping_buffer->file_used_size);
	#ifdef USER_OFFLINE_TASK
    offline_log_queue = xQueueCreate(OFFLINE_LOG_QUEUE_SIZE, sizeof(offline_log_cmd_t));

	xTimerUser = xTimerCreate("offline_log_tick", pdMS_TO_TICKS(OFFLINE_LOG_SYNC_THRESHOLD), pdTRUE, NULL, offline_log_tick);
	if(!xTimerUser) {
		LOG_ERR("creat xtimer fail\n");
		return -ESP_ERR_INVALID_RESPONSE;
	}
	cmd_bits = 1<<find_type_bit_offset(S2C_LOG_UPLOAD);	
	remote_cmd_register(cmd_bits, log_command_cb);
	task_need_run = true;
	xTaskCreate(offline_log_task, "offline_log_task",3072, NULL, 1, NULL);
    xTimerStart(xTimerUser, portMAX_DELAY);
	#else
	register_poll_func(&log_poll_config);
	#endif
	LOG_INFO("exit\n");
	return 0;
}
#else
int offline_log_init(void) {return 0;}
#endif
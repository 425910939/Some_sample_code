#ifndef __SLOG_H_
#define __SLOG_H_
#include <stdio.h>
#define BUF_SIZE 1024  
typedef struct slog_info {  
    char path[128];  
    FILE*fp;  
    int size;  
    int level;  
    int num;  
}slog_info_t;  


//#define LOG_INFO(format, arguments...)printf("[INFO] ");printf(format, ##arguments);printf("\n");
//#define LOG_DEBUG(format, arguments...)printf("[DEBUG] ");printf(format, ##arguments);printf("\n");
//#define LOG_WARNING(format, arguments...)printf("[WARNING] ");printf(format, ##arguments);printf("\n");
//#define LOG_ERROR(format, arguments...)printf("[ERROR] ");printf(format, ##arguments);printf("\n");

int log_init(char *path, int size, int level, int num);
void log_debug(const char *msg, ...) ;
#endif
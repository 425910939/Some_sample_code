#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h> 
#include <sys/stat.h>
#include "slog.h"
static slog_info_t *slog_t = NULL;

int log_init(char *path, int size, int level, int num)  
{  
    char new_path[128] = {0};  
    if (NULL == path || 0 == level) return -1;  
    slog_info_t *log = (slog_info_t *)malloc(sizeof(slog_info_t));  
    memset(log, 0, sizeof(slog_info_t));  
    if(level != 3)  
    {  
        //the num use to control file naming  
        //snprintf(new_path, 128, "%s%d",path,(int)time(NULL));  
        log->num = num;  
        if(num){
            snprintf(new_path, 128, "%s",path);  
        }else{  
            snprintf(new_path, 128, "%s.new", path);
        }
		log->fp = fopen(new_path,"at+");
        if(log->fp == NULL)  
        {  
        	printf("log init faol\n");
            free(log);  
            log = NULL;  
            return -1;  
        }  
    }  
    strncpy(log->path, path, 128);  
    log->size = (size > 0 ? size:0);  
    log->level = (level > 0 ? level:0);
	slog_t = log;
    return 0;  
}  



void inline log_debug(const char *msg, ...)  
{  
    va_list ap;  
    time_t now;  
    char *pos;  
    char message[BUF_SIZE] = {0};  
    int nMessageLen = 0;  
    int sz;  
	int i = 0;
    if(NULL == slog_t || 0 == slog_t->level) return;  
	//pos = message;
	#if 0
    now = time(NULL);  
    pos = ctime(&now);  
    sz = strlen(pos);  
    pos[sz-1]=']';  

    snprintf(message, BUF_SIZE, "[%s", pos); 
    for (pos = message; *pos; pos++);  
    	sz = pos - message; 

	snprintf(message+sz, BUF_SIZE, "[%s]", "test"); 
    for (pos = message; *pos; pos++);  
    	sz = pos - message; 
	#endif	
    va_start(ap, msg);  
    nMessageLen = vsnprintf(message, BUF_SIZE,msg,ap);  
    va_end(ap);  
    if (nMessageLen <= 0) return;  
    if (3 == slog_t->level){  
        printf("%s\n", message);  
        return;  
    }  
    if (2 == slog_t->level)  
        printf("%s\n", message);  
	
	fwrite(message,strlen(message), 1, slog_t->fp);
	fwrite("\n",1, 1, slog_t->fp);
    fflush(slog_t->fp);
}  


void log_checksize(void)  
{  
    struct stat stat_buf;  
    char new_path[128] = {0};  
    char bak_path[128] = {0};  
    if(NULL == slog_t || 3 == slog_t->level || '\0' == slog_t->path[0]) return;  
    memset(&stat_buf, 0, sizeof(struct stat));  
    fstat(fileno(slog_t->fp), &stat_buf);  
    if(stat_buf.st_size > slog_t->size)  
    {  
        fclose(slog_t->fp);  
        if(slog_t->num)  
            snprintf(new_path, 128, "%s%d", slog_t->path, (int)time(NULL));  
        else  
        {  
            snprintf(bak_path, 128, "%s.bak", slog_t->path);  
            snprintf(new_path, 128, "%s.new", slog_t->path);  
            remove(bak_path); //delete the file *.bak first  
            rename(new_path, bak_path); //change the name of the file *.new to *.bak  
        }  
        //create a new file 
        slog_t->fp = fopen(new_path, "at+");
    }  
}  


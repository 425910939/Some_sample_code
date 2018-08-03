#ifndef __HTTP_API_H__
#define __HTTP_API_H__

#define MY_HTTP_DEFAULT_PORT 80
#define RW_MODE
int http_get(const char *url,void*content,int length);
char * http_post(const char *url,char * post_str);

#endif

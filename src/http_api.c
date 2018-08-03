#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "config.h"
#include "http_api.h"

#define BUFFER_SIZE 4096


static int http_get_head_create(const char * url,const char *domain, char * download_buff,uint32_t buffer_size,char*content,int content_len)
{
	 //"Connection:Keep-Alive\r\n"
    memset(download_buff,0,buffer_size);

	snprintf(download_buff, buffer_size,\
            "GET /%s HTTP/1.1\r\n"\
            "Accept:text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n"\
            "User-Agent:Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537(KHTML, like Gecko) Chrome/47.0.2526Safari/537.36\r\n"\
            "Host:%s\r\n"\
            "Connection:close\r\n"\
            "Content-Length:%d\r\n\r\n"\
            "%s"\
            "\r\n"\
        ,url, domain,content_len,content);
	
    printf("http_get_request = %s\n",download_buff);

    return 0;
}

static int http_post_head_create(char * url, char *domain,char *http_request,uint32_t buffer_size,char* content ,int length)
{

	memset(http_request,0,buffer_size);
    snprintf(http_request,buffer_size,\
            "POST /%s HTTP/1.1\r\n"\
            "Host:%s\r\n"\
            "Accept: */*\r\n"\
            "Pragma: no-cache\r\n"\
            "Connection: close\r\n"\
            "Content-type: application/x-www-form-urlencoded\r\n"\
            "Content-length:%d\r\n\r\n%s",url,domain,length,content);

    printf("http_post_request = %s\n",http_request);
    return 0;
}

static int http_tcpclient_create(const char *host, int port){
    struct hostent *he;
    struct sockaddr_in server_addr; 
    int socket_fd;
 
    if((he = gethostbyname(host))==NULL){
        return -1;
    }
 
   
    if((socket_fd = socket(AF_INET,SOCK_STREAM,0))==-1){
		printf("socket create faile %s\n",strerror(errno));
        return -1;
    }
	
	server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr = *((struct in_addr *)he->h_addr);
 
    if(connect(socket_fd, (struct sockaddr *)&server_addr,sizeof(struct sockaddr)) == -1){
		printf("connect server faile %s\n",strerror(errno));
        return -1;
    }
 
    return socket_fd;
}
 
static void http_tcpclient_close(int socket){
    close(socket);
}
 
static int http_parse_url(const char *url,char *host,char *file,int *port)
{
    char *ptr1,*ptr2;
    int len = 0;
    if(!url || !host || !file || !port){
        return -1;
    }
 
    ptr1 = (char *)url;
 
    if(!strncmp(ptr1,"http://",strlen("http://"))){
        ptr1 += strlen("http://");
    }else if(!strncmp(ptr1,"https://",strlen("https://"))){
        ptr1 += strlen("https://");
    }else{
        return -1;
    }
	
    ptr2 = strchr(ptr1,'/');
    if(ptr2){
        len = strlen(ptr1) - strlen(ptr2);
        memcpy(host,ptr1,len);
        host[len] = '\0';
        if(*(ptr2 + 1)){
            memcpy(file,ptr2 + 1,strlen(ptr2) - 1 );
            file[strlen(ptr2) - 1] = '\0';
        }
    }else{
        memcpy(host,ptr1,strlen(ptr1));
        host[strlen(ptr1)] = '\0';
    }
    //get host and ip
    ptr1 = strchr(host,':');
    if(ptr1){
        *ptr1++ = '\0';
        *port = atoi(ptr1);
    }else{
        *port = MY_HTTP_DEFAULT_PORT;
    }
 
    return 0;
}

static char *http_parse_result(char*lpbuf)
{
    char *ptmp = NULL; 
    char *response = NULL;

    ptmp = (char*)strstr(lpbuf,"HTTP/1.1");
    if(!ptmp){
        printf("http/1.1 not faind\n");
        return NULL;
    }
    if(atoi(ptmp + 9)!=200){
        printf("result:\n%s\n",lpbuf);
        return NULL;
    }
 
    ptmp = (char*)strstr(lpbuf,"\r\n\r\n");
    if(!ptmp){
        printf("ptmp is NULL\n");
        return NULL;
    }
    response = (char *)malloc(strlen(ptmp)+1);
    if(!response){
        printf("malloc failed \n");
        return NULL;
    }
    strcpy(response,ptmp+4);
    return response;
}
 

 
static int http_tcpclient_recv(int socket,char *lpbuff)
{
    int recvnum = 0,recv_total = -1;
	int ret = 0;
	char recv_buf[BUFFER_SIZE];
	fd_set   t_set1;
	struct timeval  tv;
	
	FD_ZERO(&t_set1);
    FD_SET(socket, &t_set1);

    while(1){
        sleep(2);
        tv.tv_sec= 0;
        tv.tv_usec= 0;
        ret = select(socket +1, &t_set1, NULL, NULL, &tv);
        if (ret < 0) {
            printf("select fail %s\n",strerror(errno));
            return recv_total;
        };

        if (ret > 0){
            memset(lpbuff, 0, BUFFER_SIZE);
			#ifdef RW_MODE
            recvnum = read(socket, recv_buf, BUFFER_SIZE-1);
            #else
			recvnum = recv(socket,recv_buf,BUFFER_SIZE-1,0);
			#endif
            if (recvnum == 0){
                printf("recv fail\n");
                return recv_total;
            }
			recv_total +=recvnum;
			
            printf("%s recv_total = %d\n", recv_buf,recv_total);
			strncpy(lpbuff,http_parse_result(recv_buf),BUFFER_SIZE);
			printf("http_parse_result = %s\n",lpbuff);
        }
    }
 
    return recv_total;
}
 
static int http_tcpclient_send(int socket,char *buff,int size){
    int sent=0,tmpres=0;
 
    while(sent < size){
		#ifdef RW_MODE
		tmpres = write(socket,buff+sent,size-sent);
		#else
		tmpres = send(socket,buff+sent,size-sent,0);
		#endif
        if(tmpres == -1){
            return -1;
        }
        sent += tmpres;
    }
    return sent;
}
 

char * http_post(const char *url,char *post_str){
 
    char post[BUFFER_SIZE] = {'\0'};
    int socket_fd = -1;
    char lpbuf[BUFFER_SIZE*4] = {'\0'};
    char *ptmp;
    char host_addr[BUFFER_SIZE] = {'\0'};
    char file[BUFFER_SIZE] = {'\0'};
    int port = 0;
    int len=0;
	char *response = NULL;
 
    if(!url || !post_str){
        printf("      failed!\n");
        return NULL;
    }
 
    if(http_parse_url(url,host_addr,file,&port)){
        printf("http_parse_url failed!\n");
        return NULL;
    }
    //printf("host_addr : %s\tfile:%s\t,%d\n",host_addr,file,port);
 
    socket_fd = http_tcpclient_create(host_addr,port);
    if(socket_fd < 0){
        printf("http_tcpclient_create failed\n");
        return NULL;
    }
     
 	http_post_head_create(file,host_addr,lpbuf,sizeof(lpbuf),post_str,strlen(post_str));
    if(http_tcpclient_send(socket_fd,lpbuf,strlen(lpbuf)) < 0){
        printf("http_tcpclient_send failed..\n");
        return NULL;
    }
 
    /*it's time to recv from server*/
    if(http_tcpclient_recv(socket_fd,lpbuf) <= 0){
        printf("http_tcpclient_recv failed\n");
        return NULL;
    }
 
    http_tcpclient_close(socket_fd);
 
    return http_parse_result(lpbuf) ;
}
 
int http_get(const char *url,void*content,int length)
{
 
    char str2[BUFFER_SIZE] = {'\0'};
    int socket_fd = -1;
    char lpbuf[BUFFER_SIZE*4] = {'\0'};
	 
    char *ptmp;
    char host_addr[BUFFER_SIZE] = {'\0'};
    char file[BUFFER_SIZE] = {'\0'};
    int port = 0;
    int len=0;
 	
    if(!url){
        printf("      failed!\n");
        return -1;
    }
 
    if(http_parse_url(url,host_addr,file,&port)){
        printf("http_parse_url failed!\n");
        return -1;
    }
    printf("host_addr : %s\tfile:%s\t,%d\n",host_addr,file,port);
 
    socket_fd =  http_tcpclient_create(host_addr,port);
    if(socket_fd < 0){
        printf("http_tcpclient_create failed\n");
        return -1;
    }
 	memset(str2, 0, BUFFER_SIZE);
    strncpy(str2,(char*)content,length);
    http_get_head_create(file,host_addr,lpbuf,sizeof(lpbuf),str2,strlen(str2));

    if(http_tcpclient_send(socket_fd,lpbuf,strlen(lpbuf)) < 0){
        printf("http_tcpclient_send failed..\n");
        return -1;
    }
	
 
    if(http_tcpclient_recv(socket_fd,lpbuf) <= 0){
        printf("http_tcpclient_recv failed\n");
        return -1;
    }
    http_tcpclient_close(socket_fd);

    return 0;
}


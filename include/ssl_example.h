#ifndef __SSL_EXAMPLE_H__
#define __SSL_EXAMPLE_H__
int ssl_server(int port,int client_num,char*ip,char*certificate,char*pri_key);
int ssl_client(char*server_ip,int port);
#endif
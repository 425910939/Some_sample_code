/* sock.c
** strophe XMPP client library -- socket abstraction implementation
**
** Copyright (C) 2005-2009 Collecta, Inc. 
**
**  This software is provided AS-IS with no warranty, either express
**  or implied.
**
** This program is dual licensed under the MIT and GPLv3 licenses.
*/

/** @file
 *  Socket abstraction.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "log.h"
#include "socket_plus.h"

#define LOG_TAG		"SPLUS"

static const int s_MAX_SOCKET_WRITE_SIZE = 512;

int sock_get_server_info_plus(const char *str_web_url, char *str_domain, char *str_port, char *str_params)
{
	uint8_t start = 0;
	char* domain_start = NULL;
	char *patterns[] = {"http://", "https://", NULL};

	if (NULL == str_web_url || NULL == str_domain || NULL == str_port) //check input value invaild, str_params maybe not use in out system
	{
		return PARSE_URL_FAILED;
	}

    for (int i = 0; patterns[i]; i++){
        if (strncmp(str_web_url, patterns[i], strlen(patterns[i])) == 0)
            start = strlen(patterns[i]);
    }

	if(start == 0)
	{
		return PARSE_URL_FAILED;
	}

	domain_start = start + str_web_url;
	if (*domain_start == '\0')
	{
		return PARSE_URL_FAILED;
	}

	//get port
	char* port_start = strstr(domain_start, ":");
	char* port_end = strstr(domain_start, "/");
	if (NULL == port_start)
	{
		strcpy(str_port, "80");
		if (NULL == port_end)
		{
			strcpy(str_domain, domain_start);
			return LOAD_SUCC;
		}
		else
		{
			strncpy(str_domain, domain_start, port_end - domain_start);
		}
	}
	else
	{	
		strncpy(str_domain, domain_start, port_start - domain_start);
		port_start = port_start + 1;
		if (NULL == port_end)
		{	
			strcpy(str_port, port_start);
			return LOAD_SUCC;
		}
		else
		{
			strncpy(str_port, port_start, port_end - port_start);
		}
	}

	if ((*port_end != '\0') && (str_params != NULL))
	{
		strcpy(str_params, port_end);	
	}

	return LOAD_SUCC;
}

sock_t sock_connect_plus(const char * const host, const char * const port, int * error_reason)
{
	int err = INVALID_SOCK;
    sock_t sock = INVALID_SOCK;
    struct addrinfo *res = NULL;
	struct addrinfo	*ainfo = NULL; 
	struct addrinfo hints = 
	{
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};

    err = getaddrinfo(host, port, &hints, &res);
	if(err != 0 || res == NULL) {
		LOG_ERR("DNS lookup failed err=%d res=%p\n", err, res);
		vTaskDelay(1000 / portTICK_PERIOD_MS);        
		*error_reason = QUERY_DNS_FAILED;
		return INVALID_SOCK;
    }

	for (ainfo = res; ainfo != NULL; ainfo = ainfo->ai_next) 
	{
	    sock = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
	    if (sock < 0)
	    {
	        continue;
	    }

		LOG_ERR("... allocated socket\r\n");

        err = connect(sock, ainfo->ai_addr, ainfo->ai_addrlen);
        if (err == 0)
        {
            break;
        }
	    sock_close_plus(sock);
	}
	freeaddrinfo(res);
	sock = (ainfo == NULL) ? INVALID_SOCK : sock;

	if(sock == INVALID_SOCK)
		*error_reason = SERVER_CONNECT_FAILED;
		
	LOG_INFO("... connected\n");

    return sock;
}

int sock_close_plus(const sock_t sock)
{
	return close(sock);
}

int sock_set_blocking_plus(const sock_t sock)
{
    int rc;

    rc = fcntl(sock, F_GETFL, NULL);
    if (rc >= 0)
	{
        rc = fcntl(sock, F_SETFL, rc & (~O_NONBLOCK));
    }
	
    return rc;
}

int sock_set_nonblocking_plus(const sock_t sock)
{
    int rc;

    rc = fcntl(sock, F_GETFL, NULL);
    if (rc >= 0) 
	{
        rc = fcntl(sock, F_SETFL, rc | O_NONBLOCK);
    }
	
    return rc;
}

int sock_set_read_timeout_plus(const sock_t sock, const unsigned int sec, const unsigned int usec)
{
	struct timeval timeout={sec, usec};
	return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
}

int sock_set_write_timeout_plus(const sock_t sock, const unsigned int sec, const unsigned int usec)
{
	struct timeval timeout={sec, usec};
	return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
}

int sock_read_plus(const sock_t sock, void * const buff, const size_t len)
{
    return recv(sock, buff, len, 0);
}

int sock_readn_plus(const sock_t sock, void * const buff, const size_t len)
{
	int ret = 0;
	int recv_len = 0;
	do 
	{		
		ret = sock_read_plus(sock, (char *)buff + recv_len, len - recv_len);
		if (ret > 0)
		{
			recv_len += ret;
		}
		else
		{
			break;
		}
	} while(ret > 0);

	return recv_len;
}

int sock_write_plus(const sock_t sock, const void * const buff, const size_t len)
{
	int ret 		= -1;
	int data_len 	= len;
	int write_len 	= 0;
	int position 	= 0;
	
	while(data_len > 0)
	{
		write_len = (data_len <= s_MAX_SOCKET_WRITE_SIZE) ? data_len : s_MAX_SOCKET_WRITE_SIZE;
		ret = send(sock, (char*)buff + position, write_len, 0);
		if (ret > 0)
		{
			position += ret;
			data_len -= ret;
		}
		else
		{
			break;
		}
	}

	return position;
}

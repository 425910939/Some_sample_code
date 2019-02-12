/* sock.h
** socket abstraction header
**
** Copyright (C) 2005-2009 Collecta, Inc. 
**
**  This software is provided AS-IS with no warranty, either express
**  or implied.
**
**  This program is dual licensed under the MIT and GPLv3 licenses.
*/

/** @file
 *  Socket abstraction API.
 */

#ifndef __SOCKET_H__
#define __SOCKET_H__

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif /* C++ */

typedef enum {
    LOAD_SUCC = 0,
    LOAD_HALF,      // 1
    WIFI_DISSCONNECT ,
    SERVER_CONNECT_FAILED,
    RECEVCE_FILESIZE_FAILED,
    QUERY_DNS_FAILED,  //5 
    OPEN_SOCKET_FAILED,
    HTTP_REQ_FAILED,
    OPEN_FILE_FAILED,
    PARSE_URL_FAILED,
    WRITE_FILE_FAILED, //10
    READ_FILE_FAILED,
    LOAD_TIME_OUT,
	FILE_SIZE_ERROR,
    NOT_FIND_HTTP_BODY_END,
    HTTP_TIMEOUT,  //15
    DOWNLOAD_TASK_CREATE_ERR,
    DOWNLOAD_TASK_PARAM_ERR,
    DOWNLOAD_SIZE_OVERFLOW_ERR,
} download_upload_finish_reason_t;

#define INVALID_SOCK -1
typedef int sock_t;

int sock_get_server_info_plus(const char *str_web_url, char *str_domain, char *str_port, char *str_params);
sock_t sock_connect_plus(const char * const host, const char * const port, int * error_reason);
int sock_close_plus(const sock_t sock);
int sock_set_blocking_plus(const sock_t sock);
int sock_set_nonblocking_plus(const sock_t sock);
int sock_set_read_timeout_plus(const sock_t sock, const unsigned int sec, const unsigned int usec);
int sock_set_write_timeout_plus(const sock_t sock, const unsigned int sec, const unsigned int usec);
int sock_read_plus(const sock_t sock, void * const buff, const size_t len);
int sock_readn_plus(const sock_t sock, void * const buff, const size_t len);
int sock_write_plus(const sock_t sock, const void * const buff, const size_t len);

#ifdef __cplusplus
} /* extern "C" */	
#endif /* C++ */

#endif /* __SOCKET_H__ */

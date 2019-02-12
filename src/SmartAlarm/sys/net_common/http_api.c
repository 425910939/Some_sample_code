#include <stdio.h>
#include <string.h>
#include <socket_plus.h>
#include <log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#define LOG_TAG		"HTTPI"

char* http_get_body(const char *str_body)
{
	char* str_seprate = "\r\n\r\n";
	char* pBody = strstr(str_body, str_seprate);
	if (NULL == pBody)
	{
		return NULL;
	}

	pBody = pBody + strlen(str_seprate);
	
	LOG_INFO("Find http body header %d ,%d\n",*pBody,pBody-str_body);

	return pBody;
}

int http_get_error_code(const char *str_body)
{
	if (strlen(str_body) > 12 
		&& (str_body + 9) == strstr(str_body, "200"))
	{
		return 200;
	}

	return -1;
}

int http_send_req(const sock_t sock,const char * url,const char *domain, char * download_buff,uint32_t buffer_size)
{
     memset(download_buff,0,buffer_size);

	 sprintf(download_buff, \
            "GET %s HTTP/1.1\r\n"\
            "Accept:text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n"\
            "User-Agent:Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537(KHTML, like Gecko) Chrome/47.0.2526Safari/537.36\r\n"\
            "Host:%s\r\n"\
            "Connection:close\r\n"\
            "\r\n"\
        ,url, domain);

    LOG_INFO("http_request = %s\n",download_buff);

	if (sock_write_plus(sock, download_buff, strlen(download_buff)) != strlen(download_buff)) {
    	LOG_ERR( "... socket send failed\n");
        vTaskDelay(4000 / portTICK_PERIOD_MS);  
		return HTTP_REQ_FAILED; 
    }
    
    LOG_DBG("... socket send success\n");

    return LOAD_SUCC;
}

uint32_t http_get_download_filesize(const char *str_body)
{
	char *p = NULL;
	uint32_t filesize = 0;

	p = strstr(str_body,"Content-Length"); 

	if (p != NULL){ 
		LOG_INFO("http_get find content-length\n");
		p = strchr(p,':');
		p++; 
		filesize = strtoul(p,NULL,10); 
	} 

	return filesize;	
}
int http_get_content_length(const char *str_body)
{
	char length[16] = {0};
	
	if (str_body == NULL)
	{
		return -1;
	}

	char *start = strstr(str_body, "Content-Length:");
	if (start == NULL)
	{
		return -1;	
	}
	
	char *end = strstr(start, "\r\n");
	if (end == NULL)
	{
		return -1;
	}

	start = start + strlen("Content-Length:");
	int len = end - start;
	if (len > 0 && len < sizeof(length) - 1)
	{
		int i = 0;
		int n  =0;
		for (i=0; i<len; i++)
		{
			char ch = *(start+i);
			if (ch >= '0' && ch <= '9')
			{
				length[n] = ch;
				n++;
			}
		}

		return atoi(length);
	}
	else
	{
		return -1;
	}
}

int http_get_content_type(
	char *str_content_type,
	uint32_t str_content_type_len,
	const char *str_body)
{
	char type[64] = {0};
	
	if (str_body == NULL)
	{
		return -1;
	}

	char *start = strstr(str_body, "Content-Type:");
	if (start == NULL)
	{
		return -1;	
	}
	
	char *end = strstr(start, "\r\n");
	if (end == NULL)
	{
		return -1;
	}

	start = start + strlen("Content-Type:");
	int len = end - start;
	if (len > 0 && len < sizeof(type) - 1)
	{
		memcpy(type, start, len);
		snprintf(str_content_type, str_content_type_len, "%s", type);
		return 0;
	}
	else
	{
		return -1;
	}
}


char* http_get_chunked_body(
	char *str_dest,
	int  dest_len,
	char *str_src)
{
	int i = 0;
	char str_ch[2] = {0};
	char str_len[16] = {0};
	char str_body[256] = {0};
	int state = 0;

	memset(str_ch, 0, sizeof(str_ch));
	memset(str_len, 0, sizeof(str_len));
	memset(str_dest, 0, dest_len);
	for (i=0; i<strlen(str_src); i++)
	{
		char ch = str_src[i];
		str_ch[0] = ch;
		switch(state)
		{
			case 0:
			{
				if (ch == '\r')
				{
					state = 1;
				}
				else
				{
					if (ch == '0')
					{
						state = 4;
					}
					else
					{
						strcat(str_len, str_ch);
					}
				}
				break;
			}
			case 1:
			{
				if (ch == '\n')
				{
					state = 2;
				}
				else
				{
					state = 0;
					memset(str_len, 0, sizeof(str_len));
				}
				break;
			}
			case 2:
			{
				if (ch == '\r')
				{
					state = 3;
				}
				else
				{
					strcat(str_dest, str_ch);
				}
				break;
			}
			case 3:
			{
				state = 0;
				memset(str_len, 0, sizeof(str_len));
				break;
			}
			case 4:
			{
				break;
			}
			default:
				break;
		}
	}

	return str_dest;
}
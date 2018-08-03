#include <stdio.h>
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



static const char *url = "http://alarm.onegohome.com/api/api/devlogin?dev=30AEA42A2EF4&version=B300_V00B16.H2.L1.C1.T1";
static const char *url2 ="http://www.cnblogs.com/ymnets/p/6255674.html";
static const char*url3 = "http://www.webxml.com.cn/webservices/qqOnlineWebService.asmx/qqCheckOnline";
int main(int argc, char **argv)
{

  	char content[256] = "qqCode=474497857";

	//http_get(url,content,strlen(content));
	http_post(url3,content);
 	return 0;
}


#ifndef __TOOL_FUN_H__
#define __TOOL_FUN_H__

// 鏍规嵁鍩熷悕鑾峰彇ip
int get_ip_by_domain(const char *domain, char *ip);
// 鑾峰彇鏈満mac
int get_local_mac(const char *eth_inf, char *mac);
// 鑾峰彇鏈満ip
int get_local_ip(const char *eth_inf, char *ip);

//判断是否为文件夹
bool isDir(const char* path);
 
//遍历文件夹的驱动函数
void findFiles(const char *path);
 
//遍历文件夹de递归函数
void __findFiles(const char *path, int recursive);
#endif
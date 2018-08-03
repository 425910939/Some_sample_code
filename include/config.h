#ifndef __CONFIG_H__
#define __CONFIG_H__

typedef unsigned int uint32_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint64_t;
#define bool        int8_t
#define false       0        ///<  definition of 'false'
#define true        1        ///<  definition of 'true'

//大小端转换函数
#define BSWAP_32(x) \
    (uint32_t)((((uint32_t)(x) & 0xff000000) >> 24)|(((uint32_t)(x) & 0x00ff0000) >> 8)|(((uint32_t)(x) & 0x0000ff00) << 8)|(((uint32_t)(x) & 0x000000ff) << 24)) 
#endif
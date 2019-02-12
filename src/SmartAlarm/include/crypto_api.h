/* crypto api.h
** include sha1 md5 
**
** Copyright (C) 2005-2009 Collecta, Inc. 
**
**  This software is provided AS-IS with no warranty, either express
**  or implied.
**
**  This program is dual licensed under the MIT and GPLv3 licenses.
*/

#ifndef __CRYPTO_API_H__
#define __CRYPTO_API_H__

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif /* C++ */

void crypto_sha1(const unsigned char *input, const size_t ilen, unsigned char output[20]);
void crypto_md5(const unsigned char *input, const size_t ilen, unsigned char output[16]);
void crypto_rand_str(char *strRand, const size_t iLen);
void crypto_sha1prng(unsigned char sha1prng[16], const unsigned char *strMac);
void crypto_time_stamp(unsigned char* strTimeStamp, const size_t iLen);
int crypto_base64_encode(unsigned char *dst, size_t dlen, size_t *olen, const unsigned char *src, size_t slen);
void crypto_random_byte(unsigned char _byte[16]);


#ifdef __cplusplus
} /* extern "C" */	
#endif /* C++ */

#endif /* __CRYPTO_API_H__ */


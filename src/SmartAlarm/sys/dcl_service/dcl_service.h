/*
 * Copyright 2017-2018 deepbrain.ai, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://deepbrain.ai/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#ifndef DCL_SERVICE_H
#define DCL_SERVICE_H

#include "audio_record_interface.h"
#include "dcl_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************
//语义处理接口
*******************************************************************/
#define NLP_SERVICE_LINK_MAX_NUM 10
	
/*语义结果类型*/
typedef enum NLP_RESULT_TYPE_T
{
	NLP_RESULT_TYPE_NONE,
	NLP_RESULT_TYPE_NO_ASR,
	NLP_RESULT_TYPE_NO_LICENSE,
	NLP_RESULT_TYPE_SHORT_AUDIO,
	NLP_RESULT_TYPE_CHAT,
	NLP_RESULT_TYPE_LINK,
	NLP_RESULT_TYPE_CMD,
	NLP_RESULT_TYPE_VOL_CMD,
	NLP_RESULT_TYPE_TRANSLATE,
	NLP_RESULT_TYPE_ERROR,
}NLP_RESULT_TYPE_T;

/*语义结果-聊天结果*/
typedef struct NLP_RESULT_CHAT_T
{
	char text[1024];
	char link[1024];
}NLP_RESULT_CHAT_T;

/*语义结果-音乐资源*/
typedef struct NLP_RESULT_LINK_T
{
	char link_name[128];
	char name_tts_url[128];
	char link_url[128];
}NLP_RESULT_LINK_T;

/*语义结果-音乐资源集合*/
typedef struct NLP_RESULT_LINKS_T
{
	int link_size;
	NLP_RESULT_LINK_T link[NLP_SERVICE_LINK_MAX_NUM];
}NLP_RESULT_LINKS_T;

/*语义结果集合*/
typedef struct NLP_RESULT_T
{
	int request_sn;		//请求序列号
	char input_text[256];
	NLP_RESULT_TYPE_T type;
	NLP_RESULT_CHAT_T chat_result;
	NLP_RESULT_LINKS_T link_result;
}NLP_RESULT_T;


/******************************************************************
//语音识别服务接口
*******************************************************************/
/*语音识别结果*/
typedef struct ASR_RESULT_t
{
	int 				record_sn;		//语音识别序列号
	DCL_ERROR_CODE_t	error_code;		//错误码
	char				str_result[MAX_NLP_RESULT_LENGTH];//语义返回的原始字符串
}ASR_RESULT_t;

#ifdef __cplusplus
}
#endif

#endif //DCL_SERVICE_H



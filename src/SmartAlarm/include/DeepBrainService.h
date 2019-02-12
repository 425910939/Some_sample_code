#ifndef _DEEP_BRAIN_SERVICE_H_
#define _DEEP_BRAIN_SERVICE_H_
#include "dcl_interface.h"
#include "dcl_nlp_decode.h"
//#include "dcl_volume_decode.h"

#define TTS_URL_SIZE 2048
typedef struct ASR_SERVICE_HANDLE_t
{
	//asr result
	ASR_RESULT_t asr_result;
	//dcl asr handle
	void *dcl_asr_handle;
	NLP_RESULT_T nlp_result;
	//auth params
	DCL_AUTH_PARAMS_t auth_params;
	
	char tts_url[TTS_URL_SIZE];
}ASR_SERVICE_HANDLE_t;

typedef struct{
	int cmd_value;
	void *param;
}DeepBrainService_t;

void deep_brain_service_init();

#endif


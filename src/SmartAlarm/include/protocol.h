#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__


#define S2C_DEVICE_HEARTBEAT_ACK        (0x0002)
#define S2C_FACTORY_RESET				(0x8201)
#define S2C_WECHAT_MSG_PUSH			    (0x8202)
#define S2C_STORY_LESSON_SYNC		    (0x8203)
#define S2C_STORY_LESSON_PUSH           (0x8204)
#define S2C_STORY_REALTIME_PUSH         (0x8205)
#define S2C_STORY_LESSON_RESULT_ACK     (0x8206)
#define S2C_OTA_UPDATE_PUSH             (0x8207)
#define S2C_STORY_LESSON_DELETE			(0x8208)
#define S2C_STORY_VOLUME_CONTROL		(0x8209)
#define S2C_DEVICE_STATUS				(0x820a)
#define S2C_WECHAT_FINISH				(0x820b)
#define S2C_LOG_UPLOAD					(0x820c)

#define LOGIN_VERSION_FAIL 				(0x1)
#define LOGIN_VERSION_SUCCEED 			(0x0)
#define DEVICE_HEARTBEAT_VERSION 		(0x02)
#define DEVICE_MSGID_VERSION 			(0x0a)
#define DEVICE_DATA_VERSION 			(0x80)

#define DEVICE_SN_SIZE          (15)
#define DEVICE_MAC_SIZE         (6)
#define DEVICE_VERSION_LENGTH   (30)

#pragma pack(1)
typedef struct{
    char version[30];
    char url[0];
}server_ota_push_t;
//story command end
#pragma pack()
#endif
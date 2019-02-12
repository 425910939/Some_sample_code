#ifndef __ALARMS_H__
#define __ALARMS_H__

typedef enum{
	ALARM_ST_OFF= 0x00,
    ALARM_ST_ON,
    ALARM_ST_UNDEFINE,
}alarm_st_t;

typedef struct{
    alarm_st_t state;
	bool one_shot;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  week_map;
    int alarm_id;
    void  (*callback)(void *pointer);
}alarm_t;

int add_alarm(alarm_t *app_alarm);
void remove_alarm(int alarm_id);
void fresh_alarm(void);
int alarm_init(void);
void alarm_uninit(void);
#endif
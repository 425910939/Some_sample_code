#ifndef __APPLICATION_H__
#define __APPLICATION_H__

typedef enum{
    APP_WECHAT,
    APP_STORY,
    APP_ALARM,
    APP_DEEPBRAIN,
    APP_UNDEFINE,
}application_t;

application_t get_front_application(void);
void set_front_application(application_t app);
#endif
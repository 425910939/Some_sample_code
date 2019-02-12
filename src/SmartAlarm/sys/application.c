#include <string.h>
#include <stdio.h>
#include "esp_types.h"
#include "esp_attr.h"
#include "esp_intr.h"
#include "esp_log.h"
#include "malloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/xtensa_api.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "application.h"

#define LOG_TAG	    "app"

static portMUX_TYPE app_spinlock = portMUX_INITIALIZER_UNLOCKED;
static application_t front_app = APP_UNDEFINE;

application_t get_front_application(void)
{
    application_t app;

    portENTER_CRITICAL(&app_spinlock);
    app = front_app;
    portEXIT_CRITICAL(&app_spinlock);
    return app;
}

void set_front_application(application_t app)
{
    portENTER_CRITICAL(&app_spinlock);
    front_app = app;
    portEXIT_CRITICAL(&app_spinlock);
    return;
}
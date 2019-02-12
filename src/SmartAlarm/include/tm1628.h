#ifndef __TM1628_H_
#define __TM1628_H_
#include "display.h"

int tm1628_tick_process(display_buff_t *buff);
int tm1628_init(void);
#ifdef CONFIG_ENABLE_POWER_MANAGER
int tm1628_set_brightness(display_buff_t *buff);
#endif
#endif
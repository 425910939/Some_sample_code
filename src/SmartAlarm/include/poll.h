#ifndef __POLL_H__
#define __POLL_H__
#include <sys/time.h>
#include "rom/queue.h"

#define USE_XTIMER

#define SCALE_PER_MS 1000 //
#define POLL_TICK   100 //100ms tick

typedef enum poll_clock_source{
	PCLOCK_SOURCE_RTC = 0x00,
	PCLOCK_SOURCE_SYS = 0x01,
	PCLOCK_SOURCE_UNDEF,
}poll_clock_source_t;

typedef struct poll_func_config {
	poll_clock_source_t source;
	uint32_t period; //100ms tick
	void (*poll_callback)(void *param);
	void *param;
} poll_func_config_t;

typedef struct poll_func_entry {
	struct timeval expire;
	bool used;
	uint32_t period;
	void (*poll_callback)(void *param);
	void *param;
    LIST_ENTRY(poll_func_entry) entries;
} poll_func_entry_t;

typedef struct {
    int idx;                
} poll_event_t;

poll_func_entry_t *register_poll_func(poll_func_config_t *config);
int unregister_poll_func(poll_func_entry_t *entry);
int poll_task_init(void);
int poll_task_uninit(void);

#endif
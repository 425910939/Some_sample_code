#ifndef __POWER_MANAGER__
#define __POWER_MANAGER__

#define NAME_LEN  24
typedef enum {
	WAKE_LOCK_KEY =0, 
	WAKE_LOCK_MUSIC, 
	WAKE_LOCK_RECORD,
	WAKE_LOCK_NONE , 
} wake_type_t;

typedef struct wake_lock{
	wake_type_t type;
	uint32_t active;
	unsigned char name[NAME_LEN];
	struct wake_lock *next;
} wake_lock_t;
wake_lock_t *  wake_lock_init(wake_type_t type,unsigned char *name);
int release_wake_lock(wake_lock_t *r_lock);
int acquire_wake_lock(wake_lock_t *a_lock);
int wake_lock_destroy(wake_lock_t *d_lock);
int power_manager_init(void);
#endif
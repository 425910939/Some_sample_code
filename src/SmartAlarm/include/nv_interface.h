#ifndef __NV_INTERFACE_H__
#define __NV_INTERFACE_H__

typedef enum{
    NV_ITEM_AUDIO_VOLUME,
	NV_ITEM_POWER_ON_MODE,
    NV_ITEM_SYSTEM_PANIC_COUNT,
    NV_ITEM_UNDEFINE,
}nv_type_t;

typedef struct{
    nv_type_t name;
    int64_t value;
}nv_item_t;

int nv_init();
int get_nv_item(nv_item_t *item);
int set_nv_item(nv_item_t *item);
#endif

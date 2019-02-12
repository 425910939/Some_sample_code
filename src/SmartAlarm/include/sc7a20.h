#ifndef __SC7A20_H_
#define __SC7A20_H_
#include "extern_gpio.h"

typedef enum{
    ACC_CMD_INT_OCCUR,
    ACC_CMD_UNDEFINE,
}acc_cmd_t;

typedef struct{
    acc_cmd_t cmd;
    void *param;
    int param_size;
}acc_cmd_node_t;

void acc_enable(void);
void acc_disable(void);
int acc_init(void);
int acc_uninit(void);
#endif
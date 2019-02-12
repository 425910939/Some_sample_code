#ifndef __GC_H__
#define __GC_H__

#define GC_RULE_NAME_LENGTH (16)
#define GC_FILE_NAME_LENGTH (128)
#define GC_DIR_PATH         (256)

typedef enum{
    SD_CARD_ST_UNMOUNT = 0x00,
    SD_CARD_ST_MOUNTED,
    SD_CARD_ST_UNDEFINE = 0xff,
}sdcard_state_t;

typedef enum{
    GC_CMD_TIMEOUT = 0x00,
    GC_CMD_UNDEFINE = 0xff,
}gc_command_t;

typedef enum{
    GC_POLICY_TIME_OLDEST = 0x00,    //gc old file
    GC_POLICY_TIME_LASTEST,         //gc new file
    GC_POLICY_FILE_BIGGEST,       //gc biggest file
    GC_POLICY_FILE_SMALLEST,
}gc_policy_t;

typedef struct{
    char file_name[GC_FILE_NAME_LENGTH];
}gc_ignore_file_t;

typedef struct{
    char rule_name[GC_RULE_NAME_LENGTH];
    char gc_path[GC_DIR_PATH];
    gc_policy_t policy;
    int ignore_file_count;
    gc_ignore_file_t *file_ptr;
}gc_rule_t;

typedef struct{
    char rule_name[GC_RULE_NAME_LENGTH];
}gc_governor_t;
uint32_t get_sd_free_space(uint32_t *use_space);
int register_sdcard_notify(void *fd);
void garbage_cleanup_init(void);
void garbage_cleanup_uninit(void);
#endif
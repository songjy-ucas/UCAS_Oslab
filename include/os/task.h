#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <type.h>

#define TASK_MEM_BASE    0x52000000
#define TASK_MAXNUM      16
#define TASK_SIZE        0x10000
#define TASK_NAME_LEN    32 // 定义任务名的最大长度

#define SECTOR_SIZE 512
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* TODO: [p1-task4] implement your own task_info_t! */
typedef struct {
    char name[TASK_NAME_LEN]; // 任务名
    uint32_t offset;          // 在镜像文件中的偏移量 (bytes)
    uint32_t size;            // 在镜像文件中的大小 (bytes)
    uint64_t entry_point;     // 程序的入口地址
} task_info_t;

extern task_info_t tasks[TASK_MAXNUM]; // 使用extern声明,实际定义在main.c中，以避免重复定义

#endif
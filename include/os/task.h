#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <type.h>

#define TASK_MEM_BASE    0xffffffc052000000
#define TASK_MAXNUM      32
#define TASK_SIZE        0x10000
#define TASK_NAME_LEN    32 

// [Task 4] 临时缓冲区物理基址 (144MB处，避开内核和页表)
#define TMP_MEM_BASE     0x59000000 
// [Task 4] 用户程序固定入口虚拟地址
#define USER_ENTRYPOINT  0x10000

#define SECTOR_SIZE 512
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* [p1-task5] 定义共享内存地址和批处理任务数量 */
#define SHARED_MEM_ADDR  0x5f204000 // 一个安全、固定的内存地址
#define BATCH_TASK_NUM   4

/* TODO: [p1-task4] implement your own task_info_t! */
typedef struct {
    char name[TASK_NAME_LEN];
    uint32_t offset;
    uint32_t size;
    uint64_t entry_point;
    // P4-task2
    uint32_t p_memsz; 
} task_info_t;

extern task_info_t tasks[TASK_MAXNUM];

#endif
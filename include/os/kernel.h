#ifndef __INCLUDE_KERNEL_H__
#define __INCLUDE_KERNEL_H__

#include <type.h>
#include <common.h>

#define KERNEL_JMPTAB_BASE 0x51ffff00 //跳转表在内存中的起始地址
typedef enum {
    CONSOLE_PUTSTR,
    CONSOLE_PUTCHAR,
    CONSOLE_GETCHAR,
    SD_READ,
    SD_WRITE,
    QEMU_LOGGING,
    SET_TIMER,
    READ_FDT,
    MOVE_CURSOR,
    PRINT,
    YIELD,
    MUTEX_INIT,
    MUTEX_ACQ,
    MUTEX_RELEASE,
    NUM_ENTRIES
} jmptab_idx_t;

static inline long call_jmptab(long which, long arg0, long arg1, long arg2, long arg3, long arg4)
{
    // 计算函数指针在跳转表中的地址
    unsigned long val = \
        *(unsigned long *)(KERNEL_JMPTAB_BASE + sizeof(unsigned long) * which);
     // 将取出的地址转换成一个函数指针类型
    long (*func)(long, long, long, long, long) = (long (*)(long, long, long, long, long))val;
    
    // 调用该函数并返回结果
    return func(arg0, arg1, arg2, arg3, arg4);
}

static inline void bios_putstr(char *str)
{
    call_jmptab(CONSOLE_PUTSTR, (long)str, 0, 0, 0, 0);
}

static inline void bios_putchar(int ch)
{
    call_jmptab(CONSOLE_PUTCHAR, (long)ch, 0, 0, 0, 0);
}

static inline int bios_getchar(void)
{
    return call_jmptab(CONSOLE_GETCHAR, 0, 0, 0, 0, 0);
}

static inline int bios_sd_read(unsigned mem_address, unsigned num_of_blocks, \
                              unsigned block_id)
{
    return call_jmptab(SD_READ, (long)mem_address, (long)num_of_blocks, \
                        (long)block_id, 0, 0);
}

/************************************************************/

static inline int bios_sd_write(unsigned mem_address, unsigned num_of_blocks, \
                              unsigned block_id)
{
    return call_jmptab(SD_WRITE, (long)mem_address, (long)num_of_blocks, \
                        (long)block_id, 0, 0);
}

static inline void bios_logging(char *str)
{
    call_jmptab(QEMU_LOGGING, (long)str, 0, 0, 0, 0);
}

static inline void bios_set_timer(uint64_t stime_value)
{
    call_jmptab(SET_TIMER, (long)stime_value, 0, 0, 0, 0);
}

static inline uint64_t bios_read_fdt(enum FDT_TYPE type)
{
    return call_jmptab(READ_FDT, (long)type, 0, 0, 0, 0);
}
/************************************************************/

#endif
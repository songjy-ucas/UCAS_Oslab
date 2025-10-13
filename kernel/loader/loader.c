// 负责根据 taskid 计算用户程序在镜像/SD卡中的位置和在内存中的目标加载地址，然后调用BIOS提供的SD卡读取功能完成加载。
#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>

#define BOOT_SECTORS 1
#define KERNEL_SECTORS 15
#define APP_SECTORS 15

// 用户程序加载的内存基址和步长
#define APP_MEM_BASE    0x52000000
#define APP_MEM_STRIDE  0x10000 

uint64_t load_task_img(int taskid)
{
    /**
     * TODO:
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     
     * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
     */
     
    // 计算用户程序在 SD 卡中的起始扇区号
    int start_sector = BOOT_SECTORS + KERNEL_SECTORS + taskid * APP_SECTORS;

    // 计算用户程序要加载到的内存地址
    uint64_t load_addr = APP_MEM_BASE + (uint64_t)taskid * APP_MEM_STRIDE;

    // 3. 调用 BIOS 的 SD 卡读取功能
    bios_sd_read((uint32_t)load_addr, APP_SECTORS, start_sector);

    return load_addr;
}
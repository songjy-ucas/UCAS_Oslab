// 负责根据 taskid 计算用户程序在镜像/SD卡中的位置和在内存中的目标加载地址，然后调用BIOS提供的SD卡读取功能完成加载。
#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>

// ----------------- task3 used ----------------------
// #define BOOT_SECTORS 1
// #define KERNEL_SECTORS 15
// #define APP_SECTORS 15

// // 用户程序加载的内存基址和步长
// #define APP_MEM_BASE    0x52000000
// #define APP_MEM_STRIDE  0x10000     

// uint64_t load_task_img(int taskid)
// {
//     /**
//      * TODO:
//      * 1. [p1-task3] load task from image via task id, and return its entrypoint
     
//      * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
//      */
     
//     // 计算用户程序在 SD 卡中的起始扇区号
//     int start_sector = BOOT_SECTORS + KERNEL_SECTORS + taskid * APP_SECTORS;

//     // 计算用户程序要加载到的内存地址
//     uint64_t load_addr = APP_MEM_BASE + (uint64_t)taskid * APP_MEM_STRIDE;

//     // 3. 调用 BIOS 的 SD 卡读取功能
//     bios_sd_read((uint32_t)load_addr, APP_SECTORS, start_sector);

//     return load_addr;
// }


// ----------------- task4 used ----------------------

// next_free_mem 指向下一个可用的内存加载地址
static uint64_t next_free_mem = TASK_MEM_BASE;

// NBYTES2SEC宏
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

// 定义一个足够大的静态缓冲区，用于从SD卡读取原始扇区数据
// 假设单个用户程序不会超过16KB，缓冲区可以设置得稍大一些，比如32KB
#define READ_BUFFER_SIZE (1024 * 32)
static uint8_t read_buffer[READ_BUFFER_SIZE];

uint64_t load_task_img(const char *taskname)
{
    // 1. 在 tasks 数组中按名查找 
    int task_idx = -1;
    for (int i = 0; i < TASK_MAXNUM; i++) {
        if (tasks[i].name[0] != '\0' && strcmp(tasks[i].name, taskname) == 0) {
            task_idx = i;
            break;
        }
    }

    if (task_idx == -1) {
        bios_putstr("Task not found: '");
        bios_putstr((char *)taskname);
        bios_putstr("'\n\r");
        return 0;
    }

    // 从 task_info 中获取精确的字节级信息 
    task_info_t *info = &tasks[task_idx];
    uint32_t start_byte = info->offset;
    uint32_t file_size = info->size;

    // 2. 制定扇区读取计划 
    uint32_t start_sector = start_byte / SECTOR_SIZE;
    uint32_t end_byte = start_byte + file_size - 1;
    uint32_t end_sector = end_byte / SECTOR_SIZE;
    uint32_t num_sectors = end_sector - start_sector + 1;

    // 检查所需缓冲区大小是否超出限制 
    if (num_sectors * SECTOR_SIZE > READ_BUFFER_SIZE) {
        bios_putstr("Error: Task is too large to fit in the read buffer!\n\r");
        return 0;
    }

    // 3. 从SD卡读取包含目标程序的所有扇区到临时缓冲区 
    bios_sd_read((uintptr_t)read_buffer, num_sectors, start_sector);

    // 4. 从缓冲区中精确拷贝所需数据
    // 计算目标数据在缓冲区内的起始偏移量 
    uint32_t offset_in_buffer = start_byte % SECTOR_SIZE;

    // ====================================================================
    // 【prj2中发现问题后做如下修改】: 动态确定加载地址，并更新下一个可用地址
    // ====================================================================
    // 1. 确定本次加载到内存的目标地址
    uint64_t load_addr = next_free_mem;

    // 2. 使用 memcpy 将数据从缓冲区的正确位置，拷贝正确的大小，到最终的加载地址
    memcpy((void*)load_addr, read_buffer + offset_in_buffer, file_size);
    
    // 3. 更新 next_free_mem 指针，为下一个程序分配空间
    uint64_t end_addr = load_addr + file_size;
    #define PAGE_SIZE 4096
    // 计算对齐后的下一个空闲地址：
    // (end_addr + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE
    next_free_mem = (end_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    return load_addr; 
}
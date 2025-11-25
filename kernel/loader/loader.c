// 负责根据 taskid 计算用户程序在镜像/SD卡中的位置和在内存中的目标加载地址，然后调用BIOS提供的SD卡读取功能完成加载。
#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>

// NBYTES2SEC宏
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

// 定义一个足够大的静态缓冲区，用于从SD卡读取原始扇区数据
// 假设单个用户程序不会超过16KB，缓冲区可以设置得稍大一些，比如32KB
#define READ_BUFFER_SIZE (1024 * 32)
static uint8_t read_buffer[READ_BUFFER_SIZE];

uint64_t load_task_img(const char *taskname)
{
 // 1. 在全局 tasks 数组中按名查找
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
        return 0; // 返回0表示加载失败
    }

    task_info_t *info = &tasks[task_idx];
    uint32_t start_byte_offset = info->offset;
    uint32_t file_size = info->size;

    // 2. 计算需要读取的扇区范围
    uint32_t start_sector = start_byte_offset / SECTOR_SIZE;
    uint32_t end_sector = (start_byte_offset + file_size - 1) / SECTOR_SIZE;
    uint32_t num_sectors = end_sector - start_sector + 1;

    // 检查所需缓冲区大小是否超出限制
    if (num_sectors * SECTOR_SIZE > READ_BUFFER_SIZE) {
        bios_putstr("Error: Task is too large for the read buffer!\n\r");
        return 0;
    }

    // 3. 从SD卡读取包含目标程序的所有扇区到临时缓冲区
    bios_sd_read((uint32_t)(uintptr_t)read_buffer, num_sectors, start_sector);

    // 4. 从缓冲区中精确拷贝所需数据到最终加载地址
    // 计算目标数据在缓冲区内的起始偏移
    uint32_t offset_in_buffer = start_byte_offset % SECTOR_SIZE;

    // 确定最终加载到内存的目标地址 (假设每个任务有固定的内存空间)
    uint64_t load_addr = TASK_MEM_BASE + (uint64_t)task_idx * TASK_SIZE;

    // 使用 memcpy 将数据从缓冲区的正确位置，拷贝正确的大小，到最终的加载地址
    memcpy((void*)load_addr, read_buffer + offset_in_buffer, file_size);
    
    // BSS段的清理应由应用程序的启动代码(crt.S)或加载器完成。

    // 5. 返回程序的入口点
    return info->entry_point;
}

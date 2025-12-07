// 负责根据 taskid 计算用户程序在镜像/SD卡中的位置和在内存中的目标加载地址，然后调用BIOS提供的SD卡读取功能完成加载。
#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#include <os/mm.h>

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



/* 
 * [Task 4] 新版加载函数：加载到用户虚拟地址空间
 * 逻辑：物理缓冲区 -> 拷贝到用户页
 * 参数：
 *   taskname: 任务名
 *   pgdir:    目标进程的页目录表地址 (内核虚拟地址)
 * 返回值：
 *   成功返回用户虚拟地址入口 (USER_ENTRYPOINT)，失败返回 0
 */
uint64_t map_task(char *taskname, uintptr_t pgdir)
{
    int task_idx = -1;
    // 查找任务
    for (int i = 0; i < TASK_MAXNUM; i++) {
        if (tasks[i].name[0] != '\0' && strcmp(tasks[i].name, taskname) == 0) {
            task_idx = i;
            break;
        }
    }

    if (task_idx == -1) {
        // bios_putstr("Fail to find the task!\n");
        return 0;
    }

    task_info_t *info = &tasks[task_idx];

    // =============================================================
    // 1. 读取数据到临时物理缓冲区 (TMP_MEM_BASE)
    // =============================================================
    
    // 计算扇区信息
    uint32_t start_sector = info->offset / SECTOR_SIZE;
    // 考虑到 offset 不对齐的情况，读取长度需要覆盖首尾
    uint32_t end_sector = (info->offset + info->size - 1) / SECTOR_SIZE;
    uint32_t num_sectors = end_sector - start_sector + 1;

    // [关键修正] 传物理地址给 BIOS
    // 注意：这里我们直接用 TMP_MEM_BASE 这个常量物理地址
    bios_sd_read(TMP_MEM_BASE, num_sectors, start_sector);

    // =============================================================
    // 2. 建立页表映射并拷贝数据
    // =============================================================

    uint64_t user_va_start = USER_ENTRYPOINT;
    uint64_t user_va_end = USER_ENTRYPOINT + info->size; // 这里用 filesz 即可，BSS 另说
    
    // 2.1 遍历用户虚拟地址空间，按页分配物理内存并建立映射
    for (uint64_t va = user_va_start; va < user_va_end; va += PAGE_SIZE) {
        // alloc_page_helper 负责在 pgdir 中分配物理页并建立映射
        // 它会返回该物理页的内核虚拟地址，但这里我们要的是“先占坑”
        alloc_page_helper(va, pgdir);
    }

    // 2.2 拷贝数据 (从临时缓冲区 -> 目标物理页)
    // 计算源数据在 TMP_MEM_BASE 中的准确偏移
    uint32_t offset_in_sector = info->offset % SECTOR_SIZE;
    
    // [关键修正] 源地址转为内核虚拟地址
    // pa2kva(TMP_MEM_BASE) 得到缓冲区的虚拟基址
    uintptr_t src_va_base = pa2kva(TMP_MEM_BASE);
    uintptr_t src_ptr = src_va_base + offset_in_sector;

    // 按页拷贝逻辑 (避免 memcpy 跨物理页边界)
    uint64_t remain_size = info->size;
    uint64_t current_va = user_va_start;
    uintptr_t current_src = src_ptr;

    while (remain_size > 0) {
        // 获取当前目标页的内核虚拟地址 (destination)
        // alloc_page_helper 再次调用会直接返回已存在的页地址
        uintptr_t dest_page_kva = alloc_page_helper(current_va, pgdir);
        
        // 计算本次拷贝长度 (处理页内偏移和剩余长度)
        uint64_t page_offset = current_va % PAGE_SIZE;
        uint64_t page_remain = PAGE_SIZE - page_offset;
        uint64_t copy_len = (remain_size < page_remain) ? remain_size : page_remain;

        // 执行拷贝
        memcpy((void *)(dest_page_kva + page_offset), (void *)current_src, copy_len);

        // 更新游标
        remain_size -= copy_len;
        current_va += copy_len;
        current_src += copy_len;
    }

    // 2.3 BSS 清零 (如果有 memsz > filesz 信息)
    // 你的 task_info_t 暂时没有 memsz，如果后续加上，逻辑类似：
    // for (va = filesz_end; va < memsz_end; ...) alloc_page_helper(va) -> memset(0)

    return USER_ENTRYPOINT;
}
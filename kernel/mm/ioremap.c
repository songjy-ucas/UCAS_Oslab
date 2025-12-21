#include <os/ioremap.h>
#include <os/mm.h>
#include <pgtable.h>
#include <type.h>
#include <printk.h>

// maybe you can map it to IO_ADDR_START ?
static uintptr_t io_base = IO_ADDR_START;

void *ioremap(unsigned long phys_addr, unsigned long size)
{
    // TODO: [p5-task1] map one specific physical region to virtual address
    // 1. 强制 io_base 2MB 对齐 (向上取整)
    // 防止之前的 ioremap 留下了非对齐的尾巴
    uintptr_t align_mask = LARGE_PAGE_SIZE - 1;
    if (io_base & align_mask) {
        io_base = (io_base + align_mask) & ~align_mask;
    }

    uintptr_t va_start = io_base;
    uintptr_t va_end = io_base + size;

    // 2. 循环映射
    while (io_base < va_end) {
        int ret = kernel_map_page_helper(io_base, phys_addr, pa2kva(PGDIR_PA));
        printk("Mapping VA=%lx to PA=%lx, ret=%d\n", io_base, phys_addr, ret);
        if (ret == 0) {
            printk("ERROR: Mapping failed!\n");
            // 这里可能就是一个死循环或者错误返回
        }        
        // [修改核心] 既然 helper 映射的是 2MB，我们就跳过 2MB
        // 不要傻傻地 4KB 4KB 循环了，那是浪费且容易出错
        io_base   += LARGE_PAGE_SIZE;
        phys_addr += LARGE_PAGE_SIZE;
    }
    
    local_flush_tlb_all();
    return (void *)va_start;
}

void iounmap(void *io_addr)
{
    // TODO: [p5-task1] a very naive iounmap() is OK
    // maybe no one would call this function?
    PTE* pte = find_pte((uintptr_t)io_addr, current_running->pgdir);
    if(pte!=0)
        *pte = 0;
}

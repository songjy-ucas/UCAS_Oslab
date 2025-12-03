/* RISC-V kernel boot stage */
#include <pgtable.h>
#include <asm.h>

// 指定该部分代码放入 .bootkernel 段，确保链接时放在物理内存的起始位置
#define ARRTIBUTE_BOOTKERNEL __attribute__((section(".bootkernel"))) //bootkernel是自己定义的一个段，这里意思是 把函数定义时，只要有这个宏的东西放在.bootkernel段里

// 定义内核入口函数指针类型
typedef void (*kernel_entry_t)(unsigned long); // 意思是 是把unsigned long那个数值当作函数入口地址，然后直接跳转过去执行

/********* setup memory mapping ***********/

/**
 * @brief 简单的物理页分配器
 * 用于在启动阶段为页表分配物理空间。
 * 从 PGDIR_PA (0x51000000) 开始，每次分配 4KB (0x1000)。
 */
static uintptr_t ARRTIBUTE_BOOTKERNEL alloc_page()
{
    static uintptr_t pg_base = PGDIR_PA;
    pg_base += 0x1000;
    return pg_base;
}

/**
 * @brief 建立页表映射 (使用 2MB 大页)
 * @param va 虚拟地址 (Virtual Address)
 * @param pa 物理地址 (Physical Address)
 * @param pgdir 根页目录的基地址
 * 
 * 本函数实现了 Sv39 多级页表的填充。
 * Sv39 结构: VPN2(9bit) -> VPN1(9bit) -> VPN0(9bit) -> Offset(12bit)
 * 这里只映射到第二级 (PMD)，实现了 2MB 的大页映射，不需要第三级页表。
 */
static void ARRTIBUTE_BOOTKERNEL map_page(uint64_t va, uint64_t pa, PTE *pgdir)
{
    va &= VA_MASK; // 掩码操作，确保地址合法性
    
    // 提取一级页表索引 (VPN2)
    // 39位地址，最高9位是VPN2。Shift = 12(Offset) + 9(VPN0) + 9(VPN1) = 30
    uint64_t vpn2 =
        va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
    
    // 提取二级页表索引 (VPN1)
    // Shift = 12(Offset) + 9(VPN0) = 21
    uint64_t vpn1 = (vpn2 << PPN_BITS) ^
                    (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
    
    // 检查根页目录中对应的条目是否存在
    if (pgdir[vpn2] == 0) {
        // 如果不存在，分配一个新的物理页作为第二级页表
        // set_pfn: 设置页框号，alloc_page() >> 12
        set_pfn(&pgdir[vpn2], alloc_page() >> NORMAL_PAGE_SHIFT);
        // set_attribute: 设置有效位 (Valid)
        set_attribute(&pgdir[vpn2], _PAGE_PRESENT);
        // 清空新分配的页表内容，防止脏数据
        clear_pgdir(get_pa(pgdir[vpn2]));
    }
    
    // 获取第二级页表 (PMD) 的物理地址
    PTE *pmd = (PTE *)get_pa(pgdir[vpn2]);
    
    // 设置第二级页表项 (映射 2MB 物理大页)
    set_pfn(&pmd[vpn1], pa >> NORMAL_PAGE_SHIFT);
    
    // 设置属性：
    // _PAGE_PRESENT: 有效
    // _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC: 可读可写可执行 (RWX=111，代表这是叶子节点，即大页)
    // _PAGE_ACCESSED | _PAGE_DIRTY: 已访问、脏位 (避免首次访问触发异常)
    set_attribute(
        &pmd[vpn1], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                        _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY);
}

/**
 * @brief 启用虚拟内存
 * 写入 satp 寄存器并刷新 TLB
 */
static void ARRTIBUTE_BOOTKERNEL enable_vm()
{
    // write satp to enable paging
    // SATP_MODE_SV39: 设置模式为 Sv39
    // 0: ASID (进程ID) 设为 0
    // PGDIR_PA >> NORMAL_PAGE_SHIFT: 根页表的物理页框号 (PPN)
    set_satp(SATP_MODE_SV39, 0, PGDIR_PA >> NORMAL_PAGE_SHIFT);
    
    // 刷新 TLB (快表)，确保旧的地址翻译缓存失效
    local_flush_tlb_all();
}

/* Sv-39 mode
 * 0x0000_0000_0000_0000-0x0000_003f_ffff_ffff is for user mode
 * 0xffff_ffc0_0000_0000-0xffff_ffff_ffff_ffff is for kernel mode
 */
/**
 * @brief 初始化页表的核心函数
 */
static void ARRTIBUTE_BOOTKERNEL setup_vm()
{
    // 清空根页目录 (PGDIR_PA 对应物理地址 0x51000000)
    clear_pgdir(PGDIR_PA);
    
    // map kernel virtual address(kva) to kernel physical address(kpa)
    // kva = kpa + 0xffff_ffc0_0000_0000 use 2MB page,
    // map all physical memory
    
    // 1. 建立内核高地址映射
    // 将虚拟地址 [0xffffffc050000000, 0xffffffc060000000) 映射到 物理地址 [0x50000000, 0x60000000)
    // 这里的 pa2kva 宏用于计算对应的虚拟地址，kva2pa 宏用于计算对应的物理地址
    // 映射范围大小为 256MB，每次映射 2MB
    PTE *early_pgdir = (PTE *)PGDIR_PA;
    for (uint64_t kva = 0xffffffc050000000lu;
         kva < 0xffffffc060000000lu; kva += 0x200000lu) {
        map_page(kva, kva2pa(kva), early_pgdir);
    }
    
    // 2. 建立恒等映射 (Identity Mapping)
    // 将虚拟地址 [0x50000000, 0x51000000) 映射到 物理地址 [0x50000000, 0x51000000)
    // 这一步非常重要！因为此时 PC 指针还在低地址(0x50xxxxxx)运行。
    // 如果不建立这个映射，开启 MMU 的下一瞬间 CPU 就会因为找不到当前指令的地址而崩溃。
    // 这段映射通常只在启动阶段使用，进入内核后可以取消。
    for (uint64_t pa = 0x50000000lu; pa < 0x51000000lu;
         pa += 0x200000lu) {
        map_page(pa, pa, early_pgdir);
    }
    
    // 页表建立完毕，写入寄存器开启 MMU
    enable_vm();
}

// _start 是在链接脚本中定义的内核入口符号 (位于 main.c 或 entry.S)
extern uintptr_t _start[];

/*********** start here **************/
/**
 * @brief C 语言层面的启动入口
 * @param mhartid 硬件线程 ID (Core ID)，由 start.S 传入
 */
int ARRTIBUTE_BOOTKERNEL boot_kernel(unsigned long mhartid)
{
    // 只有 0 号核 (主核) 负责初始化页表
    if (mhartid == 0) {
        setup_vm();
    } else {
        // 其他核直接开启虚拟内存 (假设主核已经设置好了 satp 指向的页表内容)
        enable_vm();
    }

    /* enter kernel */
    // 关键跳转！
    // 1. _start 是内核入口的物理地址符号
    // 2. pa2kva(_start) 将其转换为内核的高虚拟地址 (例如 0xffffffc050201000)
    // 3. 强制转换为函数指针并调用
    // 这一步之后，PC 指针将从低地址跳转到高虚拟地址，正式进入内核。
    ((kernel_entry_t)pa2kva((uintptr_t)_start))(mhartid);

    return 0;
}
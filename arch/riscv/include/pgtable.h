#ifndef PGTABLE_H
#define PGTABLE_H

#include <type.h>

/* SATP 寄存器的模式定义 */
#define SATP_MODE_SV39 8  // Sv39 模式: 3级页表，39位虚拟地址
#define SATP_MODE_SV48 9  // Sv48 模式: 4级页表，48位虚拟地址

/* SATP 寄存器各字段的位移 */
#define SATP_ASID_SHIFT 44lu // ASID (地址空间ID) 起始位
#define SATP_MODE_SHIFT 60lu // MODE (模式) 起始位

/* 页面大小定义 */
#define NORMAL_PAGE_SHIFT 12lu // 普通页偏移量 (12位)
#define NORMAL_PAGE_SIZE (1lu << NORMAL_PAGE_SHIFT) // 普通页大小: 4KB
#define LARGE_PAGE_SHIFT 21lu  // 大页偏移量 (21位)
#define LARGE_PAGE_SIZE (1lu << LARGE_PAGE_SHIFT)   // 大页大小: 2MB

/*
 * 刷新整个本地 TLB (Translation Lookaside Buffer)。
 * 'sfence.vma' 指令也会隐式地刷新指令缓存(I-Cache)，
 * 所以通常不需要额外执行 'fence.i'。
 */
static inline void local_flush_tlb_all(void)
{
    __asm__ __volatile__ ("sfence.vma" : : : "memory");
}

/* 刷新本地 TLB 中的这一个页面的缓存 */
static inline void local_flush_tlb_page(unsigned long addr)
{
    // 只刷新指定虚拟地址 addr 对应的 TLB 条目
    __asm__ __volatile__ ("sfence.vma %0" : : "r" (addr) : "memory");
}

/* 刷新本地指令缓存 (I-Cache) */
static inline void local_flush_icache_all(void)
{
    asm volatile ("fence.i" ::: "memory");
}

/* 设置 satp 寄存器 (启用分页) */
static inline void set_satp(
    unsigned mode, unsigned asid, unsigned long ppn)
{
    // 构造 satp 寄存器的值: | MODE | ASID | PPN |
    unsigned long __v =
        (unsigned long)(((unsigned long)mode << SATP_MODE_SHIFT) | ((unsigned long)asid << SATP_ASID_SHIFT) | ppn);
    // 写入 satp 并执行 sfence.vma 确保生效
    __asm__ __volatile__("sfence.vma\ncsrw satp, %0" : : "rK"(__v) : "memory");
}

#define PGDIR_PA 0x51000000lu  // 将物理地址 0x51000000 用作内核页目录表的基地址

/*
 * PTE (页表项) 格式定义 (RISC-V Sv39):
 * | 63 ... 54 | 53 ... 10 | 9 ... 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 * | Reserved  |    PPN    | RSW     | D | A | G | U | X | W | R | V
 * 
 * PPN: 物理页号 (Physical Page Number)
 * RSW: 软件保留位
 * D: Dirty (脏位)
 * A: Accessed (访问位)
 * G: Global (全局位)
 * U: User (用户态可访问)
 * X: Executable (可执行)
 * W: Writable (可写)
 * R: Readable (可读)
 * V: Valid (有效位)
 */

#define _PAGE_ACCESSED_OFFSET 6

/* 页表项标志位掩码 */
#define _PAGE_PRESENT (1 << 0)  // Valid，有效位
#define _PAGE_READ (1 << 1)     /* Readable，可读 */
#define _PAGE_WRITE (1 << 2)    /* Writable，可写 */
#define _PAGE_EXEC (1 << 3)     /* Executable，可执行 */
#define _PAGE_USER (1 << 4)     /* User，用户态可访问 */
#define _PAGE_GLOBAL (1 << 5)   /* Global，全局映射 */
#define _PAGE_ACCESSED (1 << 6) /* Set by hardware on any access，已被访问 */
#define _PAGE_DIRTY (1 << 7)    /* Set by hardware on any write，已被修改(脏) */
#define _PAGE_SOFT (1 << 8)     /* Reserved for software，软件保留位 */

// [Task 3 Add] 定义 Swap 标志位，利用软件保留位
// 当 _PAGE_PRESENT 为 0 且 _PAGE_SWAP 为 1 时，表示页面在磁盘 Swap 区
#define _PAGE_SWAP (1 << 9)  

#define _PAGE_PFN_SHIFT 10lu    // PPN 在 PTE 中的起始位是第10位

#define VA_MASK ((1lu << 39) - 1) // 39位虚拟地址掩码

#define PPN_BITS 9lu              // 每一级页表索引占 9 位
#define NUM_PTE_ENTRY (1 << PPN_BITS) // 每一级页表包含 512 个页表项

typedef uint64_t PTE; // 页表项类型，64位无符号整数

/* 
 * 物理地址与内核虚拟地址之间的转换 
 * 假设内核使用线性映射，且位于高地址空间。
 * 常见的映射偏移量是 0xffffffff00000000 (针对 Sv39)
 */
static inline uintptr_t kva2pa(uintptr_t kva)
{
    /* [P4-task1] 内核虚拟地址 -> 物理地址 */
    // 逻辑：KVA - 偏移量 = PA
    // 注意：这里的 mask 是为了处理无符号数溢出特性，通常直接减即可
    return kva - 0xffffffc000000000lu;
}

static inline uintptr_t pa2kva(uintptr_t pa)
{
    /* [P4-task1] 物理地址 -> 内核虚拟地址 */
    // 逻辑：PA + 偏移量 = KVA
    return pa + 0xffffffc000000000lu;
}

/* 
 * 从 PTE 中获取物理页地址 (Physical Address) 
 * 逻辑：提取 PTE 中的 PPN 字段，左移 12 位 (PAGE_SHIFT) 还原成物理地址
 */
static inline uint64_t get_pa(PTE entry)
{
    /* [P4-task1] */
    // 1. entry >> _PAGE_PFN_SHIFT (10): 吧 PPN 移到最低位
    // 2. << NORMAL_PAGE_SHIFT (12): 将页号转换为物理地址 (乘以 4096)
    // 提示：RISC-V Sv39 中，PTE 的高 10 位是保留的，通常为0，这里做简单位移即可
    return (entry >> _PAGE_PFN_SHIFT) << NORMAL_PAGE_SHIFT;
}

/* 
 * 获取 PTE 中的页帧号 (Page Frame Number / PPN) 
 */
static inline long get_pfn(PTE entry)
{
    /* [P4-task1] */
    // 直接右移 10 位，丢弃低 10 位的标志位，剩下的就是 PPN
    return entry >> _PAGE_PFN_SHIFT;
}

/* 
 * 设置 PTE 中的页帧号 (PFN) 
 * 注意：需要保留原有的标志位 (低10位)
 */
static inline void set_pfn(PTE *entry, uint64_t pfn)
{
    /* [P4-task1] */
    // 1. 清除 entry 中原有的 PPN 部分 (保留低 10 位标志位)
    //    ((1lu << _PAGE_PFN_SHIFT) - 1) 生成掩码 0x3FF (即低10位全1)
    PTE flags = *entry & ((1lu << _PAGE_PFN_SHIFT) - 1);
    
    // 2. 将新的 pfn 左移到正确位置，并与标志位组合
    *entry = flags | (pfn << _PAGE_PFN_SHIFT);
}

/* 
 * 获取 PTE 的属性位 
 * mask: 需要查询的标志位掩码 (例如 _PAGE_READ)
 */
static inline long get_attribute(PTE entry, uint64_t mask)
{
    /* [P4-task1] */
    // 检查 entry 中对应的位是否被置位
    return entry & mask;
}

/* 
 * 设置 PTE 的属性位 
 * bits: 需要开启的标志位
 */
static inline void set_attribute(PTE *entry, uint64_t bits)
{
    /* [P4-task1] */
    // 使用或运算开启属性位
    *entry |= bits;
}

/* 
 * 清空页目录 (将一页内存填零)
 * pgdir_addr: 页目录的虚拟地址或物理地址 (取决于调用上下文，通常这里操作的是内核虚地址)
 */
static inline void clear_pgdir(uintptr_t pgdir_addr)
{
    /* [P4-task1] */
    // 一个页表占 4KB (NORMAL_PAGE_SIZE)
    // 将该区域的内存全部置为 0
    // 这里将其转换为指针进行操作
    
    // 假设 pgdir_addr 是对齐的，我们可以按 64位 (long) 步长清空
    long *p = (long *)pgdir_addr;
    for (int i = 0; i < (NORMAL_PAGE_SIZE / sizeof(long)); i++) {
        p[i] = 0;
    }
    
    // 或者按字节清空 (更通用，但慢一点点，编译器通常会优化)
    // char *p = (char *)pgdir_addr;
    // for (int i = 0; i < NORMAL_PAGE_SIZE; i++) {
    //     p[i] = 0;
    // }
}

#endif  // PGTABLE_H

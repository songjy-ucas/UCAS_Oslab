#include <os/mm.h>
#include <os/string.h>
#include <os/kernel.h>
#include <os/task.h>
#include <pgtable.h>
#include <os/sched.h>
#include <os/smp.h>
#include <printk.h>
#include <assert.h>
#include <os/debug.h>

/* [Fix] 包含 list.h */
#include <os/list.h>

/* [Fix] 对接 list.h API */
// add_node_to_q -> list_add_tail (插入队尾)
#define add_node_to_q(node, head) list_add_tail(node, head)
// delete_node_from_q -> list_del (从链表中删除)
#define delete_node_from_q(node)  list_del(node)
// lnode2info -> list_entry (从链表节点反推结构体)
#define lnode2info(node)          list_entry(node, alloc_info_t, lnode)

/* [Fix] 定义 alloc_info 数组和 Swap 起始扇区 */
#define USER_PAGE_MAX_NUM 1024
// #define KERN_PAGE_MAX_NUM 512 // 不再使用这个宏

// [P4-TASK3 START] 定义换页阈值和Swap起始扇区
#define SWAP_ENABLE_THRESHOLD 32      // 设置可用物理页为16页 --- > 改成32，debug发现执行用户程序之前就有24个页了，这24个是allocpage出来的，不是uva_allocpage的，in_mem_list都没有追踪这几个
uint64_t image_end_sec = 30000; // 假设Swap区从磁盘第30000扇区开始

#define PPN_MASK 0x1FF
// [P4-TASK3 END]

// 全局数组，用于存放所有用户空间页面的元数据信息
alloc_info_t alloc_info[USER_PAGE_MAX_NUM];

/* 链表头定义 (使用你的 list.h 中的宏 LIST_HEAD) */
// 链表头：用于管理当前在物理内存中的用户页面
LIST_HEAD(in_mem_list);
// 链表头：用于管理已被换出到磁盘的用户页面
LIST_HEAD(swap_out_list);
// 链表头：用于管理空闲的 alloc_info_t 元数据节点
LIST_HEAD(free_list);


// NOTE: A/C-core
// 内核动态内存分配的当前指针，指向下一个可分配的空闲内存的起始位置
static ptr_t kernMemCurr = FREEMEM_KERNEL;
// 记录当前已分配的物理页面总数
int usepage = 0;
// 全局变量，用于在换页时记录当前操作的进程的页表目录ID（在多进程场景下可能需要改进）
int pgdir_id; 

// 定义可用于动态分配的物理内存的总页数
#define TOTAL_PAGES 65536         
// 内核可动态分配内存区域的虚拟地址起始点
#define KERNELMEM_START 0xffffffc050000000lu 
// 内核可动态分配内存区域的虚拟地址结束点
#define KERNELMEM_END   0xffffffc060000000lu

// 宏：根据内核虚拟地址计算其在 page_bitmap 中的字节索引
#define BITMAP(n) ((n - KERNELMEM_START) / (8 * PAGE_SIZE))
// 宏：根据内核虚拟地址计算其在 page_bitmap 对应字节中的位偏移
#define BITMAP_OFFSET(n) (((n - KERNELMEM_START) / PAGE_SIZE) % 8)

// 全局位图，用于跟踪每个物理页框的使用状态 (0: 空闲, 1: 已使用)
static uint8_t page_bitmap[TOTAL_PAGES / 8];

// [P4-TASK3 START] 新增辅助函数声明
static alloc_info_t* swapPage();
ptr_t uva_allocPage(int numPage, uintptr_t uva);
// [P4-TASK3 END]

// 初始化内存管理器
void init_memory_manager() {
    bzero(page_bitmap, sizeof(page_bitmap)); // 将物理页状态位图清零
    usepage = 0;                             // 已分配页面计数器清零
    // 如果需要，可以在这里初始化内核保留区
    // [P4-TASK3 START]
    init_uva_alloc(); // 初始化换页元数据
    // [P4-TASK3 END]
}

// 检查物理内存是否已完全用满
bool is_memory_full()
{
    for (int i = 0; i < TOTAL_PAGES / 8; i++) {
        if (page_bitmap[i] != 0xff) { // 0xff 表示一个字节（8个页）都已分配
            return false;
        }
    }
    return true;
}

// 分配指定数量的物理页面
ptr_t allocPage(int numPage)
{
    // 对齐到页面大小
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    
    int count = 0;
    // 简单的防止死循环保护机制
    int max_checks = (TOTAL_PAGES / numPage) * 2; 

    // 线性扫描查找空闲页
    do {
        ret = ROUND(kernMemCurr, PAGE_SIZE);
        kernMemCurr = ret + numPage * PAGE_SIZE;
        
        // 如果扫描到内存末尾，则从头开始
        if (kernMemCurr >= KERNELMEM_END) {
            kernMemCurr = FREEMEM_KERNEL;
            if (is_memory_full()) {
                // [P4-TASK3 START] 内存满了，尝试换页而不是直接崩溃
                if (list_empty(&in_mem_list)) {
                    printk("Fatal: Memory Full and no page to swap out!\n");
                    assert(0);
                }
                swapPage(); // 换出一页为allocPage腾出空间
                // [P4-TASK3 END]
            }
        }
        
        count++;
        if (count > max_checks) {
            printk("Fatal: AllocPage Timeout!\n");
            assert(0); // 如果扫描次数过多，可能出现问题，断言失败
        }

    } while(page_bitmap[BITMAP(ret)] & (1 << (BITMAP_OFFSET(ret)))); // 检查位图，直到找到空闲页

    // 标记位图，表示这些页已被分配
    for(int i=0; i<numPage; i++){
        page_bitmap[BITMAP(ret + i*PAGE_SIZE)] |= (1 << (BITMAP_OFFSET(ret + i*PAGE_SIZE)));
    }
    
    usepage += numPage; // 更新已分配页面计数
    // 将新分配的页面清零，防止残留数据
    memset((void*)ret, 0, numPage * PAGE_SIZE);
    return ret; // 返回分配到的页面的起始内核虚拟地址
}

// 兼容接口：分配内核页面
ptr_t allocKernelPage(int numPage) { return allocPage(numPage); }
// 兼容接口：分配用户页面
ptr_t allocUserPage(int numPage) { return allocPage(numPage); }

// 释放一个物理页面
void freePage(ptr_t baseAddr)
{
    if (baseAddr == (ptr_t)NULL) return;
    if (baseAddr < KERNELMEM_START || baseAddr >= KERNELMEM_END) return;

    // 在位图中将对应位置零，表示页面已空闲
    page_bitmap[BITMAP(baseAddr)] &= ~(1 << (BITMAP_OFFSET(baseAddr)));
    usepage--; // 更新已分配页面计数
}

// [P4-TASK3 START] 为进程回收页面时需要考虑换页
void free_all_pages(pcb_t* pcb)
{
    PTE *pgd = (PTE*)pcb->pgdir;
    for(int i = 0; i < 256; i++) // 只遍历用户空间
    {
        if((pgd[i] & _PAGE_PRESENT) == 0) continue;

        PTE *pmd = (PTE *)pa2kva((get_pa(pgd[i])));
        for(int j = 0; j < 512; j++)
        {
            if((pmd[j] & _PAGE_PRESENT) == 0) continue;

            PTE *pte = (PTE *)pa2kva((get_pa(pmd[j])));
            for(int k = 0; k < 512; k++)
            {
                if(pte[k] & _PAGE_PRESENT){
                    freePage(pa2kva(get_pa(pte[k])));
                }
                else if (pte[k] & _PAGE_SWAP){
                    // 在此可以添加释放swap分区的逻辑
                }
            }
            freePage(pa2kva(get_pa(pmd[j])));
        }
        freePage(pa2kva(get_pa(pgd[i])));
    }
    freePage(pcb->pgdir);
}
// [P4-TASK3 END]


// 内核的动态内存分配函数 (简化版)
void *kmalloc(size_t size)
{
    // 简单实现：总是分配一整个页面
    return (void *)allocPage(1);
}

// 初始化换页机制所需的元数据链表
void init_uva_alloc(){
    for(int i=0; i<USER_PAGE_MAX_NUM; i++){
        // 将所有元数据节点加入空闲链表，表示它们都可用
        add_node_to_q(&alloc_info[i].lnode, &free_list);
        alloc_info[i].uva = 0;
        alloc_info[i].pa = 0;
        alloc_info[i].on_disk_sec = 0;
        alloc_info[i].pgdir_id = 0;
    }
}


// [P4-TASK3 START] 新增/重写以下核心函数

// 新增：纯查找PTE的函数，不存在时不分配
PTE* find_pte(uintptr_t va, uintptr_t pgdir) {
    va &= VA_MASK;
    uint64_t vpn2 = (va >> 30) & PPN_MASK;
    uint64_t vpn1 = (va >> 21) & PPN_MASK;
    uint64_t vpn0 = (va >> 12) & PPN_MASK;

    PTE *pgd = (PTE*)pgdir;
    if ((pgd[vpn2] & _PAGE_PRESENT) == 0) return NULL;
    PTE *pmd = (PTE *)pa2kva((get_pa(pgd[vpn2])));
    if((pmd[vpn1] & _PAGE_PRESENT) == 0) return NULL;
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
    return &pte[vpn0];
}

// 重写：换出页面
static alloc_info_t* swapPage(){
    if (list_empty(&in_mem_list)) {
        printk("Fatal: No page to swap out!\n");
        assert(0);
    }
    list_node_t* victim_node = in_mem_list.next; // FIFO
    alloc_info_t* info = lnode2info(victim_node);
    // =======================================================
    // // [关键调试信息]
    // printk("[SWAP_OUT] Victim page selected:\n");
    // printk("  - info_addr:   0x%x\n", (uint64_t)info);
    // printk("  - uva:         0x%x\n", info->uva);
    // printk("  - pa:          0x%x\n", info->pa);
    // printk("  - pgdir_id:    %d\n", info->pgdir_id);
    
    // // 检查 pgdir_id 的有效性
    // if (info->pgdir_id < 1 || info->pgdir_id >= NUM_MAX_TASK) {
    //     printk("  - FATAL: Invalid pgdir_id! Crashing...\n");
    //     while(1); // 死循环卡住
    // }
    
    // uintptr_t target_pgdir = pcb[info->pgdir_id].pgdir;
    // printk("  - target_pgdir: 0x%x\n", target_pgdir);
    // if (target_pgdir == 0) {
    //     printk("  - FATAL: Target pgdir is NULL! Crashing...\n");
    //     while(1); // 死循环卡住
    // }
    // =======================================================

    delete_node_from_q(victim_node);
    add_node_to_q(victim_node, &swap_out_list);
    
    bios_sd_write(info->pa, PAGE_SIZE/SECTOR_SIZE, image_end_sec);
    info->on_disk_sec = image_end_sec;
    image_end_sec += PAGE_SIZE/SECTOR_SIZE;
    
    PTE* pte = find_pte(info->uva, pcb[get_pcb_index_by_pid(info->pgdir_id)].pgdir);
    if(pte){
        uint64_t pte_val = (info->on_disk_sec << 10);
        set_attribute((PTE*)&pte_val, _PAGE_SWAP | get_attribute(*pte, 0xff));
        *pte = pte_val & ~_PAGE_PRESENT;
    }

    local_flush_tlb_page(info->uva);
    freePage(pa2kva(info->pa));
    return info;
}

// 重写：支持换页的统一页面分配函数
ptr_t uva_allocPage(int numPage, uintptr_t uva)
{
    // Step 1: 检查是否为换入 (Swap-In)
    PTE* pte = find_pte(uva, current_running->pgdir);
    if (pte && (*pte & _PAGE_SWAP)) {
        uint64_t disk_sec = get_pfn(*pte);
        alloc_info_t* in_info = NULL;

        for(list_node_t* node = swap_out_list.next; node != &swap_out_list; node = node->next) {
            alloc_info_t* temp_info = lnode2info(node);
            if (temp_info->uva == uva && temp_info->pgdir_id == current_running->pid) {
                in_info = temp_info;
                break;
            }
        }
        assert(in_info != NULL);

        if (usepage >= SWAP_ENABLE_THRESHOLD) {
            swapPage();
        }
        uintptr_t new_kva = allocPage(1);
        bios_sd_read(kva2pa(new_kva), PAGE_SIZE/SECTOR_SIZE, disk_sec);

        in_info->pa = kva2pa(new_kva);
        in_info->on_disk_sec = 0;
        delete_node_from_q(&in_info->lnode);
        // debug use
        klog("Adding page to in_mem_list: uva=0x%x, pid=%d\n", uva, current_running->pid);
        add_node_to_q(&in_info->lnode, &in_mem_list);

        return new_kva;
    }

    // Step 2: 首次分配
    if (list_empty(&free_list)) {
         printk("Fatal: No free alloc_info nodes!\n");
         assert(0);
    }
    list_node_t* new_lnode = free_list.next;
    alloc_info_t* in_info = lnode2info(new_lnode);
    delete_node_from_q(new_lnode);
    // debug use
    klog("Adding page to in_mem_list: uva=0x%x, pid=%d\n", uva, current_running->pid);
    add_node_to_q(new_lnode, &in_mem_list);
    
    in_info->uva = uva;
    in_info->pgdir_id = current_running->pid;

    // Step 3: 检查内存水位
    if(usepage >= SWAP_ENABLE_THRESHOLD){
        swapPage();
    }
    
    uintptr_t new_kva = allocPage(1);
    in_info->pa = kva2pa(new_kva);
    return new_kva;
}

// 新增：alloc_limit_page_helper
uintptr_t alloc_limit_page_helper(uintptr_t va, uintptr_t pgdir)
{
    va &= VA_MASK;
    uint64_t vpn2 = (va >> 30) & PPN_MASK;
    uint64_t vpn1 = (va >> 21) & PPN_MASK;
    uint64_t vpn0 = (va >> 12) & PPN_MASK;

    PTE *pgd = (PTE*)pgdir;
    if ((pgd[vpn2] & _PAGE_PRESENT) == 0) {
        set_pfn(&pgd[vpn2], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pgd[vpn2])));
    }
    
    PTE *pmd = (PTE *)pa2kva((get_pa(pgd[vpn2])));
    if((pmd[vpn1] & _PAGE_PRESENT) == 0){
        set_pfn(&pmd[vpn1], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pmd[vpn1])));
    }
    
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1])); 
    
    // 如果PTE无效（首次访问或已被换出）
    if((pte[vpn0] & _PAGE_PRESENT) == 0){
        // 调用统一的uva_allocPage来处理
        ptr_t kva = uva_allocPage(1, va & ~(PAGE_SIZE - 1));
        set_pfn(&pte[vpn0], kva2pa(kva) >> NORMAL_PAGE_SHIFT);
    }

    set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                            _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_USER);
    return pa2kva(get_pa(pte[vpn0]));
}
// [P4-TASK3 END]

// [P4-TASK3 START] 修改 do_page_fault
// [Task 2 新增] 缺页异常处理核心函数
void do_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // 直接委托给 alloc_limit_page_helper，和学长一样
    alloc_limit_page_helper(stval, current_running->pgdir);
    local_flush_tlb_all();
}
// [P4-TASK3 END]

// 建立映射辅助函数 ---- 用户程序页表映射建立
int map_page_helper(uintptr_t va, uintptr_t pa, uintptr_t pgdir){
    va &= VA_MASK;
    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    PTE *pgd = (PTE*)pgdir;
    if (pgd[vpn2] == 0) {
        set_pfn(&pgd[vpn2], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pgd[vpn2])));
    }
    
    PTE *pmd = (uintptr_t *)pa2kva((get_pa(pgd[vpn2])));
    if(pmd[vpn1] == 0){
        set_pfn(&pmd[vpn1], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT); // 非叶子节点 都不要 _PAGE_USER
        clear_pgdir(pa2kva(get_pa(pmd[vpn1])));
    }
    
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
    
    if(pa == 0){
        pte[vpn0] = 0;
        return 1;
    } else {
        set_pfn(&pte[vpn0], pa >> NORMAL_PAGE_SHIFT);
        set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                            _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_USER);
        return 1;
    }
    return 0;
}

// 将虚地址和给定实地址的映射关系存于给定页表(内核)使用2级页表，执行成功返回1，否则返回0 --- 使用大页2MB
int kernel_map_page_helper(uintptr_t va, uintptr_t pa, uintptr_t pgdir){
    va &= VA_MASK;
    uint64_t vpn2 =
        va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
    uint64_t vpn1 = (vpn2 << PPN_BITS) ^
                    (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
    PTE *pgd = (PTE*)pgdir;
    if (pgd[vpn2] == 0) {
        // 分配一个新的三级页目录，注意需要转化为实地址！
        set_pfn(&pgd[vpn2], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pgd[vpn2])));
    }
    PTE *pmd = (uintptr_t *)pa2kva((get_pa(pgd[vpn2])));
    if(pa==0){
        pmd[vpn1] = 0;
        return 1;
    }
    // 将对应实地址置为pa
    else if(pmd[vpn1]==0){
        set_pfn(&pmd[vpn1], pa >> NORMAL_PAGE_SHIFT);
        set_attribute(
            &pmd[vpn1], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                            _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY);
        return 1;
    }
    return 0;
}


// 分配物理页并映射 (alloc_page_helper)
uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir)
{
    va &= VA_MASK;
    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    PTE *pgd = (PTE*)pgdir;
    // 检查并分配二级页表
    if (pgd[vpn2] == 0) {
        // allocPage 返回内核虚地址，kva2pa 转为物理地址，再存入 PTE
        set_pfn(&pgd[vpn2], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT);
        // 清空新分配的页表页
        clear_pgdir(pa2kva(get_pa(pgd[vpn2])));
    }
    
    PTE *pmd = (PTE *)pa2kva((get_pa(pgd[vpn2])));
    // 检查并分配一级页表
    if(pmd[vpn1] == 0){
        set_pfn(&pmd[vpn1], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pmd[vpn1])));
    }
    
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1])); 
    
    // 检查并分配最终的物理页 (叶子节点)
    if(pte[vpn0] == 0){
        // [Task 1 修改] 直接分配物理页，暂不使用带 Swap 的逻辑
        ptr_t pa = kva2pa(allocPage(1)); 
        set_pfn(&pte[vpn0], pa >> NORMAL_PAGE_SHIFT);
    }
    
    // 设置页表项属性 (User, Valid, RWX, Dirty, Accessed)
    set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                            _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_USER);
                            
    // 返回分配到的物理页的内核虚拟地址
    return pa2kva(get_pa(pte[vpn0]));
}

// 简单的页表共享函数：拷贝内核映射到用户页表
void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir)
{
    // 利用 Sv39 的特性，直接拷贝整个页目录页
    // 实际上只需要拷贝高地址部分，但全拷贝最简单安全
    memcpy((void *)dest_pgdir + 2048, (void *)src_pgdir + 2048, PAGE_SIZE /2);
}

// TODO [P4-task4]
uintptr_t shm_page_get(int key) { return 0; }
void shm_page_dt(uintptr_t addr) {}

void free_process_memory(pcb_t *proc) 
{
    // 1. 回收页表和物理页 (你可能已经有了类似的 free_all_pages 函数)
    PTE *pgd = (PTE*)proc->pgdir;
    // 只回收用户空间 (VPN2 < 256)
    for (int i = 0; i < 256; i++) {
        if ((pgd[i] & _PAGE_PRESENT) == 0) continue;
        PTE *pmd = (PTE*)pa2kva(get_pa(pgd[i]));
        for (int j = 0; j < 512; j++) {
            if ((pmd[j] & _PAGE_PRESENT) == 0) continue;
            
            // 注意：如果 pmd[j] 是一个大页，则直接回收，不继续向下
            if ((pmd[j] & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)) != 0) {
                 freePage(pa2kva(get_pa(pmd[j])));
                 continue;
            }

            PTE *pte = (PTE*)pa2kva(get_pa(pmd[j]));
            for (int k = 0; k < 512; k++) {
                if (pte[k] & _PAGE_PRESENT) {
                    freePage(pa2kva(get_pa(pte[k])));
                } else if (pte[k] & _PAGE_SWAP) {
                    // 如果页在磁盘上，回收它占用的Swap空间
                    long swap_idx = get_pfn(pte[k]);
                    // free_swap_index(swap_idx); // 你需要实现这个函数
                }
            }
            freePage(pa2kva(get_pa(pmd[j]))); // 回收L0页表本身
        }
        freePage(pa2kva(get_pa(pgd[i]))); // 回收L1页表本身
    }
    freePage(proc->pgdir); // 回收L2页表（根页表）本身
    proc->pgdir = 0; // 防止悬空指针


    // 2. [关键] 回收换页系统的元数据
    
    // 2.1 遍历 in_mem_list，移除属于该进程的所有节点
    list_node_t *current = in_mem_list.next;
    while (current != &in_mem_list) {
        alloc_info_t *info = lnode2info(current);
        list_node_t *next = current->next; // 保存下一个节点
        
        if (info->pgdir_id == proc->pid) {
            delete_node_from_q(current);
            // 清理元数据并将其放回 free_list
            info->uva = 0;
            info->pa = 0;
            info->pgdir_id = 0;
            add_node_to_q(&info->lnode, &free_list);
        }
        current = next;
    }

    // 2.2 遍历 swap_out_list，移除属于该进程的所有节点
    current = swap_out_list.next;
    while (current != &swap_out_list) {
        alloc_info_t *info = lnode2info(current);
        list_node_t *next = current->next;
        
        if (info->pgdir_id == proc->pid) {
            delete_node_from_q(current);
            // free_swap_index(info->on_disk_sec); // 回收磁盘空间
            // 清理元数据并放回 free_list
            info->uva = 0;
            info->pa = 0;
            info->pgdir_id = 0;
            info->on_disk_sec = 0;
            add_node_to_q(&info->lnode, &free_list);
        }
        current = next;
    }

    // 3. 回收内核栈
    freePage(proc->kernel_stack_base);
    proc->kernel_sp = 0;
}

// [P4-task4] 获取系统可用内存
size_t do_get_free_memory()
{
    size_t free_pages = 0;
    size_t bitmap_size = TOTAL_PAGES / 8; // 65536 / 8 = 8192 字节
    
    for (size_t i = 0; i < bitmap_size; i++) {
        uint8_t byte = page_bitmap[i];
        
        // 优化：如果这一个字节全是1 (0xFF)，说明8个页都满了，直接跳过
        if (byte == 0xFF) {
            continue;
        }
        
        // 如果字节全是0，说明8个页都空闲
        if (byte == 0x00) {
            free_pages += 8;
            continue;
        }

        // 否则逐位检查
        for (int j = 0; j < 8; j++) {
            // 如果第 j 位是 0，说明该页空闲
            if (((byte >> j) & 1) == 0) {
                free_pages++;
            }
        }
    }

    return free_pages * PAGE_SIZE; // 返回字节数
}


// [P4-Task5] 辅助函数：根据虚拟地址获取 PTE 指针
// va: 虚拟地址
// pgdir: 页目录基地址 (内核虚地址)
// alloc: 如果中间页表不存在，是否分配？(Receiver传1, Sender传0)
// 返回值: 指向对应 PTE 的指针 (内核虚地址)，如果失败返回 0
uintptr_t get_pte_of_user_addr(uintptr_t va, uintptr_t pgdir, int alloc)
{
    va &= VA_MASK;
    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    PTE *pgd = (PTE*)pgdir;

    // --- Level 2 (Root) ---
    if ((pgd[vpn2] & _PAGE_PRESENT) == 0) {
        if (!alloc) return 0; // 不分配则直接返回失败

        // 分配 Level 1 页表
        ptr_t new_page = allocPage(1);
        set_pfn(&pgd[vpn2], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT);
        clear_pgdir(new_page);
    }

    // --- Level 1 (PMD) ---
    // 通过 get_pa 获取物理地址，再通过 pa2kva 转为内核虚地址以便访问
    PTE *pmd = (PTE *)pa2kva((get_pa(pgd[vpn2])));
    
    if ((pmd[vpn1] & _PAGE_PRESENT) == 0) {
        if (!alloc) return 0;

        // 分配 Level 0 页表
        ptr_t new_page = allocPage(1);
        set_pfn(&pmd[vpn1], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT);
        clear_pgdir(new_page);
    }

    // --- Level 0 (Leaf PTE) ---
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));

    // 返回指向最终页表项的指针 (地址)
    return (uintptr_t)&pte[vpn0];
}

// P4 Task5 新增
/* =========================================================================
 *              内存辅助函数 (用于 IPC 处理换页)
 * ========================================================================= */

// 辅助函数：检查虚拟地址所在的页是否在内存中。
// 如果在 Swap 区，则强制换入 (Swap-In)。
// 成功返回 1，失败（非法地址）返回 0。
int check_and_swap_in(uintptr_t va) {
    // 1. 获取当前进程的 PTE (不分配中间页表)
    PTE *pte = (PTE *)get_pte_of_user_addr(va, current_running->pgdir, 0);
    
    if (!pte) return 0; // 页表项不存在

    // 2. 如果页面已经在内存中，直接通过
    if (*pte & _PAGE_PRESENT) {
        return 1;
    }

    // 3. 如果页面在 Swap 分区中，必须换入！
    // Mailbox 需要 CPU 读写数据，CPU 无法直接访问磁盘上的数据。
    if (*pte & _PAGE_SWAP) {
        // 调用你 mm.c 中实现的 uva_allocPage。
        // 根据你的描述，uva_allocPage 内部会自动检测 _PAGE_SWAP 并执行 bios_sd_read。
        // 这里的 1 表示分配 1 页。
        uva_allocPage(1, va);
        
        // 再次检查确认
        if (*pte & _PAGE_PRESENT) {
            return 1;
        }
    }
    
    return 0;
}

// 更新物理地址 pa 对应的页面元数据，将其归属权转移给 new_pid 的 new_uva
void update_page_mapping_info(uintptr_t pa, pid_t new_pid, uintptr_t new_uva) {
    // 遍历在内存中的页面列表
    list_node_t *node = in_mem_list.next;
    while (node != &in_mem_list) {
        alloc_info_t *info = list_entry(node, alloc_info_t, lnode);
        if (info->pa == pa) {
            // 找到了对应的物理页，更新所有者信息
            info->pgdir_id = new_pid;
            info->uva = new_uva;
            return;
        }
        node = node->next;
    }
    // 如果在 in_mem_list 没找到，说明逻辑有严重错误（Pipe传递的必须是物理页）
    // 或者该页是内核页未被 alloc_info 管理。
}

// [mm.c] 新增

// 1. 从换页管理系统中移除页面（Pin）
// 用于 Pipe Give：页面进入内核缓冲区，不再受 Swap 算法管理
void verify_ptr_and_pin_page(uintptr_t pa) {
    list_node_t *node = in_mem_list.next;
    while (node != &in_mem_list) {
        alloc_info_t *info = list_entry(node, alloc_info_t, lnode);
        if (info->pa == pa) {
            // 从内存链表中移除
            list_del(node);
            // 重置信息并放回空闲链表
            info->uva = 0;
            info->pa = 0;
            info->pgdir_id = 0;
            info->on_disk_sec = 0;
            list_add_tail(node, &free_list);
            return;
        }
        node = node->next;
    }
}

// 2. 向换页管理系统注册页面（Unpin）
// 用于 Pipe Take：页面重新分配给用户进程，接受 Swap 管理
void register_page_for_process(uintptr_t pa, uintptr_t uva, pid_t pid) {
    if (list_empty(&free_list)) {
        // 极端情况：元数据节点不够用，这不应该发生，除非泄露
        printk("Fatal: No free alloc_info nodes in register!\n");
        assert(0);
    }
    
    list_node_t *node = free_list.next;
    list_del(node); // 从 free_list 取出
    
    alloc_info_t *info = list_entry(node, alloc_info_t, lnode);
    info->pa = pa;
    info->uva = uva;
    info->pgdir_id = pid;
    info->on_disk_sec = 0;
    
    // 加入内存管理链表
    list_add_tail(node, &in_mem_list);
}

// rw 检测缺页命令行
// exec rw 0x10800000 0x80200000 0xa0000320
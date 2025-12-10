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

/* [Fix] 包含你的 list.h */
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
#define SWAP_ENABLE_THRESHOLD 32      // 设置可用物理页为16页
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
static PTE* find_pte(uintptr_t va, uintptr_t pgdir);
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
static PTE* find_pte(uintptr_t va, uintptr_t pgdir) {
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



// #include <os/mm.h>
// #include <os/string.h>
// #include <printk.h>
// #include <assert.h>
// #include <os/task.h>
// #include <pgtable.h>
// #include <os/sched.h>
// #include <os/smp.h>
// #include <os/kernel.h>
// #include <screen.h> 

// // static ptr_t kernMemCurr = FREEMEM_KERNEL;
// // int usepage = 0;

// // // 定义内存范围和位图
// // #define TOTAL_PAGES 65536         // 总共 256MB 内存
// // #define KERNELMEM_START 0xffffffc050000000lu
// // #define KERNELMEM_END   0xffffffc060000000lu

// // // 位图操作宏
// // #define BITMAP(n) ((n - KERNELMEM_START)/(8*PAGE_SIZE))
// // #define BITMAP_OFFSET(n) (((n - KERNELMEM_START)/PAGE_SIZE)%8)



// #define MEM_START  0xffffffc050000000lu
// #define MEM_END    0xffffffc060000000lu
// #define MAX_PAGES  ((MEM_END - MEM_START) / PAGE_SIZE)

// // 物理页分配位图
// static uint8_t mem_bitmap[MAX_PAGES / 8];
// static ptr_t kernMemCurr = FREEMEM_KERNEL;
// static int total_used_pages = 0;

// // 宏：操作位图
// #define GET_BIT(map, idx)  (map[(idx)/8] &  (1 << ((idx)%8)))
// #define SET_BIT(map, idx)  (map[(idx)/8] |= (1 << ((idx)%8)))
// #define CLR_BIT(map, idx)  (map[(idx)/8] &= ~(1 << ((idx)%8)))
// #define PHY_ADDR_TO_IDX(addr) (((addr) - MEM_START) / PAGE_SIZE)
// #define IDX_TO_PHY_ADDR(idx)  (MEM_START + (idx) * PAGE_SIZE)

// // =========================================================================
// //  2. Swap 空间管理 (Swap Space Manager)
// // =========================================================================

// // 假设 Swap 区域在 SD 卡的 512MB 处开始 (2000000 扇区)
// // 请确保 Makefile 里的 padding 足够大
// #define SWAP_START_SEC  262144  // 512MB / 512B
// #define MAX_SWAP_PAGES  4096     // 支持换出 4096 个页 (16MB Swap 空间)



// #define SOFT_PAGE_LIMIT 32

// static uint8_t swap_bitmap[MAX_SWAP_PAGES / 8];

// static long alloc_swap_index() {
//     for (int i = 1; i < MAX_SWAP_PAGES; i++) {
//         if (!GET_BIT(swap_bitmap, i)) {
//             SET_BIT(swap_bitmap, i);
//             return i;
//         }
//     }
//     return -1;
// }

// // 释放 Swap 索引
// static void free_swap_index(long index) {
//     if (index >= 0 && index < MAX_SWAP_PAGES) {
//         CLR_BIT(swap_bitmap, index);
//     }
// }

// // 将 Swap 索引转换为 SD 卡扇区号
// static uint32_t swap_idx_to_sector(long index) {
//     return SWAP_START_SEC + index * 8; // 1 Page = 4KB = 8 Sectors
// }

// // =========================================================================
// //  3. FIFO 页面置换队列 (FIFO Queue)
// // =========================================================================

// typedef struct {
//     ptr_t pa;           // 物理地址
//     uintptr_t va;       // 对应的虚拟地址
//     uintptr_t pgdir;    // 所属页表基地址
//     int valid;          // 标记该记录是否有效 (Page 可能被 free 了)
// } fifo_entry_t;

// // 环形队列大小等于物理页总数
// static fifo_entry_t fifo_queue[MAX_PAGES];
// static int fifo_head = 0;
// static int fifo_tail = 0;

// void fifo_init() {
//     fifo_head = 0;
//     fifo_tail = 0;
//     for (int i = 0; i < MAX_PAGES; i++) fifo_queue[i].valid = 0;
// }

// // 加入队列 (Allocate 时调用)
// void fifo_push(ptr_t pa, uintptr_t va, uintptr_t pgdir) {
//     fifo_queue[fifo_tail].pa = pa;
//     fifo_queue[fifo_tail].va = va;
//     fifo_queue[fifo_tail].pgdir = pgdir;
//     fifo_queue[fifo_tail].valid = 1;
//     fifo_tail = (fifo_tail + 1) % MAX_PAGES;
// }

// // 移除队列中的特定物理页 (Free 时调用，可选，实现 Lazy Delete 更简单)
// // 这里我们仅提供接口，具体在 swap_out 中处理 Lazy Delete

// // =========================================================================
// //  4. 核心功能实现
// // =========================================================================

// void swap_init() {
//     bzero(mem_bitmap, sizeof(mem_bitmap));
//     bzero(swap_bitmap, sizeof(swap_bitmap));
//     fifo_init();
//     kernMemCurr = FREEMEM_KERNEL;
//     printk("Swap Manager Initialized.\n");
// }

// // 内部函数：查找指定 VA 的 PTE
// static PTE* get_pte_ptr(uintptr_t va, uintptr_t pgdir) {
//     va &= VA_MASK;
//     uint64_t vpn2 = va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
//     uint64_t vpn1 = (vpn2 << PPN_BITS) ^ (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
//     uint64_t vpn0 = (vpn2 << (PPN_BITS + PPN_BITS)) ^ (vpn1 << PPN_BITS) ^ (va >> NORMAL_PAGE_SHIFT);
    
//     PTE *pgd = (PTE*)pgdir;
//     if (!(pgd[vpn2] & _PAGE_PRESENT)) return NULL;
    
//     PTE *pmd = (PTE *)pa2kva(get_pa(pgd[vpn2]));
//     if (!(pmd[vpn1] & _PAGE_PRESENT)) return NULL;
    
//     PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
//     return &pte[vpn0];
// }

// // 核心：换出页面
// // 返回值：腾出的物理页地址 (Kernel Virtual Address)

// static int global_swap_out = 0;
// static int global_swap_in = 0;
// static ptr_t swap_out() {


    
//     ptr_t victim_pa = 0;
//     uintptr_t victim_va = 0;
//     uintptr_t victim_pgdir = 0;
//     int found = 0;

//     // 1. FIFO 查找牺牲页
//     while (fifo_head != fifo_tail) {
//         // 取出队头
//         fifo_entry_t *entry = &fifo_queue[fifo_head];
//         fifo_head = (fifo_head + 1) % MAX_PAGES;

//         if (entry->valid == 0) continue; // 跳过已无效的记录

//         // 检查物理页是否真的被占用 (Robust check)
//         long mem_idx = PHY_ADDR_TO_IDX(entry->pa);
//         if (!GET_BIT(mem_bitmap, mem_idx)) continue; // 已经被 free 了

//         // 检查页表反向映射是否一致
//         PTE *pte = get_pte_ptr(entry->va, entry->pgdir);
//         if (!pte) continue; // 页表项也没了

//         uint64_t current_pa = get_pa(*pte);
//         if (kva2pa(entry->pa) != current_pa) continue; // 页表指向了别的物理页，记录过期

//         // 检查 A/D 位 (对于 Clock 算法有用，FIFO 忽略，但可以打印调试)
//         // if (*pte & _PAGE_ACCESSED) ... 

//         // 找到了合适的牺牲页
//         victim_pa = entry->pa;
//         victim_va = entry->va;
//         victim_pgdir = entry->pgdir;
//         found = 1;
//         break;
//     }

//     if (!found) {
//         printk("[Warning] swap_out: No victim found (All freed?)\n");;
//         // assert(0);
//         return 0;
//     }

//     // 2. 分配 Swap 空间
//     long swap_idx = alloc_swap_index();
//     if (swap_idx == -1) {
//         printk("PANIC: Swap space full!\n");
//         assert(0);
//     }
//     uint32_t sector = swap_idx_to_sector(swap_idx);

//     // 3. 写入 SD 卡
//     // 注意：bios_sd_write 需要物理地址
//     bios_sd_write(kva2pa(victim_pa), 8, sector);

//     // 4. 更新 PTE
//     PTE *pte = get_pte_ptr(victim_va, victim_pgdir);
//     // 构造 Swap PTE: Valid=0, PPN=swap_index
//     // 将 swap_idx 存入 PPN 字段 (PTE >> 10)
//     *pte = (uint64_t)swap_idx << 10 |  _PAGE_SOFT; 
//     // 此时 *pte & _PAGE_PRESENT 为 0
//     // 5. 刷新 TLB
//     local_flush_tlb_all(); // 简单粗暴刷新所有，保证一致性

//     // 6. 返回物理页 (此时该物理页仍被 bitmap 标记为使用，调用者负责重用)
//     // 清空数据，防止信息泄露
//     clear_pgdir(victim_pa);

//     return victim_pa;
// }

// // 核心：分配物理页 (包含 Swap 触发逻辑)
// ptr_t allocPage(int numPage)
// {
//     // 目前仅支持 numPage=1 的换出
//     // 如果一次请求多页且内存不足，暂不处理复杂的连续换出
//     // static int debug_alloc_cnt = 0;
//     // if (debug_alloc_cnt >= 16) { 
//     // // 强制认为内存满了
//     //    return swap_out(); 
//     // }
//     // debug_alloc_cnt++;
//     ptr_t ret = 0;
//     if (total_used_pages > SOFT_PAGE_LIMIT && numPage == 1) {
//         // 尝试 Swap
//         ret = swap_out();
//         if (ret != 0) {
//             // Swap 成功，直接返回复用的物理页
//             return ret;
//         }
//         // 如果 Swap 返回 0（说明 FIFO 里没东西，或者没法换），
//         // 那么为了保证系统不死机（比如启动阶段），我们**降级**，
//         // 继续向下执行，去物理内存里找空闲页。
//     }
//     // 简单的 Next-Fit 搜索
//     ptr_t search_start = kernMemCurr;
//     int found_idx = -1;

//     // 第一次尝试：寻找空闲页
//     for (int i = 0; i < MAX_PAGES; i++) {
//         long idx = PHY_ADDR_TO_IDX(kernMemCurr);
        
//         // 检查是否连续 numPage 空闲
//         int free_count = 0;
//         for (int k = 0; k < numPage; k++) {
//             if ((idx + k) < MAX_PAGES && !GET_BIT(mem_bitmap, idx + k)) {
//                 free_count++;
//             } else {
//                 break;
//             }
//         }

//         if (free_count == numPage) {
//             found_idx = idx;
//             break;
//         }

//         kernMemCurr += PAGE_SIZE;
//         if (kernMemCurr >= MEM_END) kernMemCurr = FREEMEM_KERNEL;
//     }

//     // 如果没找到空闲页，且只需要 1 页，则执行 Swap Out
//     if (found_idx == -1) {
//         if (numPage == 1) {
//             ret = swap_out(); // 强制腾出一页
//             // return ret;
//             ret = swap_out();
//             if (ret != 0) return ret;
//             printk("Panic: allocPage failed! No victim & No free space.\n");
//             // while(1); // 调试时可以打开死循环卡住现场
//             return 0;
//         } else {
//             printk("Alloc %d pages failed (fragmentation or full)\n", numPage);
//             return 0;
//         }
//     }

//     // 找到了空闲页，设置位图
//     ret = IDX_TO_PHY_ADDR(found_idx);
//     for (int k = 0; k < numPage; k++) {
//         SET_BIT(mem_bitmap, found_idx + k);
//     }
//     total_used_pages += numPage;

//     // 更新下次搜索位置
//     kernMemCurr = ret + numPage * PAGE_SIZE;
//     if (kernMemCurr >= MEM_END) kernMemCurr = FREEMEM_KERNEL;

//     // 清空内存 (健壮性)
//     clear_pgdir(ret);

//     return ret;
// }



// void init_memory_manager() {
//     bzero(mem_bitmap, sizeof(mem_bitmap));
//     bzero(swap_bitmap, sizeof(swap_bitmap));
//     fifo_init();
//     kernMemCurr = FREEMEM_KERNEL;
//     total_used_pages = 0;

//     // 计算物理内存结束位置的索引，防止越界分配
//     int max_pages = (MEM_END - MEM_START) / PAGE_SIZE;
    
//     // 【重要】不再人为将位图置 1，让位图保持全 0 (空闲)
//     // 只标记内核静态区
//     int start_idx = PHY_ADDR_TO_IDX(FREEMEM_KERNEL);
//     for (int i = 0; i < start_idx; i++) {
//         SET_BIT(mem_bitmap, i);
//         total_used_pages++;
//     }

//     printk("> Memory Manager Initialized. Soft Limit: %d pages.\n", SOFT_PAGE_LIMIT);
// }


// void freePage(ptr_t baseAddr)
// {
//     if (baseAddr < MEM_START || baseAddr >= MEM_END) return;
    
//     long idx = PHY_ADDR_TO_IDX(baseAddr);
//     if (GET_BIT(mem_bitmap, idx)) {
//         CLR_BIT(mem_bitmap, idx);
//         total_used_pages--;
//         // 注意：我们不主动去 FIFO 删除，这属于 Lazy Remove
//         // 当 FIFO 轮到这个页时，会检查 bitmap 发现它已空闲，从而跳过
//     }
// }

// // 核心：换入页面
// void swap_in(uintptr_t va, uintptr_t pgdir) {
//     PTE *pte = get_pte_ptr(va, pgdir);
//     if (!pte) return;

//     // 1. 从 PTE 获取 swap_index
//     long swap_idx = (*pte) >> 10;
    
//     // 2. 分配新的物理页 (可能会再次触发 swap_out)
//     ptr_t new_pa = allocPage(1);
//     if (new_pa == 0) {
//         // printk("Swap in failed: OOM\n");
//         // assert(0);
//         printk("FATAL: swap_in failed for VA 0x%lx. System OOM.\n", va);
//         while(1);
//     }

//     // 3. 从 SD 卡读取数据
//     uint32_t sector = swap_idx_to_sector(swap_idx);
//     bios_sd_read(kva2pa(new_pa), 8, sector);
    
//      uint64_t *chk = (uint64_t *)new_pa;
//     int is_all_zero = 1;
//     for(int i=0; i<64; i++) { // 检查前 512 字节
//         if (chk[i] != 0) is_all_zero = 0;
//     }
    
//     // 如果是代码段 (通常 va < 0x20000) 且读出来全 0，那就是 SD 卡没读到东西
//     // if ( is_all_zero) {
//     //     printk("FATAL: Swap In code page 0x%lx is ALL ZERO! Image size check failed.\n", va);
//     //     while(1); // 死循环方便查看日志
//     // }

//     // 4. 释放 Swap 空间
//     free_swap_index(swap_idx);

//     // 5. 更新 PTE
//     // 必须保留原有的属性 (User, RWX 等)，这些通常没变，或者需要重新设置
//     // 假设换出时我们只改了 PFN 和 V 位，其他权限位保留在 PTE 中被 V=0 掩盖
//     // 为了安全，建议重新设置标准属性
//     set_pfn(pte, kva2pa(new_pa) >> NORMAL_PAGE_SHIFT);
//     set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | 
//                        _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_USER);

//     // 6. 加入 FIFO (作为新分配的页)
//     fifo_push(new_pa, va, pgdir);
//     local_flush_icache_all();
//     // 7. 刷新 TLB
//     local_flush_tlb_all();
// }

// // 辅助函数：建立映射并加入 FIFO
// uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir)
// {
//     va &= VA_MASK;
//     uint64_t vpn2 = va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
//     uint64_t vpn1 = (vpn2 << PPN_BITS) ^ (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
//     uint64_t vpn0 = (vpn2 << (PPN_BITS + PPN_BITS)) ^ (vpn1 << PPN_BITS) ^ (va >> NORMAL_PAGE_SHIFT);

//     PTE *pgd = (PTE*)pgdir;

//     // L2
//     if ((pgd[vpn2] & _PAGE_PRESENT) == 0) {
//         // 页表页通常由内核管理，不换出
//         ptr_t page = allocKernelPage(1);
//         set_pfn(&pgd[vpn2], kva2pa(page) >> NORMAL_PAGE_SHIFT);
//         set_attribute(&pgd[vpn2], _PAGE_PRESENT);
//         clear_pgdir(page);
//     }

//     // L1
//     PTE *pmd = (PTE *)pa2kva((get_pa(pgd[vpn2])));
//     if ((pmd[vpn1] & _PAGE_PRESENT) == 0) {
//         ptr_t page = allocKernelPage(1);
//         set_pfn(&pmd[vpn1], kva2pa(page) >> NORMAL_PAGE_SHIFT);
//         set_attribute(&pmd[vpn1], _PAGE_PRESENT);
//         clear_pgdir(page);
//     }

//     // L0
//     PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
    
//     // 如果页面不存在 (Lazy Alloc)
//     if ((pte[vpn0] & _PAGE_PRESENT) == 0) {
//         // 如果这里是 Swap 状态，不应调用 alloc_page_helper，而应在 trap 中调 swap_in
//         // 所以这里的逻辑是针对 "全新访问"
        
//         ptr_t page = allocUserPage(1); // 可能触发 swap_out
//         set_pfn(&pte[vpn0], kva2pa(page) >> NORMAL_PAGE_SHIFT);
//         set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | 
//                                   _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_USER);
        
//         // 【关键】加入 FIFO 队列，允许被换出
//         fifo_push(page, va, pgdir);
        
//         return page;
//     }
    
//     // 页面已存在
//     return pa2kva(get_pa(pte[vpn0]));
// }


// // kernel/mm/mm.c

// // [辅助宏] 判断是否是 Swap 页 (根据你之前的实现调整)
// // 之前你的 swap_out 实现是： *pte = ((uint64_t)swap_idx << 10) | _PAGE_SOFT;
// // 所以判断标准是：!Present && (PTE >> 10) != 0
// static int is_swap_entry(PTE pte) {
//     return !(pte & _PAGE_PRESENT) && (pte >> 10) != 0;
// }

// // ================= 资源回收函数 =================

// void free_process_memory(uintptr_t pgdir) {
//     if (!pgdir) return;

//     PTE *pgd = (PTE *)pgdir;

//     // 1. 遍历 L2 页目录 (Root)
//     // Sv39 中，低 256GB 是用户空间 (VPN2: 0~255)，高 256GB 是内核空间
//     // 我们只回收用户空间！绝对不要碰内核空间 (>= 256)
//     for (int i = 0; i < 256; i++) {
//         PTE pte2 = pgd[i];
        
//         // 如果 L2 条目为空，跳过
//         if (!(pte2 & _PAGE_PRESENT)) continue;

//         // 获取 L1 页表物理地址
//         uintptr_t pmd_pa = get_pa(pte2);
//         PTE *pmd = (PTE *)pa2kva(pmd_pa);

//         // 2. 遍历 L1 页表 (Intermediate)
//         for (int j = 0; j < 512; j++) {
//             PTE pte1 = pmd[j];
//             if (!(pte1 & _PAGE_PRESENT)) continue;

//             // 获取 L0 页表物理地址
//             uintptr_t pte_pa = get_pa(pte1);
//             PTE *pte = (PTE *)pa2kva(pte_pa);

//             // 3. 遍历 L0 页表 (Leaf) - 这里存放着真正的用户数据
//             for (int k = 0; k < 512; k++) {
//                 PTE entry = pte[k];
                
//                 if (entry & _PAGE_PRESENT) {
//                     // 情况 A: 页面在物理内存中
//                     // 只有用户页才需要释放 (虽然这里遍历的都是用户空间，但双重保险)
//                     if (entry & _PAGE_USER) {
//                         uintptr_t pa = get_pa(entry);
//                         freePage(pa); // 归还物理页
//                     }
//                 } else if (is_swap_entry(entry)) {
//                     // 情况 B: 页面在 Swap 中
//                     long swap_idx = entry >> 10;
//                     free_swap_index(swap_idx); // 归还 Swap 扇区
//                     // 调试打印 (可选)
//                     // printk("Recover Swap: %ld\n", swap_idx);
//                 }
//             }
            
//             // 回收 L0 页表本身占用的物理页
//             freePage(pte_pa);
//         }
        
//         // 回收 L1 页表本身占用的物理页
//         freePage(pmd_pa);
//         pgd[i] = 0; // 清空目录项
//     }
    
//     // 注意：pgdir 本身 (L2 页表页) 的回收通常由 PCB 销毁逻辑处理
//     // 如果你在 create_process 时为 pgdir 申请了页，这里也可以 freePage(kva2pa(pgdir));
//     // 但通常建议在 process_exit 的最后一步做
// }

// // S-Core 特有
// #ifdef S_CORE
// static ptr_t largePageMemCurr = LARGE_PAGE_FREEMEM;
// ptr_t allocLargePage(int numPage) {
//     ptr_t ret = ROUND(largePageMemCurr, LARGE_PAGE_SIZE);
//     largePageMemCurr = ret + numPage * LARGE_PAGE_SIZE;
//     return ret;    
// }
// #endif

// void *kmalloc(size_t size) {
//     int num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
//     return (void *)allocPage(num_pages);
// }

// void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir) {
//     memcpy((void*)dest_pgdir, (void*)src_pgdir, PAGE_SIZE);
// }

// uintptr_t shm_page_get(int key) { return 0; }
// void shm_page_dt(uintptr_t addr) {}

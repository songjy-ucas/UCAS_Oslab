#include <os/mm.h>
#include <os/string.h>
#include <os/kernel.h>
#include <os/task.h>
#include <pgtable.h>
#include <os/sched.h>
#include <os/smp.h>
#include <printk.h>
#include <assert.h>

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
#define KERN_PAGE_MAX_NUM 512

alloc_info_t alloc_info[USER_PAGE_MAX_NUM];
uint64_t image_end_sec = 10000; // 假设 Swap 区从磁盘第 10000 扇区开始

/* 链表头定义 (使用你的 list.h 中的宏 LIST_HEAD) */
LIST_HEAD(in_mem_list);
LIST_HEAD(swap_out_list);
LIST_HEAD(free_list);


// NOTE: A/C-core
static ptr_t kernMemCurr = FREEMEM_KERNEL;
int usepage = 0;
int pgdir_id; // 全局变量，用于记录当前操作的页表ID

#define TOTAL_PAGES 65536         
#define KERNELMEM_START 0xffffffc050000000lu 
#define KERNELMEM_END   0xffffffc060000000lu

#define BITMAP(n) ((n - KERNELMEM_START) / (8 * PAGE_SIZE))
#define BITMAP_OFFSET(n) (((n - KERNELMEM_START) / PAGE_SIZE) % 8)

static uint8_t page_bitmap[TOTAL_PAGES / 8];

void init_memory_manager() {
    bzero(page_bitmap, sizeof(page_bitmap)); 
    usepage = 0;
    // 如果需要，可以在这里初始化内核保留区
}

bool is_memory_full()
{
    for (int i = 0; i < TOTAL_PAGES / 8; i++) {
        if (page_bitmap[i] != 0xff) {
            return false;
        }
    }
    return true;
}

ptr_t allocPage(int numPage)
{
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    
    int count = 0;
    // 简单的防止死循环保护
    int max_checks = (TOTAL_PAGES / numPage) * 2; 

    do {
        ret = ROUND(kernMemCurr, PAGE_SIZE);
        kernMemCurr = ret + numPage * PAGE_SIZE;
        
        if (kernMemCurr >= KERNELMEM_END) {
            kernMemCurr = FREEMEM_KERNEL;
            if (is_memory_full()) {
                printk("Fatal: Memory Full!\n");
                assert(0);
            }
        }
        
        count++;
        if (count > max_checks) {
            printk("Fatal: AllocPage Timeout!\n");
            assert(0);
        }

    } while(page_bitmap[BITMAP(ret)] & (1 << (BITMAP_OFFSET(ret))));

    // 标记位图
    for(int i=0; i<numPage; i++){
        page_bitmap[BITMAP(ret + i*PAGE_SIZE)] |= (1 << (BITMAP_OFFSET(ret + i*PAGE_SIZE)));
    }
    
    usepage += numPage;
    // 分配后清零
    memset((void*)ret, 0, numPage * PAGE_SIZE);
    return ret;
}

// 兼容接口
ptr_t allocKernelPage(int numPage) { return allocPage(numPage); }
ptr_t allocUserPage(int numPage) { return allocPage(numPage); }

void freePage(ptr_t baseAddr)
{
    if (baseAddr == (ptr_t)NULL) return;
    if (baseAddr < KERNELMEM_START || baseAddr >= KERNELMEM_END) return;

    page_bitmap[BITMAP(baseAddr)] &= ~(1 << (BITMAP_OFFSET(baseAddr)));
    usepage--;
}

void *kmalloc(size_t size)
{
    // 简单实现：按页分配
    return (void *)allocPage(1);
}

void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir)
{
    memcpy((void *)dest_pgdir, (void *)src_pgdir, PAGE_SIZE);
}

// 初始化 Swap 元数据链表
void init_uva_alloc(){
    for(int i=0; i<USER_PAGE_MAX_NUM; i++){
        // 将所有元数据节点加入空闲链表
        add_node_to_q(&alloc_info[i].lnode, &free_list);
        alloc_info[i].uva = 0;
        alloc_info[i].pa = 0;
        alloc_info[i].on_disk_sec = 0;
        alloc_info[i].pgdir_id = 0;
    }
}

// 辅助函数声明
int map_page_helper(uintptr_t va, uintptr_t pa, uintptr_t pgdir);

// 换出页面
alloc_info_t* swapPage(){
    // 检查是否有页可换
    if (list_empty(&in_mem_list)) {
        printk("Fatal: No page to swap out!\n");
        assert(0);
    }

    list_node_t* swap_lnode = in_mem_list.next;
    
    delete_node_from_q(swap_lnode);
    add_node_to_q(swap_lnode, &swap_out_list);
    
    alloc_info_t* info = lnode2info(swap_lnode);
    bios_sd_write(info->pa, PAGE_SIZE/SECTOR_SIZE, image_end_sec);
    info->on_disk_sec = image_end_sec;
    image_end_sec += PAGE_SIZE/SECTOR_SIZE;
    
    // [Fix] 使用全局 PCB 数组
    map_page_helper(info->uva, 0, pcb[info->pgdir_id].pgdir);
    clear_pgdir(pa2kva(info->pa));
    local_flush_tlb_all();
    return info;
}

// 带换页功能的用户页分配
ptr_t uva_allocPage(int numPage, uintptr_t uva)
{
    // 1. 检查是否在换出列表中 (Swap In)
    list_node_t *node = swap_out_list.next;
    while (node != &swap_out_list) {
        alloc_info_t* in_info = lnode2info(node);  
        
        if(in_info->uva == uva && in_info->pgdir_id == pgdir_id){  
            // 换入逻辑
            alloc_info_t* out_info = swapPage(); // 先踢一个出去
            in_info->pa = out_info->pa;          // 复用物理页
            
            delete_node_from_q(&in_info->lnode);
            add_node_to_q(&in_info->lnode, &in_mem_list);
            
            // [Fix] 获取当前核 ID 并建立映射
            uint64_t my_cpu_id = get_current_cpu_id();
            map_page_helper(in_info->uva, in_info->pa, current_running->pgdir);
            local_flush_tlb_all();
            
            bios_sd_read(in_info->pa, PAGE_SIZE/SECTOR_SIZE, in_info->on_disk_sec);
            in_info->on_disk_sec = 0;
            return pa2kva(in_info->pa);
        }
        node = node->next;
    }

    // 2. 分配新页
    if (list_empty(&free_list)) {
         printk("Fatal: No free alloc_info nodes!\n");
         assert(0);
    }
    list_node_t* new_lnode = free_list.next;
    
    alloc_info_t* in_info = lnode2info(new_lnode);
    delete_node_from_q(new_lnode);
    add_node_to_q(new_lnode, &in_mem_list);
    
    in_info->uva = uva;
    in_info->pgdir_id = pgdir_id;

    // 3. 检查物理内存水位
    if(usepage >= KERN_PAGE_MAX_NUM){
        // 触发换出
        alloc_info_t* out_info = swapPage();
        in_info->pa = out_info->pa;
        return pa2kva(in_info->pa);
    }
    else{
        // 直接分配
        in_info->pa = kva2pa(allocPage(1));
        return pa2kva(in_info->pa);
    }
}

// 建立映射辅助函数
int map_page_helper(uintptr_t va, uintptr_t pa, uintptr_t pgdir){
    va &= VA_MASK;
    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    PTE *pgd = (PTE*)pgdir;
    if (pgd[vpn2] == 0) {
        set_pfn(&pgd[vpn2], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT | _PAGE_USER);
        clear_pgdir(pa2kva(get_pa(pgd[vpn2])));
    }
    
    PTE *pmd = (uintptr_t *)pa2kva((get_pa(pgd[vpn2])));
    if(pmd[vpn1] == 0){
        set_pfn(&pmd[vpn1], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT | _PAGE_USER);
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
    // [关键] 在调用 uva_allocPage 前，可以尝试设置 pgdir_id
    // 如果没有进程 ID 映射机制，暂时使用 0 或 current_running->pid
    // pgdir_id = current_running[get_current_cpu_id()]->pid;
    
    va &= VA_MASK;
    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    PTE *pgd = (PTE*)pgdir;
    if (pgd[vpn2] == 0) {
        set_pfn(&pgd[vpn2], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT | _PAGE_USER);
        clear_pgdir(pa2kva(get_pa(pgd[vpn2])));
    }
    PTE *pmd = (uintptr_t *)pa2kva((get_pa(pgd[vpn2])));
    if(pmd[vpn1] == 0){
        set_pfn(&pmd[vpn1], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT | _PAGE_USER);
        clear_pgdir(pa2kva(get_pa(pmd[vpn1])));
    }
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1])); 
    
    if(pte[vpn0] == 0){
        // 调用带换页的分配
        uintptr_t aligned_uva = (va >> NORMAL_PAGE_SHIFT) << NORMAL_PAGE_SHIFT;
        ptr_t pa = kva2pa(uva_allocPage(1, aligned_uva));
        set_pfn(&pte[vpn0], pa >> NORMAL_PAGE_SHIFT);
    }
    
    set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                            _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_USER);
                            
    return pa2kva(get_pa(pte[vpn0]));
}

// TODO [P4-task4]
uintptr_t shm_page_get(int key) { return 0; }
void shm_page_dt(uintptr_t addr) {}
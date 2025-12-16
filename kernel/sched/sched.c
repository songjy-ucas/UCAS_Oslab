#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <os/loader.h>
#include <os/string.h> 
#include <os/smp.h>
#include <os/debug.h>
#include <pgtable.h> // 必须包含，用于PTE操作
#include <csr.h>   
#include <screen.h>
#include <printk.h>
#include <assert.h>

pcb_t pcb[NUM_MAX_TASK];
// 每个核心有自己的 pid0 栈
const ptr_t pid0_stack_core0 = INIT_KERNEL_STACK + PAGE_SIZE;
const ptr_t pid0_stack_core1 = INIT_KERNEL_STACK + 2 * PAGE_SIZE;

pcb_t pid0_pcb[NR_CPUS];

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;
register pcb_t * current_running asm("tp");

extern void ret_from_exception();

void do_scheduler(void)
{
    // TODO: [p2-task3] Check sleep queue to wake up PCBs

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/

    // TODO: [p2-task1] Modify the current_running pointer.

    // 0. 获取当前核 ID (放在最前面，因为后面多次用到)
    uint64_t cpu_id = get_current_cpu_id();

    // 1. 检查睡眠队列，唤醒到期的进程
    check_sleeping();

    // 2. 获取当前进程（即将被切换掉的进程）的指针
    pcb_t *prev = current_running;

    // 3. 根据 prev 进程的当前状态，决定如何处置它
    if (prev->status == TASK_RUNNING) {
        // 注意：任何一个核的 idle 进程(pid0)都不应进入就绪队列
        if (prev != &pid0_pcb[0] && prev != &pid0_pcb[1]) {
            prev->status = TASK_READY;
            // 将其加入就绪队列队尾
            list_add_tail(&prev->list, &ready_queue);
        }
    }

    // 4. [Task 4 修改核心] 从就绪队列中遍历，寻找符合当前核 mask 要求的任务
    pcb_t *next = NULL;
    list_node_t *current_node = ready_queue.next;

    // 遍历 ready_queue
    while (current_node != &ready_queue) {
        pcb_t *candidate = list_entry(current_node, pcb_t, list);

        // [Task 4] 检查亲和性 (Affinity Check)
        // 逻辑：如果任务的 mask 的第 cpu_id 位是 1，说明允许在该核运行
        if (candidate->mask & (1 << cpu_id)) {
            next = candidate;
            // 找到了合适的任务，将其从队列中移除
            list_del(current_node);
            break; // 停止查找
        }

        // 如果当前任务不满足 mask 要求，继续看下一个
        current_node = current_node->next;
    }

    // 5. 如果队列为空，或者队列里所有任务都不允许在当前核运行
    if (next == NULL) {
        // 运行当前核对应的 idle 任务
        next = &pid0_pcb[cpu_id];
    }

    // 6. 更新 current_running 指针，状态，以及当前运行核心
    next->status = TASK_RUNNING;
    next->core_id = cpu_id; // [Task 4] 更新任务当前所在的核
    current_running = next;
    
    klog("Scheduler on core %d picked task '%s'(PID %d) with mask 0x%x\n",
         cpu_id,
         (next == &pid0_pcb[0] || next == &pid0_pcb[1]) ? "idle" : "user_task",
         next->pid,
         next->mask);

    // 7. 调用汇编实现的 switch_to 函数，完成上下文切换
    // 只有当任务确实发生变化时才进行切换
    if (prev != next) {
        // [P4] 1. 获取下一个进程的页目录虚拟地址 (Kernel Virtual Address)
        // 确保你的 PCB 结构体中有 pgdir 成员，并且在创建进程时已初始化
        uintptr_t next_pgdir_kva = next->pgdir;

        // [P4] 2. 转换为物理地址 (Physical Address)
        // satp 寄存器需要的是物理页号，所以必须转换
        uintptr_t next_pgdir_pa = kva2pa(next_pgdir_kva);

        // [P4] 3. 切换 SATP 寄存器
        // 使用 SV39 模式，ASID 设为 0 (简化)，填入物理页号 (PA >> 12)
        set_satp(SATP_MODE_SV39, 0, next_pgdir_pa >> NORMAL_PAGE_SHIFT);

        // [P4] 4. 刷新 TLB
        // 必须操作，否则 CPU 还会用旧的缓存查表，导致崩溃
        local_flush_tlb_all();
        
        // debug use
        // printk("Switch: PID %d -> %d, Next PGDIR: 0x%lx\n", 
        // prev->pid, next->pid, next->pgdir);
        
        // [P4] 5. 切换寄存器上下文
        switch_to(prev, next);
    }
}

void do_sleep(uint32_t sleep_time)
{
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
    // 1. block the current_running
    current_running->status = TASK_BLOCKED;

    // 2. set the wake up time for the blocked task
    current_running->wakeup_time = get_ticks() + (uint64_t)sleep_time * get_time_base();
    list_add_tail(&current_running->list, &sleep_queue);
    
    // 3. reschedule because the current_running is blocked.
    do_scheduler();

}

void do_block(list_node_t *pcb_node, list_head *queue)
{
    // TODO: [p2-task2] block the pcb task into the block queue
    list_add_tail(pcb_node, queue);
    // 更新进程状态为阻塞
    current_running->status = TASK_BLOCKED;
}

void do_unblock(list_node_t *pcb_node)
{
    // TODO: [p2-task2] unblock the `pcb` from the block queue
    list_del(pcb_node);
    // 获取 pcb 的指针
    pcb_t* pcb_to_unblock = list_entry(pcb_node, pcb_t, list);
    // 更新进程状态为就绪
    pcb_to_unblock->status = TASK_READY;
    // 将 pcb 节点添加到全局就绪队列的尾部，等待调度
    list_add_tail(pcb_node, &ready_queue);
}


// Helper function to find a free PCB
static pcb_t *find_free_pcb()
{
    for (int i = 1; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].status == TASK_EXITED || pcb[i].pid == -1) {
            return &pcb[i];
        }
    }
    return NULL;
}

extern void exit_trampoline();
// [P3-TASK1] A/C-Core implementation of do_exec
// 它从磁盘(镜像)读取程序，为其分配内存(栈)，设置好运行环境(寄存器)，最后放入就绪队列等待CPU运行。
/* kernel/syscall/syscall.c */

pid_t do_exec(char *name, int argc, char *argv[])
{
    // 1. 查找空闲 PCB 
    // 使用辅助函数，简化逻辑
    pcb_t *new_pcb = find_free_pcb();
    
    if (new_pcb == NULL) {
        printk("Error: No free PCB for exec!\n");
        return -1;
    }

    // 2. [Task 1] 创建新页表并共享内核映射 

    //------ 这个就是guidebook里面的“要点解读3”，需要为每个用户进程映射“一个”内核页表。-----

    // allocPage 返回内核虚拟地址
    new_pcb->pgdir = allocPage(1); 
    clear_pgdir(new_pcb->pgdir);
    // 共享内核高地址映射 (从内核根页表拷贝)
    share_pgtable(new_pcb->pgdir, pa2kva(PGDIR_PA));

    // 3. 加载任务代码 (调用 map_task)
    // map_task 会把程序加载到 USER_ENTRYPOINT (0x10000)
    uint64_t entry_point = map_task(name, new_pcb->pgdir);
    if (entry_point == 0) {
        // TODO: freePage(new_pcb->pgdir)
        return -1;
    }

    // 4. 分配栈
    new_pcb->kernel_stack_base = allocPage(1); // 内核栈 (内核虚地址)
    
    // [Task 1] 用户栈分配与映射
    // A-core USER_STACK_ADDR = 0xf00010000
    // 分配物理页，建立映射，并返回该页的内核虚地址
    uintptr_t user_stack_physical_kva = alloc_page_helper(USER_STACK_ADDR - PAGE_SIZE, new_pcb->pgdir);
    new_pcb->user_stack_base = user_stack_physical_kva; 

    // 计算栈顶指针
    ptr_t kernel_stack_top = new_pcb->kernel_stack_base + PAGE_SIZE;
    // user_stack_kva_top: 用户栈顶对应的内核虚地址 (用于写参数)
    ptr_t user_stack_kva_top = user_stack_physical_kva + PAGE_SIZE;

    // 5. 构造用户栈参数 (argc/argv)
    // 逻辑：向 user_stack_kva_top 写数据，但在 argv[] 里存 USER_STACK_ADDR 偏移后的地址
    
    int total_len = 0;
    for (int i = 0; i < argc; ++i) {
        total_len += strlen(argv[i]) + 1; 
    }

    // 字符串起始位置 (内核虚地址)
    ptr_t string_base = user_stack_kva_top - total_len;
    string_base &= ~0x7; 

    char *argv_new_addr[argc]; 
    char *current_str_pos = (char *)string_base;
    
    for (int i = 0; i < argc; ++i) {
        strcpy(current_str_pos, argv[i]);
        // 计算用户虚地址偏移
        uintptr_t offset = user_stack_kva_top - (ptr_t)current_str_pos;
        argv_new_addr[i] = (char *)(USER_STACK_ADDR - offset);
        
        current_str_pos += strlen(argv[i]) + 1;
    }

    // 指针数组起始位置 (内核虚地址)
    ptr_t argv_ptr_base = string_base - sizeof(char *) * (argc + 1);
    argv_ptr_base &= ~0x7F; 

    char **argv_on_stack = (char **)argv_ptr_base;
    for (int i = 0; i < argc; ++i) {
        argv_on_stack[i] = argv_new_addr[i]; // 存入用户虚地址
    }
    argv_on_stack[argc] = NULL; 

    // 计算最终的用户栈顶 (用户虚地址)
    uintptr_t sp_offset = user_stack_kva_top - argv_ptr_base;
    ptr_t final_user_sp = USER_STACK_ADDR - sp_offset;

    // 6. 初始化 PCB
    new_pcb->pid = process_id++;
    new_pcb->parent_pid = current_running->pid;
    new_pcb->status = TASK_READY;
    new_pcb->cursor_x = 0;
    new_pcb->cursor_y = 0;
    new_pcb->mask = current_running->mask; 
    list_init(&new_pcb->wait_list); 

    // 7. 初始化寄存器上下文
    // SP 和 argv 都是用户虚地址
    init_pcb_stack(kernel_stack_top, final_user_sp, entry_point, new_pcb, argc, (char **)final_user_sp);

    // 8. 加入就绪队列
    list_add_tail(&new_pcb->list, &ready_queue);
    
    return new_pcb->pid;
}

// Helper to free resources of a PCB
// 当进程彻底销毁时调用。但在简单的实验内核中，通常只重置状态和 PID，
// 不一定真正回收物理内存页。
static void free_pcb_resources(pcb_t *p) {
    /* 
     * [P4-Task1] 内存回收 
     * 如果实现了 freePage 和 free_all_pages，应该在这里调用
     */
    if (p->pgdir != 0) {
        // free_all_pages(p); // 释放该进程所有映射的物理页
    }
    
    if (p->kernel_stack_base != 0) {
        freePage(p->kernel_stack_base);
    }
    if (p->user_stack_base != 0) {
        freePage(p->user_stack_base);
    }
    p->status = TASK_EXITED;
    p->pid = -1; 
}

// [P3-TASK1] Process exits
// 进程自我结束，当程序 main 函数 return 或者调用 exit() 时触发。
void do_exit(void)
{
    // 1. 获取当前进程的PCB
    pcb_t *exiting_pcb = current_running;

    // ====================================================================
    // [P4-TASK3 关键新增] 资源回收
    // 必须在将 CPU 切换到其他进程之前，回收当前进程占用的所有内存资源
    // ====================================================================
    
    // 调用一个辅助函数来完成所有内存回收工作
    free_process_memory(exiting_pcb);


    // ====================================================================
    // [P4-TASK3 关键新增] 切换回内核地址空间
    // 原因：exiting_pcb->pgdir 对应的页表内存刚刚可能已经被 free_process_memory
    // 回收了。CPU 不能再继续使用一个已经被释放的页表。
    // 我们必须安全地将 CPU 的地址空间切换回内核的根页表。
    // ====================================================================
    
    set_satp(SATP_MODE_SV39, 0, PGDIR_PA >> NORMAL_PAGE_SHIFT);
    local_flush_tlb_all();


    // 2. 将当前进程的状态设置为 EXITED。
    // PCB本身暂时不释放，等待父进程通过 waitpid 回收。
    exiting_pcb->status = TASK_EXITED;

    // 3. 唤醒正在等待它的父进程
    while (!list_empty(&exiting_pcb->wait_list)) {
        pcb_t *waiter_pcb = list_entry(exiting_pcb->wait_list.next, pcb_t, list);
        list_del(&waiter_pcb->list);
        waiter_pcb->status = TASK_READY;
        list_add_tail(&waiter_pcb->list, &ready_queue);
    }
    
    // 如果有父进程，并且父进程没有在等我，可以向父进程发送信号（简化版中省略）

    // 4. 主动放弃CPU，调用调度器
    // 此时当前进程已经是 EXITED 态，它再也不会被调度到了。
    do_scheduler(); 

    // do_scheduler 永远不会返回到这里
    // 因为当前进程的上下文已经被切换出去了，并且再也不会被切换回来。
    printk("FATAL: Should not return from do_scheduler in do_exit!\n");
    assert(0);
}

// [P3-TASK1] Kill a process
// 杀死指定进程 ------- 特别注意：杀死进程需要释放什么东西？ 它占据的锁要释放
// [P3-TASK1] Kill a process
int do_kill(pid_t pid)
{
    if (pid <= 0) {
        return 0; 
    }

    pcb_t *target_pcb = NULL;
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].pid == pid) {
            target_pcb = &pcb[i];
            break;
        }
    }

    // 如果进程不存在或已经是退出状态，直接返回
    if (target_pcb == NULL || target_pcb->status == TASK_EXITED) {
        return 0; 
    }

    // 释放锁
    check_and_release_locks(target_pcb->pid); 

    // ====================================================================
    // [修复点 1] 释放目标进程的内存资源
    // 无论是自杀还是他杀，这些物理页都应该被回收
    // ====================================================================
    free_process_memory(target_pcb);

    // 保存旧状态
    task_status_t old_status = target_pcb->status;

    // 标记为 EXITED
    target_pcb->status = TASK_EXITED;

    // 唤醒父进程
    while (!list_empty(&target_pcb->wait_list)) {
        list_node_t *waiter_node = target_pcb->wait_list.next;
        list_del(waiter_node); 
        pcb_t *waiter_pcb = list_entry(waiter_node, pcb_t, list);
        waiter_pcb->status = TASK_READY;
        list_add_tail(&waiter_pcb->list, &ready_queue);
    }

    // ====================================================================
    // [修复点 2] 处理调度和页表切换
    // ====================================================================
    if (target_pcb == current_running) {
        // CASE A: 自杀 (Kill Self)
        // 既然上面已经 free_process_memory 回收了自己的页表
        // 这里必须立刻切换回内核页表，否则 CPU 会崩溃
        set_satp(SATP_MODE_SV39, 0, PGDIR_PA >> NORMAL_PAGE_SHIFT);
        local_flush_tlb_all();

        // 主动让出 CPU
        do_scheduler();
    } else {
        // CASE B: 他杀 (Kill Others)
        // 只需要将其从就绪/阻塞队列中移除
        if (old_status == TASK_READY || old_status == TASK_BLOCKED) {
            list_del(&target_pcb->list);
        }
    }
    return 1; // Success
}

// [P3-TASK1] Wait for a child process
// 等待子进程结束,实现了父进程对子进程的回收机制，防止僵尸进程泛滥。
int do_waitpid(pid_t pid)
{
    // 查找目标子进程
    pcb_t *child_pcb = NULL;
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].pid == pid) {
            child_pcb = &pcb[i];
            break;
        }
    }

    // 没找到，或者这个进程不是我的子进程，就不管它
    if (child_pcb == NULL || child_pcb->parent_pid != current_running->pid) {
        return -1; // Not found or not a child
    }

    // 检查子进程状态
    if (child_pcb->status == TASK_EXITED) {
        // 子进程已经死了（早已是僵尸进程）。那么我直接回收它的资源，不需要阻塞等待。
        free_pcb_resources(child_pcb);
        return pid;
    }

    // 子进程还在跑。
    // 当前进程（父进程）必须阻塞自己（BLOCKED），直到子进程退出。将自己加入子进程的 wait_list 中。
    current_running->status = TASK_BLOCKED;
    list_add_tail(&current_running->list, &child_pcb->wait_list);
    
    // 让出 CPU 调用调度器切换别的进程运行。
    do_scheduler();

    // 又回到父进程时，说明子进程已经退出了（在 do_exit 里唤醒了我）。现在可以安全地回收子进程的 PCB 资源了。
    free_pcb_resources(child_pcb);

    return pid;
}

// 显示进程列表 (ps 命令)
void do_process_show()
{
    printk("[Process Table]:\n");
    
    // 遍历所有可能的 PCB
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        // 检查PCB是否被使用 (pid != -1) 且不是已经完全清理的僵尸
        if (pcb[i].pid != -1 && pcb[i].status != TASK_EXITED) {
            char *status_str;
            // 将数字状态转换为可读的字符串
            switch (pcb[i].status) {
                case TASK_BLOCKED:
                    status_str = "BLOCKED"; 
                    break;
                case TASK_RUNNING:
                    status_str = "RUNNING"; 
                    break;
                case TASK_READY:
                    status_str = "READY  ";   // 加空格是为了对齐美观
                    break;
                default:
                    status_str = "UNKNOWN";
                    break;
            }
            
            // [Task 4] 打印扩展信息：mask 和 core_id
            // 注意：只有 RUNNING 状态的任务，Running on core 才有实际意义
            // 但为了调试方便，通常都打印出来，或者显示上次运行的核
            printk("[%d] PID : %d  STATUS : %s  mask : 0x%x  Running on core %d\n", 
                   i,            
                   pcb[i].pid,   
                   status_str,
                   pcb[i].mask,
                   pcb[i].core_id);  
        }
    }
}

// 这是 do_getpid 的实现，非常简单，返回当前运行进程的 PID
pid_t do_getpid()
{
    return current_running->pid;
}


/* =========================================================================
 *                         同步原语：屏障 (Barrier)
 * ========================================================================= */
barrier_t barriers[BARRIER_NUM];

void init_barriers(void)
{
    for (int i = 0; i < BARRIER_NUM; i++)
    {
        barriers[i].key = -1; // -1 表示该槽位未使用
        barriers[i].goal = 0; // 目标到达数量
        barriers[i].current_count = 0; // 当前已到达数量
        list_init(&barriers[i].wait_queue); // 初始化等待队列
    }
}

// 初始化一个屏障
int do_barrier_init(int key, int goal)
{
    // 1. 检查是否已经存在该 key 的 barrier，如果有直接返回索引
    for (int i = 0; i < BARRIER_NUM; i++)
    {
        if (barriers[i].key == key)
        {
            return i;
        }
    }

    // 2. 寻找一个空的槽位创建新的 barrier
    for (int i = 0; i < BARRIER_NUM; i++)
    {
        if (barriers[i].key == -1)
        {
            barriers[i].key = key;
            barriers[i].goal = goal;
            barriers[i].current_count = 0;
            list_init(&barriers[i].wait_queue);
            return i;
        }
    }

    return -1; 
}

// 未全到时等待屏障，全到后唤醒所有进程
void do_barrier_wait(int bar_idx)
{
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM) return;
    
    barrier_t *bar = &barriers[bar_idx];

    // 增加到达计数
    bar->current_count++;

    if (bar->current_count >= bar->goal)
    {
        // 屏障任务完成。重置计数器以便复用，并唤醒所有在排队的兄弟进程。
        bar->current_count = 0;
        
        while (!list_empty(&bar->wait_queue))
        {
            // 逐个唤醒队首的进程
            do_unblock(bar->wait_queue.next);
        }
    }
    else
    {
        // 阻塞，进入等待队列
        do_block(&current_running->list, &bar->wait_queue);
        // 这里的 do_scheduler 会导致上下文切换，当前进程暂停。
        do_scheduler();
    }
}

void do_barrier_destroy(int bar_idx)
{
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM) return;
    
    // 简单地标记为未使用，释放槽位
    barriers[bar_idx].key = -1;
    barriers[bar_idx].goal = 0;
    barriers[bar_idx].current_count = 0;
}

/* =========================================================================
 *                     同步原语：条件变量 (Condition Variable)
 * ========================================================================= */

condition_t conditions[CONDITION_NUM];

void init_conditions(void)
{
    for (int i = 0; i < CONDITION_NUM; i++)
    {
        conditions[i].key = -1;
        list_init(&conditions[i].wait_queue);
    }
}

// 初始化条件变量，逻辑同 Barrier
int do_condition_init(int key)
{
    // 1. 查找是否存在
    for (int i = 0; i < CONDITION_NUM; i++)
    {
        if (conditions[i].key == key)
        {
            return i;
        }
    }

    // 2. 寻找空位
    for (int i = 0; i < CONDITION_NUM; i++)
    {
        if (conditions[i].key == -1)
        {
            conditions[i].key = key;
            list_init(&conditions[i].wait_queue);
            return i;
        }
    }

    return -1;
}

// 条件等待wait   cond_idx: 条件变量索引 mutex_idx: 当前持有的互斥锁索引
void do_condition_wait(int cond_idx, int mutex_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;

    condition_t *cond = &conditions[cond_idx];

    // 等条件满足---> 阻塞当前进程
    do_block(&current_running->list, &cond->wait_queue);

    // 在阻塞前，必须释放持有的互斥锁！避免造成死锁。
    do_mutex_lock_release(mutex_idx);

    // 调度 (让出 CPU) 进程在此处暂停，直到 do_condition_signal/broadcast 唤醒它。
    do_scheduler();

    // 唤醒后，重新竞争并获取互斥锁。
    do_mutex_lock_acquire(mutex_idx);
}

// signal操作 
void do_condition_signal(int cond_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;

    condition_t *cond = &conditions[cond_idx];

    // 如果有等的进程，唤醒队列中的第一个等待者
    if (!list_empty(&cond->wait_queue))
    {
        do_unblock(cond->wait_queue.next);
    }
}

// broadcast操作
void do_condition_broadcast(int cond_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;

    condition_t *cond = &conditions[cond_idx];

    // 遍历队列，唤醒所有在等待这个条件的进程
    while (!list_empty(&cond->wait_queue))
    {
        do_unblock(cond->wait_queue.next);
    }
}

void do_condition_destroy(int cond_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;

    conditions[cond_idx].key = -1;
}


/* =========================================================================
 *                     进程间通信：信箱 (Mailbox)
 * ========================================================================= */
// Mailbox 是基于内存拷贝的，因此必须确保数据所在的页在物理内存中。

mailbox_t mboxes[MBOX_NUM];

void init_mbox()
{
    for (int i = 0; i < MBOX_NUM; i++)
    {
        mboxes[i].open_refs = 0;
        mboxes[i].name[0] = '\0';
        mboxes[i].head = 0;
        mboxes[i].tail = 0;
        mboxes[i].count = 0;
        
        spin_lock_init(&mboxes[i].lock);
        list_init(&mboxes[i].send_wait_queue);
        list_init(&mboxes[i].recv_wait_queue);
    }
}

int do_mbox_open(char *name)
{
    for (int i = 0; i < MBOX_NUM; i++)
    {
        if (mboxes[i].open_refs > 0 && strcmp(mboxes[i].name, name) == 0)
        {
            spin_lock_acquire(&mboxes[i].lock); 
            mboxes[i].open_refs++;
            spin_lock_release(&mboxes[i].lock);
            return i;
        }
    }

    for (int i = 0; i < MBOX_NUM; i++)
    {
        if (mboxes[i].open_refs == 0)
        {
            spin_lock_acquire(&mboxes[i].lock);
            if (mboxes[i].open_refs == 0) 
            {
                strcpy(mboxes[i].name, name);
                mboxes[i].open_refs = 1;
                mboxes[i].head = 0;
                mboxes[i].tail = 0;
                mboxes[i].count = 0;
                list_init(&mboxes[i].send_wait_queue);
                list_init(&mboxes[i].recv_wait_queue);
                
                spin_lock_release(&mboxes[i].lock);
                return i;
            }
            spin_lock_release(&mboxes[i].lock);
        }
    }
    return -1;
}

void do_mbox_close(int mbox_idx)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return;
    
    mailbox_t *mbox = &mboxes[mbox_idx];
    spin_lock_acquire(&mbox->lock);
    
    if (mbox->open_refs > 0)
    {
        mbox->open_refs--; 
        if (mbox->open_refs == 0)
        {
            mbox->name[0] = '\0';
            mbox->count = 0;
            mbox->head = 0;
            mbox->tail = 0;
        }
    }
    spin_lock_release(&mbox->lock);
}

// 生产者发送 (写入信箱)
int do_mbox_send(int mbox_idx, void * msg, int msg_length)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return 0;
    if (msg_length <= 0) return 0;
    if (msg_length > MAX_MBOX_LENGTH) return 0; 

    mailbox_t *mbox = &mboxes[mbox_idx];
    char *data = (char *)msg;

    // [重要修正] 在拿锁之前或写入之前，必须确保用户缓冲区在内存中
    // 如果 msg 跨页了，需要检查每一页。这里简单处理，检查首尾。
    // 如果页面在 Swap 中，这里会触发 bios_sd_read，产生磁盘 IO。
    if (!check_and_swap_in((uintptr_t)data)) return 0;
    if (!check_and_swap_in((uintptr_t)data + msg_length - 1)) return 0;

    spin_lock_acquire(&mbox->lock);

    while ((MAX_MBOX_LENGTH - mbox->count) < msg_length)
    {
        do_block(&current_running->list, &mbox->send_wait_queue);            
        spin_lock_release(&mbox->lock);
        do_scheduler(); 
        spin_lock_acquire(&mbox->lock);
        
        if (mbox->open_refs == 0) {
            spin_lock_release(&mbox->lock);
            return 0;
        }
        
        // 唤醒后，重新检查页面是否还在内存 (可能睡眠期间又被换出了)
        if (!check_and_swap_in((uintptr_t)data)) {
             spin_lock_release(&mbox->lock);
             return 0;
        }
    }

    // 内存拷贝
    for (int i = 0; i < msg_length; i++)
    {
        // 严谨写法：每拷贝一个字节前都应该确保该地址可访问
        // 但为了性能，通常只在跨页边界时检查。
        // 这里假设 msg_length 很小(64B)，不会跨页，或者上面已经 SwapIn 了。
        mbox->buf[mbox->tail] = data[i];
        mbox->tail = (mbox->tail + 1) % MAX_MBOX_LENGTH;
    }
    
    mbox->count += msg_length;

    while (!list_empty(&mbox->recv_wait_queue))
    {
        do_unblock(mbox->recv_wait_queue.next);
    }

    spin_lock_release(&mbox->lock);
    return msg_length; 
}

// 消费者接收 (读出信箱)
int do_mbox_recv(int mbox_idx, void * msg, int msg_length)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return 0;
    if (msg_length <= 0) return 0;
    
    mailbox_t *mbox = &mboxes[mbox_idx];
    char *data = (char *)msg;

    // [重要修正] 接收缓冲区也必须在内存中，否则 CPU 无法写入
    if (!check_and_swap_in((uintptr_t)data)) return 0;
    if (!check_and_swap_in((uintptr_t)data + msg_length - 1)) return 0;

    spin_lock_acquire(&mbox->lock);

    while (mbox->count < msg_length)
    {
        do_block(&current_running->list, &mbox->recv_wait_queue);  
        spin_lock_release(&mbox->lock);
        do_scheduler(); 
        spin_lock_acquire(&mbox->lock);
        
        if (mbox->open_refs == 0) {
            spin_lock_release(&mbox->lock);
            return 0;
        }
        
        // 重新检查
        if (!check_and_swap_in((uintptr_t)data)) {
             spin_lock_release(&mbox->lock);
             return 0;
        }
    }

    // 内存拷贝
    for (int i = 0; i < msg_length; i++)
    {
        data[i] = mbox->buf[mbox->head];
        mbox->head = (mbox->head + 1) % MAX_MBOX_LENGTH;
    }
    
    mbox->count -= msg_length;

    while (!list_empty(&mbox->send_wait_queue))
    {
        do_unblock(mbox->send_wait_queue.next);
    }

    spin_lock_release(&mbox->lock);
    return msg_length;
}



// mailbox_t mboxes[MBOX_NUM];

// void init_mbox()
// {
//     for (int i = 0; i < MBOX_NUM; i++)
//     {
//         mboxes[i].open_refs = 0; // 有多少人打开了它
//         mboxes[i].name[0] = '\0';
//         mboxes[i].head = 0; // 读指针
//         mboxes[i].tail = 0; // 写指针
//         mboxes[i].count = 0; // 当前缓冲区的字节数
        
//         spin_lock_init(&mboxes[i].lock); // 用自旋锁保护并发访问
//         list_init(&mboxes[i].send_wait_queue); // 发送等待队列（满时等待）
//         list_init(&mboxes[i].recv_wait_queue); // 接收等待队列（空时等待）
//     }
// }

// // 建立打开
// int do_mbox_open(char *name)
// {
//     // 查找是否已存在同名信箱
//     for (int i = 0; i < MBOX_NUM; i++)
//     {
//         if (mboxes[i].open_refs > 0 && strcmp(mboxes[i].name, name) == 0)
//         {
//             spin_lock_acquire(&mboxes[i].lock); 
//             mboxes[i].open_refs++;
//             spin_lock_release(&mboxes[i].lock);
//             return i; // 返回信箱 ID
//         }
//     }

//     // 如果不存在，找一个空闲槽位创建
//     for (int i = 0; i < MBOX_NUM; i++)
//     {
//         if (mboxes[i].open_refs == 0)
//         {
//             spin_lock_acquire(&mboxes[i].lock);
//             // 拿锁后再次确认它没被别人抢走
//             if (mboxes[i].open_refs == 0) 
//             {
//                 strcpy(mboxes[i].name, name);
//                 mboxes[i].open_refs = 1;
//                 mboxes[i].head = 0;
//                 mboxes[i].tail = 0;
//                 mboxes[i].count = 0;
//                 list_init(&mboxes[i].send_wait_queue);
//                 list_init(&mboxes[i].recv_wait_queue);
                
//                 spin_lock_release(&mboxes[i].lock);
//                 return i;
//             }
//             spin_lock_release(&mboxes[i].lock);
//         }
//     }

//     return -1;
// }

// // 关闭信箱
// void do_mbox_close(int mbox_idx)
// {
//     if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return;
    
//     mailbox_t *mbox = &mboxes[mbox_idx];
    
//     spin_lock_acquire(&mbox->lock);
    
//     if (mbox->open_refs > 0)
//     {
//         mbox->open_refs--; 
//         if (mbox->open_refs == 0)
//         {
//             // 对于最后一个使用者，有义务释放mbox
//             mbox->name[0] = '\0';
//             mbox->count = 0;
//             mbox->head = 0;
//             mbox->tail = 0;
//         }
//     }
    
//     spin_lock_release(&mbox->lock);
// }

// // 生产者发送 
// int do_mbox_send(int mbox_idx, void * msg, int msg_length)
// {
//     if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return 0;
//     if (msg_length <= 0) return 0;
//     // 如果单条消息超过信箱总容量，返回失败
//     if (msg_length > MAX_MBOX_LENGTH) return 0; 

//     mailbox_t *mbox = &mboxes[mbox_idx];
//     char *data = (char *)msg;

//     spin_lock_acquire(&mbox->lock); // 上锁

//     // 剩余空间不足时，循环等待
//     while ( (MAX_MBOX_LENGTH - mbox->count) < msg_length )
//     {
//         do_block(&current_running->list, &mbox->send_wait_queue);            
//         spin_lock_release(&mbox->lock); // 必须放锁 否则调度出去，其它进程拿不到锁就死锁了
//         do_scheduler(); 
//         spin_lock_acquire(&mbox->lock); // 唤醒后重新抢锁检查条件
        
//         // 唤醒后检查信箱是否被意外关闭
//         if (mbox->open_refs == 0) {
//             spin_lock_release(&mbox->lock);
//             return 0;
//         }
//     }

//     // 空间足够一次性写入 则写入
//     for (int i = 0; i < msg_length; i++)
//     {
//         mbox->buf[mbox->tail] = data[i];
//         mbox->tail = (mbox->tail + 1) % MAX_MBOX_LENGTH;
//     }
    
//     mbox->count += msg_length; // 已有字节数增加

//     // 写入了数据，唤醒所有因为空而等待接收的进程
//     while (!list_empty(&mbox->recv_wait_queue))
//     {
//         do_unblock(mbox->recv_wait_queue.next);
//     }

//     spin_lock_release(&mbox->lock);
//     return msg_length; 
// }

// // 消费者接收
// int do_mbox_recv(int mbox_idx, void * msg, int msg_length)
// {
//     if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return 0;
//     if (msg_length <= 0) return 0;
    
//     mailbox_t *mbox = &mboxes[mbox_idx];
//     char *data = (char *)msg;

//     spin_lock_acquire(&mbox->lock); // 上锁

//     // 数据少于目标读取，循环等待
//     while (mbox->count < msg_length)
//     {
//         do_block(&current_running->list, &mbox->recv_wait_queue);  
//         spin_lock_release(&mbox->lock); // 放锁
//         do_scheduler(); 
//         spin_lock_acquire(&mbox->lock);
        
//         if (mbox->open_refs == 0) {
//             spin_lock_release(&mbox->lock);
//             return 0;
//         }
//     }

//     // 从环形缓冲区读到用户 buffer
//     for (int i = 0; i < msg_length; i++)
//     {
//         data[i] = mbox->buf[mbox->head];
//         mbox->head = (mbox->head + 1) % MAX_MBOX_LENGTH;
//     }
    
//     mbox->count -= msg_length;

//     // 唤醒所有因为满而等待发送的进程
//     while (!list_empty(&mbox->send_wait_queue))
//     {
//         do_unblock(mbox->send_wait_queue.next);
//     }

//     spin_lock_release(&mbox->lock);
//     return msg_length;
// }


// [Task 4] 实现 sys_taskset
// 如果 pid 为 0，修改当前进程；否则修改指定 pid 进程
void do_taskset(int mask, int pid)
{
    // 如果 mask 为 0，这通常是非法的
    if (mask == 0) return;

    if (pid == 0) {
        current_running->mask = mask;
    } else {
        // 遍历进程表找到对应 pid
        // 假设 NUM_MAX_TASK 是最大支持任务数
        for (int i = 0; i < NUM_MAX_TASK; i++) {
            if (pcb[i].pid == pid && pcb[i].status != TASK_EXITED) {
                pcb[i].mask = mask;
                return;
            }
        }
    }
}

// 根据pid获取该pid对应的pcb的索引
int get_pcb_index_by_pid(pid_t pid) {
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].pid == pid) {
            return i;
        }
    }
    return -1; // 未找到
}


// P4 Task5 ----------------- pipe 管道 -----------------
pipe_t pipes[MAX_PIPES];

void init_pipes(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].valid = 0;
        pipes[i].name[0] = '\0';
        pipes[i].head = 0;
        pipes[i].tail = 0;
        pipes[i].count = 0;
        spin_lock_init(&pipes[i].lock);
        list_init(&pipes[i].send_wait_queue);
        list_init(&pipes[i].recv_wait_queue);
    }
}

int do_pipe_open(const char *name) {
    // 1. 查找是否存在同名管道
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i].valid && strcmp(pipes[i].name, name) == 0) {
            return i;
        }
    }

    // 2. 寻找空闲槽位创建新管道
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].valid) {
            spin_lock_acquire(&pipes[i].lock);
            if (!pipes[i].valid) { // Double check
                strcpy(pipes[i].name, name);
                pipes[i].valid = 1;
                pipes[i].head = 0;
                pipes[i].tail = 0;
                pipes[i].count = 0;
                // 清空等待队列
                list_init(&pipes[i].send_wait_queue);
                list_init(&pipes[i].recv_wait_queue);
                
                spin_lock_release(&pipes[i].lock);
                return i;
            }
            spin_lock_release(&pipes[i].lock);
        }
    }
    return -1; // 没有可用管道资源
}

// Sender: 剥离物理页 -> 存入管道 -> 解除映射
long do_pipe_give_pages(int pipe_idx, void *src, size_t length) {
    if (pipe_idx < 0 || pipe_idx >= MAX_PIPES || !pipes[pipe_idx].valid) return -1;
    if ((uintptr_t)src % NORMAL_PAGE_SIZE != 0 || length % NORMAL_PAGE_SIZE != 0) return -1;

    pipe_t *p = &pipes[pipe_idx];
    uintptr_t current_va = (uintptr_t)src;
    uintptr_t end_va = current_va + length;
    long pages_transferred = 0;

    while (current_va < end_va) {
        // 1. 确保页面在内存中
        if (!check_and_swap_in(current_va)) return -1;

        PTE *pte = (PTE *)get_pte_of_user_addr(current_va, current_running->pgdir, 0);
        if (!pte || !(*pte & _PAGE_PRESENT)) return -1;
        
        uint64_t ppn = get_pfn(*pte);
        uintptr_t pa = kva2pa(pa2kva(ppn << NORMAL_PAGE_SHIFT));

        spin_lock_acquire(&p->lock);
        
        // 等待空间
        while (p->count >= PIPE_BUFFER_SIZE) {
            do_block(&current_running->list, &p->send_wait_queue);
            spin_lock_release(&p->lock);
            do_scheduler();
            spin_lock_acquire(&p->lock);
            
            if (!p->valid) {
                spin_lock_release(&p->lock);
                return -1;
            }
            // 睡眠醒来，必须重新检查 Swap！页面可能被换出了
            if (p->count >= PIPE_BUFFER_SIZE) {
                 spin_lock_release(&p->lock);
                 if (!check_and_swap_in(current_va)) return -1;
                 
                 // 重新获取物理地址（可能变了）
                 pte = (PTE *)get_pte_of_user_addr(current_va, current_running->pgdir, 0);
                 ppn = get_pfn(*pte);
                 pa = kva2pa(pa2kva(ppn << NORMAL_PAGE_SHIFT));
                 
                 spin_lock_acquire(&p->lock);
            }
        }

        // 2. [关键修复] Pin 住页面：从 Swap 链表中移除
        // 这样 Swap 算法就绝对不会选中这个页面进行换出了
        verify_ptr_and_pin_page(pa);

        // 3. 放入 Pipe
        p->page_buffer[p->head] = ppn;
        p->head = (p->head + 1) % PIPE_BUFFER_SIZE;
        p->count++;
        
        // 4. Sender 丧失所有权 (Unmap)
        *pte = 0; 
        local_flush_tlb_page(current_va); 

        // 唤醒接收者
        while (!list_empty(&p->recv_wait_queue)) {
            do_unblock(p->recv_wait_queue.next);
        }
        spin_lock_release(&p->lock);

        current_va += NORMAL_PAGE_SIZE;
        pages_transferred += NORMAL_PAGE_SIZE;
    }
    return pages_transferred;
}

// Receiver: 获取物理页 -> 建立映射
long do_pipe_take_pages(int pipe_idx, void *dst, size_t length) {
    if (pipe_idx < 0 || pipe_idx >= MAX_PIPES || !pipes[pipe_idx].valid) return -1;
    if ((uintptr_t)dst % NORMAL_PAGE_SIZE != 0 || length % NORMAL_PAGE_SIZE != 0) return -1;

    pipe_t *p = &pipes[pipe_idx];
    uintptr_t current_va = (uintptr_t)dst;
    uintptr_t end_va = current_va + length;
    long pages_transferred = 0;

    while (current_va < end_va) {
        uint64_t ppn = 0;

        spin_lock_acquire(&p->lock);
        while (p->count <= 0) {
            do_block(&current_running->list, &p->recv_wait_queue);
            spin_lock_release(&p->lock);
            do_scheduler();
            spin_lock_acquire(&p->lock);
            
            if (!p->valid) {
                spin_lock_release(&p->lock);
                return -1;
            }
        }

        // 取出 PPN
        ppn = p->page_buffer[p->tail];
        p->tail = (p->tail + 1) % PIPE_BUFFER_SIZE;
        p->count--;

        while (!list_empty(&p->send_wait_queue)) {
            do_unblock(p->send_wait_queue.next);
        }
        spin_lock_release(&p->lock);

        // [Receiver 建立映射]
        PTE *pte = (PTE *)get_pte_of_user_addr(current_va, current_running->pgdir, 1);
        
        // 1. [关键修复] 清理旧页面
        if (*pte & _PAGE_PRESENT) {
            uint64_t old_ppn = get_pfn(*pte);
            uintptr_t old_pa = kva2pa(pa2kva(old_ppn << NORMAL_PAGE_SHIFT));
            
            // 释放物理内存
            freePage(pa2kva(old_pa));
            // 释放元数据 (防止 stale node 留在 in_mem_list 中导致 crash)
            verify_ptr_and_pin_page(old_pa);
        } 
        
        // 2. 写入新 PTE
        set_pfn(pte, ppn);
        set_attribute(pte, _PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_WRITE | 
                           _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY);

        // 3. [关键修复] 注册新页面 (Unpin)
        // 现在页面属于当前进程，允许被 Swap
        uintptr_t new_pa = kva2pa(pa2kva(ppn << NORMAL_PAGE_SHIFT));
        register_page_for_process(new_pa, current_va, current_running->pid);

        local_flush_tlb_page(current_va);
        current_va += NORMAL_PAGE_SIZE;
        pages_transferred += NORMAL_PAGE_SIZE;
    }
    return pages_transferred;
}

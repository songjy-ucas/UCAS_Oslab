#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <os/loader.h>
#include <os/string.h> 
#include <os/smp.h>

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

    // 1. 检查睡眠队列，唤醒到期的进程
    //    这个函数应该将睡眠结束的进程从 BLOCKED 状态改为 READY，并加入 ready_queue
    // 1. 检查睡眠队列，唤醒到期的进程
    check_sleeping();

    // 2. 获取当前进程（即将被切换掉的进程）的指针
    pcb_t *prev = current_running;

    // 3. 根据 prev 进程的当前状态，决定如何处置它
    if (prev->status == TASK_RUNNING) {
        // 注意：idle 进程(pid0)是一个特例，它永不应进入就绪队列。
        // [P3-TASK3] 这里的判断条件修改为只要不是任何一个核的 pid0
        if (prev != &pid0_pcb[0] && prev != &pid0_pcb[1]) {
            prev->status = TASK_READY;
            list_add_tail(&prev->list, &ready_queue);
        }
    }

    // 4. 从就绪队列中选择下一个要运行的进程
    pcb_t *next;
    if (!list_empty(&ready_queue)) {
        // 如果就绪队列不为空，取出队头的任务作为下一个
        next = list_entry(ready_queue.next, pcb_t, list);
        // 将其从就绪队列中移除
        list_del(ready_queue.next);
    } else {
        // [P3-TASK3] 获取当前 CPU ID，运行对应的 idle 任务
        uint64_t cpu_id = get_current_cpu_id();
        next = &pid0_pcb[cpu_id];
    }

    // 5. 更新 current_running 指针，并将新任务的状态设置为 RUNNING
    current_running = next;
    current_running->status = TASK_RUNNING;
    
    // 6. 调用汇编实现的 switch_to 函数，完成上下文切换
    switch_to(prev, next);
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
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].status == TASK_EXITED || pcb[i].pid == -1) {
            return &pcb[i];
        }
    }
    return NULL;
}

extern void exit_trampoline();
// [P3-TASK1] A/C-Core implementation of do_exec
// 它从磁盘(镜像)读取程序，为其分配内存(栈)，设置好运行环境(寄存器)，最后放入就绪队列等待CPU运行。
pid_t do_exec(char *name, int argc, char *argv[])
{
    printk("do_exec: starting to load %s\n", name); // debug use
    // 1. 查找空闲 PCB 
    pcb_t *new_pcb = find_free_pcb();
    if (new_pcb == NULL) {
        printk("Error: No free PCB for exec!\n");
        return -1;
    }

    // 2. 加载任务代码
    uint64_t entry_point = load_task_img(name);
    if (entry_point == 0) {
        printk("Error: Task image '%s' not found!\n", name);
        return -1;
    }

    // 3. 分配内核栈和用户栈
    // 每个进程都有两个栈：
    // - 内核栈：当进程通过系统调用或中断进入内核态时使用，保证内核安全。
    // - 用户栈：进程在用户态跑代码、函数调用时使用。
    // allocKernelPage/allocUserPage 分配物理内存页。
    new_pcb->kernel_stack_base = allocKernelPage(1);
    new_pcb->user_stack_base = allocUserPage(1);
    
    // 栈是向下生长的，所以栈顶指针 = 基地址 + 页大小
    ptr_t kernel_stack = new_pcb->kernel_stack_base + PAGE_SIZE;
    ptr_t user_stack = new_pcb->user_stack_base + PAGE_SIZE;

    // 4. 构造用户栈参数 (argc/argv)
    // 这是一个非常有技巧性的部分。我们需要把 main函数的参数(argc, argv) 
    // 提前塞到新进程的用户栈里，这样新进程一开始运行 main(argc, argv) 时就能读到参数。
    // 内存布局通常是: [高地址] 字符串内容 -> 指针数组 -> [低地址] 栈顶 
    // --- 和guidebook里设计有不同，指针数组和数据的存放位置做了调整，靠近栈顶存放了指针数组
    
    // 4.1 计算所有参数字符串的总长度
    int total_len = 0;
    for (int i = 0; i < argc; ++i) {
        total_len += strlen(argv[i]) + 1; // +1 是为了字符串结尾的 '\0'
    }

    // 4.2 在栈顶预留字符串空间，并做 8 字节对齐
    // string_base 是存放具体字符数据的起始位置
    ptr_t string_base = user_stack - total_len;
    string_base &= ~0x7; // 向下 8 字节对齐，保证内存访问效率

    // 4.3 拷贝字符串内容到栈上
    // 我们需要把内核里的 argv 字符串一个个搬运到用户栈的新位置
    char *argv_new_addr[argc]; 
    char *current_str_pos = (char *)string_base;
    
    for (int i = 0; i < argc; ++i) {
        strcpy(current_str_pos, argv[i]);
        argv_new_addr[i] = current_str_pos; // 记录下这个参数在用户栈里的新地址
        current_str_pos += strlen(argv[i]) + 1;
    }

    // 4.4 在字符串下方预留指针数组的空间
    // 这里的数组存放的是 char* 指针。
    // 数组大小: argc 个指针 + 1 个 NULL 结尾符 (C语言标准要求 argv 必须以 NULL 结尾)
    ptr_t argv_ptr_base = string_base - sizeof(char *) * (argc + 1);
    
    // 4.5 做一次 128 字节对齐 
    // RISC-V的ABI[2] 要求栈指针的地址是128 bit 对齐的
    argv_ptr_base &= ~0x7F; 

    // 4.6 将指针数组拷贝到栈上
    // 现在 argv_ptr_base 就是最终的 argv 数组在用户栈上的地址
    char **argv_on_stack = (char **)argv_ptr_base;
    for (int i = 0; i < argc; ++i) {
        argv_on_stack[i] = argv_new_addr[i]; // 填入刚才记录的新地址
    }
    argv_on_stack[argc] = NULL; // 标准要求 argv 数组最后以 NULL 结尾

    // 4.7 最终的用户栈顶
    // 现在栈顶指针 SP 应该指向这个指针数组的开始位置
    ptr_t final_user_sp = argv_ptr_base;

    // 5. 初始化 PCB 基本字段
    new_pcb->pid = process_id++; // 分配唯一的 PID
    new_pcb->parent_pid = current_running->pid; // 记录谁创建了我（父进程）
    new_pcb->status = TASK_READY; // 状态设为就绪，等待被调度
    new_pcb->cursor_x = 0;
    new_pcb->cursor_y = 0;
    list_init(&new_pcb->wait_list); // 初始化等待队列，以后等待我的进程会挂在这里

    // 6. 调用init_pcb_stack初始化寄存器上下文
    // init_pcb_stack 负责伪造一个现场(TrapFrame)，让 CPU 以为这个进程是被中断打断的。
    // 当调度器执行 switch_to 恢复现场时，CPU 就会：
    // - 跳转到 entry_point (程序的 main 函数)
    // - 栈指针 SP 变成 final_user_sp
    // - 寄存器 a0 = argc, a1 = argv (即 final_user_sp)
    init_pcb_stack(kernel_stack, final_user_sp, entry_point, new_pcb, argc, (char **)final_user_sp);

    // 7. 加入就绪队列
    list_add_tail(&new_pcb->list, &ready_queue);
    
    return new_pcb->pid;
}

// Helper to free resources of a PCB
// 当进程彻底销毁时调用。但在简单的实验内核中，通常只重置状态和 PID，
// 不一定真正回收物理内存页。 ------ 但是在简单内存管理时，我们现在的mm.c里面只有alloc，并没有free内存（这里先暂时注释掉free）
static void free_pcb_resources(pcb_t *p) {
    if (p->kernel_stack_base != 0) {
        // Assuming you have a way to free pages
        //freeKernelPage(p->kernel_stack_base);
    }
    if (p->user_stack_base != 0) {
        //freeUserPage(p->user_stack_base);
    }
    p->status = TASK_EXITED;
    p->pid = -1; // 标记为未使用，供 find_free_pcb 再次分配
}

// [P3-TASK1] Process exits
// 进程自我结束，当程序 main 函数 return 或者调用 exit() 时触发。
void do_exit(void)
{
    // 1. 获取当前进程的PCB
    pcb_t *exiting_pcb = current_running;

    // 2. 将当前进程的状态设置为 EXITED (僵尸态)。
    // 这里我们不直接销毁 PCB，因为父进程可能还需要读取返回值 (waitpid)。我们只是标记它“死了”，资源回收通常由父进程的 waitpid 完成。
    exiting_pcb->status = TASK_EXITED;

    // 3. 核心修改：唤醒正在等待它的父进程（或其他进程）
    // 如果有进程（通常是父进程）之前调用了 waitpid 并在等待我，现在我退出了，需要把它们从阻塞状态唤醒。
    while (!list_empty(&exiting_pcb->wait_list)) {
        // 从等待队列中取出一个等待者
        pcb_t *waiter_pcb = list_entry(exiting_pcb->wait_list.next, pcb_t, list);
        list_del(&waiter_pcb->list);

        // 将等待者的状态从 BLOCKED 改为 READY
        waiter_pcb->status = TASK_READY;

        // 将等待者放回就绪队列，以便它可以被调度器选中
        list_add_tail(&waiter_pcb->list, &ready_queue);
    }

    do_scheduler(); // 主动调度到其他进程
}

// [P3-TASK1] Kill a process
// 杀死指定进程 ------- 特别注意：杀死进程需要释放什么东西？ 它占据的锁要释放
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

    if (target_pcb == NULL || target_pcb->status == TASK_EXITED) {
        return 0; 
    }

    check_and_release_locks(target_pcb->pid); // 释放锁函数内部也实现了 唤醒所有等待锁的进程 即 block->wait_list 里面的所有进程

    // 保存旧状态，用于后续判断是否需要移出队列
    task_status_t old_status = target_pcb->status;

    // 设置为 EXITED，标记该进程已死亡
    target_pcb->status = TASK_EXITED;

    // 唤醒所有等待该进程退出的父进程 (Waitpid 机制) 即target_pcb->wait_list里面的进程
    while (!list_empty(&target_pcb->wait_list)) {
        list_node_t *waiter_node = target_pcb->wait_list.next;
        // 这里需要将等待者从 wait_list 移除，否则下次还会遍历到
        list_del(waiter_node); 
        pcb_t *waiter_pcb = list_entry(waiter_node, pcb_t, list);
        waiter_pcb->status = TASK_READY;
        list_add_tail(&waiter_pcb->list, &ready_queue);
    }

    // 根据目标进程是否是当前进程，决定调度行为
    if (target_pcb == current_running) {
        // 自杀 (Shell kill 自身，或者用户程序调用 kill)
        // 必须主动让出 CPU，否则代码会继续向下执行导致错误
        do_scheduler();
    } else {
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
                    status_str = "BLOCKED"; // 阻塞
                    break;
                case TASK_RUNNING:
                    status_str = "RUNNING"; // 正在运行
                    break;
                case TASK_READY:
                    status_str = "READY";   // 就绪
                    break;
                default:
                    status_str = "UNKNOWN";
                    break;
            }
            
            // 打印信息
            printk("[%d] PID : %d\tSTATUS : %s\n", 
                   i,            
                   pcb[i].pid,   
                   status_str);  
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

mailbox_t mboxes[MBOX_NUM];

void init_mbox()
{
    for (int i = 0; i < MBOX_NUM; i++)
    {
        mboxes[i].open_refs = 0; // 有多少人打开了它
        mboxes[i].name[0] = '\0';
        mboxes[i].head = 0; // 读指针
        mboxes[i].tail = 0; // 写指针
        mboxes[i].count = 0; // 当前缓冲区的字节数
        
        spin_lock_init(&mboxes[i].lock); // 用自旋锁保护并发访问
        list_init(&mboxes[i].send_wait_queue); // 发送等待队列（满时等待）
        list_init(&mboxes[i].recv_wait_queue); // 接收等待队列（空时等待）
    }
}

// 建立打开
int do_mbox_open(char *name)
{
    // 查找是否已存在同名信箱
    for (int i = 0; i < MBOX_NUM; i++)
    {
        if (mboxes[i].open_refs > 0 && strcmp(mboxes[i].name, name) == 0)
        {
            spin_lock_acquire(&mboxes[i].lock); 
            mboxes[i].open_refs++;
            spin_lock_release(&mboxes[i].lock);
            return i; // 返回信箱 ID
        }
    }

    // 如果不存在，找一个空闲槽位创建
    for (int i = 0; i < MBOX_NUM; i++)
    {
        if (mboxes[i].open_refs == 0)
        {
            spin_lock_acquire(&mboxes[i].lock);
            // 拿锁后再次确认它没被别人抢走
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

// 关闭信箱
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
            // 对于最后一个使用者，有义务释放mbox
            mbox->name[0] = '\0';
            mbox->count = 0;
            mbox->head = 0;
            mbox->tail = 0;
        }
    }
    
    spin_lock_release(&mbox->lock);
}

// 生产者发送 
int do_mbox_send(int mbox_idx, void * msg, int msg_length)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return 0;
    if (msg_length <= 0) return 0;
    // 如果单条消息超过信箱总容量，返回失败
    if (msg_length > MAX_MBOX_LENGTH) return 0; 

    mailbox_t *mbox = &mboxes[mbox_idx];
    char *data = (char *)msg;

    spin_lock_acquire(&mbox->lock); // 上锁

    // 剩余空间不足时，循环等待
    while ( (MAX_MBOX_LENGTH - mbox->count) < msg_length )
    {
        do_block(&current_running->list, &mbox->send_wait_queue);            
        spin_lock_release(&mbox->lock); // 必须放锁 否则调度出去，其它进程拿不到锁就死锁了
        do_scheduler(); 
        spin_lock_acquire(&mbox->lock); // 唤醒后重新抢锁检查条件
        
        // 唤醒后检查信箱是否被意外关闭
        if (mbox->open_refs == 0) {
            spin_lock_release(&mbox->lock);
            return 0;
        }
    }

    // 空间足够一次性写入 则写入
    for (int i = 0; i < msg_length; i++)
    {
        mbox->buf[mbox->tail] = data[i];
        mbox->tail = (mbox->tail + 1) % MAX_MBOX_LENGTH;
    }
    
    mbox->count += msg_length; // 已有字节数增加

    // 写入了数据，唤醒所有因为空而等待接收的进程
    while (!list_empty(&mbox->recv_wait_queue))
    {
        do_unblock(mbox->recv_wait_queue.next);
    }

    spin_lock_release(&mbox->lock);
    return msg_length; 
}

// 消费者接收
int do_mbox_recv(int mbox_idx, void * msg, int msg_length)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return 0;
    if (msg_length <= 0) return 0;
    
    mailbox_t *mbox = &mboxes[mbox_idx];
    char *data = (char *)msg;

    spin_lock_acquire(&mbox->lock); // 上锁

    // 数据少于目标读取，循环等待
    while (mbox->count < msg_length)
    {
        do_block(&current_running->list, &mbox->recv_wait_queue);  
        spin_lock_release(&mbox->lock); // 放锁
        do_scheduler(); 
        spin_lock_acquire(&mbox->lock);
        
        if (mbox->open_refs == 0) {
            spin_lock_release(&mbox->lock);
            return 0;
        }
    }

    // 从环形缓冲区读到用户 buffer
    for (int i = 0; i < msg_length; i++)
    {
        data[i] = mbox->buf[mbox->head];
        mbox->head = (mbox->head + 1) % MAX_MBOX_LENGTH;
    }
    
    mbox->count -= msg_length;

    // 唤醒所有因为满而等待发送的进程
    while (!list_empty(&mbox->send_wait_queue))
    {
        do_unblock(mbox->send_wait_queue.next);
    }

    spin_lock_release(&mbox->lock);
    return msg_length;
}
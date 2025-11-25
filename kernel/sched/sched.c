#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <os/loader.h>
#include <os/string.h> 

#include <csr.h>   
#include <screen.h>
#include <printk.h>
#include <assert.h>

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)pid0_stack,
    .user_sp = (ptr_t)pid0_stack
};

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
    check_sleeping();

    // 2. 获取当前进程（即将被切换掉的进程）的指针
    pcb_t *prev = current_running;

    // 3. 根据 prev 进程的当前状态，决定如何处置它
    //    这是本次修改的核心
    if (prev->status == TASK_RUNNING) {
        // 状态为 RUNNING:
        // 这意味着进程是因为时间片用完而被正常调度。
        // 它仍然是可运行的，所以把它变更为 READY 状态，并放回就绪队列末尾。
        // 注意：idle 进程(pid0)是一个特例，它永不应进入就绪队列。
        if (prev != &pid0_pcb) {
            prev->status = TASK_READY;
            list_add_tail(&prev->list, &ready_queue);
        }
    } else if (prev->status == TASK_BLOCKED) {
        // 状态为 BLOCKED:
        // 这意味着进程在运行中调用了阻塞操作（如 do_block, do_sleep）。
        // 它正在等待某个事件，因此不应该被放回就绪队列。
        // 我们在这里什么都不做，让它留在它自己的等待队列里（如 sleep_queue）。
    } else if (prev->status == TASK_EXITED) {
        // 状态为 EXITED:
        // 进程已经执行完毕并调用了 do_exit。
        // 它不应该再被调度。我们在这里什么都不做。
        // 其 PCB 资源最终会由父进程的 waitpid 或其他机制回收。
    }
    // 注意：prev->status 不应该是 READY，因为它刚刚还在运行。

    // 4. 从就绪队列中选择下一个要运行的进程
    pcb_t *next;
    if (!list_empty(&ready_queue)) {
        // 如果就绪队列不为空，取出队头的任务作为下一个
        next = list_entry(ready_queue.next, pcb_t, list);
        // 将其从就绪队列中移除
        list_del(ready_queue.next);
    } else {
        // 如果就绪队列为空，说明当前没有其他任务可运行，只能运行 idle 任务
        next = &pid0_pcb;
    }

    // 5. 更新 current_running 指针，并将新任务的状态设置为 RUNNING
    current_running = next;
    current_running->status = TASK_RUNNING;
    
    // 6. 调用汇编实现的 switch_to 函数，完成上下文切换
    //    prev 的上下文将被保存，next 的上下文将被恢复
    switch_to(prev, next);
    // TODO: [p2-task1] switch_to current_running

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
    new_pcb->kernel_stack_base = allocKernelPage(1);
    new_pcb->user_stack_base = allocUserPage(1);
    ptr_t kernel_stack = new_pcb->kernel_stack_base + PAGE_SIZE;
    ptr_t user_stack = new_pcb->user_stack_base + PAGE_SIZE;

    // 4. 构造用户栈参数 (argc/argv)
    //    布局: [高] 字符串 -> 指针数组 -> [低] 栈顶
    
    // 4.1 计算所有参数字符串的总长度
    int total_len = 0;
    for (int i = 0; i < argc; ++i) {
        total_len += strlen(argv[i]) + 1; // +1 是为了 '\0'
    }

    // 4.2 在栈顶预留字符串空间，并做 8 字节对齐
    ptr_t string_base = user_stack - total_len;
    string_base &= ~0x7; // 向下 8 字节对齐

    // 4.3 拷贝字符串内容到栈上
    //     我们用一个临时数组来记录拷贝后的新地址
    char *argv_new_addr[argc]; 
    char *current_str_pos = (char *)string_base;
    
    for (int i = 0; i < argc; ++i) {
        strcpy(current_str_pos, argv[i]);
        argv_new_addr[i] = current_str_pos; // 记录这个参数在用户栈的新地址
        current_str_pos += strlen(argv[i]) + 1;
    }

    // 4.4 在字符串下方预留指针数组的空间
    //     数组大小: argc 个指针 + 1 个 NULL 结尾符
    ptr_t argv_ptr_base = string_base - sizeof(char *) * (argc + 1);
    //     再做一次 128 字节对齐 (RISC-V 栈通常要求 16 字节对齐，128 更稳妥)
    argv_ptr_base &= ~0x7F; // 强迫症式对齐，模仿学长那种严谨风格

    // 4.5 将指针数组拷贝到栈上
    char **argv_on_stack = (char **)argv_ptr_base;
    for (int i = 0; i < argc; ++i) {
        argv_on_stack[i] = argv_new_addr[i];
    }
    argv_on_stack[argc] = NULL; // 标准要求 argv 数组最后以 NULL 结尾

    // 4.6 最终的用户栈顶
    ptr_t final_user_sp = argv_ptr_base;

    // 5. 初始化 PCB 基本字段
    new_pcb->pid = process_id++;
    new_pcb->parent_pid = current_running->pid;
    new_pcb->status = TASK_READY;
    new_pcb->cursor_x = 0;
    new_pcb->cursor_y = 0;
    list_init(&new_pcb->wait_list);

    // 6. 调用封装好的函数初始化寄存器上下文
    //    注意：我们将 final_user_sp 同时作为 栈顶(sp) 和 参数数组地址(argv/a1) 传入
    //    因为此时栈顶存放的正好就是那个指针数组
    init_pcb_stack(kernel_stack, final_user_sp, entry_point, new_pcb, argc, (char **)final_user_sp);

    // 7. 加入就绪队列
    list_add_tail(&new_pcb->list, &ready_queue);
    
    return new_pcb->pid;
}

// Helper to free resources of a PCB
static void free_pcb_resources(pcb_t *p) {
    if (p->kernel_stack_base != 0) {
        // Assuming you have a way to free pages
        // freeKernelPage(p->kernel_stack_base);
    }
    if (p->user_stack_base != 0) {
        // freeUserPage(p->user_stack_base);
    }
    p->status = TASK_EXITED;
    p->pid = -1; // Mark as unused
}


// // [P3-TASK1] Process exits
// void do_exit(void)
// {
//     // 1. Set current process status to EXITED
//     current_running->status = TASK_EXITED;

//     // 2. Wake up any waiting processes (parent)
//     // Unblock all processes in the wait_list
//     while (!list_empty(&current_running->wait_list)) {
//         list_node_t *waiter_node = current_running->wait_list.next;
//         pcb_t *waiter_pcb = list_entry(waiter_node, pcb_t, list);
//         do_unblock(waiter_node);
//     }

//     // 3. Switch to another process.
//     // The resources of this exited process will be cleaned up
//     // by its parent in waitpid.
//     do_scheduler();
// }

// [P3-TASK1] Process exits
void do_exit(void)
{
    // 1. 获取当前进程（即正在退出的进程）的PCB
    pcb_t *exiting_pcb = current_running;

    // 2. 将当前进程的状态设置为 EXITED。
    //    此时，它成了一个“僵尸进程”，等待父进程来回收资源。
    exiting_pcb->status = TASK_EXITED;

    // 3. 核心修改：唤醒正在等待它的父进程（或其他进程）
    //    遍历自己的 wait_list，将所有在等待的进程 unblock。
    while (!list_empty(&exiting_pcb->wait_list)) {
        // 从等待队列中取出一个等待者（通常是父进程）
        pcb_t *waiter_pcb = list_entry(exiting_pcb->wait_list.next, pcb_t, list);
        list_del(&waiter_pcb->list);

        // 将等待者的状态从 BLOCKED 改为 READY
        waiter_pcb->status = TASK_READY;

        // 将等待者放回就绪队列，以便它可以被调度器选中
        list_add_tail(&waiter_pcb->list, &ready_queue);
    }

    // 4. 当前进程已经退出，不能再继续执行。
    //    必须调用调度器，将CPU交给其他进程。
    do_scheduler();

    // 这之后的代码永远不会被执行
}

// [P3-TASK1] Kill a process
int do_kill(pid_t pid)
{
    // Cannot kill idle task or itself in this simple way
    if (pid <= 0 || pid == current_running->pid) {
        return 0; // Failure
    }

    pcb_t *target_pcb = NULL;
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].pid == pid) {
            target_pcb = &pcb[i];
            break;
        }
    }

    if (target_pcb == NULL || target_pcb->status == TASK_EXITED) {
        return 0; // Process not found or already exited
    }

    // 1. Set status to EXITED
    target_pcb->status = TASK_EXITED;

    // 2. Wake up any processes waiting on the killed process
    while (!list_empty(&target_pcb->wait_list)) {
        list_node_t *waiter_node = target_pcb->wait_list.next;
        // pcb_t *waiter_pcb = list_entry(waiter_node, pcb_t, list);
        do_unblock(waiter_node);
    }

    // 3. Remove from any queue it might be in (e.g., ready_queue, sleep_queue)
    // This is important to prevent the scheduler from picking it again.
    list_del(&target_pcb->list);

    // Resources will be cleaned up by its parent's waitpid.
    return 1; // Success
}

// [P3-TASK1] Wait for a child process
int do_waitpid(pid_t pid)
{
    // 1. Find the target child process
    pcb_t *child_pcb = NULL;
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].pid == pid) {
            child_pcb = &pcb[i];
            break;
        }
    }

    // If no such process, or it's not our child (optional check), return error
    if (child_pcb == NULL || child_pcb->parent_pid != current_running->pid) {
        return -1; // Not found or not a child
    }

    // 2. Check child's status
    if (child_pcb->status == TASK_EXITED) {
        // Child has already exited (is a "zombie"), clean it up now.
        free_pcb_resources(child_pcb);
        return pid;
    }

    // 3. If child is still running, block the current process
    // and add it to the child's wait_list.
    current_running->status = TASK_BLOCKED;
    list_add_tail(&current_running->list, &child_pcb->wait_list);
    
    // 4. Yield to scheduler
    do_scheduler();

    // 5. When woken up, the child has exited. Clean up its resources.
    free_pcb_resources(child_pcb);

    return pid;
}
void do_process_show()
{

    printk("[Process Table]:\n");
    
    // 遍历所有可能的PCB
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        // 检查PCB是否被使用 (pid != -1)
        if (pcb[i].pid != -1 && pcb[i].status != TASK_EXITED) {
            char *status_str;
            switch (pcb[i].status) {
                case TASK_BLOCKED:
                    status_str = "BLOCKED";
                    break;
                case TASK_RUNNING:
                    status_str = "RUNNING";
                    break;
                case TASK_READY:
                    status_str = "READY";
                    break;
                default:
                    status_str = "UNKNOWN";
                    break;
            }
            
      
            printk("[%d] PID : %d\tSTATUS : %s\n", 
                   i,            
                   pcb[i].pid,   
                   status_str);  
        }
    }
}
// 这是 do_getpid 的实现，非常简单
pid_t do_getpid()
{
    return current_running->pid;
}

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

// void do_scheduler(void)
// {
//     // TODO: [p2-task3] Check sleep queue to wake up PCBs

//     /************************************************************/
//     /* Do not touch this comment. Reserved for future projects. */
//     /************************************************************/

//     // TODO: [p2-task1] Modify the current_running pointer.


//     check_sleeping();
//     // 1. 保存当前任务的指针，并准备下一个任务的指针
//     pcb_t *prev = current_running;
//     pcb_t *next;

//     // 2. 从就绪队列中选择下一个要运行的任务
//     if (!list_empty(&ready_queue)) {
//         // 如果就绪队列不为空，取出队头的任务作为下一个
//         next = list_entry(ready_queue.next, pcb_t, list);
//         // 将其从就绪队列中移除
//         list_del(ready_queue.next);
//     } else {
//         // 如果就绪队列为空，说明只有 idle 任务(pid0)可以运行
//         next = &pid0_pcb;
//     }

//     // 3. 如果当前任务(prev)不是 idle 任务，则将其放回就绪队列的末尾
//     //    这样就实现了 Round-Robin (轮转)
//     //    状态为 RUNNING 的任务才需要放回，被阻塞的任务不应放回
//     if (prev != &pid0_pcb && prev->status == TASK_RUNNING) {
//         prev->status = TASK_READY;
//         list_add_tail(&prev->list, &ready_queue);
//     }
    
//     // 4. 更新 current_running 指针，并设置新任务的状态
//     next->status = TASK_RUNNING;
//     current_running = next;

//     // 5. 调用汇编实现的 switch_to 函数，完成上下文切换
//     //    prev 的上下文将被保存，next 的上下文将被恢复
//     switch_to(prev, next);

//     // TODO: [p2-task1] switch_to current_running

// }

void do_scheduler(void)
{
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
    // 1. Find a free PCB
    pcb_t *new_pcb = find_free_pcb();
    if (new_pcb == NULL) {
        printk("Error: No free PCB for exec!\n");
        return -1; // No free PCB
    }

    // 2. Load task image
    uint64_t entry_point = load_task_img(name);
    if (entry_point == 0) {
        printk("Error: Task image '%s' not found!\n", name);
        return -1; // Task not found
    }

    // 3. Allocate kernel and user stack
    new_pcb->kernel_stack_base = allocKernelPage(1);
    new_pcb->user_stack_base = allocUserPage(1);
    ptr_t kernel_stack = new_pcb->kernel_stack_base + PAGE_SIZE;
    ptr_t user_stack = new_pcb->user_stack_base + PAGE_SIZE;

    // 4. Setup user stack for argc/argv (the complex part)
    // As per Figure P3-4
    // We will copy argv strings and the argv pointer array to the new user stack
    char *argv_stack[argc];
    int total_len = 0;
    for(int i = 0; i < argc; ++i){
        total_len += strlen(argv[i]) + 1; // +1 for '\0'
    }

    // Allocate space for strings on the stack, align to 8 bytes
    ptr_t string_base = user_stack - total_len;
    string_base &= ~0x7; // 8-byte alignment

    // Copy strings
    char *current_str_pos = (char *)string_base;
    for(int i = 0; i < argc; ++i){
        strcpy(current_str_pos, argv[i]);
        argv_stack[i] = current_str_pos; // Store the new address
        current_str_pos += strlen(argv[i]) + 1;
    }

    // Allocate space for the argv pointer array
    ptr_t argv_ptr_base = string_base - sizeof(char *) * (argc + 1); // +1 for NULL terminator
    
    // Copy the pointer array
    char **argv_on_stack = (char **)argv_ptr_base;
    for(int i = 0; i < argc; ++i){
        argv_on_stack[i] = argv_stack[i];
    }
    argv_on_stack[argc] = NULL; // As per C standard

    // The final user stack pointer
    ptr_t final_user_sp = argv_ptr_base;


    // 5. Initialize PCB fields
    new_pcb->pid = process_id++;
    new_pcb->parent_pid = current_running->pid;
    new_pcb->status = TASK_READY;
    new_pcb->cursor_x = 0;
    new_pcb->cursor_y = 0;
    list_init(&new_pcb->wait_list);

    // 6. Initialize register context on kernel stack
    // We need a modified version of init_pcb_stack that can set a0 and a1
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));
    memset(pt_regs, 0, sizeof(regs_context_t));
    
    // Set arguments for the new process's main function
    pt_regs->regs[10] = argc;         // a0 = argc
    pt_regs->regs[11] = final_user_sp; // a1 = argv

    pt_regs->regs[2] = final_user_sp; // sp = user_sp
    pt_regs->regs[4] = (reg_t)new_pcb; // tp = current pcb
    pt_regs->sepc = entry_point;
    pt_regs->sstatus = (read_csr(sstatus) | SR_SPIE) & ~SR_SPP;

    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    pt_switchto->regs[0] = (reg_t)&ret_from_exception; // ra

    new_pcb->kernel_sp = (reg_t)pt_switchto;
    new_pcb->user_sp = final_user_sp;

    // 7. Add to ready queue
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
        pcb_t *waiter_pcb = list_entry(waiter_node, pcb_t, list);
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
    // 打印一个表头
    printk("PID\tSTATUS\t\tKERNEL_SP\tUSER_SP\n\r");
    
    // 遍历所有可能的PCB
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        // 检查PCB是否被使用 (pid != -1)
        if (pcb[i].pid != -1 && pcb[i].status != TASK_EXITED) { // 不显示已退出的进程
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
                /* case TASK_EXITED: 已经被上面的if排除了
                    status_str = "EXITED";
                    break; */
                default:
                    status_str = "UNKNOWN";
                    break;
            }
            printk("[%d]\t%s\t\t0x%x\t0x%x\n\r", 
                   pcb[i].pid, 
                   status_str, 
                   pcb[i].kernel_sp, 
                   pcb[i].user_sp);
        }
    }
}

// 这是 do_getpid 的实现，非常简单
pid_t do_getpid()
{
    return current_running->pid;
}

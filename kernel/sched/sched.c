#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)pid0_stack,
    .user_sp = (ptr_t)pid0_stack,
    .status = TASK_RUNNING,
    .list = {&(pid0_pcb.list), &(pid0_pcb.list)} // 独立于ready_quene之外，自己指向自己，不入队列
};

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

void do_scheduler(void)
{
    // TODO: [p2-task3] Check sleep queue to wake up PCBs

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/

    // TODO: [p2-task1] Modify the current_running pointer.
    pcb_t *prev_task = current_running;
    pcb_t *next_task ;
    
    if (list_is_empty(&ready_queue)) {
        return;
    }
    
    next_task = list_entry(ready_queue.next, pcb_t, list);
    list_del(ready_queue.next); // 从队列中移除

    if (prev_task->pid != 0) {
        if (prev_task->status == TASK_RUNNING) {
            // 任务是主动 yield 或被抢占的，状态仍然是 RUNNING
            // 把它变回 READY 并放回就绪队列尾部
            prev_task->status = TASK_READY;
            list_add_tail(&prev_task->list, &ready_queue);
        }
    }

    //  更新下一个任务的状态并切换
    next_task->status = TASK_RUNNING;
    current_running = next_task;

    // TODO: [p2-task1] switch_to current_running
    switch_to(prev_task, next_task);

}

void do_sleep(uint32_t sleep_time)
{
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
}

void do_block(list_node_t *pcb_node, list_head *queue)
{
    // TODO: [p2-task2] block the pcb task into the block queue
}

void do_unblock(list_node_t *pcb_node)
{
    // TODO: [p2-task2] unblock the `pcb` from the block queue
}

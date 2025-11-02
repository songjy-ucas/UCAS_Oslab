#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/kernel.h>
#include <os/time.h>
#include <os/mm.h>
#include <screen.h>
#include <printk.h>
#include <os/string.h>
#include <assert.h>

// [p2-task5] 定义调度算法所需的宏 
#define MIN_TIMESLICE_WAITING 0 // 等待阶段的最小时间片，让飞机直接等待
#define PRIORITY_WEIGHT 1 // 权重因子
#define PRIORITY_SCALE_FACTOR 10 // 权重因子实际效果缩小倍数

#define TOTAL_CREDITS_PER_EPOCH 20 // 每个调度周期的总信用点数 (减小以加快响应)
#define WAITING_CREDITS 0          // “等待”状态的进程获得的信用点

#define FLY_BASE_TIMESLICE 0
#define FLY_WORKLOAD_SCALE 10
#define NON_FLY_TIMESLICE 10

int global_round_num = 1;
int global_phase_num = 0;

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

// //task1-4 do_scheduler
// void do_scheduler(void)
// {
//     // TODO: [p2-task3] Check sleep queue to wake up PCBs
//     check_sleeping();
//     /************************************************************/
//     /* Do not touch this comment. Reserved for future projects. */
//     /************************************************************/

//     // TODO: [p2-task1] Modify the current_running pointer.
//     pcb_t *prev_task = current_running;
//     pcb_t *next_task ;

//     if (prev_task->pid != 0) {
//         if (prev_task->status == TASK_RUNNING) {
//             // 任务是主动 yield 或被抢占的，状态仍然是 RUNNING
//             // 把它变回 READY 并放回就绪队列尾部
//             prev_task->status = TASK_READY;
//             list_add_tail(&prev_task->list, &ready_queue);
//         }
//     }
    
//     next_task = list_entry(ready_queue.next, pcb_t, list);
//     list_del(ready_queue.next); // 从队列中移除 --------- debug经历：一定要先把RUNNING的存回ready_queue，再取队列第一个，否则单一用户程序运行时会出错   

//     //  更新下一个任务的状态并切换
//     next_task->status = TASK_RUNNING;
//     current_running = next_task;
    
//     printl("%d",current_running->pid);

//     // TODO: [p2-task1] switch_to current_running
//     switch_to(prev_task, next_task);

// }

// task5 do_scheduler
void do_scheduler(void)
{
    // 阶段 1: 准备工作 - 唤醒睡眠任务，并将刚运行完的任务归位
    // -----------------------------------------------------------------
    check_sleeping();

    pcb_t *prev_task = current_running;
    if (prev_task->pid != 0 && prev_task->status == TASK_RUNNING) {
        prev_task->status = TASK_READY;
        list_add_tail(&prev_task->list, &ready_queue);
    }
    // -----------------------------------------------------------------

    // 阶段 2: [核心] 动态发现全局轮次和全局Phase
    // -----------------------------------------------------------------
    int min_round = 0x7FFFFFFF;
    int min_phase_in_min_round = 1; // 默认假设本轮都过了检查点
    int fly_exists = 0;

    // Pass 1: 找到最小轮次，即全局轮次
    for (int i = 1; i < NUM_MAX_TASK; i++) {
        if (pcb[i].pid != 0 && strncmp(pcb[i].name, "fly", 3) == 0) {
            fly_exists = 1;
            if (pcb[i].round < min_round) {
                min_round = pcb[i].round;
            }
        }
    }
    if (fly_exists) {
        global_round_num = min_round;
    }

    // Pass 2: 在全局轮次内，找到最小Phase，即全局Phase
    if (fly_exists) {
        for (int i = 1; i < NUM_MAX_TASK; i++) {
            if (pcb[i].pid != 0 && strncmp(pcb[i].name, "fly", 3) == 0) {
                // 只关心处于全局轮次的飞机
                if (pcb[i].round == global_round_num) {
                    if (pcb[i].in_checkpoint_phase == 0) {
                        min_phase_in_min_round = 0; // 只要有一个没到，全局就是0
                        break; // 找到了0，无需再找
                    }
                }
            }
        }
        global_phase_num = min_phase_in_min_round;
    }
    // -----------------------------------------------------------------

    // 阶段 3: [轮询] 从队头开始，找到第一个有资格运行的任务
    // -----------------------------------------------------------------
    pcb_t *next_task = NULL;
    int queue_size = 0;
    list_node_t *node = ready_queue.next;
    while(node != &ready_queue) {
        queue_size++;
        node = node->next;
    }

    for (int i = 0; i < queue_size; i++) {
        pcb_t *candidate = list_entry(ready_queue.next, pcb_t, list);
        int is_eligible = 0;

        // [资格审查 - 严格遵循“最小值为准”原则]
        if (strncmp(candidate->name, "fly", 3) == 0) {
            if (candidate->round < global_round_num) {
                // 轮次落后，无条件获得资格
                is_eligible = 1;
            } else if (candidate->round == global_round_num) {
                // 与大部队同步，只有自己的phase也等于全局最小phase时才有资格
                if (candidate->in_checkpoint_phase == global_phase_num) {
                    is_eligible = 1;
                }
            }
        } else {
            is_eligible = 1; // 非飞机任务总是有资格
        }

        if (is_eligible) {
            next_task = candidate;
            break; // 找到了，跳出循环
        } else {
            // 不合格，挪到队尾，继续审查下一个
            list_del(ready_queue.next);
            list_add_tail(&candidate->list, &ready_queue);
        }
    }
    // -----------------------------------------------------------------

    // 阶段 4: 分配时间片并执行切换
    // -----------------------------------------------------------------
    if (next_task != NULL) {
        list_del(&next_task->list);
        if (strncmp(next_task->name, "fly", 3) == 0) {
            int workload = (next_task->workload > 0) ? next_task->workload : 1;
            next_task->time_slice = FLY_BASE_TIMESLICE + (workload * FLY_WORKLOAD_SCALE) / 20;
        } else {
            next_task->time_slice = NON_FLY_TIMESLICE;
        }
        current_running = next_task;
    } else {
        // 如果转了一圈都没找到合格的，切换到idle
        current_running = &pid0_pcb;
    }

    current_running->status = TASK_RUNNING;
    switch_to(prev_task, current_running);
    // -----------------------------------------------------------------
}



void do_sleep(uint32_t sleep_time)
{
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    current_running->status = TASK_BLOCKED;
    // 2. set the wake up time for the blocked task
    current_running->wakeup_time = get_timer()+sleep_time;
    // 3. reschedule because the current_running is blocked.
    list_add_tail(&current_running->list,&sleep_queue); // 放入sleep队列
    do_scheduler();
}

void do_block(list_node_t *pcb_node, list_head *queue)
{
    // TODO: [p2-task2] block the pcb task into the block queue
    
    // 1. 获取 pcb 指针并修改状态为 BLOCKED
    pcb_t *blocked_pcb = list_entry(pcb_node, pcb_t, list);
    blocked_pcb->status = TASK_BLOCKED;

    // 2. 将 pcb_node 添加到指定的阻塞队列 `queue` 的尾部
    //    不用 list_del，因为 pcb_node 来自一个不在任何队列中的 RUNNING 进程。
    list_add_tail(pcb_node, queue);
}

void do_unblock(list_node_t *pcb_node)
{
    // TODO: [p2-task2] unblock the `pcb` from the block queue
    // 1. 获取 pcb 指针并修改状态为 READY
    pcb_t *pcb_to_wake = list_entry(pcb_node, pcb_t, list);
    pcb_to_wake->status = TASK_READY;

    // 2. 将 pcb_node 从它当前所在的阻塞队列中移除
    list_del(pcb_node);

    // 3. 将 pcb_node 添加到就绪队列 `ready_queue` 的尾部
    list_add_tail(pcb_node, &ready_queue);
}

void do_set_sche_workload(int round, int phase, int workload){
  // TODO:task5
    // 直接将用户报告的飞机位置相关信息，存入 pcb_t 结构体中我们为此新增的字段
    if (current_running != NULL && current_running->pid != 0) {
        current_running->round = round;
        current_running->in_checkpoint_phase = phase;
        current_running->workload = workload;
    }
}
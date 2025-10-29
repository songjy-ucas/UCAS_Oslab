#include <os/list.h>
#include <os/sched.h>
#include <type.h>

uint64_t time_elapsed = 0; // CPU 核心已经走过的时钟周期数ticks，一直增加
uint64_t time_base = 0; //频率基准值，它表示一秒钟对应多少个 ticks

uint64_t get_ticks() // 直接从硬件获取当前的绝对时钟周期数。这是所有时间功能的最底层、最核心的接口
{
    __asm__ __volatile__(
        "rdtime %0" // RISC-V 架构的一条特殊指令，作用就是读取硬件计时器 time CSR的值
        : "=r"(time_elapsed)); // 汇编指令执行完毕后，将那个通用寄存器的值赋给 C 语言的 time_elapsed 变量
    return time_elapsed;
}

uint64_t get_timer() // 获取以秒为单位的系统运行时间
{
    return get_ticks() / time_base;
}

uint64_t get_time_base()
{
    return time_base;
}

void latency(uint64_t time) // 实现一个忙等待的延迟。这个函数会空转CPU，直到指定的时间（以秒为单位）过去。

{
    uint64_t begin_time = get_timer();

    while (get_timer() - begin_time < time);
    return;
}

void check_sleeping(void) // 检查并唤醒睡眠队列中到期的任务。这是实现 sleep() 功能的核心部分。
{
    // TODO: [p2-task3] Pick out tasks that should wake up from the sleep queue
    // 声明用于遍历的指针
    list_node_t *current_node, *next_node;
    pcb_t *pcb_to_check;

    // 获取当前时间作为判断基准
    uint64_t current_time = get_timer();

    // 安全地遍历睡眠队列
    // 使用 next_node 提前保存下一个节点，防止在删除 current_node 时破坏遍历过程
    for (current_node = sleep_queue.next, next_node = current_node->next; 
         current_node != &sleep_queue; 
         current_node = next_node, next_node = current_node->next)
    {
        // 从链表节点反向计算出其所属的 PCB 结构体的地址
        pcb_to_check = list_entry(current_node, pcb_t, list);

        // 检查该进程的睡眠时间是否已到
        if (pcb_to_check->wakeup_time <= current_time) {
            // 时间到了，唤醒这个进程
            do_unblock(&pcb_to_check->list); 
        }
    }
}
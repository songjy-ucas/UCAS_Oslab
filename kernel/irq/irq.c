// 建立C语言层面的分发机制，根据 scause 寄存器的值调用不同的处理函数。
#include <os/irq.h>
#include <os/time.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/kernel.h>
#include <printk.h>
#include <assert.h>
#include <screen.h>

handler_t irq_table[IRQC_COUNT]; // 中断处理函数表
handler_t exc_table[EXCC_COUNT]; // 异常处理函数表

void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause) // 负责判断陷阱类型并分发给正确的处理函数
{
    // TODO: [p2-task3] & [p2-task4] interrupt handler.
    // call corresponding handler by the value of `scause`
    // 通过 scause 的最高位判断是中断(interrupt)还是异常(exception)
    if (scause & (1UL << 63)) {
        // Interrupt
        // 清除最高位，得到中断号
        uint64_t irq_num = scause & ~(1UL << 63);
        // 从中断表中调用对应的处理函数
        irq_table[irq_num](regs, stval, scause);
    } else {
        // Exception
        // scause 的值本身就是异常号，从异常表中调用对应的处理函数,和irq保持一致可以也做一次 & ~(1UL << 63)
        uint64_t exc_num = scause & ~(1UL << 63);
        exc_table[exc_num](regs, stval, scause);
    }
}

void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause) // 当计时器硬件触发中断时，该函数会被 interrupt_helper 调用。
{
    // TODO: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    // 设置下一次中断
    bios_set_timer(get_ticks() + TIMER_INTERVAL); //下一次查询中断的时间

    // 将当前运行进程的时间片计数器减 1
    if (current_running->pid != 0) {
        current_running->time_slice--;
    }

    // 如果时间片用完，则调用调度器进行轮转
    if (current_running->time_slice <= 0) {
        do_scheduler(); 
    }
    // printl("%d",current_running->pid);
    // do_scheduler();
}

void init_exception() // 初始化中断和异常处理机制,这个函数在内核启动的早期阶段被调用，用于建立整个陷阱处理的框架。
{
    /* TODO: [p2-task3] initialize exc_table */
    /* NOTE: handle_syscall, handle_other, etc.*/
    // handle_other:这是一个通用的、默认的处理函数。它的任务是处理所有没有被专门指定处理函数的异常。会用来打印报错
    // handle_syscall:处理系统调用

    // 异常初始化
    exc_table[EXCC_INST_MISALIGNED ] = handle_other;
    exc_table[EXCC_INST_ACCESS     ] = handle_other;
    exc_table[EXCC_BREAKPOINT      ] = handle_other;
    exc_table[EXCC_LOAD_ACCESS     ] = handle_other;
    exc_table[EXCC_STORE_ACCESS    ] = handle_other;
    exc_table[EXCC_SYSCALL         ] = handle_syscall;
    exc_table[EXCC_INST_PAGE_FAULT ] = handle_other;
    exc_table[EXCC_LOAD_PAGE_FAULT ] = handle_other;
    exc_table[EXCC_STORE_PAGE_FAULT] = handle_other;   

    /* TODO: [p2-task4] initialize irq_table */
    /* NOTE: handle_int, handle_other, etc.*/
    // 中断初始化
    irq_table[IRQC_U_SOFT ] = handle_other;
    irq_table[IRQC_S_SOFT ] = handle_other;
    irq_table[IRQC_M_SOFT ] = handle_other;
    irq_table[IRQC_U_TIMER] = handle_other;
    irq_table[IRQC_S_TIMER] = handle_irq_timer;
    irq_table[IRQC_M_TIMER] = handle_other;
    irq_table[IRQC_U_EXT  ] = handle_other;
    irq_table[IRQC_S_EXT  ] = handle_other;
    irq_table[IRQC_M_EXT  ] = handle_other;
    /* TODO: [p2-task3] set up the entrypoint of exceptions */
    setup_exception(); // 调用汇编函数(进入trap.S内的setup_exception)，将 exception_handler_entry 的地址写入 stvec
}

void handle_other(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    char* reg_name[] = {
        "zero "," ra  "," sp  "," gp  "," tp  ",
        " t0  "," t1  "," t2  ","s0/fp"," s1  ",
        " a0  "," a1  "," a2  "," a3  "," a4  ",
        " a5  "," a6  "," a7  "," s2  "," s3  ",
        " s4  "," s5  "," s6  "," s7  "," s8  ",
        " s9  "," s10 "," s11 "," t3  "," t4  ",
        " t5  "," t6  "
    };
    for (int i = 0; i < 32; i += 3) {
        for (int j = 0; j < 3 && i + j < 32; ++j) {
            printk("%s : %016lx ",reg_name[i+j], regs->regs[i+j]);
        }
        printk("\n\r");
    }
    printk("sstatus: 0x%lx sbadaddr: 0x%lx scause: %lu\n\r",
           regs->sstatus, regs->sbadaddr, regs->scause);
    printk("sepc: 0x%lx\n\r", regs->sepc);
    printk("tval: 0x%lx cause: 0x%lx\n", stval, scause);
    assert(0);
}

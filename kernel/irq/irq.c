#include <os/irq.h>
#include <os/time.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/kernel.h>
#include <printk.h>
#include <assert.h>
#include <screen.h>
#include <os/mm.h>

#define SCAUSE_IRQ_MASK 0x8000000000000000
handler_t irq_table[IRQC_COUNT];
handler_t exc_table[EXCC_COUNT];

void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task3] & [p2-task4] interrupt handler.
    // call corresponding handler by the value of `scause`
    if (scause & SCAUSE_IRQ_MASK) {
        // It's an interrupt
        irq_table[scause & ~SCAUSE_IRQ_MASK](regs, stval, scause);
    } else {
        // It's an exception
        exc_table[scause](regs, stval, scause);
    }
}

void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    bios_set_timer(get_ticks()+TIMER_INTERVAL); //下一次查询中断的时间
    printl("%d",current_running->pid);
    do_scheduler();
}

// [Task 2 新增] 缺页异常处理的包装函数
void handle_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // 调用 mm.c 中的实际处理函数
    do_page_fault(regs);
}

void init_exception()
{
    /* TODO: [p2-task3] initialize exc_table */
    /* NOTE: handle_syscall, handle_other, etc.*/

    /* TODO: [p2-task4] initialize irq_table */
    /* NOTE: handle_int, handle_other, etc.*/

    /* TODO: [p2-task3] set up the entrypoint of exceptions */

    /* Initialize exc_table */
    for (int i = 0; i < EXCC_COUNT; i++) {
        exc_table[i] = &handle_other;
    }

    // [Task 2 修改] 注册缺页异常处理函数
    // 12: 取指令缺页, 13: 读缺页, 15: 写缺页
    exc_table[EXCC_INST_PAGE_FAULT ] = &handle_page_fault;
    exc_table[EXCC_LOAD_PAGE_FAULT ] = &handle_page_fault;
    exc_table[EXCC_STORE_PAGE_FAULT] = &handle_page_fault;

    exc_table[EXCC_SYSCALL] = &handle_syscall;
    irq_table[IRQC_U_SOFT ] = handle_other;
    irq_table[IRQC_S_SOFT ] = handle_other;
    irq_table[IRQC_M_SOFT ] = handle_other;
    irq_table[IRQC_U_TIMER] = handle_other;
    irq_table[IRQC_S_TIMER] = handle_irq_timer;
    irq_table[IRQC_M_TIMER] = handle_other;
    irq_table[IRQC_U_EXT  ] = handle_other;
    irq_table[IRQC_S_EXT  ] = handle_other;
    irq_table[IRQC_M_EXT  ] = handle_other;
    //setup_exception();
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

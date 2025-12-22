#include <os/irq.h>
#include <os/time.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/kernel.h>
#include <printk.h>
#include <assert.h>
#include <screen.h>
#include <os/mm.h>
#include <os/debug.h>
#include <asm/unistd.h>
#include <csr.h>
#include <plic.h>
#include <os/net.h>

#define SCAUSE_IRQ_MASK 0x8000000000000000
handler_t irq_table[IRQC_COUNT];
handler_t exc_table[EXCC_COUNT];

void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task3] & [p2-task4] interrupt handler.
    // call corresponding handler by the value of `scause`
    uint64_t exc_code = scause & (~SCAUSE_IRQ_FLAG);
    if (scause & SCAUSE_IRQ_MASK) {
        // It's an interrupt
        klog("IRQ received, code: %d\n", exc_code); // Log the IRQ code
        irq_table[scause & ~SCAUSE_IRQ_MASK](regs, stval, scause);        
    } else {
        // It's an exception
        klog("Exception received, code: %d\n", exc_code); // Log the Exception code
        exc_table[scause](regs, stval, scause);
    }
}

void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    bios_set_timer(get_ticks()+TIMER_INTERVAL); //下一次查询中断的时间
    klog("PID %d do time irq schedluer\n",current_running->pid);
    if (regs->sstatus & (1L << 8) != 0){
        ;
    } else { 
        do_scheduler();
    }
}

// static PTE check_pte_status(uintptr_t va, uintptr_t pgdir) {
//     va &= VA_MASK;
//     uint64_t vpn2 = va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
//     uint64_t vpn1 = (vpn2 << PPN_BITS) ^ (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
//     uint64_t vpn0 = (vpn2 << (PPN_BITS + PPN_BITS)) ^ (vpn1 << PPN_BITS) ^ (va >> NORMAL_PAGE_SHIFT);
    
//     PTE *pgd = (PTE*)pgdir;
//     if (!(pgd[vpn2] & _PAGE_PRESENT)) return 0;
    
//     PTE *pmd = (PTE *)pa2kva(get_pa(pgd[vpn2]));
//     if (!(pmd[vpn1] & _PAGE_PRESENT)) return 0;
    
//     PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
//     return pte[vpn0];
// }


// [Task 2 新增] 缺页异常处理的包装函数
void handle_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // 调用 mm.c 中的实际处理函数 task2
    do_page_fault(regs, stval, scause);

    // // task 3 换页处理
    // uintptr_t va = stval;
    // uintptr_t pgdir = current_running->pgdir;
    
    // // 检查页表状态
    // PTE pte_val = check_pte_status(va, pgdir);

    // if (check_swap(pte_val)) {
    //     // Case 1: 页面在 Swap 中 (Valid=0, Value!=0)
    //     // printk("Swap In: VA=0x%lx\n", va);
    //     swap_in(va, pgdir);
    // } else {
    //     // Case 2: 页面从未分配 (Valid=0, Value=0) -> Lazy Allocation
    //     alloc_page_helper(va, pgdir);
    // }
}

void handle_irq_ext(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p5-task3] external interrupt handler.
    // Note: plic_claim and plic_complete will be helpful ...
    // Note: plic_claim and plic_complete will be helpful ...
    
    // 1. 获取当前发生中断的外设 ID
    int id = plic_claim();
    
    // 2. 判断是否为 E1000 网卡中断
    // 讲义指出：QEMU 上 ID 为 33，开发板上 ID 为 3
    // 这两个宏定义通常在 plic.h 中
    if(id == PLIC_E1000_QEMU_IRQ || id == PLIC_E1000_PYNQ_IRQ)  
    {
        // 调试信息，确认进入了网卡中断（调试完成后可注释掉）
        klog("e1000 interrupt\n\r");
        
        // 调用 net.c 中的网卡中断处理逻辑
        net_handle_irq();
    }
    else
    {
        // 如果是其他未知设备的中断，当作异常处理或者忽略
        handle_other(regs, stval, scause);
    }
    
    // 3. 通知 PLIC 中断处理完成，允许该外设再次发送中断
    plic_complete(id);
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
    // irq_table[IRQC_S_EXT  ] = handle_other;
    // irq_table[IRQC_M_EXT  ] = handle_other;

    // [Task 3] 注册 S 态外部中断处理函数
    irq_table[IRQC_S_EXT  ] = handle_irq_ext;   
    irq_table[IRQC_M_EXT  ] = handle_irq_ext; // 如果 M 态也可能触发，可以一并注册
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

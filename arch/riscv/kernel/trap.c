#include <os/debug.h> // 包含 printk 定义
#include <csr.h>    // 包含 CSR 定义

void debug_trap_handler(unsigned long scause, unsigned long stval, unsigned long sepc) {
    // 过滤掉时钟中断 (S态时钟中断通常是 0x8000...005)
    // 也可以根据你的需要过滤掉其他中断
    if ((scause & SCAUSE_IRQ_FLAG) && ((scause & 0xff) == IRQ_S_TIMER)) {
        return; 
    }

    klog("[Trap Debug] scause: 0x%lx, stval: 0x%lx, sepc: 0x%lx\n", 
           scause, stval, sepc);
    
    // 如果是缺页异常 (12, 13, 15)，可以打印更醒目一点
    if (scause == 12 || scause == 13 || scause == 15) {
        klog("    -> Page Fault at 0x%lx\n", stval);
    }
    // 如果是系统调用 (8)，打印一下
    if (scause == 8) {
        klog("    -> Syscall at 0x%lx\n", sepc);
    }
}
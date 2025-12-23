#include <atomic.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <printk.h>
#include <os/debug.h>
#include <os/irq.h>

// 大内核锁
spin_lock_t kernel_lock;

void smp_init()
{
    /* TODO: P3-TASK3 multicore*/
    // 初始化大内核锁
    spin_lock_init(&kernel_lock);
}

void wakeup_other_hart()
{
    /* TODO: P3-TASK3 multicore*/
    // 发送核间中断 (IPI) 给其他核心 NULL 表示发送给所有其他核心
    send_ipi(NULL);
}

void lock_kernel() // 上锁
{
    /* TODO: P3-TASK3 multicore*/
    disable_interrupt();
    klog("Process Attempting to acquire BKL\n");
    spin_lock_acquire(&kernel_lock);
    klog("Process Acquired the BKL\n");
}

void unlock_kernel() // 解锁
{
    /* TODO: P3-TASK3 multicore*/
    klog("Process Unlocking the BKL\n");
    spin_lock_release(&kernel_lock);
}



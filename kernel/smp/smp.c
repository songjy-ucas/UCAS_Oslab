#include <atomic.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <printk.h>
#include <os/debug.h>
#include <os/irq.h>

// // 大内核锁
// spin_lock_t kernel_lock;

// void smp_init()
// {
//     /* TODO: P3-TASK3 multicore*/
//     // 初始化大内核锁
//     spin_lock_init(&kernel_lock);
// }

void wakeup_other_hart()
{
    /* TODO: P3-TASK3 multicore*/
    // 发送核间中断 (IPI) 给其他核心 NULL 表示发送给所有其他核心
    send_ipi(NULL);
}

// void lock_kernel() // 上锁
// {
//     /* TODO: P3-TASK3 multicore*/
//     klog("Process Attempting to acquire BKL\n");
//     disable_interrupt(); 
//     spin_lock_acquire(&kernel_lock);
//     klog("Process Acquired the BKL\n");
// }

// void unlock_kernel() // 解锁
// {
//     /* TODO: P3-TASK3 multicore*/
//     spin_lock_release(&kernel_lock); 
//     klog("Process Unlocking the BKL\n");
// }

typedef struct {
    spin_lock_t lock;      // 真正的物理锁
    int owner_cpu;         // 记录谁拿着锁
    int rec_count;         // 记录拿了几层
} recursive_lock_t;

recursive_lock_t kernel_lock; // 全局大内核锁

void smp_init() {
    spin_lock_init(&kernel_lock.lock);
    kernel_lock.owner_cpu = -1;
    kernel_lock.rec_count = 0;
}

void lock_kernel() {
    int hart_id = get_current_cpu_id();
    klog("Process Attempting to acquire BKL\n");    
    // 1. 关中断 (这是必须的，防止时钟中断进来)
    disable_interrupt();

    // 2. 检查是不是自己已经拿着锁了 (重入检测)
    if (kernel_lock.owner_cpu == hart_id) {
        kernel_lock.rec_count++; // 只是增加计数
        return; 
    }

    // 3. 真的去抢锁
    // klog("CORE %d: Attempting BKL\n", hart_id);
    spin_lock_acquire(&kernel_lock.lock);
    
    klog("Process Acquired the BKL\n");    
    // 4. 登记
    kernel_lock.owner_cpu = hart_id;
    kernel_lock.rec_count = 1;
    // klog("CORE %d: Acquired BKL\n", hart_id);
}

void unlock_kernel() {
    int hart_id = get_current_cpu_id();

    // 安全检查
    if (kernel_lock.owner_cpu != hart_id) {
         // printk("Fatal: Unlock by wrong CPU\n");
         while(1);
    }

    // 1. 减少计数
    kernel_lock.rec_count--;

    // 2. 计数归零才真的释放
    if (kernel_lock.rec_count == 0) {
        kernel_lock.owner_cpu = -1;
        spin_lock_release(&kernel_lock.lock);
        // klog("CORE %d: Unlocked BKL\n", hart_id);
    }

    klog("Process Unlocking the BKL\n");    
    // 注意：unlock_kernel 不要主动 enable_interrupt
    // 因为如果是 Page Fault 处理完，我们要返回到发生 Fault 的地方继续执行（还在内核），
    // 那里依然需要关中断状态。
    // 如果是返回用户态，sret 指令会自动根据 SPIE 恢复中断。
}
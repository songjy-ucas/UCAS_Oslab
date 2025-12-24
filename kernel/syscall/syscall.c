#include <os/irq.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/lock.h>
#include <screen.h>
#include <sys/syscall.h>
#include <printk.h>

long (*syscall[NUM_SYSCALLS])();

void handle_syscall(regs_context_t *regs, uint64_t interrupt, uint64_t cause)
{
    /* TODO: [p2-task3] handle syscall exception */
    /**
     * HINT: call syscall function like syscall[fn](arg0, arg1, arg2),
     * and pay attention to the return value and sepc
     */
    // 1. 从寄存器中获取系统调用号 (a7) 和参数 (a0, a1, a2, ...)
    long syscall_num = regs->regs[17]; // a7 contains the syscall number
    long arg0 = regs->regs[10];      // a0
    long arg1 = regs->regs[11];      // a1
    long arg2 = regs->regs[12];      // a2
    
    long return_value;

    // 2. 检查系统调用号是否有效，并在表中查找对应的处理函数
    if (syscall_num >= 0 && syscall_num < NUM_SYSCALLS && syscall[syscall_num] != NULL)
    {
        // 3. 调用查找到的函数。我们传递最多3个参数，
        //    如果函数需要的参数更少，多余的参数会被忽略。
        return_value = syscall[syscall_num](arg0, arg1, arg2);
    }
    else
    {
        // 如果系统调用号无效，打印错误信息并返回-1
        printk("> [SYSCALL] ERROR: Unknown syscall number %d\n", syscall_num);
        return_value = -1;
    }

    // 4. 将返回值存入 a0 寄存器
    regs->regs[10] = return_value;

    // 5. 将 sepc 指向 ecall 指令的下一条指令，以确保正确返回
    regs->sepc += 4;
}


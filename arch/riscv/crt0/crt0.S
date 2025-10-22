# 该汇编程序是用户程序的"入口"，在 main 函数执行前，它为程序建立一个基本的C语言运行环境：设置栈指针sp和清零.bss段

#include <asm.h>

# Makefile 为每个用户程序分配的内存空间大小 (0x10000 = 64KB)
.equ USER_MEM_SIZE, 0x10000
.equ KERNEL_STACK_SAVE, 0x5f000000  # 内核栈的保存位置
.section ".entry_function","ax"
ENTRY(_start)

    /* TODO: [p1-task3] setup C runtime environment for the user program */
    # 先保存kernel栈指针位置，再设置用户程序栈指针 (sp)
    la t1,KERNEL_STACK_SAVE
    sd sp,0(t1)               # 保存内核栈指针
    la sp, _start
    li t0, USER_MEM_SIZE      # t0 = 0x10000
    add sp, sp, t0            # sp = sp + t0

    # 保存内核的返回地址，在ra里面，放入栈中
    addi sp, sp, -8   # 在栈上分配8字节空间
    sd ra, 0(sp)      # 将 ra 存入栈顶

    # 清零 .bss 段
    la a0, __bss_start
    la a1, __BSS_END__
    bgeu a0, a1, bss_clear_done

bss_clear_loop:
    sd zero, (a0)
    addi a0, a0, 8
    bltu a0, a1, bss_clear_loop

bss_clear_done:

    /* TODO: [p1-task3] enter main function */
    # 进入 C 代码的 main 函数
    call main

    /* TODO: [p1-task3] finish task and return to the kernel, replace this in p3-task2! */
    # 从栈上恢复内核返回地址
    ld ra, 0(sp)
    addi sp, sp, 8
    # 恢复内核栈指针
    la t1, KERNEL_STACK_SAVE
    ld sp, 0(t1)
    # 返回内核
    ret

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)

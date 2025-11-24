/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

#define KERNEL_STACK		0x50500000

.globl _edata
.globl _end

.section ".entry_function","ax"
ENTRY(_start)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  /* TODO: [p1-task2] clear BSS for flat non-ELF images */
  la s0, _edata     // 加载BSS段起始地址到 s0
  la s1, _end       // 加载BSS段结束地址到 s1
clear_bss_loop:
  bgeu s0, s1, bss_done // 如果 s0 >= s1，说明BSS已清空，跳转到 bss_done
  sw zero, 0(s0)        // 将0写入 s0 指向的内存地址 (4字节)
  addi s0, s0, 4        // s0 指针加4
  j clear_bss_loop      // 继续循环
bss_done:
  // -- END OF ADDED CODE FOR TASK 2 --


  /* TODO: [p1-task2] setup C environment */
  // -- BEGIN OF ADDED CODE FOR TASK 2 --
  // 设置栈指针 (sp)
  li sp, KERNEL_STACK

  // 跳转到C语言的main函数
  j main

  /* TODO: [p1-task2] setup C environment */


loop:
  wfi
  j loop

END(_start)

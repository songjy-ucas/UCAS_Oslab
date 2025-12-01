/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

#define KERNEL_STACK		0x50500000
#define PAGE_SIZE 4096 // 4K
#define INIT_KERNEL_STACK 0x50500000
.globl _edata
.globl _end

.section ".entry_function","ax"
ENTRY(_start)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero
  mv s2, a0
  mv s3, a1
  csrr a0, mhartid
  bnez a0, stack_init 

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
  # li sp, KERNEL_STACK
  # j main

  /* TODO: [p1-task2] setup C environment */

stack_init:

  li sp, INIT_KERNEL_STACK   // 0x50500000
  li t0, PAGE_SIZE           // 4096

  
  csrr a0, mhartid           // 读取 hartid
  addi a0, a0, 1             // a0 = hartid + 1
  mul t0, t0, a0             // t0 = (hartid + 1) * 4096
  add sp, sp, t0  
  mv a0, s2
  mv a1, s3
  // 跳转到 C 语言入口 main
  j main
loop:
  wfi
  j loop

END(_start)

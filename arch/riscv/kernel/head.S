/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

#define KERNEL_STACK		0x50500000

.section ".entry_function","ax"
ENTRY(_start)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  /* TODO: [p1-task2] clear BSS for flat non-ELF images */
  // clear .bss, using symbols from the provided riscv.lds
  la t0, __bss_start
  la t1, __BSS_END__
bss_clear_loop:
  // Use sd (store doubleword) if the address is 8-byte aligned,
  // which is good practice. Loop until a0 is no longer less than a1.
  sd zero, (t0)
  addi t0, t0, 8
  blt t0, t1, bss_clear_loop


  /* TODO: [p1-task2] setup C environment */
  // setup stack
  li sp, KERNEL_STACK
  call main   // 标准的C语言调用约定规定，函数的前两个整数参数就是通过 a0 和 a1 传递的,即TASK_NUM_ADDR和TASK_INFO_ADDR的值


loop:
  wfi
  j loop

END(_start)

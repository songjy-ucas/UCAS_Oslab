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
  la a0, __bss_start
  la a1, __BSS_END__
bss_clear_loop:
  // Use sd (store doubleword) if the address is 8-byte aligned,
  // which is good practice. Loop until a0 is no longer less than a1.
  sd zero, (a0)
  addi a0, a0, 8
  blt a0, a1, bss_clear_loop


  /* TODO: [p1-task2] setup C environment */
  // setup stack
  li sp, KERNEL_STACK
  call main


loop:
  wfi
  j loop

END(_start)

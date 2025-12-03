/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

/* maybe you need another stack for booting? */
.equ boot_stack_top_base, 0x52002000
.equ per_cpu_stack_size, 0x1000

.section ".bootkernel_entry","ax"
ENTRY(_boot)
  /*
   * this is the entrypoint of the kernel. 
   * Don't clear BSS here
   */

  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  /* 新逻辑: sp = 0x52002000 - (hartid * 0x800) */
  li t0, boot_stack_top_base   // t0 = 0x52002000
  li t1, per_cpu_stack_size    // t1 = 0x1000

  csrr a0, CSR_MHARTID 
  mul t1, t1, a0               // t1 = 偏移量 (0 或 0x800)
  sub sp, t0, t1               // sp = Base - 偏移量

  // this will never return
  call boot_kernel


loop:
  wfi
  j loop

END(_boot)

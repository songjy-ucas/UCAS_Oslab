/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef CSR_H
#define CSR_H

/* Status register flags */ // 定义 sstatus 寄存器中一些关键控制位的位置。
#define SR_SIE    0x00000002 /* Supervisor Interrupt Enable */ // 第1位。这是中断总开关
#define SR_SPIE   0x00000020 /* Previous Supervisor IE */ // 第5位。硬件用它来保存陷入前的 SIE 状态。
#define SR_SPP    0x00000100 /* Previously Supervisor */ // 第8位。SPP=0 表示陷入前是用户态，SPP=1 表示陷入前是内核态。sret 指令根据它决定返回到哪个特权级。
#define SR_SUM    0x00040000 /* Supervisor User Memory Access */ //  第18位。如果置1，允许内核态代码直接访问用户态的内存页面。

#define SR_FS           0x00006000 /* Floating-point Status */
#define SR_FS_OFF       0x00000000
#define SR_FS_INITIAL   0x00002000
#define SR_FS_CLEAN     0x00004000
#define SR_FS_DIRTY     0x00006000

#define SR_XS           0x00018000 /* Extension Status */
#define SR_XS_OFF       0x00000000
#define SR_XS_INITIAL   0x00008000
#define SR_XS_CLEAN     0x00010000
#define SR_XS_DIRTY     0x00018000

#define SR_SD           0x8000000000000000 /* FS/XS dirty */

/* SATP flags */ // satp寄存器是虚拟内存管理的核心，用于开启分页机制。
#define SATP_PPN        0x00000FFFFFFFFFFF // 页表基地址。指向最高级页表的物理地址。
#define SATP_MODE_39    0x8000000000000000
#define SATP_MODE       SATP_MODE_39 //  虚拟地址模式,等于0x8...时表示使用 Sv39 分页模式，如果为0，则不开启分页。

/* SCAUSE */
#define SCAUSE_IRQ_FLAG   (1UL << 63) // 定义了 scause 的最高位，用于判断是中断还是异常。

// 中断类型代码 
#define IRQ_U_SOFT		0
#define IRQ_S_SOFT		1
#define IRQ_M_SOFT		3
#define IRQ_U_TIMER		4
#define IRQ_S_TIMER		5
#define IRQ_M_TIMER		7
#define IRQ_U_EXT		8
#define IRQ_S_EXT		9
#define IRQ_M_EXT		11

// 异常类型代码
#define EXC_INST_MISALIGNED	0
#define EXC_INST_ACCESS		1
#define EXC_BREAKPOINT		3
#define EXC_LOAD_ACCESS		5
#define EXC_STORE_ACCESS	7
#define EXC_SYSCALL		8
#define EXC_INST_PAGE_FAULT	12
#define EXC_LOAD_PAGE_FAULT	13
#define EXC_STORE_PAGE_FAULT	15

/* SIE (Interrupt Enable) and SIP (Interrupt Pending) flags */
// sie: 中断使能寄存器（分开关面板）。sie |= SIE_STIE 表示打开定时器中断。
// sip: 中断挂起寄存器。硬件或软件可以通过写这个寄存器来触发一个中断。
#define SIE_SSIE    (0x1 << IRQ_S_SOFT)
#define SIE_STIE    (0x1 << IRQ_S_TIMER)
#define SIE_SEIE    (0x1 << IRQ_S_EXT)

// 为每个CSR寄存器定义了其在CSR地址空间中的唯一12位地址。
// 汇编器和编译器使用这些宏。当写 csrr a0, sstatus 时，汇编器会查找 sstatus 对应的地址 0x100，并生成包含这个地址的机器指令。
#define CSR_CYCLE   0xc00
#define CSR_TIME    0xc01
#define CSR_INSTRET   0xc02
#define CSR_SSTATUS   0x100
#define CSR_SIE     0x104
#define CSR_STVEC   0x105
#define CSR_SCOUNTEREN    0x106
#define CSR_SSCRATCH    0x140
#define CSR_SEPC    0x141
#define CSR_SCAUSE    0x142
#define CSR_STVAL   0x143
#define CSR_SIP     0x144
#define CSR_SATP    0x180
#define CSR_CYCLEH    0xc80
#define CSR_TIMEH   0xc81
#define CSR_INSTRETH    0xc82

#endif /* CSR_H */

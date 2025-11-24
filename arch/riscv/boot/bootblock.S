#include <asm/biosdef.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc, 0x502001fc
// [p1-task4] 元数据在bootblock中的位置
.equ task_num_loc, 0x502001fa       
.equ task_info_loc, 0x502001f8     

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50201000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0x50150000

.data
msg_boot: .string "It's zhangyang's bootloader...\n\r"

.text
.global main

main:
	// fence on all memory and I/O
	fence
    // a0 is mhartid
	bnez a0, secondary

	// [p1-task1] call BIOS to print string
	la   a0, msg_boot
	li   a7, BIOS_PUTSTR
    // 【关键修正】先将地址加载到寄存器，再通过寄存器跳转
	li   t0, bios_func_entry
    jalr t0

	// [p1-task2] call BIOS to read kernel & apps in SD card
	li   a0, kernel           // a0: 加载目标地址
	
	li   t0, os_size_loc
	lh   a1, 0(t0)            // a1: 从元数据区读取OS大小 (扇区数)
	li   a2, 1                // a2: 从第1个扇区开始读 (0是bootblock)
	li   a7, BIOS_SDREAD
    // 【关键修正】先将地址加载到寄存器，再通过寄存器跳转
	li   t0, bios_func_entry
	jalr t0

	// [p1-task4] 加载任务相关参数并传递给内核
    // 将元信息加载到参数寄存器 a0 和 a1
    li   t0, task_num_loc
    lh   a0, 0(t0)            // a0 = task_num

    li   t0, task_info_loc
    lh   a1, 0(t0)            // a1 = task_info_start_sector

	// [p1-task2] jump to kernel to start UCAS-OS
	li t0, kernel             // 加载内核地址到 t0
	jr t0                     // 跳转到内核

	/************************************************************/
secondary:
	/* TODO [P3-task3]: 
	 * 1. Mask all interrupts
	 * 2. let stvec pointer to kernel_main
	 * 3. enable software interrupt for ipi
	 */

wait_for_wakeup:
	wfi
	j wait_for_wakeup

	/************************************************************/
stop:
	j stop
	nop
		
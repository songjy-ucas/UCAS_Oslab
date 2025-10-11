#include <asm/biosdef.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc, 0x502001fc

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50201000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0x50150000

.data
msg_boot: .string "It's songjunyi23's bootloader..."

.text
.global main

main:
	// fence on all memory and I/O
	fence

	// TODO: [p1-task1] call BIOS to print string "It's bootblock!"
	la a0,msg_boot               // load address of msg into a0
	li a7,BIOS_PUTSTR         // load BIO_PRINT function code into a7
    jal bios_func_entry      // jump to BIOS --- call BIOS function

	// TODO: [p1-task2] call BIOS to read kernel in SD card
	li   a0, kernel           // load kernel's address
	
	li   t0, os_size_loc
	lh   a1, 0(t0)            // a1: os size (in sector)
	li   a2, 1                // a2: start sector number ---- 0 is bootblock, 1 is kernel
	li   a7, BIOS_SDREAD      // a7: bios function number
	jal  bios_func_entry      // call bios

	// TODO: [p1-task4] load task-related arguments and pass them to kernel


	// TODO: [p1-task2] jump to kernel to start UCAS-OS
	li t0, kernel             // load kernel address to t0
	jr t0                     // jump to kernel

	/************************************************************/
	/* Do not touch this comment. Reserved for future projects. */
	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's a bootloader...\n\r"


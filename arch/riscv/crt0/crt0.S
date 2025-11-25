#include <asm.h>
#include "syscall.h"
.section ".entry_function","ax"
ENTRY(_start)

    addi sp, sp, -16
    sd   ra, 8(sp)
    sd   fp, 0(sp)
    addi fp, sp, 16
    /* TODO: [p1-task3] setup C runtime environment for the user program */
    la   t0, _edata
    la   t1, _end
1:
    bgeu t0, t1, 2f      # If t0 >= t1, BSS is cleared, jump to label 2
    sd   zero, 0(t0)     # Store zero (double word) at address in t0
    addi t0, t0, 8       # Move to the next 8 bytes
    j    1b              # Jump back to loop
2:

    call main
    li   a7, SYSCALL_EXIT
    ecall
    # ld   fp, 0(sp)
    # addi sp, sp, 16

    # ret
    /* TODO: [p1-task3] enter main function */

    
    /* TODO: [p1-task3] finish task and return to the kernel, replace this in p3-task2! */

    /************************************************************/
    /* TODO: [P3] call sys_exit */
    /************************************************************/
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)

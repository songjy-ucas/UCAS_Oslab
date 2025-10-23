#include <asm.h>
#include <csr.h>
#include <asm/regs.h>

#define SWITCH_TO_RA_OFFSET   0
#define SWITCH_TO_SP_OFFSET   8
#define SWITCH_TO_S0_OFFSET   16
#define SWITCH_TO_S1_OFFSET   24
#define SWITCH_TO_S2_OFFSET   32
#define SWITCH_TO_S3_OFFSET   40
#define SWITCH_TO_S4_OFFSET   48
#define SWITCH_TO_S5_OFFSET   56
#define SWITCH_TO_S6_OFFSET   64
#define SWITCH_TO_S7_OFFSET   72
#define SWITCH_TO_S8_OFFSET   80
#define SWITCH_TO_S9_OFFSET   88
#define SWITCH_TO_S10_OFFSET  96
#define SWITCH_TO_S11_OFFSET  104
#define SWITCH_TO_SIZE        112 // 14 regs * 8 bytes = 112 bytes

.macro SAVE_CONTEXT
  /* TODO: [p2-task3] save all general purpose registers here! */
  /* HINT: Pay attention to the function of tp and sp, and save them carefully! */

  /*
   * Disable user-mode memory access as it should only be set in the
   * actual user copy routines.
   *
   * Disable the FPU to detect illegal usage of floating point in kernel
   * space.
   */
  li t0, SR_SUM | SR_FS

  /* TODO: [p2-task3] save sstatus, sepc, stval and scause on kernel stack */

.endm

.macro RESTORE_CONTEXT
  /* TODO: Restore all general purpose registers and sepc, sstatus */
  /* HINT: Pay attention to sp again! */
.endm

ENTRY(enable_preempt)
  not t0, x0
  csrs CSR_SIE, t0
  jr ra
ENDPROC(enable_preempt)

ENTRY(disable_preempt)
  csrw CSR_SIE, zero
  jr ra
ENDPROC(disable_preempt)

ENTRY(enable_interrupt)
  li t0, SR_SIE
  csrs CSR_SSTATUS, t0
  jr ra
ENDPROC(enable_interrupt)

ENTRY(disable_interrupt)
  li t0, SR_SIE
  csrc CSR_SSTATUS, t0
  jr ra
ENDPROC(disable_interrupt)

// NOTE: the address of previous pcb in a0
// NOTE: the address of next pcb in a1
ENTRY(switch_to)
  addi sp, sp, -(SWITCH_TO_SIZE)

  /* TODO: [p2-task1] save all callee save registers on kernel stack,
   * see the definition of `struct switchto_context` in sched.h*/
  sd ra, SWITCH_TO_RA_OFFSET(sp)
  sd s0, SWITCH_TO_S0_OFFSET(sp)
  sd s1, SWITCH_TO_S1_OFFSET(sp)
  sd s2, SWITCH_TO_S2_OFFSET(sp)
  sd s3, SWITCH_TO_S3_OFFSET(sp)
  sd s4, SWITCH_TO_S4_OFFSET(sp)
  sd s5, SWITCH_TO_S5_OFFSET(sp)
  sd s6, SWITCH_TO_S6_OFFSET(sp)
  sd s7, SWITCH_TO_S7_OFFSET(sp)
  sd s8, SWITCH_TO_S8_OFFSET(sp)
  sd s9, SWITCH_TO_S9_OFFSET(sp)
  sd s10, SWITCH_TO_S10_OFFSET(sp)
  sd s11, SWITCH_TO_S11_OFFSET(sp)
  /* TODO: [p2-task1] restore all callee save registers from kernel stack,
   * see the definition of `struct switchto_context` in sched.h*/
  sd sp, 0(a0) //将当前的栈顶(sp)保存到 prev_pcb->kernel_sp
  ld sp, 0(a1) // 从 next_pcb->kernel_sp 恢复新进程的栈顶到 sp
  mv tp, a1  // 更新 current_running (tp) 为 next_pcb

  ld ra, SWITCH_TO_RA_OFFSET(sp)
  ld s0, SWITCH_TO_S0_OFFSET(sp)
  ld s1, SWITCH_TO_S1_OFFSET(sp)
  ld s2, SWITCH_TO_S2_OFFSET(sp)
  ld s3, SWITCH_TO_S3_OFFSET(sp)
  ld s4, SWITCH_TO_S4_OFFSET(sp)
  ld s5, SWITCH_TO_S5_OFFSET(sp)
  ld s6, SWITCH_TO_S6_OFFSET(sp)
  ld s7, SWITCH_TO_S7_OFFSET(sp)
  ld s8, SWITCH_TO_S8_OFFSET(sp)
  ld s9, SWITCH_TO_S9_OFFSET(sp)
  ld s10, SWITCH_TO_S10_OFFSET(sp)
  ld s11, SWITCH_TO_S11_OFFSET(sp)
  addi sp, sp, SWITCH_TO_SIZE
  jr ra
ENDPROC(switch_to)

ENTRY(ret_from_exception)
  /* TODO: [p2-task3] restore context via provided macro and return to sepc */
  /* HINT: remember to check your sp, does it point to the right address? */
ENDPROC(ret_from_exception)

ENTRY(exception_handler_entry)

  /* TODO: [p2-task3] save context via the provided macro */


  /* TODO: [p2-task3] load ret_from_exception into $ra so that we can return to
   * ret_from_exception when interrupt_help complete.
   */


  /* TODO: [p2-task3] call interrupt_helper
   * NOTE: don't forget to pass parameters for it.
   */


ENDPROC(exception_handler_entry)

#include <asm.h>
#include <csr.h>
#include <asm/regs.h>


.macro SAVE_CONTEXT
  /* TODO: [p2-task3] save all general purpose registers here! */
  /* HINT: Pay attention to the function of tp and sp, and save them carefully! */
  
  sd sp, PCB_USER_SP(tp)    // store user stack in pcb
  ld sp, PCB_KERNEL_SP(tp)  // recover kernel stack from pcb
  
  addi sp, sp, -OFFSET_SIZE

  sd x0, OFFSET_REG_ZERO(sp)
  sd ra, OFFSET_REG_RA(sp)
  sd tp, OFFSET_REG_TP(sp)
  sd gp, OFFSET_REG_GP(sp)
  sd t0, OFFSET_REG_T0(sp)
  sd t1, OFFSET_REG_T1(sp)
  sd t2, OFFSET_REG_T2(sp)

  ld t0, PCB_USER_SP(tp)
  sd t0, OFFSET_REG_SP(sp)  // store user stack in stack

  sd s0, OFFSET_REG_S0(sp)
  sd s1, OFFSET_REG_S1(sp)
  sd a0, OFFSET_REG_A0(sp)
  sd a1, OFFSET_REG_A1(sp)
  sd a2, OFFSET_REG_A2(sp)
  sd a3, OFFSET_REG_A3(sp)
  sd a4, OFFSET_REG_A4(sp)
  sd a5, OFFSET_REG_A5(sp)
  sd a6, OFFSET_REG_A6(sp)
  sd a7, OFFSET_REG_A7(sp)
  sd s2, OFFSET_REG_S2(sp)
  sd s3, OFFSET_REG_S3(sp)
  sd s4, OFFSET_REG_S4(sp)
  sd s5, OFFSET_REG_S5(sp)
  sd s6, OFFSET_REG_S6(sp)
  sd s7, OFFSET_REG_S7(sp)
  sd s8, OFFSET_REG_S8(sp)
  sd s9, OFFSET_REG_S9(sp)
  sd s10, OFFSET_REG_S10(sp)
  sd s11, OFFSET_REG_S11(sp)
  sd t3, OFFSET_REG_T3(sp)
  sd t4, OFFSET_REG_T4(sp)
  sd t5, OFFSET_REG_T5(sp)
  sd t6, OFFSET_REG_T6(sp)


  /*
   * Disable user-mode memory access as it should only be set in the
   * actual user copy routines.
   *
   * Disable the FPU to detect illegal usage of floating point in kernel
   * space.
   */

  //li t0, SR_SUM | SR_FS

  /* TODO: [p2-task3] save sstatus, sepc, stval and scause on kernel stack */
  
  csrr t0, sstatus
  sd t0, OFFSET_REG_SSTATUS(sp)
  csrr t1, sepc
  sd t1, OFFSET_REG_SEPC(sp)
  csrr t2, stval
  sd t2, OFFSET_REG_SBADADDR(sp)
  csrr t3, scause
  sd t3, OFFSET_REG_SCAUSE(sp)

.endm

.macro RESTORE_CONTEXT
  /* TODO: Restore all general purpose registers and sepc, sstatus */
  /* HINT: Pay attention to sp again! */
  // current sp in PCB is out of date
 
  ld x0, OFFSET_REG_ZERO(sp)
  ld ra, OFFSET_REG_RA(sp)
  ld tp, OFFSET_REG_TP(sp)
  ld t0, OFFSET_REG_T0(sp)
  ld t1, OFFSET_REG_T1(sp)
  ld t2, OFFSET_REG_T2(sp)
  ld s0, OFFSET_REG_S0(sp)
  ld s1, OFFSET_REG_S1(sp)
  ld a0, OFFSET_REG_A0(sp)
  ld a1, OFFSET_REG_A1(sp)
  ld a2, OFFSET_REG_A2(sp)
  ld a3, OFFSET_REG_A3(sp)
  ld a4, OFFSET_REG_A4(sp)
  ld a5, OFFSET_REG_A5(sp)
  ld a6, OFFSET_REG_A6(sp)
  ld a7, OFFSET_REG_A7(sp)
  ld s2, OFFSET_REG_S2(sp)
  ld s3, OFFSET_REG_S3(sp)
  ld s4, OFFSET_REG_S4(sp)
  ld s5, OFFSET_REG_S5(sp)
  ld s6, OFFSET_REG_S6(sp)
  ld s7, OFFSET_REG_S7(sp)
  ld s8, OFFSET_REG_S8(sp)
  ld s9, OFFSET_REG_S9(sp)
  ld s10, OFFSET_REG_S10(sp)
  ld s11, OFFSET_REG_S11(sp)
  ld t3, OFFSET_REG_SSTATUS(sp)
  ld t4, OFFSET_REG_SEPC(sp)
  ld t5, OFFSET_REG_SBADADDR(sp)
  ld t6, OFFSET_REG_SCAUSE(sp)

  csrw sstatus, t3
  ld t3, OFFSET_REG_T3(sp)
  csrw sepc, t4
  ld t4, OFFSET_REG_T4(sp)
  csrw stval, t5
  ld t5, OFFSET_REG_T5(sp)
  csrw scause, t6
  ld t6, OFFSET_REG_T6(sp)

  addi sp, sp, OFFSET_SIZE
  sd   sp, PCB_KERNEL_SP(tp)    //store kernel stack in pcb
  ld   sp, PCB_USER_SP(tp)      //recover user stack form pcb
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
 # 1. 在 prev 任务的内核栈上分配空间
  addi sp, sp, -SWITCH_TO_SIZE

  # 2. 保存所有 callee-saved 寄存器到 prev 的栈上

  sd ra, SWITCH_TO_RA(sp)
  sd s0, SWITCH_TO_S0(sp)
  sd s1, SWITCH_TO_S1(sp)
  sd s2, SWITCH_TO_S2(sp)
  sd s3, SWITCH_TO_S3(sp)
  sd s4, SWITCH_TO_S4(sp)
  sd s5, SWITCH_TO_S5(sp)
  sd s6, SWITCH_TO_S6(sp)
  sd s7, SWITCH_TO_S7(sp)
  sd s8, SWITCH_TO_S8(sp)
  sd s9, SWITCH_TO_S9(sp)
  sd s10, SWITCH_TO_S10(sp)
  sd s11, SWITCH_TO_S11(sp)

  # 3. 将刚保存好的上下文的栈顶 (当前的 sp) 存入 prev 的 PCB
  sd sp, PCB_KERNEL_SP(a0)

  # 4. 从 next 的 PCB 中加载它的内核栈顶到 sp
  ld sp, PCB_KERNEL_SP(a1)

  # 5. 从 next 任务的内核栈中恢复 callee-saved 寄存器
  #    同样，我们不再恢复 sp！
  ld ra, SWITCH_TO_RA(sp)
  ld s0, SWITCH_TO_S0(sp)
  ld s1, SWITCH_TO_S1(sp)
  ld s2, SWITCH_TO_S2(sp)
  ld s3, SWITCH_TO_S3(sp)
  ld s4, SWITCH_TO_S4(sp)
  ld s5, SWITCH_TO_S5(sp)
  ld s6, SWITCH_TO_S6(sp)
  ld s7, SWITCH_TO_S7(sp)
  ld s8, SWITCH_TO_S8(sp)
  ld s9, SWITCH_TO_S9(sp)
  ld s10, SWITCH_TO_S10(sp)
  ld s11, SWITCH_TO_S11(sp)

  # 6. 释放 next 任务内核栈上的上下文空间 
  addi sp, sp, SWITCH_TO_SIZE

  # 7. 将 CPU 的 tp 寄存器更新为 next_pcb 的地址 (current_running = next)
  mv tp, a1
  
  # 8. 返回。此时 ra 已经被恢复为 next 任务的返回地址 (即任务入口点)
  ret
  /* TODO: [p2-task1] save all callee save registers on kernel stack,
   * see the definition of `struct switchto_context` in sched.h*/

  /* TODO: [p2-task1] restore all callee save registers from kernel stack,
   * see the definition of `struct switchto_context` in sched.h*/

ENDPROC(switch_to)

ENTRY(ret_from_exception)
  /* TODO: [p2-task3] restore context via provided macro and return to sepc */
  /* HINT: remember to check your sp, does it point to the right address? */
 
  // [P3-TASK3] 离开内核前释放大内核锁
  call unlock_kernel

  call enable_preempt

  RESTORE_CONTEXT

  sret

ENDPROC(ret_from_exception)

ENTRY(exception_handler_entry)

  /* TODO: [p2-task3] save context via the provided macro */
  
  SAVE_CONTEXT

  /* ================== 新增代码开始 ================== */
  /* 准备参数调用 C 打印函数: 
     void debug_trap_handler(unsigned long scause, unsigned long stval, unsigned long sepc) 
  */
  //csrr a0, scause  // 第1个参数
  //csrr a1, stval   // 第2个参数
  //csrr a2, sepc    // 第3个参数
  
  //call debug_trap_handler
  /* ================== 新增代码结束 ================== */

  call disable_preempt

  // [P3-TASK3] 进入内核处理前获取大内核锁
  call lock_kernel

  /* TODO: [p2-task3] load ret_from_exception into $ra so that we can return to
   * ret_from_exception when interrupt_help complete.
   */
  /* TODO: [p2-task3] call interrupt_helper
   * NOTE: don't forget to pass parameters for it.
   */

  mv a0, sp
  csrr a1, stval
  csrr a2, scause
  call interrupt_helper

  j ret_from_exception

ENDPROC(exception_handler_entry)

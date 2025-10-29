#include <asm.h>
#include <csr.h>
#include <asm/regs.h>

.macro SAVE_CONTEXT
  /* TODO: [p2-task3] save all general purpose registers here! */
  /* HINT: Pay attention to the function of tp and sp, and save them carefully! */
  sd sp, PCB_USER_SP(tp)    // store user stack
  ld sp, PCB_KERNEL_SP(tp)  // recover kernel stack 
  addi sp, sp, -OFFSET_SIZE
  sd x0, OFFSET_REG_ZERO(sp)

  sd ra, OFFSET_REG_RA(sp)

  sd gp, OFFSET_REG_GP(sp)
  sd tp, OFFSET_REG_TP(sp)

  sd t0, OFFSET_REG_T0(sp)
  sd t1, OFFSET_REG_T1(sp)
  sd t2, OFFSET_REG_T2(sp)

  ld t0, PCB_USER_SP(tp)
  sd t0, OFFSET_REG_SP(sp)  // store user stack

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
  li t0, SR_SUM | SR_FS # 生成一个掩码

  /* TODO: [p2-task3] save sstatus, sepc, stval and scause on kernel stack */
  csrr t0, sstatus # sstatus 记录了异常发生前的CPU状态  csrr即：把后面的寄存器的值赋给前面的寄存器
  csrr t1, sepc  # sepc 存的是导致异常的那条指令的地址
  csrr t2, stval # sbadaddr保存导致访存错误的地址
  csrr t3, scause # scause 存的是异常/中断的原因代码
  sd t0, OFFSET_REG_SSTATUS(sp)
  sd t1, OFFSET_REG_SEPC(sp)
  sd t2, OFFSET_REG_SBADADDR(sp)
  sd t3, OFFSET_REG_SCAUSE(sp)
.endm

.macro RESTORE_CONTEXT
  /* TODO: Restore all general purpose registers and sepc, sstatus */
  /* HINT: Pay attention to sp again! */
  ld x0, OFFSET_REG_ZERO(sp)

  ld ra, OFFSET_REG_RA(sp)

  ld gp, OFFSET_REG_GP(sp)
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
  csrw sepc, t4
  csrw stval, t5
  csrw scause, t6

  ld t3, OFFSET_REG_T3(sp)
  ld t4, OFFSET_REG_T4(sp)
  ld t5, OFFSET_REG_T5(sp)
  ld t6, OFFSET_REG_T6(sp)
  
  addi sp, sp, OFFSET_SIZE
  sd   sp, PCB_KERNEL_SP(tp)
  ld   sp, PCB_USER_SP(tp) 
.endm

# 这四个函数操作的是两个不同的CSR寄存器，sie 和 sstatus，它们共同控制着中断的开关。

# preempt在抢占式调度中，主要用来控制定时器中断的开关。
# 打开 sie 寄存器中所有的中断分开关
ENTRY(enable_preempt)
  not t0, x0
  csrs CSR_SIE, t0 # CSR_SIE = CSR_SIE & t0
  jr ra
ENDPROC(enable_preempt)

# 关闭 sie 寄存器中所有的中断分开关
ENTRY(disable_preempt)
  csrw CSR_SIE, zero
  jr ra
ENDPROC(disable_preempt)

# 打开中断总开关
# sstatus.SIE 是总开关。SIE = 1: 总电闸合上，允许S-mode的中断发生。SIE = 0: 总电闸拉下，屏蔽所有S-mode的中断，无论各个分开关是什么状态。
ENTRY(enable_interrupt)
  li t0, SR_SIE
  csrs CSR_SSTATUS, t0
  jr ra
ENDPROC(enable_interrupt)

# 关闭中断总开关
ENTRY(disable_interrupt)
  li t0, SR_SIE
  csrc CSR_SSTATUS, t0
  jr ra
ENDPROC(disable_interrupt)

// NOTE: the address of previous pcb in a0
// NOTE: the address of next pcb in a1
ENTRY(switch_to)

  addi sp, sp, -(SWITCH_TO_SIZE)
  sd sp, PCB_KERNEL_SP(a0)  # 将当前的栈顶(sp)保存到 prev_pcb->kernel_sp 

  /* TODO: [p2-task1] save all callee save registers on kernel stack,
   * see the definition of `struct switchto_context` in sched.h*/
  sd ra, SWITCH_TO_RA(sp)
  sd sp, SWITCH_TO_SP(sp) 
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
  /* TODO: [p2-task1] restore all callee save registers from kernel stack,
   * see the definition of `struct switchto_context` in sched.h*/
  ld sp, PCB_KERNEL_SP(a1) # 从 next_pcb->kernel_sp 恢复新进程的栈顶到 sp
  mv tp, a1                # 更新 current_running (tp) 为 next_pcb,约定tp保存指向当前pcb的指针

  ld ra, SWITCH_TO_RA(sp)
  ld sp, SWITCH_TO_SP(sp)  
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

  addi sp, sp, SWITCH_TO_SIZE
  sd sp, PCB_KERNEL_SP(a1)  # 当前栈顶sp存入next_pcb
  jr ra
ENDPROC(switch_to)


ENTRY(ret_from_exception) # 恢复现场，并再次进入用户态
  /* TODO: [p2-task3] restore context via provided macro and return to sepc */
  /* HINT: remember to check your sp, does it point to the right address? */
  RESTORE_CONTEXT # 调用前面写好的汇编程序去恢复现场
  sret # 与ret不同，ret用于函数调用返回，sret执行一系列复杂且原子的硬件操作，用于跨越特权级：
       # 读取 sstatus 寄存器中的 SPP  位，来决定要返回到哪个特权级（U-mode, S-mode等）。
       # 读取 sstatus 中的 SPIE  位，并用它的值来设置 sstatus 中的 SIE (Supervisor Interrupt Enable) 位，从而恢复中断状态。
       # 把 sepc 寄存器里的地址加载到 PC 寄存器中，跳转到那里执行。

ENDPROC(ret_from_exception)

ENTRY(exception_handler_entry) # 它是所有trap发生后，CPU硬件自动跳转到的第一个软件入口。它的地址被设置在 stvec 寄存器中。它用于从用户态进入内核态

  /* TODO: [p2-task3] save context via the provided macro */
  SAVE_CONTEXT # 调用前面写好的汇编程序去保存现场

  /* TODO: [p2-task3] load ret_from_exception into $ra so that we can return to
   * ret_from_exception when interrupt_help complete.
   */


  /* TODO: [p2-task3] call interrupt_helper
   * NOTE: don't forget to pass parameters for it.
   */
  addi a0, sp, 0
  csrr a1, stval
  csrr a2, scause # 准备好C函数 interrupt_helper 需要的参数 sp、scause(trap原因)、stval(trap的额外信息)---这两个寄存器是RISC-V硬件本身定义的，直接使用就可以
  call interrupt_helper

  la ra, ret_from_exception # 执行完后进入ret_from_exception去恢复现场
  ret

ENDPROC(exception_handler_entry)

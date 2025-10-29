#include <asm.h>
#include <csr.h>

ENTRY(setup_exception)

  /* TODO: [p2-task3] save exception_handler_entry into STVEC */
  # 告诉 CPU 所有陷阱（中断、异常、系统调用）都应该跳转到指定的处理入口 exception_handler_entry
  la t0, exception_handler_entry
  csrw CSR_STVEC, t0
  /* TODO: [p2-task4] enable interrupts globally */
 
  ret
ENDPROC(setup_exception)

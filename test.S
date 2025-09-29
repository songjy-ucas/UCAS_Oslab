# 判断 1 到 200 的质数, 封装在 main 函数中

.text
.global main
main:
    # --- 函数 Prologue ---
    # 因为 main 函数中使用了 callee-saved 寄存器 s0 和 s1,并且调用了其他函数 (jal), 所以必须保存 ra, s0, s1
    addi sp, sp, -12
    sw ra, 8(sp)
    sw s0, 4(sp)
    sw s1, 0(sp)
    
    li s0, 1
    li s1, 200

main_loop:
    mv a0, s0
    jal ra, is_prime

    addi s0, s0, 1
    ble s0, s1, main_loop

    # --- 函数 Epilogue --- 按照与保存时相反的顺序，从栈中恢复寄存器
    lw ra, 8(sp)
    lw s0, 4(sp)
    lw s1, 0(sp)
    addi sp, sp, 12

    # 使用 ret 指令返回
    ret

is_prime:
    addi sp, sp, -8
    sw ra, 4(sp)
    sw s0, 0(sp)

    li t0, 1
    ble a0, t0, not_prime

    li t0, 2
    beq a0, t0, is_prime_true

    li s0, 2

check_factor_loop:
    rem t1, a0, s0
    beq t1, zero, not_prime
    addi s0, s0, 1
    blt s0, a0, check_factor_loop

    j is_prime_true

not_prime:
    li a0, 0
    j exit_is_prime

is_prime_true:
    li a0, 1
    j exit_is_prime

exit_is_prime:
    lw ra, 4(sp)
    lw s0, 0(sp)
    addi sp, sp, 8
    ret
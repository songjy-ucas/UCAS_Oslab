# 判断 1 到 200 的质数

.text
.global _start     

_start:
    # s0 作为主循环计数器 i 
    # s1 作为循环上限 200
    # s2 作为结果存储的基地址
    li s0, 1        
    li s1, 200
    li s2, 0x50200000       

main_loop:
    # 如果 i <= 1, 那么不是质数
    li t2, 1
    ble s0, t2, not_prime_handler

    # 如果 i == 2, 那么是质数
    li t2, 2
    beq s0, t2, is_prime_handler

    # 从 j = 2 开始检查因子
    li t0, 2        

check_factor_loop:
    # 如果 j >= i, 说明没有找到因子, i 是质数
    bge t0, s0, is_prime_handler
    # 计算 i % j, 结果放入 t1
    rem t1, s0, t0 
    # 如果余数为 0, 说明 j是i的因子, i 不是质数
    beq t1, zero, not_prime_handler
    # j = j + 1, 继续检查下一个因子
    addi t0, t0, 1
    j check_factor_loop

is_prime_handler:
    # i (在 s0 中) 是质数
    li a0,1
    j store_result
not_prime_handler:
    # i (在 s0 中) 不是质数
    li a0,0
    j store_result

store_result:
    # 计算偏移量 (i - 1) * 4
    addi t3, s0, -1    
    slli t3, t3, 2      # t3 = t3 * 4  (逻辑左移2位等效于乘以4)
    # 计算最终地址: 基地址 + 偏移量
    add t4, s2, t3      
    sw a0, 0(t4)        

continue_main_loop:
    addi s0, s0, 1
    # 如果 i <= 200, 继续循环
    ble s0, s1, main_loop

exit:
    li a7, 93       # exit 系统调用号
    li a0, 0        # 返回码 0 (成功)
    ecall           # 执行系统调用

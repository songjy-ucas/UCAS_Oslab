
.equ RESULT_ADDR, 0x1000

.data
.global result_storage
result_storage:
    .word 0

.text
.global main
main:

    addi sp, sp, -4
    sw ra, 0(sp)

    li t0, 1
    li t1, 0
    li t2, 50

loop:
    add t1, t1, t0
    addi t0, t0, 1
    ble t0, t2, loop

    la t3, RESULT_ADDR
    sw t1, 0(t3)

    lw ra, 0(sp)
    addi sp, sp, 4
    
    ret

// TODO: [p0-task1] Add your code here!







    
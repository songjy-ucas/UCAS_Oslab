#sum 1 to 50

.data
result: .word 0

.text
.global _start   #程序入口为_start 

_start:
	#initialize
	li t0, 1   #set i = 1
	li t1, 50  #loop 50 times
	li t2, 0   #set sum =0

loop:
	bgt t0, t1, end	
	
	add t2, t2, t0	#sum = sum + i

	add t0, t0, 1	
	
	j loop 	
end: 
	la t3, result 	#load the address of result to t3
	sw t2, 0(t3)	#store the data(t2) to the address(t3)

exit:
  	li a0, 0	
	li a7, 93

	ecall

// TODO: [p0-task1] Add your code here!







    
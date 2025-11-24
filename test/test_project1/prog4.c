#include <os/task.h>
#include <os/kernel.h>
#include <stdio.h> // 包含我们新的头文件

// 不再需要在这里定义 print_int_to_terminal 函数了

int main()
{
    volatile int *shared_data = (int *)SHARED_MEM_ADDR;
    
    int current_value = *shared_data;
    long long final_result = (long long)current_value * current_value;
    *shared_data = (int)final_result;
    bios_putstr("I am prog4. Input: ");
    print_int(current_value);
    bios_putstr(", Final result: ");
    print_int((int)final_result); // 使用 print_int
    bios_putstr("\n\r");

    return 0;
}

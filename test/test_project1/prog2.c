#include <os/task.h>
#include <os/kernel.h>
#include <stdio.h> // 包含我们新的头文件

int main()
{
    volatile int *shared_data = (int *)SHARED_MEM_ADDR;

    int current_value = *shared_data;
    int result = current_value + 10;
    *shared_data = result;
    
    // 新增的输出部分
    bios_putstr("I am prog2. Input: ");
    print_int(current_value);
    bios_putstr(", Result: ");
    print_int(result);
    bios_putstr("\n\r");

    return 0;
}

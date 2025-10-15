#include "lib.h" 

int main(void)
{
    // 从共享内存获取上一个任务传递过来的值
    int input_val = get_shared_mem();

    // 计算结果
    int result = input_val + 10;
    
    print_s("Prog2: Input ");
    print_d(input_val);
    print_s(", adding 10, output: ");
    print_d(result);
    print_s("\n\r");

    // 将计算结果写入共享内存，供下一个任务使用
    set_shared_mem(result);

    return 0;
}
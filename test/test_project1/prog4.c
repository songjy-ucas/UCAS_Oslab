#include "lib.h" 

int main(void)
{
    // 从共享内存获取上一个任务传递过来的值
    int input_val = get_shared_mem();

    // 计算结果
    int result = input_val * input_val;

    print_s("Prog4: Input ");
    print_d(input_val);
    print_s(", squaring, final result: ");
    print_d(result);
    print_s("\n\r");

    // 最后一个任务，同样将结果写入共享内存，以便调试或后续扩展
    set_shared_mem(result);

    return 0;
}
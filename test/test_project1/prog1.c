#include <os/task.h>
#include <os/kernel.h> // 假设此头文件提供了 bios_* 函数原型

/**
 * @brief 从终端读取一个整数。
 * 支持退格键删除。
 * @return 读取到的整数。
 */
static int read_int_from_terminal()
{
    char buffer[32];
    int i = 0;
    while (i < sizeof(buffer) - 1) {
        int ch = -1;
        while(ch == -1) {
            ch = bios_getchar();
        }

        if (ch == '\r' || ch == '\n') {
            break;
        } else if (ch == 8 || ch == 127) { // 退格键
            if (i > 0) {
                i--;
                bios_putchar(8); bios_putchar(' '); bios_putchar(8);
            }
        } else if (ch >= '0' && ch <= '9') {
            buffer[i++] = (char)ch;
            bios_putchar((char)ch);
        }
    }
    buffer[i] = '\0';
    bios_putstr("\n\r");

    // 简易的 atoi 实现
    int result = 0;
    for (i = 0; buffer[i] != '\0'; ++i) {
        result = result * 10 + (buffer[i] - '0');
    }
    return result;
}

int main()
{
    // 定义一个指向共享内存地址的指针
    volatile int *shared_data = (int *)SHARED_MEM_ADDR;

    bios_putstr("I am prog1. Please input an integer: ");
    
    // 从终端读取一个整数
    int input_number = read_int_from_terminal();

    // 将整数写入共享内存
    *shared_data = input_number;

    return 0; // 退出，控制权交还内核
}

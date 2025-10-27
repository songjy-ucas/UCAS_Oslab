#include "lib.h"
#include <kernel.h>

#define SHARED_MEM_PA    0x5f000010 // 共享内存地址

// 实现 print_s 函数 --- 字符串打印
void print_s(const char *s)
{
    while (*s) {
        bios_putchar(*s++);
    }
}

// 实现 print_d 函数 --- 十进制整数打印
void print_d(int d)
{
    if (d < 0) {
        bios_putchar('-');
        d = -d;
    }
    if (d == 0) {
        bios_putchar('0');
        return;
    }
    char buf[32];
    int i = 0;
    while (d > 0) {
        buf[i++] = (d % 10) + '0';
        d /= 10;
    }
    while (i > 0) {
        bios_putchar(buf[--i]);
    }
}

// 实现 print_c 函数 --- 字符打印
void print_c(char c)
{
    bios_putchar(c);
}


// 共享内存函数的实现
int get_shared_mem(void)
{
    return *((volatile int*)SHARED_MEM_PA);
}

void set_shared_mem(int val)
{
    *((volatile int*)SHARED_MEM_PA) = val;
}

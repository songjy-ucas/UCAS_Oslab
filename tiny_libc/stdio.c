#include <os/kernel.h> // 需要 bios_putchar

/**
 * @brief 将整数打印到终端。
 * @param n 要打印的整数。
 */
void print_int(int n)
{
    if (n == 0) {
        bios_putchar('0');
        return;
    }
    
    if (n < 0) {
        bios_putchar('-');
        n = -n;
    }

    char buffer[32];
    int i = 0;
    while (n > 0) {
        buffer[i++] = (n % 10) + '0';
        n /= 10;
    }
    
    while (i > 0) {
        bios_putchar(buffer[--i]);
    }
}

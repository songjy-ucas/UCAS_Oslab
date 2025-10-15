#ifndef __USER_LIB_H__
#define __USER_LIB_H__


// 打印一个字符串。
void print_s(const char *s);

// 打印一个十进制整数。
void print_d(int d);

// 打印一个字符。
void print_c(char c);

// 共享内存函数的声明
int get_shared_mem(void);
void set_shared_mem(int val);

#endif 
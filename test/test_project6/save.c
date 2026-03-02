#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

void sys_fs_sync(void); // 定义在 tiny_libc/syscall.c

int main(void)
{
    sys_fs_sync(); // 将脏块刷新到 SD 卡
    return 0;
}


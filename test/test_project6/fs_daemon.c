#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

void sys_fs_sync(void); // Defined in tiny_libc/syscall.c

int main(void)
{
    printf("FS Daemon Started. Sync freq: 30s.\n");
    while (1) {
        sys_sleep(30); // Sleep for 30 seconds
        sys_fs_sync(); // Flush dirty blocks to SD card
    }
    return 0;
}
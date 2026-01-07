#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char buff[64];

int main(void)
{
    // 打开名为 "large.bin" 的文件
    // 参数 3 通常对应 O_RDWR | O_CREAT (读写模式，且如果不存在则创建)
    int fd = sys_fs_open("large.bin", 3); // O_RDWR
    if (fd < 0) {
        printf("Error: Cannot open large.bin\n");
        return -1;
    }

    // 定义三个测试字符串，分别用于测试不同层级的索引
    char *msg1 = "Direct block data at 0";
    char *msg2 = "Single indirect data at 1MB";
    char *msg3 = "Double indirect data at 128MB";

    printf("Testing large file support...\n");

    // ==================================================
    // 1. 测试直接指针 (Direct Pointers)
    // ==================================================
    // 写入文件开头 (偏移量 0)。
    // 这部分数据通常由 inode 中的直接指针数组（如 direct_ptrs[0]）直接索引。
    printf("Writing to offset 0...\n");
    sys_fs_write(fd, msg1, strlen(msg1));

    // ==================================================
    // 2. 测试一级间接指针 (Single Indirect Pointer)
    // ==================================================
    // 将文件指针移动到 1MB 处 (1024 * 1024 字节)。
    // 假设块大小为 4KB，直接指针通常只能覆盖前 40KB-48KB 的数据。
    // 1MB 显然超出了直接指针的范围，但在一级间接指针覆盖范围内（通常为 4MB）。
    // 这会触发文件系统分配一级索引块。
    printf("Writing to offset 1MB...\n");
    sys_fs_lseek(fd, 1024 * 1024, 0); // SEEK_SET (从文件头开始偏移)
    sys_fs_write(fd, msg2, strlen(msg2));

    // ==================================================
    // 3. 测试二级间接指针 (Double Indirect Pointer)
    // ==================================================
    // 将文件指针移动到 128MB 处 (128 * 1024 * 1024 字节)。
    // 一级间接指针通常能覆盖 4MB (1024个指针 * 4KB/块) 的数据。
    // 128MB 远远超出了 4MB，因此需要用到二级间接指针。
    // 这验证了文件系统是否支持大跨度的稀疏文件写入。
    printf("Writing to offset 128MB...\n");
    sys_fs_lseek(fd, 128 * 1024 * 1024, 0); // SEEK_SET
    sys_fs_write(fd, msg3, strlen(msg3));

    // ==================================================
    // 4. 数据回读验证 (Verify Data)
    // ==================================================
    printf("Verifying data...\n");

    // -> 验证直接索引数据
    sys_fs_lseek(fd, 0, 0); // 回到文件头
    memset(buff, 0, 64);    // 清空缓冲区
    sys_fs_read(fd, buff, strlen(msg1));
    printf("Read at 0: %s\n", buff);

    // -> 验证一级间接索引数据
    sys_fs_lseek(fd, 1024 * 1024, 0); // 跳到 1MB
    memset(buff, 0, 64);
    sys_fs_read(fd, buff, strlen(msg2));
    printf("Read at 1MB: %s\n", buff);

    // -> 验证二级间接索引数据
    sys_fs_lseek(fd, 128 * 1024 * 1024, 0); // 跳到 128MB
    memset(buff, 0, 64);
    sys_fs_read(fd, buff, strlen(msg3));
    printf("Read at 128MB: %s\n", buff);

    // 关闭文件
    sys_fs_close(fd);
    printf("Large file test completed!\n");

    return 0;
}
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char buff[64];

int main(void)
{
    int fd = sys_fs_open("1.txt", O_RDWR);

    // write 'hello world!' * 10
    for (int i = 0; i < 10; i++)
    {
        sys_fs_write(fd, "hello world!\n", 13);
    }

    // read
    for (int i = 0; i < 10; i++)
    {
        sys_fs_read(fd, buff, 13);
        for (int j = 0; j < 13; j++)
        {
            printf("%c", buff[j]);
        }
    }

    sys_fs_close(fd);

    return 0;
}
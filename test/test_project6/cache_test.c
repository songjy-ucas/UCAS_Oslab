#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define FILE_SIZE (512 * 1024) // 512KB
static char buff[4096];

void test_read_cache()
{
    printf("--- Read Cache Performance Test ---");
    int fd = sys_fs_open("read_test.bin", 3); // O_RDWR
    if (fd < 0) {
        printf("Creating test file...");
        fd = sys_fs_open("read_test.bin", 3);
        for (int i = 0; i < FILE_SIZE / 4096; i++) {
            sys_fs_write(fd, buff, 4096);
        }
    }
    sys_fs_lseek(fd, 0, 0); // SEEK_SET

    // Clear Cache to ensure Cold Read
    printf("Clearing cache for Cold Read...");
    int cfd = sys_fs_open("/proc/sys/vm", 3);
    sys_fs_write(cfd, "clear_cache = 1\n", 16);
    sys_fs_close(cfd);
    sys_fs_sync(); // Trigger update

    // First Read (Cold Cache)
    long start = sys_get_tick();
    for (int i = 0; i < FILE_SIZE / 4096; i++) {
        sys_fs_read(fd, buff, 4096);
    }
    long end = sys_get_tick();
    printf("Cold Read Ticks: %ld\n", end - start);

    // Second Read (Warm Cache)
    sys_fs_lseek(fd, 0, 0);
    start = sys_get_tick();
    for (int i = 0; i < FILE_SIZE / 4096; i++) {
        sys_fs_read(fd, buff, 4096);
    }
    end = sys_get_tick();
    printf("Warm Read Ticks: %ld\n", end - start);

    sys_fs_close(fd);
}

void test_write_performance()
{
    printf("\n--- Write Performance Test ---");
    
    // Switch to Write-Through
    printf("Switching to Write-Through...");
    int cfd = sys_fs_open("/proc/sys/vm", 3);
    sys_fs_write(cfd, "page_cache_policy = 0\nwrite_back_freq = 30\n", 44);
    sys_fs_close(cfd);
    sys_fs_sync(); // Force update policy
    sys_sleep(1); // Wait for sync if needed

    int fd = sys_fs_open("write_test.bin", 3);
    long start = sys_get_tick();
    for (int i = 0; i < 128; i++) { // 512KB
        sys_fs_write(fd, buff, 4096);
    }
    long end = sys_get_tick();
    printf("Write-Through Ticks: %ld\n", end - start);
    sys_fs_close(fd);

    // Switch to Write-Back
    printf("Switching to Write-Back...");
    cfd = sys_fs_open("/proc/sys/vm", 3);
    sys_fs_write(cfd, "page_cache_policy = 1\nwrite_back_freq = 30\n", 44);
    sys_fs_close(cfd);
    sys_fs_sync(); // Force update policy
    sys_sleep(1);

    fd = sys_fs_open("write_test_wb.bin", 3);
    start = sys_get_tick();
    for (int i = 0; i < 128; i++) {
        sys_fs_write(fd, buff, 4096);
    }
    end = sys_get_tick();
    printf("Write-Back Ticks: %ld\n", end - start);
    sys_fs_close(fd);
}



void test_metadata()
{
    printf("\n--- Metadata Performance Test ---");
    printf("Creating 100 files...");
    int cfd = sys_fs_open("/proc/sys/vm", 3);
    sys_fs_write(cfd, "page_cache_policy = 0\nwrite_back_freq = 30\n", 44);
    sys_fs_close(cfd); 

    char name[16];
    for (int i = 0; i < 100; i++) {
        itoa(i, name, 16, 10);
        strcat(name, ".txt");
        int fd = sys_fs_open(name, 3);
        sys_fs_close(fd);
    }

    // 1. With Dcache
    printf("Testing with Dcache ENABLED...");
    cfd = sys_fs_open("/proc/sys/vm", 3);
    sys_fs_write(cfd, "dcache_enable = 1\n", 18);
    sys_fs_close(cfd);
    sys_fs_sync();

    long start = sys_get_tick();
    for (int k = 0; k < 10; k++) { 
        for (int i = 0; i < 100; i++) {
            itoa(i, name, 16, 10);
            strcat(name, ".txt");
            int fd = sys_fs_open(name, 1); // O_RDONLY
            if (fd >= 0) sys_fs_close(fd);
        }
    }
    long end = sys_get_tick();
    printf("Dcache ENABLED Ticks: %ld\n", end - start);

    // 2. Without Dcache
    printf("Testing with Dcache DISABLED...");
    cfd = sys_fs_open("/proc/sys/vm", 3);
    sys_fs_write(cfd, "dcache_enable = 0\nclear_cache = 1\n", 34); 
    sys_fs_close(cfd);
    sys_fs_sync();

    start = sys_get_tick();
    for (int k = 0; k < 10; k++) {
        for (int i = 0; i < 100; i++) {
            itoa(i, name, 16, 10);
            strcat(name, ".txt");
            int fd = sys_fs_open(name, 1);
            if (fd >= 0) sys_fs_close(fd);
        }
    }
    end = sys_get_tick();
    printf("Dcache DISABLED Ticks: %ld\n", end - start);
}

int main(void)
{
    memset(buff, 'A', 4096);
    
    test_read_cache();
    test_write_performance();
    test_metadata();

    return 0;
}
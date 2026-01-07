#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

// 定义测试文件的大小为 512KB
#define FILE_SIZE (512 * 1024) // 512KB
// 定义一个 4KB 的缓冲区，用于读写操作
static char buff[4096];

// ==========================================
// 测试 1: 读缓存性能 (Read Cache Performance)
// 目的: 比较无缓存(从磁盘读)和有缓存(从内存读)的速度差异
// ==========================================
void test_read_cache()
{
    printf("--- Read Cache Performance Test ---");
    
    // 打开或创建测试文件 "read_test.bin"，模式 3 通常对应 O_RDWR | O_CREAT
    int fd = sys_fs_open("read_test.bin", 3); // O_RDWR
    
    // 如果文件打开成功（通常意味着文件已存在但可能大小不够，或者作为新文件创建）
    // 为了确保测试文件有内容，这里做了一个简单的检查和填充逻辑
    // 注意：这里逻辑稍微依赖于实现，如果 fd < 0 才会填充，意味着如果文件不存在才创建并填充
    if (fd < 0) {
        printf("Creating test file...");
        // 重新尝试创建文件
        fd = sys_fs_open("read_test.bin", 3);
        // 循环写入数据直到达到 512KB
        for (int i = 0; i < FILE_SIZE / 4096; i++) {
            sys_fs_write(fd, buff, 4096);
        }
    }
    // 将文件指针重置到文件开头，准备读取
    sys_fs_lseek(fd, 0, 0); // SEEK_SET

    // --- 关键步骤：清除缓存 ---
    // 为了模拟“冷读”（Cold Read），必须强制操作系统丢弃内存中的文件页缓存
    // 这里通过向内核提供的虚拟文件 /proc/sys/vm 写入指令来实现
    printf("Clearing cache for Cold Read...");
    int cfd = sys_fs_open("/proc/sys/vm", 3);
    sys_fs_write(cfd, "clear_cache = 1\n", 16); // 发送清空指令
    sys_fs_close(cfd);
    sys_fs_sync(); // 触发同步，确保指令生效

    // --- 第一次读取 (冷读) ---
    // 此时数据不在内存中，必须从 SD 卡/磁盘读取，速度较慢
    long start = sys_get_tick(); // 记录开始时间
    for (int i = 0; i < FILE_SIZE / 4096; i++) {
        sys_fs_read(fd, buff, 4096);
    }
    long end = sys_get_tick(); // 记录结束时间
    printf("Cold Read Ticks: %ld\n", end - start);

    // --- 第二次读取 (热读) ---
    // 刚才读取的数据应该已经被操作系统缓存在内存（Page Cache）中了
    // 再次读取时，直接从内存拷贝，不经过磁盘 IO，速度应该非常快
    sys_fs_lseek(fd, 0, 0); // 回到文件开头
    start = sys_get_tick();
    for (int i = 0; i < FILE_SIZE / 4096; i++) {
        sys_fs_read(fd, buff, 4096);
    }
    end = sys_get_tick();
    printf("Warm Read Ticks: %ld\n", end - start);

    sys_fs_close(fd);
}

// ==========================================
// 测试 2: 写性能 (Write Performance)
// 目的: 比较直写(Write-Through)和回写(Write-Back)策略的性能
// ==========================================
void test_write_performance()
{
    printf("\n--- Write Performance Test ---");
    
    // --- 场景 A: 直写 (Write-Through) ---
    // 每次 write 调用都会等待数据真正写入磁盘才返回，安全性高但慢
    printf("Switching to Write-Through...");
    int cfd = sys_fs_open("/proc/sys/vm", 3);
    // page_cache_policy = 0 代表直写模式
    sys_fs_write(cfd, "page_cache_policy = 0\nwrite_back_freq = 30\n", 44);
    sys_fs_close(cfd);
    sys_fs_sync(); // 强制更新策略配置
    sys_sleep(1); // 等待配置生效

    // 打开文件进行写入测试
    int fd = sys_fs_open("write_test.bin", 3);
    long start = sys_get_tick();
    // 写入 128 * 4KB = 512KB 数据
    for (int i = 0; i < 128; i++) { // 512KB
        sys_fs_write(fd, buff, 4096);
    }
    long end = sys_get_tick();
    printf("Write-Through Ticks: %ld\n", end - start);
    sys_fs_close(fd);

    // --- 场景 B: 回写 (Write-Back) ---
    // write 调用只需写入内存缓存即可返回，后台线程或触发器负责后续刷盘，速度快
    printf("Switching to Write-Back...");
    cfd = sys_fs_open("/proc/sys/vm", 3);
    // page_cache_policy = 1 代表回写模式
    sys_fs_write(cfd, "page_cache_policy = 1\nwrite_back_freq = 30\n", 44);
    sys_fs_close(cfd);
    sys_fs_sync(); // 强制更新策略配置
    sys_sleep(1);

    // 打开另一个文件进行测试
    fd = sys_fs_open("write_test_wb.bin", 3);
    start = sys_get_tick();
    // 同样写入 512KB
    for (int i = 0; i < 128; i++) {
        sys_fs_write(fd, buff, 4096);
    }
    end = sys_get_tick();
    printf("Write-Back Ticks: %ld\n", end - start);
    sys_fs_close(fd);
}


// ==========================================
// 测试 3: 元数据性能 (Metadata Performance)
// 目的: 测试目录项缓存(Dcache)对文件查找(Open操作)的加速效果
// ==========================================
void test_metadata()
{
    printf("\n--- Metadata Performance Test ---");
    printf("Creating 100 files...");
    
    // 为了加快创建速度，临时设置为 Write-Through (或者 Write-Back，取决于之前的状态)
    // 这里代码似乎意图是设置为 Write-Through 保证创建的文件落盘，或者这行配置是为了复位环境
    int cfd = sys_fs_open("/proc/sys/vm", 3);
    sys_fs_write(cfd, "page_cache_policy = 0\nwrite_back_freq = 30\n", 44);
    sys_fs_close(cfd); 

    // 创建 100 个空文件: 0.txt, 1.txt, ..., 99.txt
    char name[16];
    for (int i = 0; i < 100; i++) {
        itoa(i, name, 16, 10); // 将整数转换为字符串，10进制
        strcat(name, ".txt");
        int fd = sys_fs_open(name, 3); // 创建文件
        sys_fs_close(fd);
    }

    // --- 场景 A: 启用 Dcache ---
    // Dcache 会缓存 "文件名 -> inode号" 的映射，避免重复读取目录数据块
    printf("Testing with Dcache ENABLED...");
    cfd = sys_fs_open("/proc/sys/vm", 3);
    sys_fs_write(cfd, "dcache_enable = 1\n", 18); // 开启 Dcache
    sys_fs_close(cfd);
    sys_fs_sync();

    long start = sys_get_tick();
    // 反复打开这 100 个文件 10 次 (共 1000 次 open 操作)
    // 如果 Dcache 生效，除了第一次，后续查找都应该在内存中命中
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

    // --- 场景 B: 禁用 Dcache ---
    // 每次 open 都需要遍历磁盘上的目录数据块来查找文件名
    printf("Testing with Dcache DISABLED...");
    cfd = sys_fs_open("/proc/sys/vm", 3);
    // dcache_enable = 0: 关闭 Dcache
    // clear_cache = 1: 同时清空现有的页缓存，确保测试环境纯净
    sys_fs_write(cfd, "dcache_enable = 0\nclear_cache = 1\n", 34); 
    sys_fs_close(cfd);
    sys_fs_sync();

    start = sys_get_tick();
    // 同样反复打开 1000 次
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
    // 初始化缓冲区，填充字符 'A'
    memset(buff, 'A', 4096);
    
    // 依次运行三个性能测试
    test_read_cache();       // 测试读缓存
    test_write_performance();// 测试写策略
    test_metadata();         // 测试目录项缓存

    return 0;
}
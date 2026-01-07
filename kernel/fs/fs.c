#include "os/debug.h"
#include <os/string.h>
#include <os/fs.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/time.h>
#include <screen.h>
#include <printk.h>

// 超级块（Superblock）实例，存储文件系统的元数据（大小、布局等）
static superblock_t superblock;
// 文件描述符数组，管理当前打开的文件
static fdesc_t fdesc_array[NUM_FDESCS];

// 文件系统在磁盘上的起始扇区
#define FS_START_SEC FS_START_SECTOR

#include <os/lock.h>

// ==========================================
// 页缓存（Block Cache）相关定义
// ==========================================
#define CACHE_SIZE 128                  // 缓存块的数量
#define CACHE_POLICY_WRITE_BACK 1       // 写回策略：写入缓存，延迟写入磁盘
#define CACHE_POLICY_WRITE_THROUGH 0    // 直写策略：同时写入缓存和磁盘

// 当前缓存策略，默认为写回（Write-Back）
int page_cache_policy = CACHE_POLICY_WRITE_BACK;
int write_back_freq = 30; // seconds，写回频率（虽然代码中未直接使用定时器触发，但预留了变量）

// 缓存条目结构体
typedef struct page_cache_entry {
    uint32_t block_id;      // 对应的磁盘块号
    uint8_t valid;          // 有效位：该缓存行是否有数据
    uint8_t dirty;          // 脏位：数据是否被修改且未写入磁盘
    uint32_t last_access;   // 最后访问时间（用于LRU置换算法）
    uint8_t data[BLOCK_SIZE]; // 实际的数据块内容（4KB）
} page_cache_entry_t;

// 静态分配的页缓存数组
static page_cache_entry_t page_cache[CACHE_SIZE];

// ==========================================
// 目录项缓存（Dentry Cache）相关定义
// ==========================================
// 作用：加速路径解析，避免频繁读取目录的数据块
#define DCACHE_SIZE 128
typedef struct dcache_entry {
    uint32_t parent_ino;    // 父目录的 inode 号
    uint32_t ino;           // 当前文件的 inode 号
    char name[MAX_FILE_NAME]; // 文件名
    uint8_t valid;          // 有效位
} dcache_entry_t;

static dcache_entry_t dcache[DCACHE_SIZE];

// 用于记录逻辑时间，辅助LRU算法
static uint32_t current_access_time = 0;
// 缓存锁，保证缓存操作的原子性
static spin_lock_t cache_lock = {UNLOCKED};
int dcache_enable = 1; // 目录项缓存开关

// 将指定的缓存块刷回磁盘（不加锁版本，由调用者保证锁）
static void flush_block_unlocked(int index)
{
    // 只有当有效且为脏（Dirty）时才需要写回磁盘
    if (page_cache[index].valid && page_cache[index].dirty) {
        // klog("Performing block flushing...\n");
        // 调用底层 SD 卡驱动写入数据
        // kva2pa: 内核虚拟地址转物理地址
        bios_sd_write(kva2pa((uintptr_t)page_cache[index].data),
                      SECTOR_PER_BLOCK, // 块大小对应的扇区数（通常 4KB = 8扇区）
                      FS_START_SEC + page_cache[index].block_id * SECTOR_PER_BLOCK);
        page_cache[index].dirty = 0; // 清除脏位
    }
}

// 在缓存中查找指定块号的索引
static int get_cache_index(uint32_t block_id)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (page_cache[i].valid && page_cache[i].block_id == block_id) {
            return i; // 命中
        }
    }
    return -1; // 未命中
}

// 获取要被替换的缓存块索引（LRU算法 - 最近最少使用）
static int get_victim_index(void)
{
    int victim = -1;
    uint32_t min_time = 0xFFFFFFFF;

    // 第一遍扫描：优先寻找未使用的无效条目
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!page_cache[i].valid) return i;
    }

    // 第二遍扫描：如果没有空闲条目，寻找 last_access 最小的（最久未访问的）
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (page_cache[i].last_access < min_time) {
            min_time = page_cache[i].last_access;
            victim = i;
        }
    }
    return victim;
}

// 封装后的读取块函数：带缓存支持
static void sd_read_block(uint32_t block_id, void *buf)
{
    spin_lock_acquire(&cache_lock);
    int index = get_cache_index(block_id);
    if (index != -1) {
        // Cache Hit（缓存命中）
        page_cache[index].last_access = ++current_access_time; // 更新访问时间
        memcpy(buf, page_cache[index].data, BLOCK_SIZE); // 从缓存拷贝数据到用户buffer
        spin_lock_release(&cache_lock);
        return;
    }

    // Cache Miss（缓存未命中）
    int victim = get_victim_index();
    flush_block_unlocked(victim); // 如果牺牲者是脏的，先写回磁盘

    // 加载新块到缓存
    page_cache[victim].block_id = block_id;
    page_cache[victim].valid = 1;
    page_cache[victim].dirty = 0;
    page_cache[victim].last_access = ++current_access_time;

    // 从磁盘物理读取
    bios_sd_read(kva2pa((uintptr_t)page_cache[victim].data),
                 SECTOR_PER_BLOCK,
                 FS_START_SEC + block_id * SECTOR_PER_BLOCK);

    memcpy(buf, page_cache[victim].data, BLOCK_SIZE);
    spin_lock_release(&cache_lock);
}

// 封装后的写入块函数：带缓存支持
static void sd_write_block(uint32_t block_id, void *buf)
{
    spin_lock_acquire(&cache_lock);
    int index = get_cache_index(block_id);
    if (index == -1) {
        // Cache Miss：如果不命中，分配一个缓存块
        // 注意：这里没有从磁盘读旧数据，因为假设是全块覆盖写入
        // 如果是部分写入，逻辑层必须先读再改再写
        int victim = get_victim_index();
        flush_block_unlocked(victim);
        index = victim;

        page_cache[index].block_id = block_id;
        page_cache[index].valid = 1;
    }

    // 更新缓存内容
    memcpy(page_cache[index].data, buf, BLOCK_SIZE);
    page_cache[index].last_access = ++current_access_time;

    // 根据写策略处理
    if (page_cache_policy == CACHE_POLICY_WRITE_THROUGH) {
        // 直写：立即写入磁盘
        page_cache[index].dirty = 0;
        bios_sd_write(kva2pa((uintptr_t)page_cache[index].data),
                      SECTOR_PER_BLOCK,
                      FS_START_SEC + block_id * SECTOR_PER_BLOCK);
    } else {
        // 写回：标记为脏，稍后刷盘
        page_cache[index].dirty = 1;
    }
    spin_lock_release(&cache_lock);
}

// 内核态字符串查找辅助函数
static char *k_strstr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++; n++;
        }
        if (!*n) return (char *)haystack;
    }
    return 0;
}

// 文件系统同步函数：处理配置更新及脏块刷盘
// 这个函数可能会在系统空闲循环或定时器中调用
void do_fs_sync(void)
{
    // 1. 更新配置 (从 /proc/sys/vm 虚拟文件读取)
    // 这允许用户通过写文件来动态改变缓存策略
    int fd = do_open("/proc/sys/vm", O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        int len = do_read(fd, buf, 255);
        if (len > 0) {
            buf[len] = '\0';

            // --- 解析 page_cache_policy ---
            char *p_policy = k_strstr(buf, "page_cache_policy = ");
            if (p_policy) {
                int new_policy = 0;
                char *p = p_policy + 20;
                while (*p >= '0' && *p <= '9') {
                    new_policy = new_policy * 10 + (*p - '0');
                    p++;
                }
                // 如果从 Write-Back 切换到 Write-Through，必须立即刷盘防止数据丢失
                if (page_cache_policy == CACHE_POLICY_WRITE_BACK && new_policy == CACHE_POLICY_WRITE_THROUGH) {
                    spin_lock_acquire(&cache_lock);
                    for (int i = 0; i < CACHE_SIZE; i++) flush_block_unlocked(i);
                    spin_lock_release(&cache_lock);
                }
                page_cache_policy = new_policy;
            }

            // --- 解析 write_back_freq ---
            char *p_freq = k_strstr(buf, "write_back_freq = ");
            if (p_freq) {
                int new_freq = 0;
                char *p = p_freq + 18;
                while (*p >= '0' && *p <= '9') {
                    new_freq = new_freq * 10 + (*p - '0');
                    p++;
                }
                if (new_freq > 0) write_back_freq = new_freq;
            }

            // --- 解析 dcache_enable ---
            char *p_dcache = k_strstr(buf, "dcache_enable = ");
            if (p_dcache) {
                int val = 0;
                char *p = p_dcache + 16;
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                }
                dcache_enable = val;
            }

            // --- 解析 clear_cache (强制清空缓存) ---
            // 这通常用于测试，确保接下来的读取是从磁盘（Cold Read）
            if (k_strstr(buf, "clear_cache = 1")) {
                spin_lock_acquire(&cache_lock);
                for (int i = 0; i < CACHE_SIZE; i++) {
                    // 先刷脏块，避免数据丢失
                    flush_block_unlocked(i);
                    // 标记无效
                    page_cache[i].valid = 0;
                }
                spin_lock_release(&cache_lock);

                // 同时清空 Dcache
                for (int i = 0; i < DCACHE_SIZE; i++) {
                    dcache[i].valid = 0;
                }
            }
        }
        do_close(fd);
    }

    // 2. 周期性刷盘 (仅针对 Write-Back 策略)
    if (page_cache_policy == CACHE_POLICY_WRITE_BACK) {
        spin_lock_acquire(&cache_lock);
        for (int i = 0; i < CACHE_SIZE; i++) {
            flush_block_unlocked(i);
        }
        spin_lock_release(&cache_lock);
    }
}

// 辅助函数：将指定范围的块清零（用于分配新块时初始化）
static void clear_blocks(uint32_t start_block, uint32_t num_blocks)
{
    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    for (uint32_t i = 0; i < num_blocks; i++) {
        sd_write_block(start_block + i, buf);
    }
}

// 辅助函数：在位图（Bitmap）中分配一个空闲位
// map_offset: 位图起始块号
// map_count: 位图占用的块数
// total_count: 总资源数（防止越界）
static int alloc_bit(uint32_t map_offset, uint32_t map_count, uint32_t total_count)
{
    uint8_t buf[BLOCK_SIZE];
    for (uint32_t i = 0; i < map_count; i++) {
        sd_read_block(map_offset + i, buf);
        for (uint32_t j = 0; j < BLOCK_SIZE; j++) {
            if (buf[j] != 0xff) { // 优化：如果字节不是全1，说明有空闲位
                for (int k = 0; k < 8; k++) {
                    if (!((buf[j] >> k) & 1)) { // 找到为0的位
                        uint32_t index = i * BLOCK_SIZE * 8 + j * 8 + k;
                        if (index >= total_count) return -1;
                        buf[j] |= (1 << k); // 标记为1（已占用）
                        sd_write_block(map_offset + i, buf); // 写回位图
                        return index;
                    }
                }
            }
        }
    }
    return -1; // 无资源可用
}

// 辅助函数：释放位图中的一位
static void free_bit(uint32_t map_offset, uint32_t index)
{
    uint8_t buf[BLOCK_SIZE];
    uint32_t block_idx = index / (BLOCK_SIZE * 8);      // 位图中的第几个块
    uint32_t byte_idx = (index % (BLOCK_SIZE * 8)) / 8; // 块中的第几个字节
    uint32_t bit_idx = index % 8;                       // 字节中的第几位

    sd_read_block(map_offset + block_idx, buf);
    buf[byte_idx] &= ~(1 << bit_idx); // 置0
    sd_write_block(map_offset + block_idx, buf);
}

// 辅助函数：从磁盘 inode 表中读取 inode
static void get_inode(uint32_t ino, inode_t *inode)
{
    char buf[BLOCK_SIZE];
    // 计算该 inode 在 Inode Table 中的位置
    uint32_t block_idx = (ino * sizeof(inode_t)) / BLOCK_SIZE;
    uint32_t offset = (ino * sizeof(inode_t)) % BLOCK_SIZE;
    sd_read_block(superblock.inode_table_offset + block_idx, buf);
    memcpy((uint8_t *)inode, (uint8_t *)(buf + offset), sizeof(inode_t));
}

// 辅助函数：将 inode 写入磁盘 inode 表
static void set_inode(uint32_t ino, inode_t *inode)
{
    char buf[BLOCK_SIZE];
    uint32_t block_idx = (ino * sizeof(inode_t)) / BLOCK_SIZE;
    uint32_t offset = (ino * sizeof(inode_t)) % BLOCK_SIZE;
    sd_read_block(superblock.inode_table_offset + block_idx, buf);
    memcpy((uint8_t *)(buf + offset), (uint8_t *)inode, sizeof(inode_t));
    sd_write_block(superblock.inode_table_offset + block_idx, buf);
}

// 核心函数：逻辑块地址映射到物理块地址
// logical_block: 文件内的第几个块（0, 1, 2...）
// allocate: 如果块不存在，是否分配新块（0=否，1=是）
static int get_block_addr(inode_t *inode, uint32_t logical_block, int allocate)
{
    // --- 直接寻址 (Direct Pointers) ---
    // NDIRECT 通常是 10-12 个
    if (logical_block < NDIRECT) {
        if (inode->direct_ptrs[logical_block] == 0) {
            if (!allocate) return 0;
            // 分配数据块
            int new_blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
            if (new_blk < 0) return -1;
            inode->direct_ptrs[logical_block] = new_blk;
            clear_blocks(superblock.data_offset + new_blk, 1); // 新块清零
            return new_blk;
        }
        return inode->direct_ptrs[logical_block];
    }

    logical_block -= NDIRECT;

    // --- 一级间接寻址 (Single Indirect) ---
    // INDIRECT_BLOCK_COUNT = BLOCK_SIZE / 4 = 1024
    if (logical_block < INDIRECT_BLOCK_COUNT) {
        // 如果一级索引块不存在，先分配一级索引块
        if (inode->indirect_ptr[0] == 0) {
            if (!allocate) return 0;
            int new_blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
            if (new_blk < 0) return -1;
            inode->indirect_ptr[0] = new_blk;
            clear_blocks(superblock.data_offset + new_blk, 1);
        }
        char buf[BLOCK_SIZE];
        sd_read_block(superblock.data_offset + inode->indirect_ptr[0], buf);
        uint32_t *ptrs = (uint32_t *)buf;
        // 在索引块中查找/分配实际数据块
        if (ptrs[logical_block] == 0) {
            if (!allocate) return 0;
            int new_blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
            if (new_blk < 0) return -1;
            ptrs[logical_block] = new_blk;
            clear_blocks(superblock.data_offset + new_blk, 1);
            sd_write_block(superblock.data_offset + inode->indirect_ptr[0], buf); // 更新索引块
            return new_blk;
        }
        return ptrs[logical_block];
    }

    logical_block -= INDIRECT_BLOCK_COUNT;

    // --- 二级间接寻址 (Double Indirect) ---
    // 支持范围：1024 * 1024 个块
    if (logical_block < INDIRECT_BLOCK_COUNT * INDIRECT_BLOCK_COUNT) {
        // 如果二级索引块（顶级）不存在，分配它
        if (inode->indirect_ptr[1] == 0) {
            if (!allocate) return 0;
            int new_blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
            if (new_blk < 0) return -1;
            inode->indirect_ptr[1] = new_blk;
            clear_blocks(superblock.data_offset + new_blk, 1);
        }

        uint32_t l1_idx = logical_block / INDIRECT_BLOCK_COUNT; // 一级索引在二级块中的位置
        uint32_t l2_idx = logical_block % INDIRECT_BLOCK_COUNT; // 数据块在一级块中的位置

        char buf[BLOCK_SIZE];
        sd_read_block(superblock.data_offset + inode->indirect_ptr[1], buf);
        uint32_t *l1_ptrs = (uint32_t *)buf;

        // 如果一级索引块不存在，分配它
        if (l1_ptrs[l1_idx] == 0) {
            if (!allocate) return 0;
            int new_blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
            if (new_blk < 0) return -1;
            l1_ptrs[l1_idx] = new_blk;
            clear_blocks(superblock.data_offset + new_blk, 1);
            sd_write_block(superblock.data_offset + inode->indirect_ptr[1], buf);
        }

        int l2_blk = l1_ptrs[l1_idx];
        sd_read_block(superblock.data_offset + l2_blk, buf);
        uint32_t *l2_ptrs = (uint32_t *)buf;

        // 如果实际数据块不存在，分配它
        if (l2_ptrs[l2_idx] == 0) {
            if (!allocate) return 0;
            int new_blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
            if (new_blk < 0) return -1;
            l2_ptrs[l2_idx] = new_blk;
            clear_blocks(superblock.data_offset + new_blk, 1);
            sd_write_block(superblock.data_offset + l2_blk, buf);
            return new_blk;
        }
        return l2_ptrs[l2_idx];
    }

    return -1; // 超出最大文件大小
}

// 递归释放 inode 占用的所有数据块和间接索引块
static void free_inode_blocks(inode_t *inode)
{
    /* 释放直接指针指向的块 */
    for (int i = 0; i < NDIRECT; i++) {
        if (inode->direct_ptrs[i] != 0) {
            free_bit(superblock.block_map_offset, inode->direct_ptrs[i]);
            inode->direct_ptrs[i] = 0;
        }
    }
    /* 释放一级间接指针 */
    if (inode->indirect_ptr[0] != 0) {
        char buf[BLOCK_SIZE];
        sd_read_block(superblock.data_offset + inode->indirect_ptr[0], buf);
        uint32_t *ptrs = (uint32_t *)buf;
        for (int i = 0; i < INDIRECT_BLOCK_COUNT; i++) {
            if (ptrs[i] != 0) free_bit(superblock.block_map_offset, ptrs[i]); // 释放数据块
        }
        free_bit(superblock.block_map_offset, inode->indirect_ptr[0]); // 释放索引块本身
        inode->indirect_ptr[0] = 0;
    }
    /* 释放二级间接指针 */
    if (inode->indirect_ptr[1] != 0) {
        char buf1[BLOCK_SIZE], buf2[BLOCK_SIZE];
        sd_read_block(superblock.data_offset + inode->indirect_ptr[1], buf1);
        uint32_t *l1_ptrs = (uint32_t *)buf1;
        for (int i = 0; i < INDIRECT_BLOCK_COUNT; i++) {
            if (l1_ptrs[i] != 0) {
                sd_read_block(superblock.data_offset + l1_ptrs[i], buf2);
                uint32_t *l2_ptrs = (uint32_t *)buf2;
                for (int j = 0; j < INDIRECT_BLOCK_COUNT; j++) {
                    if (l2_ptrs[j] != 0) free_bit(superblock.block_map_offset, l2_ptrs[j]);
                }
                free_bit(superblock.block_map_offset, l1_ptrs[i]);
            }
        }
        free_bit(superblock.block_map_offset, inode->indirect_ptr[1]);
        inode->indirect_ptr[1] = 0;
    }
}

// 简单的字符串哈希函数，用于 Dcache 索引
static uint32_t dcache_hash(uint32_t parent_ino, char *name)
{
    uint32_t hash = parent_ino;
    while (*name) hash = (hash << 5) + *name++;
    return hash % DCACHE_SIZE;
}

// 添加条目到 Dcache
static void dcache_add(uint32_t parent_ino, char *name, uint32_t ino)
{
    uint32_t idx = dcache_hash(parent_ino, name);
    dcache[idx].parent_ino = parent_ino;
    dcache[idx].ino = ino;
    strcpy(dcache[idx].name, name);
    dcache[idx].valid = 1;
}

// 在 Dcache 中查找
static int dcache_lookup(uint32_t parent_ino, char *name)
{
    uint32_t idx = dcache_hash(parent_ino, name);
    // 校验 hash 碰撞（检查 parent_ino 和 name 是否完全匹配）
    if (dcache[idx].valid && dcache[idx].parent_ino == parent_ino && strcmp(dcache[idx].name, name) == 0) {
        return dcache[idx].ino;
    }
    return -1;
}

// 从 Dcache 中删除
static void dcache_del(uint32_t parent_ino, char *name)
{
    uint32_t idx = dcache_hash(parent_ino, name);
    if (dcache[idx].valid && dcache[idx].parent_ino == parent_ino && strcmp(dcache[idx].name, name) == 0) {
        dcache[idx].valid = 0;
    }
}

// 在目录中查找文件名为 name 的目录项
// dir_ino: 目录的 inode 号
// entry: 输出参数，返回找到的目录项
static int find_entry(uint32_t dir_ino, char *name, dentry_t *entry)
{
    // 1. 先查 Dcache (如果开启)
    if (dcache_enable) {
        int cached_ino = dcache_lookup(dir_ino, name);
        if (cached_ino != -1) {
            entry->ino = cached_ino;
            strcpy(entry->name, name);
            return 0;
        }
    }

    // 2. 查磁盘 (遍历目录的数据块)
    inode_t inode;
    get_inode(dir_ino, &inode);
    if (inode.mode != IM_DIR) return -1; // 确保是目录

    char buf[BLOCK_SIZE];
    uint32_t num_dentries = inode.size / sizeof(dentry_t); // 目录项总数
    uint32_t dentries_per_block = BLOCK_SIZE / sizeof(dentry_t);

    for (uint32_t lb = 0; num_dentries > 0; lb++) {
        int pb = get_block_addr(&inode, lb, 0); // 获取第 lb 个数据块
        if (pb == 0) continue;
        sd_read_block(superblock.data_offset + pb, buf);
        dentry_t *dentries = (dentry_t *)buf;
        // 遍历块内的所有目录项
        for (uint32_t j = 0; j < dentries_per_block && num_dentries > 0; j++, num_dentries--) {
            if (strcmp(dentries[j].name, name) == 0) {
                // 找到匹配项
                memcpy((uint8_t *)entry, (uint8_t *)&dentries[j], sizeof(dentry_t));

                // 3. 将结果填入 Dcache
                if (dcache_enable) {
                    dcache_add(dir_ino, name, dentries[j].ino);
                }
                return 0;
            }
        }
    }
    return -1;
}

// 解析路径，返回目标文件的 inode 号
// 支持绝对路径和相对路径
static int lookup_path(char *path)
{
    // 确定起始 inode：如果是 '/' 开头则从根目录开始，否则从当前工作目录开始
    uint32_t curr_ino = (path[0] == '/') ? superblock.root_ino : current_running->cwd_ino;

    char name[MAX_FILE_NAME];
    int p = (path[0] == '/') ? 1 : 0;

    // 逐级解析路径: /home/user -> home -> user
    while (path[p]) {
        int q = 0;
        // 提取一级文件名
        while (path[p] && path[p] != '/') {
            if (q < MAX_FILE_NAME - 1) name[q++] = path[p];
            p++;
        }
        name[q] = '\0';
        while (path[p] == '/') p++; // 跳过连续的斜杠

        if (q > 0) {
            dentry_t entry;
            // 在当前目录查找该文件名
            if (find_entry(curr_ino, name, &entry) != 0) return -1;
            curr_ino = entry.ino; // 进入下一级
        }
    }
    return curr_ino;
}

// 辅助工具：将路径拆分为“父目录路径”和“文件名”
// 例如: /home/user/file.txt -> parent_ino指向 /home/user, name="file.txt"
static int get_parent_and_name(char *path, uint32_t *parent_ino, char *name)
{
    char parent_path[128];
    int len = strlen(path);
    int last_slash = -1;
    // 找到最后一个斜杠的位置
    for (int i = 0; i < len; i++) if (path[i] == '/') last_slash = i;

    if (last_slash == -1) {
        // 没有斜杠，说明在当前目录下
        *parent_ino = current_running->cwd_ino;
        strcpy(name, path);
    } else {
        if (last_slash == 0) {
            // 只有根目录斜杠，例如 /file
            *parent_ino = superblock.root_ino;
        } else {
            // 提取父目录路径
            memcpy((uint8_t *)parent_path, (uint8_t *)path, last_slash);
            parent_path[last_slash] = '\0';
            *parent_ino = lookup_path(parent_path);
        }
        strcpy(name, path + last_slash + 1);
    }
    return (*parent_ino == (uint32_t)-1) ? -1 : 0;
}

// 初始化文件系统
void init_fs(void)
{
    char buf[BLOCK_SIZE];
    // 读取超级块（第0块）
    sd_read_block(0, buf);
    memcpy((uint8_t *)&superblock, (uint8_t *)buf, sizeof(superblock_t));

    // 检查魔数，如果不匹配，说明磁盘未格式化，执行 mkfs
    if (superblock.magic != SUPERBLOCK_MAGIC) {
        printk("Magic number 0x%x not match 0x%x, mkfs...\n", superblock.magic, SUPERBLOCK_MAGIC);
        do_mkfs();
        sd_read_block(0, buf);
        memcpy((uint8_t *)&superblock, (uint8_t *)buf, sizeof(superblock_t));
        printk("mkfs succeeded!\n");
    }

    // 打印文件系统信息
    printk("[FS] Start initialize filesystem!\n");
    printk("[FS] Superblock Info:\n");
    printk("  Magic: 0x%x\n", superblock.magic);
    printk("  size: %d sectors\n", superblock.size_sectors);
    printk("  start_sector: %d\n", superblock.fs_start_sector);
    printk("  block_map_offset: %d\n", superblock.block_map_offset);
    printk("  inode_map_offset: %d\n", superblock.inode_map_offset);
    printk("  inode_table_offset: %d\n", superblock.inode_table_offset);
    printk("  data_offset: %d\n", superblock.data_offset);
    printk("  inode_entry_size: %dB, dentry_size: %dB\n", sizeof(inode_t), sizeof(dentry_t));

    // 初始化文件描述符数组
    for (int i = 0; i < NUM_FDESCS; i++) fdesc_array[i].is_used = 0;
}

// 格式化文件系统
int do_mkfs(void)
{
    /* 1. 废弃所有页缓存 */
    spin_lock_acquire(&cache_lock);
    for (int i = 0; i < CACHE_SIZE; i++) {
        page_cache[i].valid = 0;
        page_cache[i].dirty = 0;
    }
    spin_lock_release(&cache_lock);

    /* 2. 废弃所有目录项缓存 */
    for (int i = 0; i < DCACHE_SIZE; i++) {
        dcache[i].valid = 0;
    }

    // 计算布局参数
    uint32_t fs_size_bytes = 512 * 1024 * 1024; // 假设 512MB
    uint32_t fs_size_blocks = fs_size_bytes / BLOCK_SIZE;
    uint32_t fs_size_sectors = fs_size_bytes / SECTOR_SIZE;

    uint32_t block_map_size = 4; // 块位图大小
    uint32_t inode_map_size = 1; // inode位图大小
    uint32_t inode_table_size = (4096 * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE; // inode表大小

    // 计算各区域的起始块号
    uint32_t sb_block = 0;
    uint32_t bmap_block = sb_block + 1;
    uint32_t imap_block = bmap_block + block_map_size;
    uint32_t itable_block = imap_block + inode_map_size;
    uint32_t data_block = itable_block + inode_table_size;

    // 填充超级块结构
    memset(&superblock, 0, sizeof(superblock_t));
    superblock.magic = SUPERBLOCK_MAGIC;
    superblock.fs_start_sector = FS_START_SEC;
    superblock.size_sectors = fs_size_sectors;
    superblock.block_map_offset = bmap_block;
    superblock.block_map_count = block_map_size;
    superblock.inode_map_offset = imap_block;
    superblock.inode_map_count = inode_map_size;
    superblock.inode_table_offset = itable_block;
    superblock.inode_table_count = inode_table_size;
    superblock.data_offset = data_block;
    superblock.data_count = fs_size_blocks - data_block;
    superblock.inode_count = 4096;
    superblock.block_count = fs_size_blocks;
    superblock.root_ino = 1; // 根目录 inode 号固定为 1

    // 清空数据区（可选，这里为了安全清空了开头部分）
    clear_blocks(0, data_block);

    // 写入超级块
    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    memcpy((uint8_t *)buf, (uint8_t *)&superblock, sizeof(superblock_t));
    sd_write_block(0, buf);

    // 初始化 inode 位图：保留 0 和 1
    memset(buf, 0, BLOCK_SIZE);
    buf[0] |= (1 << 0) | (1 << 1); 
    sd_write_block(imap_block, buf);

    // 初始化块位图：保留前两个块
    memset(buf, 0, BLOCK_SIZE);
    buf[0] |= (1 << 0) | (1 << 1);
    sd_write_block(bmap_block, buf);

    // 创建根目录 inode
    inode_t root;
    memset(&root, 0, sizeof(inode_t));
    root.ino = 1; root.mode = IM_DIR; root.nlinks = 2; // "." 和 ".." 算链接
    root.size = 2 * sizeof(dentry_t); // 初始大小为两个目录项
    root.direct_ptrs[0] = 1; // 分配第1个数据块
    set_inode(1, &root);

    // 初始化根目录的数据块，写入 "." 和 ".."
    memset(buf, 0, BLOCK_SIZE);
    dentry_t *de = (dentry_t *)buf;
    strcpy(de[0].name, "."); de[0].ino = 1;
    strcpy(de[1].name, ".."); de[1].ino = 1;
    sd_write_block(data_block + 1, buf);

    // 预创建 /proc 系统目录，用于配置
    do_mkdir("/proc");
    do_mkdir("/proc/sys");
    int fd = do_open("/proc/sys/vm", O_RDWR);
    if (fd >= 0) {
        char *content = "page_cache_policy = 1\nwrite_back_freq = 30\n";
        do_write(fd, content, strlen(content));
        do_close(fd);
    }

    return 0;
}

// 显示文件系统统计信息
int do_statfs(void)
{
    printk("[FS] Superblock Info:\n");
    printk("  Magic: 0x%lx, Size: %d sectors\n", (uint64_t)superblock.magic, superblock.size_sectors);
    printk("  Data Area: Start %d, Count %d\n", superblock.data_offset, superblock.data_count);
    return 0;
}

// 切换当前工作目录
int do_cd(char *path)
{
    int ino = lookup_path(path);
    if (ino < 0) return -1;
    inode_t inode;
    get_inode(ino, &inode);
    if (inode.mode != IM_DIR) return -1; // 必须是目录
    current_running->cwd_ino = ino; // 更新 PCB 中的当前目录 inode
    return 0;
}

// 列出目录内容
int do_ls(char *path, int option)
{
    // 如果 path 为空，列出当前目录
    int ino = (path == NULL || path[0] == '\0') ? current_running->cwd_ino : lookup_path(path);
    if (ino < 0) return -1;

    inode_t inode;
    get_inode(ino, &inode);
    if (inode.mode != IM_DIR) return -1;

    char buf[BLOCK_SIZE];
    uint32_t num_dentries = inode.size / sizeof(dentry_t);
    uint32_t per_block = BLOCK_SIZE / sizeof(dentry_t);

    // 遍历该目录的所有数据块
    for (uint32_t lb = 0; num_dentries > 0; lb++) {
        int pb = get_block_addr(&inode, lb, 0);
        if (pb == 0) continue;
        sd_read_block(superblock.data_offset + pb, buf);
        dentry_t *de = (dentry_t *)buf;
        // 打印块内的所有目录项
        for (int j = 0; j < per_block && num_dentries > 0; j++, num_dentries--) {
            inode_t target;
            get_inode(de[j].ino, &target);
            if (option) { // 详细模式 (ls -l)
                printk("[%d] %s  links:%d  size:%d  ino:%d  ",
                       target.mode, (target.mode == IM_DIR ? "DIR" : "FILE"),
                       target.nlinks, target.size, de[j].ino);
                // 颜色和图标处理（蓝色显示目录）
                if (target.mode == IM_DIR) bios_putstr(ANSI_FG_BLUE);
                if (target.mode == IM_DIR) bios_putstr(" ");
                else bios_putstr(" ");
                bios_putstr(de[j].name);
                if (target.mode == IM_DIR) bios_putstr("/");
                bios_putstr(ANSI_NONE);
                bios_putstr("  ");
                printk("\n");
            } else { // 简单模式
                if (target.mode == IM_DIR) bios_putstr(ANSI_FG_BLUE);
                if (target.mode == IM_DIR) bios_putstr(" ");
                else bios_putstr(" ");
                bios_putstr(de[j].name);
                if (target.mode == IM_DIR) bios_putstr("/");
                bios_putstr(ANSI_NONE);
                bios_putstr("  ");
            }
        }
    }
    if (!option) bios_putstr("\n\r");
    pcb_t *curr = current_running;
    screen_move_cursor(curr->cursor_x, curr->cursor_y + 1);
    return 0;
}

// 创建目录
int do_mkdir(char *path)
{
    uint32_t parent_ino;
    char name[MAX_FILE_NAME];
    // 解析父目录 inode 和 新目录名
    if (get_parent_and_name(path, &parent_ino, name) != 0) return -1;

    // 检查目录是否已存在
    dentry_t tmp;
    if (find_entry(parent_ino, name, &tmp) == 0) return -1;

    // 1. 分配新的 inode 和数据块
    int ino = alloc_bit(superblock.inode_map_offset, superblock.inode_map_count, superblock.inode_count);
    int blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
    if (ino < 0 || blk < 0) return -1;

    // 2. 初始化新目录的 inode
    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    new_inode.ino = ino; new_inode.mode = IM_DIR; new_inode.nlinks = 2; // . 和 ..
    new_inode.size = 2 * sizeof(dentry_t);
    new_inode.direct_ptrs[0] = blk;
    set_inode(ino, &new_inode);

    // 3. 在新目录的数据块中写入 "." 和 ".."
    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    dentry_t *de = (dentry_t *)buf;
    strcpy(de[0].name, "."); de[0].ino = ino;
    strcpy(de[1].name, ".."); de[1].ino = parent_ino;
    sd_write_block(superblock.data_offset + blk, buf);

    // 4. 将新目录项添加到父目录中
    inode_t parent;
    get_inode(parent_ino, &parent);
    uint32_t dentries_per_block = BLOCK_SIZE / sizeof(dentry_t);
    uint32_t entry_idx = parent.size / sizeof(dentry_t);
    uint32_t blk_idx = entry_idx / dentries_per_block;
    uint32_t off_idx = entry_idx % dentries_per_block;

    // 获取父目录的下一个可用写入块（flag=1表示如果块不够则分配新块）
    int pb = get_block_addr(&parent, blk_idx, 1);
    if (pb < 0) return -1;

    sd_read_block(superblock.data_offset + pb, buf);
    de = (dentry_t *)buf;
    strcpy(de[off_idx].name, name);
    de[off_idx].ino = ino;
    sd_write_block(superblock.data_offset + pb, buf);

    // 更新父目录大小和链接数（子目录的 ".." 指向父目录，所以 nlinks+1）
    parent.size += sizeof(dentry_t);
    parent.nlinks++; 
    set_inode(parent_ino, &parent);

    return 0;
}

// 删除空目录
int do_rmdir(char *path)
{
    int ino = lookup_path(path);
    if (ino < 0) return -1;
    inode_t inode;
    get_inode(ino, &inode);
    if (inode.mode != IM_DIR) return -1;
    // 只能删除空目录（大小只有 "." 和 ".."）
    if (inode.size > 2 * sizeof(dentry_t)) return -1; /* Not empty */
    return do_rm(path); // 调用通用删除函数
}

// 打开文件
int do_open(char *path, int mode)
{
    int ino = lookup_path(path);
    // 如果文件不存在
    if (ino < 0) {
        if (mode == O_RDONLY) return -1; // 只读模式下报错
        // 创建模式：创建新文件
        uint32_t parent_ino;
        char name[MAX_FILE_NAME];
        if (get_parent_and_name(path, &parent_ino, name) != 0) return -1;

        // 分配 inode
        int new_ino = alloc_bit(superblock.inode_map_offset, superblock.inode_map_count, superblock.inode_count);
        if (new_ino < 0) return -1;

        // 初始化文件 inode
        inode_t new_inode;
        memset(&new_inode, 0, sizeof(inode_t));
        new_inode.ino = new_ino; new_inode.mode = IM_REG; new_inode.nlinks = 1;
        new_inode.size = 0;
        set_inode(new_ino, &new_inode);

        // 添加到父目录
        inode_t parent;
        get_inode(parent_ino, &parent);
        uint32_t dentries_per_block = BLOCK_SIZE / sizeof(dentry_t);
        uint32_t entry_idx = parent.size / sizeof(dentry_t);
        uint32_t blk_idx = entry_idx / dentries_per_block;
        uint32_t off_idx = entry_idx % dentries_per_block;

        int pb = get_block_addr(&parent, blk_idx, 1);
        char buf[BLOCK_SIZE];
        sd_read_block(superblock.data_offset + pb, buf);
        dentry_t *de = (dentry_t *)buf;
        strcpy(de[off_idx].name, name);
        de[off_idx].ino = new_ino;
        sd_write_block(superblock.data_offset + pb, buf);

        parent.size += sizeof(dentry_t);
        set_inode(parent_ino, &parent);
        ino = new_ino;
    }

    // 分配文件描述符 fd
    int fd = -1;
    for (int i = 0; i < NUM_FDESCS; i++) {
        if (!fdesc_array[i].is_used) { fd = i; break; }
    }
    if (fd == -1) return -1;

    // 初始化 fd
    fdesc_array[fd].is_used = 1;
    fdesc_array[fd].ino = ino;
    fdesc_array[fd].access = mode;
    fdesc_array[fd].read_ptr = 0;
    fdesc_array[fd].write_ptr = 0;
    return fd;
}

// 读文件
int do_read(int fd, char *buff, int length)
{
    if (fd < 0 || fd >= NUM_FDESCS || !fdesc_array[fd].is_used) return -1;
    fdesc_t *f = &fdesc_array[fd];
    inode_t inode;
    get_inode(f->ino, &inode);

    // 边界检查
    if (f->read_ptr >= inode.size) return 0;
    if (f->read_ptr + length > inode.size) length = inode.size - f->read_ptr;

    int read = 0; char buf[BLOCK_SIZE];
    while (read < length) {
        uint32_t lb = f->read_ptr / BLOCK_SIZE; // 逻辑块号
        uint32_t offset = f->read_ptr % BLOCK_SIZE; // 块内偏移
        uint32_t copy_len = BLOCK_SIZE - offset;
        if (copy_len > length - read) copy_len = length - read;

        int pb = get_block_addr(&inode, lb, 0); // 获取物理块
        if (pb != 0) {
            sd_read_block(superblock.data_offset + pb, buf);
            memcpy((uint8_t *)(buff + read), (uint8_t *)(buf + offset), copy_len);
        } else memset(buff + read, 0, copy_len); // 空洞文件处理

        f->read_ptr += copy_len;
        read += copy_len;
    }
    return read;
}

// 写文件
int do_write(int fd, char *buff, int length)
{
    if (fd < 0 || fd >= NUM_FDESCS || !fdesc_array[fd].is_used) return -1;
    fdesc_t *f = &fdesc_array[fd];
    inode_t inode;
    get_inode(f->ino, &inode);

    int written = 0; char buf[BLOCK_SIZE];
    while (written < length) {
        uint32_t lb = f->write_ptr / BLOCK_SIZE;
        uint32_t offset = f->write_ptr % BLOCK_SIZE;
        uint32_t copy_len = BLOCK_SIZE - offset;
        if (copy_len > length - written) copy_len = length - written;

        // 获取或分配物理块 (allocate=1)
        int pb = get_block_addr(&inode, lb, 1);
        if (pb < 0) {
            printk("Error: do_write get_block_addr failed. lb=%d\n", lb);
            break;
        }

        // 读取-修改-写入 (RMW) 流程
        sd_read_block(superblock.data_offset + pb, buf);
        memcpy((uint8_t *)(buf + offset), (uint8_t *)(buff + written), copy_len);
        sd_write_block(superblock.data_offset + pb, buf);

        f->write_ptr += copy_len;
        written += copy_len;
    }
    // 更新文件大小
    if (f->write_ptr > inode.size) { inode.size = f->write_ptr; }
    set_inode(f->ino, &inode);
    return written;
}

// 关闭文件
int do_close(int fd)
{
    if (fd < 0 || fd >= NUM_FDESCS) return -1;
    fdesc_array[fd].is_used = 0;
    return 0;
}

// 移动文件读写指针
int do_lseek(int fd, int offset, int whence)
{
    if (fd < 0 || fd >= NUM_FDESCS || !fdesc_array[fd].is_used) return -1;
    fdesc_t *f = &fdesc_array[fd];
    inode_t inode;
    get_inode(f->ino, &inode);

    int new_ptr = f->read_ptr;
    if (whence == SEEK_SET) new_ptr = offset;
    else if (whence == SEEK_CUR) new_ptr += offset;
    else if (whence == SEEK_END) new_ptr = inode.size + offset;

    if (new_ptr < 0) new_ptr = 0;
    f->read_ptr = new_ptr; f->write_ptr = new_ptr;
    return new_ptr;
}

// 创建硬链接
int do_ln(char *src_path, char *dst_path)
{
    // 获取源文件 inode
    int src_ino = lookup_path(src_path);
    if (src_ino < 0) return -1;
    inode_t src_inode;
    get_inode(src_ino, &src_inode);
    if (src_inode.mode == IM_DIR) return -1; // 不能对目录建立硬链接

    // 解析目标路径
    uint32_t parent_ino;
    char name[MAX_FILE_NAME];
    if (get_parent_and_name(dst_path, &parent_ino, name) != 0) return -1;

    // 在目标父目录中增加一项
    inode_t parent;
    get_inode(parent_ino, &parent);
    uint32_t entry_idx = parent.size / sizeof(dentry_t);
    int pb = get_block_addr(&parent, entry_idx / (BLOCK_SIZE / sizeof(dentry_t)), 1);
    if (pb < 0) return -1;

    char buf[BLOCK_SIZE];
    sd_read_block(superblock.data_offset + pb, buf);
    dentry_t *de = (dentry_t *)buf;
    strcpy(de[entry_idx % (BLOCK_SIZE / sizeof(dentry_t))].name, name);
    de[entry_idx % (BLOCK_SIZE / sizeof(dentry_t))].ino = src_ino; // 指向同一个 inode
    sd_write_block(superblock.data_offset + pb, buf);

    parent.size += sizeof(dentry_t);
    set_inode(parent_ino, &parent);

    // 增加源文件的链接计数
    src_inode.nlinks++;
    set_inode(src_ino, &src_inode);
    return 0;
}

// 删除文件或空目录
int do_rm(char *path)
{
    uint32_t parent_ino;
    char name[MAX_FILE_NAME];
    if (get_parent_and_name(path, &parent_ino, name) != 0) return -1;

    inode_t parent;
    get_inode(parent_ino, &parent);
    uint32_t num_dentries = parent.size / sizeof(dentry_t);
    uint32_t per_block = BLOCK_SIZE / sizeof(dentry_t);

    int target_ino = -1;
    int found = 0;
    uint32_t target_lb, target_idx;

    // 1. 在父目录中查找目标文件
    for (uint32_t lb = 0; lb * per_block < num_dentries; lb++) {
        int pb = get_block_addr(&parent, lb, 0);
        char buf[BLOCK_SIZE];
        sd_read_block(superblock.data_offset + pb, buf);
        dentry_t *de = (dentry_t *)buf;
        for (uint32_t j = 0; j < per_block && (lb * per_block + j) < num_dentries; j++) {
            if (strcmp(de[j].name, name) == 0) {
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return -1;
                target_ino = de[j].ino;
                target_lb = lb; target_idx = j;
                found = 1; break;
            }
        }
        if (found) break;
    }
    if (!found) return -1;

    if (dcache_enable) {
        dcache_del(parent_ino, name);
    }

    /* 2. 删除目录项：将最后一项移动到被删除项的位置（覆盖），避免中间出现空洞 */
    uint32_t last_idx = (num_dentries - 1) % per_block;
    uint32_t last_lb = (num_dentries - 1) / per_block;
    if (!(target_lb == last_lb && target_idx == last_idx)) {
        char buf_last[BLOCK_SIZE], buf_target[BLOCK_SIZE];
        int pb_last = get_block_addr(&parent, last_lb, 0);
        int pb_target = get_block_addr(&parent, target_lb, 0);
        
        // 读取最后一个目录项
        sd_read_block(superblock.data_offset + pb_last, buf_last);
        dentry_t *de_last = (dentry_t *)buf_last;
        
        if (pb_last == pb_target) {
            // 如果在同一个块内
            dentry_t *de_target = (dentry_t *)buf_last;
            de_target[target_idx] = de_last[last_idx]; // 覆盖
            sd_write_block(superblock.data_offset + pb_last, buf_last);
        } else {
            // 如果在不同块
            sd_read_block(superblock.data_offset + pb_target, buf_target);
            dentry_t *de_target = (dentry_t *)buf_target;
            de_target[target_idx] = de_last[last_idx]; // 覆盖
            sd_write_block(superblock.data_offset + pb_target, buf_target);
        }
    }
    parent.size -= sizeof(dentry_t);
    
    // 3. 处理目标文件 inode
    if (target_ino != -1) {
        inode_t target;
        get_inode(target_ino, &target);
        // 如果删除的是目录，减少父目录的链接数（因为少了子目录的 ".."）
        if (target.mode == IM_DIR) parent.nlinks--;
        set_inode(parent_ino, &parent);
        
        target.nlinks--;
        // 如果链接数为0，释放实际数据块和 inode
        if (target.nlinks == 0) {
            free_inode_blocks(&target);
            free_bit(superblock.inode_map_offset, target_ino);
        } else set_inode(target_ino, &target);
    }
    return 0;
}
#include "os/debug.h"
#include <os/string.h>
#include <os/fs.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/time.h>
#include <screen.h>
#include <printk.h>

static superblock_t superblock;
static fdesc_t fdesc_array[NUM_FDESCS];

#define FS_START_SEC FS_START_SECTOR

#include <os/lock.h>

#define CACHE_SIZE 128
#define CACHE_POLICY_WRITE_BACK 1
#define CACHE_POLICY_WRITE_THROUGH 0

int page_cache_policy = CACHE_POLICY_WRITE_BACK;
int write_back_freq = 30; // seconds

typedef struct page_cache_entry {
    uint32_t block_id;
    uint8_t valid;
    uint8_t dirty;
    uint32_t last_access;
    uint8_t data[BLOCK_SIZE];
} page_cache_entry_t;

static page_cache_entry_t page_cache[CACHE_SIZE];

/* Dentry Cache */
#define DCACHE_SIZE 128
typedef struct dcache_entry {
    uint32_t parent_ino;
    uint32_t ino;
    char name[MAX_FILE_NAME];
    uint8_t valid;
} dcache_entry_t;

static dcache_entry_t dcache[DCACHE_SIZE];

static uint32_t current_access_time = 0;
static spin_lock_t cache_lock = {UNLOCKED};
int dcache_enable = 1;

static void flush_block_unlocked(int index)
{
    if (page_cache[index].valid && page_cache[index].dirty) {
        // klog("Performing block flushing...\n");
        bios_sd_write(kva2pa((uintptr_t)page_cache[index].data),
                      SECTOR_PER_BLOCK,
                      FS_START_SEC + page_cache[index].block_id * SECTOR_PER_BLOCK);
        page_cache[index].dirty = 0;
    }
}

static int get_cache_index(uint32_t block_id)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (page_cache[i].valid && page_cache[i].block_id == block_id) {
            return i;
        }
    }
    return -1;
}

static int get_victim_index(void)
{
    int victim = -1;
    uint32_t min_time = 0xFFFFFFFF;

    // First pass: find invalid entry
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!page_cache[i].valid) return i;
    }

    // Second pass: find LRU
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (page_cache[i].last_access < min_time) {
            min_time = page_cache[i].last_access;
            victim = i;
        }
    }
    return victim;
}

static void sd_read_block(uint32_t block_id, void *buf)
{
    spin_lock_acquire(&cache_lock);
    int index = get_cache_index(block_id);
    if (index != -1) {
        // Cache Hit
        page_cache[index].last_access = ++current_access_time;
        memcpy(buf, page_cache[index].data, BLOCK_SIZE);
        spin_lock_release(&cache_lock);
        return;
    }

    // Cache Miss
    int victim = get_victim_index();
    flush_block_unlocked(victim);

    page_cache[victim].block_id = block_id;
    page_cache[victim].valid = 1;
    page_cache[victim].dirty = 0;
    page_cache[victim].last_access = ++current_access_time;

    bios_sd_read(kva2pa((uintptr_t)page_cache[victim].data),
                 SECTOR_PER_BLOCK,
                 FS_START_SEC + block_id * SECTOR_PER_BLOCK);

    memcpy(buf, page_cache[victim].data, BLOCK_SIZE);
    spin_lock_release(&cache_lock);
}

static void sd_write_block(uint32_t block_id, void *buf)
{
    spin_lock_acquire(&cache_lock);
    int index = get_cache_index(block_id);
    if (index == -1) {
        // Cache Miss
        int victim = get_victim_index();
        flush_block_unlocked(victim);
        index = victim;

        page_cache[index].block_id = block_id;
        page_cache[index].valid = 1;
        // No need to read from disk since we are overwriting the whole block
    }

    // Update Cache
    memcpy(page_cache[index].data, buf, BLOCK_SIZE);
    page_cache[index].last_access = ++current_access_time;

    if (page_cache_policy == CACHE_POLICY_WRITE_THROUGH) {
        page_cache[index].dirty = 0;
        bios_sd_write(kva2pa((uintptr_t)page_cache[index].data),
                      SECTOR_PER_BLOCK,
                      FS_START_SEC + block_id * SECTOR_PER_BLOCK);
    } else {
        page_cache[index].dirty = 1;
    }
    spin_lock_release(&cache_lock);
}

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

void do_fs_sync(void)
{
    // 1. Update Config
    int fd = do_open("/proc/sys/vm", O_RDONLY);
    if (fd >= 0) {
        char buf[256]; // Increased buffer size to ensure we catch all commands
        int len = do_read(fd, buf, 255);
        if (len > 0) {
            buf[len] = '\0';

            // --- Parse page_cache_policy ---
            char *p_policy = k_strstr(buf, "page_cache_policy = ");
            if (p_policy) {
                int new_policy = 0;
                char *p = p_policy + 20;
                while (*p >= '0' && *p <= '9') {
                    new_policy = new_policy * 10 + (*p - '0');
                    p++;
                }
                // If switching from Write-Back to Write-Through, flush immediately
                if (page_cache_policy == CACHE_POLICY_WRITE_BACK && new_policy == CACHE_POLICY_WRITE_THROUGH) {
                    spin_lock_acquire(&cache_lock);
                    for (int i = 0; i < CACHE_SIZE; i++) flush_block_unlocked(i);
                    spin_lock_release(&cache_lock);
                }
                page_cache_policy = new_policy;
            }

            // --- Parse write_back_freq ---
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

            // --- Parse dcache_enable ---
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

            // --- Parse clear_cache ---
            // This is crucial for the "Cold Read" test
            if (k_strstr(buf, "clear_cache = 1")) {
                spin_lock_acquire(&cache_lock);
                for (int i = 0; i < CACHE_SIZE; i++) {
                    // Flush dirty blocks before clearing to avoid data loss
                    flush_block_unlocked(i);
                    // Invalidate the entry
                    page_cache[i].valid = 0;
                }
                spin_lock_release(&cache_lock);

                // Also clear Dcache
                for (int i = 0; i < DCACHE_SIZE; i++) {
                    dcache[i].valid = 0;
                }
            }
        }
        do_close(fd);
    }

    // 2. Periodic Flush (for Write-Back policy)
    if (page_cache_policy == CACHE_POLICY_WRITE_BACK) {
        spin_lock_acquire(&cache_lock);
        for (int i = 0; i < CACHE_SIZE; i++) {
            flush_block_unlocked(i);
        }
        spin_lock_release(&cache_lock);
    }
}

static void clear_blocks(uint32_t start_block, uint32_t num_blocks)
{
    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    for (uint32_t i = 0; i < num_blocks; i++) {
        sd_write_block(start_block + i, buf);
    }
}

static int alloc_bit(uint32_t map_offset, uint32_t map_count, uint32_t total_count)
{
    uint8_t buf[BLOCK_SIZE];
    for (uint32_t i = 0; i < map_count; i++) {
        sd_read_block(map_offset + i, buf);
        for (uint32_t j = 0; j < BLOCK_SIZE; j++) {
            if (buf[j] != 0xff) {
                for (int k = 0; k < 8; k++) {
                    if (!((buf[j] >> k) & 1)) {
                        uint32_t index = i * BLOCK_SIZE * 8 + j * 8 + k;
                        if (index >= total_count) return -1;
                        buf[j] |= (1 << k);
                        sd_write_block(map_offset + i, buf);
                        return index;
                    }
                }
            }
        }
    }
    return -1;
}

static void free_bit(uint32_t map_offset, uint32_t index)
{
    uint8_t buf[BLOCK_SIZE];
    uint32_t block_idx = index / (BLOCK_SIZE * 8);
    uint32_t byte_idx = (index % (BLOCK_SIZE * 8)) / 8;
    uint32_t bit_idx = index % 8;

    sd_read_block(map_offset + block_idx, buf);
    buf[byte_idx] &= ~(1 << bit_idx);
    sd_write_block(map_offset + block_idx, buf);
}

static void get_inode(uint32_t ino, inode_t *inode)
{
    char buf[BLOCK_SIZE];
    uint32_t block_idx = (ino * sizeof(inode_t)) / BLOCK_SIZE;
    uint32_t offset = (ino * sizeof(inode_t)) % BLOCK_SIZE;
    sd_read_block(superblock.inode_table_offset + block_idx, buf);
    memcpy((uint8_t *)inode, (uint8_t *)(buf + offset), sizeof(inode_t));
}

static void set_inode(uint32_t ino, inode_t *inode)
{
    char buf[BLOCK_SIZE];
    uint32_t block_idx = (ino * sizeof(inode_t)) / BLOCK_SIZE;
    uint32_t offset = (ino * sizeof(inode_t)) % BLOCK_SIZE;
    sd_read_block(superblock.inode_table_offset + block_idx, buf);
    memcpy((uint8_t *)(buf + offset), (uint8_t *)inode, sizeof(inode_t));
    sd_write_block(superblock.inode_table_offset + block_idx, buf);
}

static int get_block_addr(inode_t *inode, uint32_t logical_block, int allocate)
{
    if (logical_block < NDIRECT) {
        if (inode->direct_ptrs[logical_block] == 0) {
            if (!allocate) return 0;
            int new_blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
            if (new_blk < 0) return -1;
            inode->direct_ptrs[logical_block] = new_blk;
            clear_blocks(superblock.data_offset + new_blk, 1);
            return new_blk;
        }
        return inode->direct_ptrs[logical_block];
    }

    logical_block -= NDIRECT;

    /* Single Indirect */
    if (logical_block < INDIRECT_BLOCK_COUNT) {
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
        if (ptrs[logical_block] == 0) {
            if (!allocate) return 0;
            int new_blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
            if (new_blk < 0) return -1;
            ptrs[logical_block] = new_blk;
            clear_blocks(superblock.data_offset + new_blk, 1);
            sd_write_block(superblock.data_offset + inode->indirect_ptr[0], buf);
            return new_blk;
        }
        return ptrs[logical_block];
    }

    logical_block -= INDIRECT_BLOCK_COUNT;

    /* Double Indirect */
    if (logical_block < INDIRECT_BLOCK_COUNT * INDIRECT_BLOCK_COUNT) {
        if (inode->indirect_ptr[1] == 0) {
            if (!allocate) return 0;
            int new_blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
            if (new_blk < 0) return -1;
            inode->indirect_ptr[1] = new_blk;
            clear_blocks(superblock.data_offset + new_blk, 1);
        }

        uint32_t l1_idx = logical_block / INDIRECT_BLOCK_COUNT;
        uint32_t l2_idx = logical_block % INDIRECT_BLOCK_COUNT;

        char buf[BLOCK_SIZE];
        sd_read_block(superblock.data_offset + inode->indirect_ptr[1], buf);
        uint32_t *l1_ptrs = (uint32_t *)buf;

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

    return -1;
}

static void free_inode_blocks(inode_t *inode)
{
    /* Direct */
    for (int i = 0; i < NDIRECT; i++) {
        if (inode->direct_ptrs[i] != 0) {
            free_bit(superblock.block_map_offset, inode->direct_ptrs[i]);
            inode->direct_ptrs[i] = 0;
        }
    }
    /* Single Indirect */
    if (inode->indirect_ptr[0] != 0) {
        char buf[BLOCK_SIZE];
        sd_read_block(superblock.data_offset + inode->indirect_ptr[0], buf);
        uint32_t *ptrs = (uint32_t *)buf;
        for (int i = 0; i < INDIRECT_BLOCK_COUNT; i++) {
            if (ptrs[i] != 0) free_bit(superblock.block_map_offset, ptrs[i]);
        }
        free_bit(superblock.block_map_offset, inode->indirect_ptr[0]);
        inode->indirect_ptr[0] = 0;
    }
    /* Double Indirect */
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

static uint32_t dcache_hash(uint32_t parent_ino, char *name)
{
    uint32_t hash = parent_ino;
    while (*name) hash = (hash << 5) + *name++;
    return hash % DCACHE_SIZE;
}

static void dcache_add(uint32_t parent_ino, char *name, uint32_t ino)
{
    uint32_t idx = dcache_hash(parent_ino, name);
    dcache[idx].parent_ino = parent_ino;
    dcache[idx].ino = ino;
    strcpy(dcache[idx].name, name);
    dcache[idx].valid = 1;
}

static int dcache_lookup(uint32_t parent_ino, char *name)
{
    uint32_t idx = dcache_hash(parent_ino, name);
    if (dcache[idx].valid && dcache[idx].parent_ino == parent_ino && strcmp(dcache[idx].name, name) == 0) {
        return dcache[idx].ino;
    }
    return -1;
}

static void dcache_del(uint32_t parent_ino, char *name)
{
    uint32_t idx = dcache_hash(parent_ino, name);
    if (dcache[idx].valid && dcache[idx].parent_ino == parent_ino && strcmp(dcache[idx].name, name) == 0) {
        dcache[idx].valid = 0;
    }
}

static int find_entry(uint32_t dir_ino, char *name, dentry_t *entry)
{
    // 1. Look in Dcache (Only if enabled)
    if (dcache_enable) {
        int cached_ino = dcache_lookup(dir_ino, name);
        if (cached_ino != -1) {
            entry->ino = cached_ino;
            strcpy(entry->name, name);
            return 0;
        }
    }

    // 2. Look in Disk (Directory Blocks)
    inode_t inode;
    get_inode(dir_ino, &inode);
    if (inode.mode != IM_DIR) return -1;

    char buf[BLOCK_SIZE];
    uint32_t num_dentries = inode.size / sizeof(dentry_t);
    uint32_t dentries_per_block = BLOCK_SIZE / sizeof(dentry_t);

    for (uint32_t lb = 0; num_dentries > 0; lb++) {
        int pb = get_block_addr(&inode, lb, 0);
        if (pb == 0) continue;
        sd_read_block(superblock.data_offset + pb, buf);
        dentry_t *dentries = (dentry_t *)buf;
        for (uint32_t j = 0; j < dentries_per_block && num_dentries > 0; j++, num_dentries--) {
            if (strcmp(dentries[j].name, name) == 0) {
                memcpy((uint8_t *)entry, (uint8_t *)&dentries[j], sizeof(dentry_t));

                // 3. Add to Dcache (Only if enabled)
                if (dcache_enable) {
                    dcache_add(dir_ino, name, dentries[j].ino);
                }
                return 0;
            }
        }
    }
    return -1;
}

static int lookup_path(char *path)
{
    uint32_t curr_ino = (path[0] == '/') ? superblock.root_ino : current_running->cwd_ino;

    char name[MAX_FILE_NAME];
    int p = (path[0] == '/') ? 1 : 0;

    while (path[p]) {
        int q = 0;
        while (path[p] && path[p] != '/') {
            if (q < MAX_FILE_NAME - 1) name[q++] = path[p];
            p++;
        }
        name[q] = '\0';
        while (path[p] == '/') p++;

        if (q > 0) {
            dentry_t entry;
            if (find_entry(curr_ino, name, &entry) != 0) return -1;
            curr_ino = entry.ino;
        }
    }
    return curr_ino;
}

static int get_parent_and_name(char *path, uint32_t *parent_ino, char *name)
{
    char parent_path[128];
    int len = strlen(path);
    int last_slash = -1;
    for (int i = 0; i < len; i++) if (path[i] == '/') last_slash = i;

    if (last_slash == -1) {
        *parent_ino = current_running->cwd_ino;
        strcpy(name, path);
    } else {
        if (last_slash == 0) {
            *parent_ino = superblock.root_ino;
        } else {
            memcpy((uint8_t *)parent_path, (uint8_t *)path, last_slash);
            parent_path[last_slash] = '\0';
            *parent_ino = lookup_path(parent_path);
        }
        strcpy(name, path + last_slash + 1);
    }
    return (*parent_ino == (uint32_t)-1) ? -1 : 0;
}

void init_fs(void)
{
    char buf[BLOCK_SIZE];
    sd_read_block(0, buf);
    memcpy((uint8_t *)&superblock, (uint8_t *)buf, sizeof(superblock_t));

    if (superblock.magic != SUPERBLOCK_MAGIC) {
        printk("Magic number 0x%x not match 0x%x, mkfs...\n", superblock.magic, SUPERBLOCK_MAGIC);
        do_mkfs();
        sd_read_block(0, buf);
        memcpy((uint8_t *)&superblock, (uint8_t *)buf, sizeof(superblock_t));
        printk("mkfs succeeded!\n");
    }

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

    for (int i = 0; i < NUM_FDESCS; i++) fdesc_array[i].is_used = 0;
}

int do_mkfs(void)
{
    /* Invalidate Page Cache */
    spin_lock_acquire(&cache_lock);
    for (int i = 0; i < CACHE_SIZE; i++) {
        page_cache[i].valid = 0;
        page_cache[i].dirty = 0;
    }
    spin_lock_release(&cache_lock);

    /* Invalidate Directory Entry Cache (Dcache) */
    for (int i = 0; i < DCACHE_SIZE; i++) {
        dcache[i].valid = 0;
    }

    uint32_t fs_size_bytes = 512 * 1024 * 1024;
    uint32_t fs_size_blocks = fs_size_bytes / BLOCK_SIZE;
    uint32_t fs_size_sectors = fs_size_bytes / SECTOR_SIZE;

    uint32_t block_map_size = 4;
    uint32_t inode_map_size = 1;
    uint32_t inode_table_size = (4096 * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;

    uint32_t sb_block = 0;
    uint32_t bmap_block = sb_block + 1;
    uint32_t imap_block = bmap_block + block_map_size;
    uint32_t itable_block = imap_block + inode_map_size;
    uint32_t data_block = itable_block + inode_table_size;

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
    superblock.root_ino = 1;

    clear_blocks(0, data_block);

    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    memcpy((uint8_t *)buf, (uint8_t *)&superblock, sizeof(superblock_t));
    sd_write_block(0, buf);

    memset(buf, 0, BLOCK_SIZE);
    buf[0] |= (1 << 0) | (1 << 1); // Reserve inode 0 and 1
    sd_write_block(imap_block, buf);

    memset(buf, 0, BLOCK_SIZE);
    buf[0] |= (1 << 0) | (1 << 1);
    sd_write_block(bmap_block, buf);

    inode_t root;
    memset(&root, 0, sizeof(inode_t));
    root.ino = 1; root.mode = IM_DIR; root.nlinks = 2;
    root.size = 2 * sizeof(dentry_t);
    root.direct_ptrs[0] = 1;
    set_inode(1, &root);

    memset(buf, 0, BLOCK_SIZE);
    dentry_t *de = (dentry_t *)buf;
    strcpy(de[0].name, "."); de[0].ino = 1;
    strcpy(de[1].name, ".."); de[1].ino = 1;
    sd_write_block(data_block + 1, buf);

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

int do_statfs(void)
{
    printk("[FS] Superblock Info:\n");
    printk("  Magic: 0x%lx, Size: %d sectors\n", (uint64_t)superblock.magic, superblock.size_sectors);
    printk("  Data Area: Start %d, Count %d\n", superblock.data_offset, superblock.data_count);
    return 0;
}

int do_cd(char *path)
{
    int ino = lookup_path(path);
    if (ino < 0) return -1;
    inode_t inode;
    get_inode(ino, &inode);
    if (inode.mode != IM_DIR) return -1;
    current_running->cwd_ino = ino;
    return 0;
}

int do_ls(char *path, int option)
{
    int ino = (path == NULL || path[0] == '\0') ? current_running->cwd_ino : lookup_path(path);
    if (ino < 0) return -1;

    inode_t inode;
    get_inode(ino, &inode);
    if (inode.mode != IM_DIR) return -1;

    char buf[BLOCK_SIZE];
    uint32_t num_dentries = inode.size / sizeof(dentry_t);
    uint32_t per_block = BLOCK_SIZE / sizeof(dentry_t);

    for (uint32_t lb = 0; num_dentries > 0; lb++) {
        int pb = get_block_addr(&inode, lb, 0);
        if (pb == 0) continue;
        sd_read_block(superblock.data_offset + pb, buf);
        dentry_t *de = (dentry_t *)buf;
        for (int j = 0; j < per_block && num_dentries > 0; j++, num_dentries--) {
            inode_t target;
            get_inode(de[j].ino, &target);
            if (option) {
                printk("[%d] %s  links:%d  size:%d  ino:%d  ",
                       target.mode, (target.mode == IM_DIR ? "DIR" : "FILE"),
                       target.nlinks, target.size, de[j].ino);
                if (target.mode == IM_DIR) bios_putstr(ANSI_FG_BLUE);
                if (target.mode == IM_DIR) bios_putstr(" ");
                else bios_putstr(" ");
                bios_putstr(de[j].name);
                if (target.mode == IM_DIR) bios_putstr("/");
                bios_putstr(ANSI_NONE);
                bios_putstr("  ");
                printk("\n");
            } else {
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

int do_mkdir(char *path)
{
    uint32_t parent_ino;
    char name[MAX_FILE_NAME];
    if (get_parent_and_name(path, &parent_ino, name) != 0) return -1;

    dentry_t tmp;
    if (find_entry(parent_ino, name, &tmp) == 0) return -1;

    int ino = alloc_bit(superblock.inode_map_offset, superblock.inode_map_count, superblock.inode_count);
    int blk = alloc_bit(superblock.block_map_offset, superblock.block_map_count, superblock.data_count);
    if (ino < 0 || blk < 0) return -1;

    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    new_inode.ino = ino; new_inode.mode = IM_DIR; new_inode.nlinks = 2;
    new_inode.size = 2 * sizeof(dentry_t);
    new_inode.direct_ptrs[0] = blk;
    set_inode(ino, &new_inode);

    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    dentry_t *de = (dentry_t *)buf;
    strcpy(de[0].name, "."); de[0].ino = ino;
    strcpy(de[1].name, ".."); de[1].ino = parent_ino;
    sd_write_block(superblock.data_offset + blk, buf);

    inode_t parent;
    get_inode(parent_ino, &parent);
    uint32_t dentries_per_block = BLOCK_SIZE / sizeof(dentry_t);
    uint32_t entry_idx = parent.size / sizeof(dentry_t);
    uint32_t blk_idx = entry_idx / dentries_per_block;
    uint32_t off_idx = entry_idx % dentries_per_block;

    int pb = get_block_addr(&parent, blk_idx, 1);
    if (pb < 0) return -1;

    sd_read_block(superblock.data_offset + pb, buf);
    de = (dentry_t *)buf;
    strcpy(de[off_idx].name, name);
    de[off_idx].ino = ino;
    sd_write_block(superblock.data_offset + pb, buf);

    parent.size += sizeof(dentry_t);
    parent.nlinks++; // Parent now has a subdirectory pointing to it via ".."
    set_inode(parent_ino, &parent);

    return 0;
}

int do_rmdir(char *path)
{
    int ino = lookup_path(path);
    if (ino < 0) return -1;
    inode_t inode;
    get_inode(ino, &inode);
    if (inode.mode != IM_DIR) return -1;
    if (inode.size > 2 * sizeof(dentry_t)) return -1; /* Not empty */
    return do_rm(path);
}

int do_open(char *path, int mode)
{
    int ino = lookup_path(path);
    if (ino < 0) {
        if (mode == O_RDONLY) return -1;
        uint32_t parent_ino;
        char name[MAX_FILE_NAME];
        if (get_parent_and_name(path, &parent_ino, name) != 0) return -1;

        int new_ino = alloc_bit(superblock.inode_map_offset, superblock.inode_map_count, superblock.inode_count);
        if (new_ino < 0) return -1;

        inode_t new_inode;
        memset(&new_inode, 0, sizeof(inode_t));
        new_inode.ino = new_ino; new_inode.mode = IM_REG; new_inode.nlinks = 1;
        new_inode.size = 0;
        set_inode(new_ino, &new_inode);

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

    int fd = -1;
    for (int i = 0; i < NUM_FDESCS; i++) {
        if (!fdesc_array[i].is_used) { fd = i; break; }
    }
    if (fd == -1) return -1;

    fdesc_array[fd].is_used = 1;
    fdesc_array[fd].ino = ino;
    fdesc_array[fd].access = mode;
    fdesc_array[fd].read_ptr = 0;
    fdesc_array[fd].write_ptr = 0;
    return fd;
}

int do_read(int fd, char *buff, int length)
{
    if (fd < 0 || fd >= NUM_FDESCS || !fdesc_array[fd].is_used) return -1;
    fdesc_t *f = &fdesc_array[fd];
    inode_t inode;
    get_inode(f->ino, &inode);
    if (f->read_ptr >= inode.size) return 0;
    if (f->read_ptr + length > inode.size) length = inode.size - f->read_ptr;
    int read = 0; char buf[BLOCK_SIZE];
    while (read < length) {
        uint32_t lb = f->read_ptr / BLOCK_SIZE;
        uint32_t offset = f->read_ptr % BLOCK_SIZE;
        uint32_t copy_len = BLOCK_SIZE - offset;
        if (copy_len > length - read) copy_len = length - read;
        int pb = get_block_addr(&inode, lb, 0);
        if (pb != 0) {
            sd_read_block(superblock.data_offset + pb, buf);
            memcpy((uint8_t *)(buff + read), (uint8_t *)(buf + offset), copy_len);
        } else memset(buff + read, 0, copy_len);
        f->read_ptr += copy_len;
        read += copy_len;
    }
    return read;
}

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
        int pb = get_block_addr(&inode, lb, 1);
        if (pb < 0) {
            printk("Error: do_write get_block_addr failed. lb=%d\n", lb);
            break;
        }
        sd_read_block(superblock.data_offset + pb, buf);
        memcpy((uint8_t *)(buf + offset), (uint8_t *)(buff + written), copy_len);
        sd_write_block(superblock.data_offset + pb, buf);
        f->write_ptr += copy_len;
        written += copy_len;
    }
    if (f->write_ptr > inode.size) { inode.size = f->write_ptr; }
    set_inode(f->ino, &inode);
    return written;
}

int do_close(int fd)
{
    if (fd < 0 || fd >= NUM_FDESCS) return -1;
    fdesc_array[fd].is_used = 0;
    return 0;
}

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

int do_ln(char *src_path, char *dst_path)
{
    int src_ino = lookup_path(src_path);
    if (src_ino < 0) return -1;
    inode_t src_inode;
    get_inode(src_ino, &src_inode);
    if (src_inode.mode == IM_DIR) return -1;

    uint32_t parent_ino;
    char name[MAX_FILE_NAME];
    if (get_parent_and_name(dst_path, &parent_ino, name) != 0) return -1;

    inode_t parent;
    get_inode(parent_ino, &parent);
    uint32_t entry_idx = parent.size / sizeof(dentry_t);
    int pb = get_block_addr(&parent, entry_idx / (BLOCK_SIZE / sizeof(dentry_t)), 1);
    if (pb < 0) return -1;

    char buf[BLOCK_SIZE];
    sd_read_block(superblock.data_offset + pb, buf);
    dentry_t *de = (dentry_t *)buf;
    strcpy(de[entry_idx % (BLOCK_SIZE / sizeof(dentry_t))].name, name);
    de[entry_idx % (BLOCK_SIZE / sizeof(dentry_t))].ino = src_ino;
    sd_write_block(superblock.data_offset + pb, buf);

    parent.size += sizeof(dentry_t);
    set_inode(parent_ino, &parent);

    src_inode.nlinks++;
    set_inode(src_ino, &src_inode);
    return 0;
}

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

    /* Swap with last dentry */
    uint32_t last_idx = (num_dentries - 1) % per_block;
    uint32_t last_lb = (num_dentries - 1) / per_block;
    if (!(target_lb == last_lb && target_idx == last_idx)) {
        char buf_last[BLOCK_SIZE], buf_target[BLOCK_SIZE];
        int pb_last = get_block_addr(&parent, last_lb, 0);
        int pb_target = get_block_addr(&parent, target_lb, 0);
        sd_read_block(superblock.data_offset + pb_last, buf_last);
        dentry_t *de_last = (dentry_t *)buf_last;
        if (pb_last == pb_target) {
            dentry_t *de_target = (dentry_t *)buf_last;
            de_target[target_idx] = de_last[last_idx];
            sd_write_block(superblock.data_offset + pb_last, buf_last);
        } else {
            sd_read_block(superblock.data_offset + pb_target, buf_target);
            dentry_t *de_target = (dentry_t *)buf_target;
            de_target[target_idx] = de_last[last_idx];
            sd_write_block(superblock.data_offset + pb_target, buf_target);
        }
    }
    parent.size -= sizeof(dentry_t);
    if (target_ino != -1) {
        inode_t target;
        get_inode(target_ino, &target);
        if (target.mode == IM_DIR) parent.nlinks--;
        set_inode(parent_ino, &parent);
        target.nlinks--;
        if (target.nlinks == 0) {
            free_inode_blocks(&target);
            free_bit(superblock.inode_map_offset, target_ino);
        } else set_inode(target_ino, &target);
    }
    return 0;
}
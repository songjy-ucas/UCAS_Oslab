#ifndef __INCLUDE_OS_FS_H__
#define __INCLUDE_OS_FS_H__

#include <type.h>

/* 文件系统相关的宏定义 */
#define SUPERBLOCK_MAGIC 0xDF4C4459
#define NUM_FDESCS 16

#define SECTOR_SIZE 512
#define BLOCK_SIZE  4096 /* 每个块 4KB */
#define SECTOR_PER_BLOCK (BLOCK_SIZE / SECTOR_SIZE)

/* 
 * 文件系统布局 (起始于 FS_START_SECTOR):
 * [ 超级块 (1 block) ] [ 块位图 ] [ Inode位图 ] [ Inode表 ] [ 数据块 ]
 */
/* 起始于 512MB: 512 * 1024 * 1024 / 512 = 1048576 */
#define FS_START_SECTOR 1048576 

#define O_RDONLY 1  /* 只读打开 */
#define O_WRONLY 2  /* 只写打开 */
#define O_RDWR   3  /* 读写打开 */

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define MAX_FILE_NAME 24

/* Inode 类型 */
#define IM_REG  0 /* 常规文件 */
#define IM_DIR  1 /* 目录 */

/* 文件系统的数据结构 */
typedef struct superblock {
    uint32_t magic;
    uint32_t fs_start_sector;
    uint32_t size_sectors;    /* 文件系统总扇区数 */
    
    uint32_t block_map_offset; /* 块位图相对于文件系统起始位置的偏移（块数） */
    uint32_t block_map_count;  /* 块位图占用的块数 */

    uint32_t inode_map_offset; /* Inode位图相对于文件系统起始位置的偏移（块数） */
    uint32_t inode_map_count;  /* Inode位图占用的块数 */

    uint32_t inode_table_offset; /* Inode表相对于文件系统起始位置的偏移（块数） */
    uint32_t inode_table_count;  /* Inode表占用的块数 */

    uint32_t data_offset;      /* 数据区域相对于文件系统起始位置的偏移（块数） */
    uint32_t data_count;       /* 数据块的数量 */

    uint32_t inode_count;      /* Inode 总数 */
    uint32_t block_count;      /* 块总数 */

    uint32_t root_ino;         /* 根目录的 Inode 号 */
} superblock_t;

typedef struct dentry {
    char name[MAX_FILE_NAME];
    uint32_t ino;
    uint32_t pad;
} dentry_t;

/* Inode 中的直接指针数量 */
#define NDIRECT 12
/* 一个 4KB 块中的指针数量 (4096 / 4 = 1024) */
#define INDIRECT_BLOCK_COUNT (BLOCK_SIZE / sizeof(uint32_t))

typedef struct inode { 
    uint32_t ino;          /* Inode 号 */
    uint32_t mode;         /* IM_REG 或 IM_DIR */
    uint32_t access;       /* 权限 (rwx) */
    uint32_t nlinks;       /* 硬链接计数 */
    uint32_t size;         /* 文件大小 (字节) */
    uint32_t direct_ptrs[NDIRECT]; /* 直接数据块指针 */
    uint32_t indirect_ptr[3];      /* 0: 一级间接, 1: 二级间接, 2: 三级间接 */
    uint32_t pad[12];       /* 填充以对齐或保留空间 */
} inode_t;

typedef struct fdesc {
    uint32_t ino;          /* 该描述符指向的 Inode 号 */
    uint32_t read_ptr;     /* 当前读偏移量 */
    uint32_t write_ptr;    /* 当前写偏移量 */
    uint32_t access;       /* 访问模式 (O_RDONLY 等) */
    uint32_t is_used;      /* 该描述符是否正在使用 */
} fdesc_t;

/* 文件系统函数声明 */
extern void init_fs(void);
extern int do_mkfs(void);
extern int do_statfs(void);
extern int do_cd(char *path);
extern int do_mkdir(char *path);
extern int do_rmdir(char *path);
extern int do_ls(char *path, int option);
extern int do_open(char *path, int mode);
extern int do_read(int fd, char *buff, int length);
extern int do_write(int fd, char *buff, int length);
extern int do_close(int fd);
extern int do_ln(char *src_path, char *dst_path);
extern int do_rm(char *path);
extern int do_lseek(int fd, int offset, int whence);
extern void check_write_back(void);
extern void do_fs_sync(void);

#endif
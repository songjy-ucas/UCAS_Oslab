#ifndef __INCLUDE_OS_FS_H__
#define __INCLUDE_OS_FS_H__

#include <type.h>

/* macros of file system */
#define SUPERBLOCK_MAGIC 0xDF4C4459
#define NUM_FDESCS 16

#define SECTOR_SIZE 512
#define BLOCK_SIZE  4096 /* 4KB per block */
#define SECTOR_PER_BLOCK (BLOCK_SIZE / SECTOR_SIZE)

/* 
 * File System Layout (starting at FS_START_SECTOR):
 * [ Superblock (1 block) ] [ Block Map ] [ Inode Map ] [ Inode Table ] [ Data Blocks ]
 */
/* Start at 512MB: 512 * 1024 * 1024 / 512 = 1048576 */
#define FS_START_SECTOR 1048576 

#define O_RDONLY 1  /* read only open */
#define O_WRONLY 2  /* write only open */
#define O_RDWR   3  /* read/write open */

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define MAX_FILE_NAME 24

/* Inode types */
#define IM_REG  0 /* Regular file */
#define IM_DIR  1 /* Directory */

/* data structures of file system */
typedef struct superblock {
    uint32_t magic;
    uint32_t fs_start_sector;
    uint32_t size_sectors;    /* Total size of FS in sectors */
    
    uint32_t block_map_offset; /* Offset in blocks from start of FS */
    uint32_t block_map_count;  /* Number of blocks occupied by block map */

    uint32_t inode_map_offset; /* Offset in blocks from start of FS */
    uint32_t inode_map_count;  /* Number of blocks occupied by inode map */

    uint32_t inode_table_offset; /* Offset in blocks from start of FS */
    uint32_t inode_table_count;  /* Number of blocks occupied by inode table */

    uint32_t data_offset;      /* Offset in blocks from start of FS */
    uint32_t data_count;       /* Number of data blocks */

    uint32_t inode_count;      /* Total number of inodes */
    uint32_t block_count;      /* Total number of blocks */

    uint32_t root_ino;         /* Inode number of root directory */
} superblock_t;

typedef struct dentry {
    char name[MAX_FILE_NAME];
    uint32_t ino;
    uint32_t pad;
} dentry_t;

/* Number of direct pointers in inode */
#define NDIRECT 12
/* Number of pointers in a 4KB block (4096 / 4 = 1024) */
#define INDIRECT_BLOCK_COUNT (BLOCK_SIZE / sizeof(uint32_t))

typedef struct inode { 
    uint32_t ino;          /* Inode number */
    uint32_t mode;         /* IM_REG or IM_DIR */
    uint32_t access;       /* Permissions (rwx) */
    uint32_t nlinks;       /* Number of hard links */
    uint32_t size;         /* File size in bytes */
    uint32_t direct_ptrs[NDIRECT]; /* Direct data blocks */
    uint32_t indirect_ptr[3];      /* 0: single, 1: double, 2: triple indirect */
    uint32_t pad[12];       /* Padding to align or reserve space */
} inode_t;

typedef struct fdesc {
    uint32_t ino;          /* The inode number this fd points to */
    uint32_t read_ptr;     /* Current read offset */
    uint32_t write_ptr;    /* Current write offset */
    uint32_t access;       /* Access mode (O_RDONLY, etc.) */
    uint32_t is_used;      /* Whether this descriptor is in use */
} fdesc_t;

/* fs function declarations */
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
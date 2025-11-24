#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_FILE "./image"
#define ARGS "[--extended] [--vm] <bootblock> <executable-file> ..."

#define SECTOR_SIZE 512
#define BOOT_LOADER_SIG_OFFSET 0x1fe // Bootloader 签名位置
#define OS_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 2) // OS 大小信息
#define TASK_NUM_LOC (BOOT_LOADER_SIG_OFFSET - 4) // 用户程序数量
#define TASK_INFO_START_LOC (BOOT_LOADER_SIG_OFFSET - 6) // task_info数组的起始扇区
#define BATCH_INFO_START_LOC (BOOT_LOADER_SIG_OFFSET - 8)
#define BOOT_LOADER_SIG_1 0x55
#define BOOT_LOADER_SIG_2 0xaa

#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0)) // 计算字节数对应的扇区数

/* TODO: [p1-task4] design your own task_info_t */
#define TASK_NAME_LEN    32 // 定义任务名的最大长度
typedef struct {
    char name[TASK_NAME_LEN];
    uint32_t offset;
    uint32_t size;
    uint64_t entry_point;
} task_info_t;

#define TASK_MAXNUM 16
static task_info_t taskinfo[TASK_MAXNUM];

//批处理任务
#define BATCH_TASK_NUM   4
const char *batch_task_names[BATCH_TASK_NUM] = {
    "prog1", "prog2", "prog3", "prog4"
};

/* structure to store command line options */
static struct {
    int vm;
    int extended;
} options;

static int g_task_info_offset = 0; // 全局变量，记录 task_info 数组在镜像文件中的偏移位置

/* prototypes of local functions */
static void create_image(int nfiles, char *files[]);
static void error(char *fmt, ...);
static void read_ehdr(Elf64_Ehdr *ehdr, FILE *fp);
static void read_phdr(Elf64_Phdr *phdr, FILE *fp, int ph, Elf64_Ehdr ehdr);
static uint64_t get_entrypoint(Elf64_Ehdr ehdr);
static uint32_t get_filesz(Elf64_Phdr phdr);
static uint32_t get_memsz(Elf64_Phdr phdr);
static void write_segment(Elf64_Phdr phdr, FILE *fp, FILE *img, int *phyaddr);
static void write_padding(FILE *img, int *phyaddr, int new_phyaddr);
static void write_img_info(int os_size_bytes, short tasknum, int task_info_offset, int batch_file_offset, FILE *img);
static char* get_filename(char* path); // 从路径中提取文件名,因为task4需要通过识别文件名启动用户程序

int main(int argc, char **argv)
{
    char *progname = argv[0];

    /* process command line options */
    options.vm = 0;
    options.extended = 0;
    while ((argc > 1) && (argv[1][0] == '-') && (argv[1][1] == '-')) {
        char *option = &argv[1][2];

        if (strcmp(option, "vm") == 0) {
            options.vm = 1;
        } else if (strcmp(option, "extended") == 0) {
            options.extended = 1;
        } else {
            error("%s: invalid option\nusage: %s %s\n", progname,
                  progname, ARGS);
        }
        argc--;
        argv++;
    }
    if (options.vm == 1) {
        error("%s: option --vm not implemented\n", progname);
    }
    if (argc < 3) {
        /* at least 3 args (createimage bootblock main) */
        error("usage: %s %s\n", progname, ARGS);
    }
    create_image(argc - 1, argv + 1);
    return 0;
}

/* TODO: [p1-task4] assign your task_info_t somewhere in 'create_image' */
static void create_image(int nfiles, char *files[])
{
    int tasknum = nfiles - 2;
    int nbytes_kernel = 0; // kernel 占用的字节数
    int phyaddr = 0; // 物理地址偏移量，表示已经写入镜像文件的字节数
    FILE *fp = NULL, *img = NULL;
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;

    /* open the image file */
    img = fopen(IMAGE_FILE, "w");
    assert(img != NULL);
    
 // 1. 处理 bootblock (files[0])
    fp = fopen(files[0], "r");
    assert(fp != NULL);
    read_ehdr(&ehdr, fp);
    for (int ph = 0; ph < ehdr.e_phnum; ph++) {
        read_phdr(&phdr, fp, ph, ehdr);
        if (phdr.p_type == PT_LOAD) write_segment(phdr, fp, img, &phyaddr);
    }
    write_padding(img, &phyaddr, SECTOR_SIZE);
    fclose(fp);

    // 2. 处理 kernel (files[1])
    fp = fopen(files[1], "r");
    assert(fp != NULL);
    read_ehdr(&ehdr, fp);
    for (int ph = 0; ph < ehdr.e_phnum; ph++) {
        read_phdr(&phdr, fp, ph, ehdr);
        if (phdr.p_type == PT_LOAD) {
            nbytes_kernel += phdr.p_filesz; // 正确计算内核字节数
            write_segment(phdr, fp, img, &phyaddr);
        }
    }
    fclose(fp);

// 3. 依次写入所有 user apps, 同时在内存中构建好 taskinfo 数组
    for (int i = 0; i < tasknum; ++i) {
        char* filename = files[i + 2];
        int task_start_addr = phyaddr; // app 的 offset 就是当前文件指针的位置

        fp = fopen(filename, "r");
        assert(fp != NULL);
        read_ehdr(&ehdr, fp);

        // 填充内存中的 taskinfo 数组
        strncpy(taskinfo[i].name, get_filename(filename), TASK_NAME_LEN);
        taskinfo[i].entry_point = ehdr.e_entry;
        taskinfo[i].offset = task_start_addr;

        int app_begin_phyaddr = phyaddr;
        for (int ph = 0; ph < ehdr.e_phnum; ph++) {
            read_phdr(&phdr, fp, ph, ehdr);
            if (phdr.p_type == PT_LOAD) {
                write_segment(phdr, fp, img, &phyaddr);
            }
        }
        taskinfo[i].size = phyaddr - app_begin_phyaddr;
        fclose(fp);
    }
    
    //  在追加 task_info 之前，先进行填充
    //  确保 task_info 将从下一个扇区的边界开始写入
    int os_size_bytes = phyaddr;
    int next_sector_addr = NBYTES2SEC(phyaddr) * SECTOR_SIZE;
    write_padding(img, &phyaddr, next_sector_addr);
    
    // 5. 将 task_info 数组追加到文件末尾
    g_task_info_offset = phyaddr; // 记录对齐后的起始位置
    fwrite(taskinfo, sizeof(task_info_t), tasknum, img);
    phyaddr += sizeof(task_info_t) * tasknum;

    // [p1-task5] 6. 写入批处理文件
    int batch_info_start_addr = NBYTES2SEC(phyaddr) * SECTOR_SIZE;
    write_padding(img, &phyaddr, batch_info_start_addr);
    int batch_info_offset = phyaddr; // 记录批处理文件的起始地址
    write_padding(img, &phyaddr, batch_info_offset + SECTOR_SIZE);

    // 6. 将元信息写回 bootblock 区域
    write_img_info(os_size_bytes, tasknum, g_task_info_offset, batch_info_offset, img);

    fclose(img);
}
static void read_ehdr(Elf64_Ehdr * ehdr, FILE * fp)
{
    int ret;

    ret = fread(ehdr, sizeof(*ehdr), 1, fp);
    assert(ret == 1);
    assert(ehdr->e_ident[EI_MAG1] == 'E');
    assert(ehdr->e_ident[EI_MAG2] == 'L');
    assert(ehdr->e_ident[EI_MAG3] == 'F');
}

static void read_phdr(Elf64_Phdr * phdr, FILE * fp, int ph,
                      Elf64_Ehdr ehdr)
{
    int ret;

    fseek(fp, ehdr.e_phoff + ph * ehdr.e_phentsize, SEEK_SET);
    ret = fread(phdr, sizeof(*phdr), 1, fp);
    assert(ret == 1);
    if (options.extended == 1) {
        printf("\tsegment %d\n", ph);
        printf("\t\toffset 0x%04lx", phdr->p_offset);
        printf("\t\tvaddr 0x%04lx\n", phdr->p_vaddr);
        printf("\t\tfilesz 0x%04lx", phdr->p_filesz);
        printf("\t\tmemsz 0x%04lx\n", phdr->p_memsz);
    }
}

static uint64_t get_entrypoint(Elf64_Ehdr ehdr)
{
    return ehdr.e_entry;
}

static uint32_t get_filesz(Elf64_Phdr phdr)
{
    return phdr.p_filesz;
}

static uint32_t get_memsz(Elf64_Phdr phdr)
{
    return phdr.p_memsz;
}

static void write_segment(Elf64_Phdr phdr, FILE *fp, FILE *img, int *phyaddr) // 
{
    if (phdr.p_memsz != 0 && phdr.p_type == PT_LOAD) {
        /* write the segment itself */
        /* NOTE: expansion of .bss should be done by kernel or runtime env! */
        if (options.extended == 1) {
            printf("\t\twriting 0x%04lx bytes\n", phdr.p_filesz);
        }
        fseek(fp, phdr.p_offset, SEEK_SET);
        while (phdr.p_filesz-- > 0) {
            fputc(fgetc(fp), img); // 从输入文件读取一个字节，写入到镜像文件
            (*phyaddr)++; // 更新物理地址偏移量
        }
    }
}

static void write_padding(FILE *img, int *phyaddr, int new_phyaddr)
{
    if (options.extended == 1 && *phyaddr < new_phyaddr) {
        printf("\t\twrite 0x%04x bytes for padding\n", new_phyaddr - *phyaddr);
    }

    while (*phyaddr < new_phyaddr) {
        fputc(0, img);
        (*phyaddr)++;
    }
}

// 辅助函数，从路径中提取文件名
static char* get_filename(char* path) {
    char* s = strrchr(path, '/');
    return (s == NULL) ? path : s + 1;
}

static void write_img_info(int os_size_bytes, short tasknum, int task_info_offset, int batch_file_offset, FILE *img) {
    fseek(img, OS_SIZE_LOC, SEEK_SET);
    short os_size_sectors = NBYTES2SEC(os_size_bytes - SECTOR_SIZE);
    fwrite(&os_size_sectors, sizeof(short), 1, img);

    fseek(img, TASK_NUM_LOC, SEEK_SET);
    fwrite(&tasknum, sizeof(short), 1, img);
    
    fseek(img, TASK_INFO_START_LOC, SEEK_SET);
    short task_info_start_sector = (short)(task_info_offset / SECTOR_SIZE);
    fwrite(&task_info_start_sector, sizeof(short), 1, img);

    // [p1-task5] 写入“批处理文件”的起始扇区号
    fseek(img, BATCH_INFO_START_LOC, SEEK_SET);
    short batch_file_start_sector = (short)(batch_file_offset / SECTOR_SIZE);
    fwrite(&batch_file_start_sector, sizeof(short), 1, img);

    fseek(img, BOOT_LOADER_SIG_OFFSET, SEEK_SET);
    fputc(BOOT_LOADER_SIG_1, img);
    fputc(BOOT_LOADER_SIG_2, img);
}


/* print an error message and exit */
static void error(char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    if (errno != 0) {
        perror(NULL);
    }
    exit(EXIT_FAILURE);
}

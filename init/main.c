#include <common.h>
#include <asm.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/loader.h>
#include <type.h>

#define VERSION_BUF 50

#define OS_SIZE_ADDR         0x502001fc // Kernel的扇区数地址
#define TASK_NUM_ADDR        0x502001fa // 用户程序数量的地址
#define TASK_INFO_START_ADDR 0x502001f8 // task_info 数组起始扇区号的地址

int version = 2; // version must between 0 and 9
char buf[VERSION_BUF];

// Task info array
task_info_t tasks[TASK_MAXNUM];
char name_buffer[TASK_NAME_LEN];

static void get_str(char *buffer, int max_len); // 从键盘获取字符串输入,用于task4使用程序名加载

static int bss_check(void)
{
    for (int i = 0; i < VERSION_BUF; ++i)
    {
        if (buf[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
}

static void init_task_info(short num_tasks, short task_info_start_sector)
{
    if (num_tasks > TASK_MAXNUM) num_tasks = TASK_MAXNUM;
    if (num_tasks <= 0) return;

    int num_info_sectors = NBYTES2SEC(sizeof(task_info_t) * num_tasks);
    
    bios_sd_read((uint32_t)tasks, num_info_sectors, task_info_start_sector);
}

static void get_str(char *buffer, int max_len)
{
    int i = 0;
    while (i < max_len - 1) {
        int c = -1;
        while(c == -1) {
            c = bios_getchar();
        }
        if (c == '\r' || c == '\n') {
            break;
        } else if (c == 8 || c == 127) {
            if (i > 0) {
                i--;
                bios_putchar(8); bios_putchar(' '); bios_putchar(8);
            }
        } else {
            buffer[i++] = (char)c;
            bios_putchar((char)c);
        }
    }
    buffer[i] = '\0';
    bios_putstr("\n\r");
}

/************************************************************/
/* Do not touch this comment. Reserved for future projects. */
/************************************************************/

int main(int argc, char *argv[]) // argc 就是 task_num, argv 就是 task_info_start_sector
{   
    // 读取传递过来的参数
    short num_tasks = (short)argc;
    short task_info_start_sector = (short)(uintptr_t)argv;

    // Check whether .bss section is set to zero
    int check = bss_check();

    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info(num_tasks, task_info_start_sector);

    // Output 'Hello OS!', bss check result and OS version
    char output_str[] = "bss check: _ version: _\n\r";
    char output_val[2] = {0};
    int i, output_val_pos = 0;

    output_val[0] = check ? 't' : 'f';
    output_val[1] = version + '0';
    for (i = 0; i < sizeof(output_str); ++i)
    {
        buf[i] = output_str[i];
        if (buf[i] == '_')
        {
            buf[i] = output_val[output_val_pos++];
        }
    }

    bios_putstr("Hello OS!\n\r");
    bios_putstr(buf);

    // TODO: Load tasks by either task id [p1-task3] or task name [p1-task4],
    //   and then execute them.
    
    // 打印可用的任务列表
    bios_putstr("Available tasks:\n\r");
    for (int i = 0; i < num_tasks; i++) {    
        bios_putstr(tasks[i].name);
        bios_putstr("\n\r");
    }    

 while (1)
    {
        // 【关键修正】添加了清晰的交互提示
        bios_putstr("\n\rPlease enter task name: ");
        
        get_str(name_buffer, TASK_NAME_LEN);

        if (name_buffer[0] == '\0') {
            continue;
        }

        // 调用加载器
        uint64_t entry_point = load_task_img(name_buffer);
        
        // 如果加载成功 (返回非0入口点)
        if (entry_point != 0) {
            bios_putstr("  [Kernel] Loading and running task '");
            bios_putstr(name_buffer);
            bios_putstr("'...\n\r\n\r");
            
            // 跳转执行
            void (*task_entry)(void) = (void (*)(void))entry_point;
            task_entry();
            
            // 如果用户程序返回
            bios_putstr("\n\r  [Kernel] Task '");
            bios_putstr(name_buffer);
            bios_putstr("' has returned.\n\r");
        }
        // 如果加载失败，load_task_img 内部会打印 "Task not found"
    }

    // ----------------------- task3 used -----------------------
    // while (1)
    // {
    //     bios_putstr("\n\rPlease enter task id (0-");
    //     bios_putchar(num_tasks - 1 + '0');
    //     bios_putstr("): ");

    //     // 1. 获取键盘输入
    //     int input_char;
    //     while ((input_char = bios_getchar()) == -1) {

    //     }
    //     // 现在，input_char 中是一个有效的字符ASCII码
    //     char input = (char)input_char;

    //     bios_putchar(input); // 回显用户输入
    //     bios_putstr("\n\r");

    //     // 2. 检查输入是否合法
    //     if (input >= '0' && input < '0' + num_tasks) {
    //         int task_id = input - '0';
            
    //         // 3. 打印加载信息
    //         bios_putstr("Loading and running task ");
    //         bios_putchar(task_id + '0');
    //         bios_putstr("...\n\r");
            
    //         // 4. 调用 load_task_img 加载用户程序
    //         uint64_t entry_point = load_task_img(task_id);
            
    //         // 5. 跳转到用户程序执行
    //         // 定义一个函数指针，指向用户程序的入口点
    //         void (*task_entry)(void) = (void (*)(void))entry_point;
    //         // 调用用户程序的入口点，开始执行用户程序
    //         task_entry();

    //     } else {
    //         // 输入不合法，打印错误信息
    //         bios_putstr("Invalid task id!\n\r");
    //     }
    // }

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
     //   asm volatile("wfi"); // Wait For Interrupt，CPU 会一直保持睡眠，直到有一个外部中断
      int c;
      while ((c = bios_getchar()) == -1) {
          // an empty loop body
      }
      bios_putchar((char)c); // echo back
    }

    return 0;
}

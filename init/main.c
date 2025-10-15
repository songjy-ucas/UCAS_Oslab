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
#define BATCH_INFO_START_ADDR 0x502001f6 // 批处理文件起始扇区号的地址
#define BATCH_FILE_SECTORS   1           // 为批处理文件分配的扇区数

int version = 2; // version must between 0 and 9
char buf[VERSION_BUF];

// Task info array and user input buffer
task_info_t tasks[TASK_MAXNUM];
char name_buffer[TASK_NAME_LEN];
char user_buffer[TASK_NAME_LEN]; // 用户输入缓冲区，当前先暂时设置和任务名一样大
short batch_file_start_sector;
char batch_buffer[SECTOR_SIZE * BATCH_FILE_SECTORS];
char task_list_buffer[SECTOR_SIZE]; // 用于接收mkbatch的输入

// History buffer for commands
#define HISTORY_MAX 10 // 最多保存10条历史命令
#define CMD_MAX_LEN TASK_NAME_LEN // 命令最大长度
char history_buffer[HISTORY_MAX][CMD_MAX_LEN];
int history_count = 0;   // 当前存储的历史命令数量
int history_current = 0; // 当前通过箭头选择的历史命令索引

static void get_str(char *buffer, int max_len); // 从键盘获取字符串输入,用于task4使用程序名加载

static void add_to_history(const char *command); // 添加命令到历史记录

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
    jmptab[SD_WRITE]        = (long (*)())sd_write;
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
    buffer[0] = '\0'; // 每次开始前都清空缓冲区

    while (1) { // 使用无限循环，通过 break 退出
        int c = -1;
        while(c == -1) {
            c = bios_getchar();
        }

        // --- 核心修改：处理转义序列 ---
        if (c == 27) { // 检测到 ESC 字符，可能是箭头键
            // 尝试读取接下来的两个字符
            int c2 = bios_getchar();
            int c3 = bios_getchar();

            if (c2 == 91) { // [
                if (c3 == 65) { // 'A' -> 上箭头
                    if (history_count > 0 && history_current > 0) {
                        history_current--;
                        // 清除当前行
                        for (int j = 0; j < i; j++) {
                            bios_putchar('\b'); bios_putchar(' '); bios_putchar('\b');
                        }
                        // 复制并显示历史命令
                        strcpy(buffer, history_buffer[history_current % HISTORY_MAX]);
                        i = strlen(buffer);
                        bios_putstr(buffer);
                    }
                } else if (c3 == 66) { // 'B' -> 下箭头
                    if (history_count > 0 && history_current < history_count - 1) {
                        history_current++;
                        // 清除当前行
                        for (int j = 0; j < i; j++) {
                            bios_putchar('\b'); bios_putchar(' '); bios_putchar('\b');
                        }
                        // 复制并显示历史命令
                        strcpy(buffer, history_buffer[history_current % HISTORY_MAX]);
                        i = strlen(buffer);
                        bios_putstr(buffer);
                    } else if (history_current == history_count - 1) {
                        history_current++;
                         // 清除当前行
                        for (int j = 0; j < i; j++) {
                            bios_putchar('\b'); bios_putchar(' '); bios_putchar('\b');
                        }
                        // 显示一个空行
                        buffer[0] = '\0';
                        i = 0;
                    }
                }
                // 在这里可以添加对左右箭头 (C/D) 的处理
            }
            continue; // 处理完转义序列，继续下一次循环
        }
        
        // --- 处理普通按键 ---
        if (c == '\r' || c == '\n') {
            break; // 遇到回车，结束输入
        } else if (c == 8 || c == 127) { // Backspace 或 Delete
            if (i > 0) {
                i--;
                // 在屏幕上执行 "退格-空格-退格" 来擦除字符
                bios_putchar('\b'); bios_putchar(' '); bios_putchar('\b');
            }
        } else if (i < max_len - 1) {
            // 普通字符，加入缓冲区并回显
            buffer[i++] = (char)c;
            bios_putchar((char)c);
        }
    }
    buffer[i] = '\0'; // 添加字符串结束符
    bios_putstr("\n\r");
}

static void add_to_history(const char *command)
{
    // 过滤掉无效命令：1. 如果命令是空的 (用户直接按回车)，则不添加 2. 如果命令与上一条历史记录完全相同，则不添加，避免重复
    if (command[0] == '\0' || 
       (history_count > 0 && strcmp(command, history_buffer[(history_count - 1) % HISTORY_MAX]) == 0)) 
    {
        return;
    }

    // 将命令字符串复制到历史记录缓冲区中。使用取模运算 (%) 实现环形队列：当 history_count 达到 HISTORY_MAX 时，新的命令会覆盖掉最旧的命令 (索引为 0 的那条)
    strcpy(history_buffer[history_count % HISTORY_MAX], command);
    history_count++;

    // 将当前历史指针指向“未来”的位置 (即最新的命令之后),这样做是为了让用户下一次按“上箭头”时，能立刻回到刚刚输入的这条最新命令
    history_current = history_count; 
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

    // 读取0f6处的批处理文件起始扇区号
    batch_file_start_sector = *((short *)BATCH_INFO_START_ADDR);
    
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

 while (1)
    {
        bios_putstr("Songjunyi's OS --enter 'help' for more> "); // 命令行提示符

        // 每次输入前，重置历史指针到最新位置
        history_current = history_count;

        get_str(user_buffer, TASK_NAME_LEN);

        // 将刚输入的命令添加到历史记录
        add_to_history(user_buffer);

        if (user_buffer[0] == '\0') {
            continue;
        }
        if (strcmp(user_buffer, "help") == 0) {
            bios_putstr("Available commands:\n\r");
            bios_putstr("  help - Show this help message\n\r");
            bios_putstr("  list - List all available tasks\n\r");
            bios_putstr("  run - Ready to Load and run the specified task\n\r");
            bios_putstr("  mkbatch - Create a batch job file\n\r");
            bios_putstr("  execbatch - Execute the batch job\n\r");
            continue;
        } 
        else if (strcmp(user_buffer, "list") == 0) {
            bios_putstr("Available tasks:\n\r");
            for (int i = 0; i < num_tasks; i++) {    
                bios_putstr("  ");
                bios_putstr(tasks[i].name);
                bios_putstr("\n\r");
            }    
            continue;
        } 
        else if (strcmp(user_buffer, "run") == 0) {
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
        // 命令: mkbatch
        else if (strcmp(user_buffer, "mkbatch") == 0) { // 1.mkbatch,创建批处理文件
            // 2. 打印提示信息，引导用户输入
            bios_putstr("Please enter task names (space-separated): ");
            
            // 3. 调用 get_str() 等待并接收包含所有任务名的新一行输入
            get_str(task_list_buffer, sizeof(task_list_buffer));
            // 如果用户直接按回车，则取消操作
            if (task_list_buffer[0] == '\0') {
                bios_putstr("Batch creation cancelled.\n\r");
                continue;
            }

            // 清空用于写入磁盘的批处理缓冲区
            memset(batch_buffer, 0, sizeof(batch_buffer));

            // 4. 解析的源字符串是 task_list_buffer
            char *tasks_str = task_list_buffer; 
            char *current_pos = batch_buffer;
            int buffer_space = sizeof(batch_buffer);

            bios_putstr("Creating batch file with tasks:\n\r");
     
            while (*tasks_str != '\0') {
                // 跳过任务名之间的所有前导空格。
                while (*tasks_str == ' ') tasks_str++;
                // 到达字符串末尾，结束循环
                if (*tasks_str == '\0') break;

                char *task_name_start = tasks_str; // 记录任务名的起始位置
                // 找到任务名的结尾（下一个空格或字符串末尾）
                while (*tasks_str != ' ' && *tasks_str != '\0') {
                    tasks_str++;
                }

                int name_len = tasks_str - task_name_start; // 计算任务名长度
                // 确保任务名长度合法且缓冲区有足够空间存
                if (name_len > 0 && (name_len + 1) < buffer_space) {
                    strncpy(current_pos, task_name_start, name_len); // 复制任务名
                    current_pos[name_len] = '\0';
                    
                    bios_putstr("  - Added '");
                    bios_putstr(current_pos); // 打印添加的任务名
                    bios_putstr("'\n\r");
                    
                    current_pos += name_len + 1;
                    buffer_space -= name_len + 1;
                } else {
                    bios_putstr("Error: Batch file buffer is full or task name is invalid.\n\r");
                    break;
                }
            }

            // 写入磁盘
            bios_putstr("Writing batch file to disk...\n\r");
            bios_sd_write((uint32_t)batch_buffer, BATCH_FILE_SECTORS, batch_file_start_sector);
            bios_putstr("Done.\n\r");
            continue;
        }
        // 命令: execbatch
        else if (strcmp(user_buffer, "execbatch") == 0) {
            bios_putstr("Executing batch file from disk...\n\r");
            // 从SD卡读取批处理文件到缓冲区，使用从bootblock读取的扇区号
            bios_sd_read((uint32_t)batch_buffer, BATCH_FILE_SECTORS, batch_file_start_sector);

            // 检查批处理文件是否为空
            if (batch_buffer[0] == '\0') {
                bios_putstr("Batch file is empty or not found.\n\r");
                continue;
            }

            char *current_task_name = batch_buffer;
            // 循环执行任务，直到遇到文件末尾的双'\0'
            while (*current_task_name != '\0') {
                uint64_t entry_point = load_task_img(current_task_name);

                if (entry_point != 0) {
                    bios_putstr("\n\r[Kernel] Running batch task '");
                    bios_putstr(current_task_name);
                    bios_putstr("'...\n\r");
                    
                    void (*task_entry)(void) = (void (*)(void))entry_point;
                    task_entry(); // 执行任务

                    bios_putstr("[Kernel] Task '");
                    bios_putstr(current_task_name);
                    bios_putstr("' finished.\n\r");
                } else {
                    bios_putstr("\n\r[Kernel] Error: Failed to load task '");
                    bios_putstr(current_task_name);
                    bios_putstr("'. Aborting batch execution.\n\r");
                    break; // 加载失败，终止批处理
                }                
                // 移动指针到下一个任务名 (跳过当前任务名和它的'\0')
                current_task_name += strlen(current_task_name) + 1;
            }
            bios_putstr("\n\rBatch execution finished.\n\r");
            continue;
        }
        else {
            bios_putstr("Unknown command. Type 'help' for a list of commands.\n\r");
            continue;
        }
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
      // Wait For Interrupt，CPU 会一直保持睡眠，直到有一个外部中断
      int c;
      while ((c = bios_getchar()) == -1) {
          // an empty loop body
      }
      bios_putchar((char)c); // echo back
    }

    return 0;
}

#include <common.h>
#include <asm.h>
#include <asm/unistd.h>
#include <os/loader.h>
#include <os/irq.h>
#include <os/sched.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/mm.h>
#include <os/time.h>
#include <os/list.h>
#include <sys/syscall.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
#include <type.h>
#include <csr.h>

extern void ret_from_exception();
extern long (*syscall[])();
#define VERSION_BUF 50

#define OS_SIZE_ADDR         0x502001fc // Kernel的扇区数地址
#define TASK_NUM_ADDR        0x502001fa // 用户程序数量的地址
#define TASK_INFO_START_ADDR 0x502001f8 // task_info 数组起始扇区号的地址
#define BATCH_INFO_START_ADDR 0x502001f6 // [p1-task5] 新增: 批处理文件起始扇区号的地址
int version = 2; // version must between 0 and 9
char buf[VERSION_BUF];

// Task info array
task_info_t tasks[TASK_MAXNUM];
char name_buffer[TASK_NAME_LEN];

extern pcb_t pcb[NUM_MAX_TASK];
extern pcb_t pid0_pcb; // 内核 idle 任务的 PCB

 // static int parse_command(char *buffer, char *argv[]); // 复用上一版的命令行解析器
 // static void execute_task(const char *task_name); // 复用上一版的任务执行器


// static void get_str(char *buffer, int max_len); // 从键盘获取字符串输入,用于task4使用程序名加载


static int bss_check(void)
{
    for (int i = 0; i < VERSION_BUF; ++i)
    {
        if (buf[i] != 0)
        {
            return 0; // BSS check failed
        }
    }
    return 1; // BSS check passed
}
static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
    jmptab[SD_WRITE]        = (long (*)())sd_write;
    jmptab[QEMU_LOGGING]    = (long (*)())qemu_logging;
    jmptab[SET_TIMER]       = (long (*)())set_timer;
    jmptab[READ_FDT]        = (long (*)())read_fdt;
    jmptab[MOVE_CURSOR]     = (long (*)())screen_move_cursor;
    jmptab[PRINT]           = (long (*)())printk;
    jmptab[YIELD]           = (long (*)())do_scheduler;
    jmptab[MUTEX_INIT]      = (long (*)())do_mutex_lock_init;
    jmptab[MUTEX_ACQ]       = (long (*)())do_mutex_lock_acquire;
    jmptab[MUTEX_RELEASE]   = (long (*)())do_mutex_lock_release;

    // TODO: [p2-task1] (S-core) initialize system call table.
    



    jmptab[SYSCALL_REFLUSH] = (long (*)())screen_reflush;
}

static void init_task_info(short num_tasks, short task_info_start_sector)
{
    if (num_tasks <= 0 || num_tasks > TASK_MAXNUM) return;

    // 计算存储 task_info 需要多少个扇区
    int num_info_sectors = NBYTES2SEC(sizeof(task_info_t) * num_tasks);
    
    // 调用BIOS从SD卡读取task_info到全局的tasks数组中
    bios_sd_read((uint32_t)(uintptr_t)tasks, num_info_sectors, task_info_start_sector);
}

// static void get_str(char *buffer, int max_len)
// {
//     int i = 0;
//     while (i < max_len - 1) {
//         int c = -1;
//         while(c == -1) {
//             c = bios_getchar();
//         }
//         if (c == '\r' || c == '\n') {
//             break;
//         } else if (c == 8 || c == 127) {
//             if (i > 0) {
//                 i--;
//                 bios_putchar(8); bios_putchar(' '); bios_putchar(8);
//             }
//         } else {
//             buffer[i++] = (char)c;
//             bios_putchar((char)c);
//         }
//     }
//     buffer[i] = '\0';
//     bios_putstr("\n\r");
// }

/************************************************************/
void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb, int argc, char *argv[])
{
    /* Initialize registers on kernel stack */
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));
    
    // 清空所有寄存器，防止有垃圾数据
    memset(pt_regs, 0, sizeof(regs_context_t));

    /* 设置关键寄存器 */
    pt_regs->regs[2] = user_stack;       // sp: 用户栈顶
    pt_regs->regs[4] = (reg_t)pcb;       // tp: 指向当前 PCB
    pt_regs->regs[10] = (reg_t)argc;     // a0: 参数个数
    pt_regs->regs[11] = (reg_t)argv;     // a1: 参数数组指针

    pt_regs->sepc = entry_point;         // sepc: 用户程序入口地址
    
    // 设置 sstatus:
    // SR_SPIE: 开启中断 (以便用户程序能响应时钟中断)
    // ~SR_SPP: 确保 sret 后回到 U-Mode (User Mode)
    pt_regs->sstatus = (read_csr(sstatus) | SR_SPIE) & ~SR_SPP;

    /* 模拟 switch_to 的上下文 */
    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    
    // 清空 switch_to 上下文
    memset(pt_switchto, 0, sizeof(switchto_context_t));

    // ra 指向异常返回入口，这样第一次调度到该进程时，
    // 执行 ret 会跳转到 ret_from_exception，
    // 进而执行 RESTORE_CONTEXT 并 sret 进入用户态。
    pt_switchto->regs[0] = (reg_t)&ret_from_exception; // ra

    // 保存最终的内核栈顶指针到 PCB
    pcb->kernel_sp = (reg_t)pt_switchto;
    pcb->user_sp = user_stack;
}

static void init_pcb(void)
{
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */
    

    /* TODO: [p2-task1] remember to initialize 'current_running' */
     /* 初始化 PCB 数组和就绪队列 */
    list_init(&ready_queue);
    list_init(&sleep_queue);
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        memset(&pcb[i], 0, sizeof(pcb_t));
        pcb[i].pid = -1;
    }

    /* 加载用户任务并初始化它们的 PCB */
    const char *task_names[] = {"shell"};
    int num_user_tasks = sizeof(task_names) / sizeof(const char *);

        int pcb_idx = 1; // pcb[0] 留给 idle 任务
        pcb[pcb_idx].pid = process_id++;

        // 2. 加载任务代码
        uint64_t entry_point = load_task_img(task_names[0]);
        if (entry_point == 0) {
           printk("Error: Task image '%s' not found!\n", task_names[0]);
        }
        
        ptr_t kernel_stack_base = allocKernelPage(1);
        ptr_t user_stack_base   = allocUserPage(1);

        ptr_t kernel_stack = kernel_stack_base + PAGE_SIZE;
        ptr_t user_stack   = user_stack_base   + PAGE_SIZE;

        pcb[pcb_idx].status = TASK_READY;
        pcb[pcb_idx].cursor_x = 0;
        pcb[pcb_idx].cursor_y = 0;

        // 调用我们新的、简化的栈初始化函数
        init_pcb_stack(kernel_stack, user_stack, entry_point, &pcb[pcb_idx],0,NULL);
        list_init(&pcb[pcb_idx].list); 
      
        list_add_tail(&pcb[pcb_idx].list, &ready_queue);
    
    /* 初始化 pid0 (idle 任务) 并设置 current_running */
    pid0_pcb.pid = 0; // pid0 的 pid 应该是0
    pid0_pcb.status = TASK_RUNNING;
    pid0_pcb.kernel_sp = (reg_t)pid0_stack ;
    list_init(&pid0_pcb.list);
    
    // 设置 current_running 的初始值
    current_running = &pid0_pcb;

}

static void init_syscall(void)
{
    // TODO: [p2-task3] initialize system call table.
    syscall[SYSCALL_SLEEP]          = (long (*)())do_sleep;
    syscall[SYSCALL_YIELD]          = (long (*)())do_scheduler;
    syscall[SYSCALL_WRITE]          = (long (*)())screen_write;
    syscall[SYSCALL_CURSOR]         = (long (*)())screen_move_cursor;
    syscall[SYSCALL_REFLUSH]        = (long (*)())screen_reflush;
    syscall[SYSCALL_GET_TIMEBASE]   = (long (*)())get_time_base;
    syscall[SYSCALL_GET_TICK]       = (long (*)())get_ticks;
    syscall[SYSCALL_LOCK_INIT]      = (long (*)())do_mutex_lock_init;
    syscall[SYSCALL_LOCK_ACQ]       = (long (*)())do_mutex_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE]   = (long (*)())do_mutex_lock_release;

    syscall[SYSCALL_EXEC]           = (long (*)())do_exec;
    syscall[SYSCALL_EXIT]           = (long (*)())do_exit;
    syscall[SYSCALL_KILL]           = (long (*)())do_kill;
    syscall[SYSCALL_WAITPID]        = (long (*)())do_waitpid;
    syscall[SYSCALL_PS]             = (long (*)())do_process_show;
    syscall[SYSCALL_GETPID]         = (long (*)())do_getpid;
    syscall[SYSCALL_READCH]         = (long (*)())bios_getchar;
    syscall[SYSCALL_CLEAR]          = (long (*)())screen_clear;
    syscall[SYSCALL_WRITECH]        = (long (*)())screen_write_ch;
    syscall[SYSCALL_BARR_INIT]      = (long (*)())do_barrier_init;
    syscall[SYSCALL_BARR_WAIT]      = (long (*)())do_barrier_wait;
    syscall[SYSCALL_BARR_DESTROY]   = (long (*)())do_barrier_destroy;

    syscall[SYSCALL_COND_INIT]      = (long (*)())do_condition_init;
    syscall[SYSCALL_COND_WAIT]      = (long (*)())do_condition_wait;
    syscall[SYSCALL_COND_SIGNAL]    = (long (*)())do_condition_signal;
    syscall[SYSCALL_COND_BROADCAST] = (long (*)())do_condition_broadcast;
    syscall[SYSCALL_COND_DESTROY]   = (long (*)())do_condition_destroy;
    syscall[SYSCALL_MBOX_OPEN]  = (long (*)())do_mbox_open;
    syscall[SYSCALL_MBOX_CLOSE] = (long (*)())do_mbox_close;
    syscall[SYSCALL_MBOX_SEND]  = (long (*)())do_mbox_send;
    syscall[SYSCALL_MBOX_RECV]  = (long (*)())do_mbox_recv;

}
/************************************************************/


int main(int argc, char *argv[])
{   
    short num_tasks = (short)argc;
    short task_info_start_sector = (short)(uintptr_t)argv;
    bss_check();
    init_jmptab();
    init_task_info(num_tasks, task_info_start_sector);
    init_pcb();
    asm volatile("mv tp, %0" : : "r" (current_running));
    time_base = bios_read_fdt(TIMEBASE);
    init_locks();
    init_barriers();   
    init_conditions();
    printk("> [INIT] PCB initialization succeeded.\n");
    printk("> [INIT] Lock mechanism initialization succeeded.\n");

    asm volatile("csrs sstatus, %[mask]" :: [mask] "r" (SR_SUM));

    init_exception();
    printk("> [INIT] Interrupt processing initialization succeeded.\n");
    init_syscall();
    printk("> [INIT] System call initialized successfully.\n");
    init_screen();
    printk("> [INIT] SCREEN initialization succeeded.\n");
    // 设置初始时钟中断
    bios_set_timer(get_ticks()+TIMER_INTERVAL);

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
        // If you do non-preemptive scheduling, it's used to surrender control
        //do_scheduler();

        // If you do preemptive scheduling, they're used to enable CSR_SIE and wfi
        enable_preempt();
        asm volatile("wfi");
    }

    return 0;
}
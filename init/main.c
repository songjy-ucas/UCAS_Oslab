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
#include <os/smp.h>
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
// [P3-TASK3] pid0_pcb 变为数组
extern pcb_t pid0_pcb[NR_CPUS];

// [P3-TASK3] 用于多核同步的标志位
volatile int is_init_finished = 0;

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

    // [Task 4] Mask Initialization
    // 默认情况下，继承父进程的 mask，如果是第一个进程则允许所有核
    if (current_running) {
        pcb->mask = current_running->mask;
    } else {
        pcb->mask = 0x3; // 允许 Core 0 和 Core 1
    }
    pcb->core_id = -1; // 尚未运行
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
    
    /* [P3-TASK3] 初始化 Core 0 和 Core 1 的 idle 任务 */
    // Core 0
    pid0_pcb[0].pid = 0;
    pid0_pcb[0].status = TASK_RUNNING;
    pid0_pcb[0].kernel_sp = (reg_t)pid0_stack_core0;
    list_init(&pid0_pcb[0].list);

    // Core 1
    pid0_pcb[1].pid = 0;
    pid0_pcb[1].status = TASK_RUNNING;
    pid0_pcb[1].kernel_sp = (reg_t)pid0_stack_core1;
    list_init(&pid0_pcb[1].list);
    
    // 设置 current_running 的初始值，根据当前 CPU ID
    uint64_t cpu_id = get_current_cpu_id();
    current_running = &pid0_pcb[cpu_id];

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

    syscall[SYSCALL_TASKSET]    = (long (*)())do_taskset;

}
/************************************************************/


int main(int argc, char *argv[])
{   
   // [P3-TASK3] 获取当前核心 ID
    uint64_t cpu_id = get_current_cpu_id();

    if (cpu_id == 0) {
        // ============ Core 0 初始化流程 ============
        
        // 1. 初始化全局数据结构
        smp_init(); // 初始化大内核锁
        
        lock_kernel(); // 上大内核锁，防止 Core 1 提前运行

        short num_tasks = (short)argc;
        short task_info_start_sector = (short)(uintptr_t)argv;
        bss_check();
        init_jmptab();
        init_task_info(num_tasks, task_info_start_sector);
        
        // init_pcb 会初始化两个核的 pid0_pcb，并设置 Core 0 的 current_running
        init_pcb(); 
        
        // 设置 tp 寄存器指向 Core 0 的 current_running
        asm volatile("mv tp, %0" : : "r" (current_running));
        
        time_base = bios_read_fdt(TIMEBASE);
        init_locks();
        init_barriers();   
        init_conditions();
        init_mbox(); 

        printk("> [INIT] PCB initialization succeeded.\n");
        printk("> [INIT] Lock mechanism initialization succeeded.\n");

        asm volatile("csrs sstatus, %[mask]" :: [mask] "r" (SR_SUM));

        init_exception();
        printk("> [INIT] Interrupt processing initialization succeeded.\n");
        init_syscall();
        printk("> [INIT] System call initialized successfully.\n");
        init_screen();
        printk("> [INIT] SCREEN initialization succeeded.\n");
        
        printk("> [INIT] CPU #%u has entered kernel with VM!\n",
            (unsigned int)get_current_cpu_id());
        // TODO: [p4-task1 cont.] remove the brake and continue to start user processes.
        kernel_brake();

        // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
        // NOTE: The function of sstatus.sie is different from sie's
    

        // // 设置初始时钟中断
        // bios_set_timer(get_ticks()+TIMER_INTERVAL);

        // 2. 唤醒 Core 1
        unlock_kernel(); // 释放大内核锁，允许 Core 1 继续执行
        wakeup_other_hart();
        
        // 3. 标记初始化完成，允许 Core 1 继续执行
        is_init_finished = 0;
        while(is_init_finished == 0) {
           asm volatile("nop"); // 稍微缓一下，避免总线占满
        }

    //    lock_kernel();

        asm volatile("fence w, r" ::: "memory"); // 强制写屏障，确保 1 被写入主存

    } else {
        lock_kernel();
        
        // ============ Core 1 初始化流程 ============
        
        while (1) {
           // 每次读取前加屏障，或者读取后加屏障，确保读到最新值
           // 但最简单的是只要变量是 volatile 的，配合适当的 fence
           if (is_init_finished == 0) break;
           asm volatile("nop"); // 稍微缓一下，避免总线占满
        }

    // 确保看到 Core 0 在修改标志位之前做的所有内存操作
    asm volatile("fence r, r" ::: "memory"); 

        // 2. 设置 Core 1 的环境
        // init_pcb 已经初始化了 pid0_pcb[1]，这里设置 current_running
        current_running = &pid0_pcb[1];
        asm volatile("mv tp, %0" : : "r" (current_running));
        printk("> [INIT] Core 1 ready to started.\n");
        asm volatile("csrs sstatus, %[mask]" :: [mask] "r" (SR_SUM));

        // // 设置初始时钟中断
        // bios_set_timer(get_ticks()+TIMER_INTERVAL);
        
        printk("> [INIT] Core 1 started.\n"); 
        // Core 0 在赋值完后：
        is_init_finished = 1;
        unlock_kernel();
    }


    // 设置初始时钟中断
    bios_set_timer(get_ticks()+TIMER_INTERVAL);
    // 设置异常入口 stvec,并开全局中断
    //lock_kernel();
    setup_exception();
    //unlock_kernel();
    // Infinite while loop
    while (1)
    {
        enable_preempt();
        asm volatile("wfi");
    }

    return 0;
}




// // [P3-TASK3] 用于多核同步的标志位
// // 0: 未初始化, 1: Core 0 完成全局初始化
// volatile int is_core0_init_finished = 0;

// int main(int argc, char *argv[])
// {   
//     // 获取当前核心 ID
//     uint64_t cpu_id = get_current_cpu_id();

//     if (cpu_id == 0) {
//         // ============ Core 0 ============
        
//         // 1. 初始化全局资源
//         smp_init();      // 初始化大内核锁等
//         lock_kernel();   // 持有锁进行初始化

//         short num_tasks = (short)argc;
//         short task_info_start_sector = (short)(uintptr_t)argv;
        
//         bss_check();
//         init_jmptab();
//         init_task_info(num_tasks, task_info_start_sector);
        
//         // 初始化 PCB、页表、锁、中断向量等
//         init_pcb(); // Shell 任务在这里被创建并加入 ready_queue
        
//         // 设置 Core 0 的 current_running
//         asm volatile("mv tp, %0" : : "r" (current_running));

//         time_base = bios_read_fdt(TIMEBASE);
//         init_locks();
//         init_barriers();   
//         init_conditions();
//         init_mbox(); 

//         printk("> [INIT] Core 0: Global initialization succeeded.\n");

//         asm volatile("csrs sstatus, %[mask]" :: [mask] "r" (SR_SUM));

//         init_exception();
//         init_syscall();
//         init_screen();
        
//         // 2. 唤醒 Core 1
//         // 在唤醒前，必须确保内存写入对 Core 1 可见
//         is_core0_init_finished = 1;
//         asm volatile("fence w, w" ::: "memory"); // 写屏障
        
//         wakeup_other_hart(); // 调用 SBI 唤醒 Core 1
        
//         // 3. 设置 Core 0 的时钟和中断
//         bios_set_timer(get_ticks() + TIMER_INTERVAL);
//         setup_exception(); // 设置 stvec
        
//         unlock_kernel(); // 释放锁，允许 Core 1 运行
        
//         // 4. 重要：Core 0 主动让出 CPU，进入调度器
//         // 这样调度器发现 shell 在 ready_queue 里，就会运行 shell
//         enable_preempt();
//         do_scheduler();

//     } else {
//         // ============ Core 1 ============
        
//         // 1. 等待 Core 0 完成全局初始化
//         // 即使被唤醒了，也要确认 Core 0 已经把该初始化的都初始化了
//         while (is_core0_init_finished == 0) {
//              asm volatile("nop");
//         }
//         asm volatile("fence r, r" ::: "memory"); // 读屏障，确保后续读到的是最新数据

//         lock_kernel(); //以此保证打印和后续初始化的原子性(可选，视具体实现而定)

//         // 2. 设置 Core 1 的环境
//         // init_pcb 已经在 Core 0 里初始化了 pid0_pcb[1]
//         current_running = &pid0_pcb[1];
//         asm volatile("mv tp, %0" : : "r" (current_running));
        
//         asm volatile("csrs sstatus, %[mask]" :: [mask] "r" (SR_SUM));

//         // 3. 设置 Core 1 的时钟和中断
//         bios_set_timer(get_ticks() + TIMER_INTERVAL);
//         setup_exception(); // 设置 stvec
        
//         printk("> [INIT] Core 1 started.\n");
        
//         unlock_kernel();

//         // 4. Core 1 主动让出 CPU
//         // 此时 Shell 应该已经被 Core 0 抢走了，Core 1 会去跑 Idle 任务
//         enable_preempt();
//         do_scheduler();
//     }

//     // 理论上永远不会运行到这里
//     while (1) {
//         enable_preempt();
//         asm volatile("wfi");
//     }

//     return 0;
// }





// int main(int argc, char *argv[])
// {   

//     // short num_tasks = (short)argc;
//     // short task_info_start_sector = (short)(uintptr_t)argv;

//     // bss_check();
//     // init_jmptab();
   
//     // init_task_info(num_tasks, task_info_start_sector);

//     // init_pcb();
//     // printk("> [INIT] PCB initialization succeeded.\n");
//     // asm volatile("mv tp, %0" : : "r" (current_running));

//     // time_base = bios_read_fdt(TIMEBASE);
//     // init_locks();
//     // init_barriers();  
//     // init_conditions();
//     // printk("> [INIT] Lock mechanism initialization succeeded.\n");

//     // asm volatile("csrs sstatus, %[mask]" :: [mask] "r" (SR_SUM));

//     // init_exception();
//     // printk("> [INIT] Interrupt processing initialization succeeded.\n");
//     // init_syscall();
//     // printk("> [INIT] System call initialized successfully.\n");
//     // init_screen();
//     // printk("> [INIT] SCREEN initialization succeeded.\n");

//     uint64_t cpu_id = get_current_cpu_id();

//     if (cpu_id == 0) {
//         // =============== 主核 (Master) ===============
//         // 1. 初始化系统
//         bss_check();
//         init_jmptab();
//         init_task_info((short)argc, (short)(uintptr_t)argv);
//         init_pcb(); // 初始化包括两个 idle 在内的所有 PCB

//         // 设置主核 current_running
//         current_running = &pid0_pcb[0];
//         asm volatile("mv tp, %0" : : "r" (current_running));
//         time_base = bios_read_fdt(TIMEBASE);
//         init_locks();
//         smp_init();      // 初始化内核锁
//         init_barriers();
//         init_conditions();
        
//         asm volatile("csrs sstatus, %[mask]" :: [mask] "r" (SR_SUM));

//         init_exception(); // 设置主核 stvec
//         init_syscall();
//         init_screen();

//         printk("> [INIT] Core 0 initialized. Waking up Core 1...\n");

//         // 2. 唤醒从核
//         wakeup_other_hart();

//     } else {
//         // =============== 从核 (Slave) ===============
//         // 1. 设置从核 current_running
//         current_running = &pid0_pcb[1];
//         asm volatile("mv tp, %0" : : "r" (current_running));

//         // 2. 设置从核异常向量 (stvec)
//         // setup_exception(); // 这里的 setup_exception 实际上是 trap.S 里的 helper，设置 stvec

//         // 3. 允许 SUM
//         asm volatile("csrs sstatus, %[mask]" :: [mask] "r" (SR_SUM));
        
//         printk("> [INIT] Core 1 started running.\n");
//     }

//     bios_set_timer(get_ticks()+TIMER_INTERVAL);
//     setup_exception();

//     // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
//     while (1)
//     {
//         // If you do non-preemptive scheduling, it's used to surrender control
//         //do_scheduler();

//         // If you do preemptive scheduling, they're used to enable CSR_SIE and wfi
//         enable_preempt();
//         asm volatile("wfi");
//     }

//     return 0;
// }

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
#include <os/ioremap.h>
#include <os/net.h>
#include <os/fs.h>
#include <e1000.h>
#include <plic.h>

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

/* [P4-Task1] 新增: 用于双核启动同步的原子计数器 */
/* 作用: 确保两个核都进入 main 后，再取消临时映射，防止崩盘 */
volatile int cpu_sync_barrier = 0;

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
    jmptab[PRINTL]          = (long (*)())printl;

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
    pt_regs->sstatus = SR_SPIE | SR_SUM;  // SPIE set to 1

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
    /* 初始化 PCB 数组和就绪队列 */
    list_init(&ready_queue);
    list_init(&sleep_queue);
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        memset(&pcb[i], 0, sizeof(pcb_t));
        pcb[i].pid = -1;
    }

    /* 加载用户任务 (Shell) */
    const char *task_names[] = {"shell"};
    int pcb_idx = 1; // 这里的 1 是硬编码的，指定 Shell 占用 pcb[1]

    // =============================================================
    // [修改 2] 为 Shell 创建页表并共享内核映射
    // =============================================================
    pcb[pcb_idx].pgdir = allocPage(1); // 分配页目录表
    clear_pgdir(pcb[pcb_idx].pgdir);   // 清空
    share_pgtable(pcb[pcb_idx].pgdir, pa2kva(PGDIR_PA)); // 共享内核映射

    // =============================================================
    // [修改 3] 使用 map_task 加载 Shell
    // =============================================================
    // 之前写 load_task_img 是错的，这里必须用 map_task
    uint64_t entry_point = map_task((char *)task_names[0], pcb[pcb_idx].pgdir);
    
    if (entry_point == 0) {
        printk("Error: Task image '%s' not found!\n", task_names[0]);
        while(1); // 找不到 Shell 直接死循环停止，方便调试
    }

    // =============================================================
    // [修改 4] 分配栈并建立映射
    // =============================================================
    
    // 分配内核栈 (直接分配物理页，使用其内核虚地址)
    pcb[pcb_idx].kernel_stack_base = allocPage(8);
    ptr_t kernel_stack = pcb[pcb_idx].kernel_stack_base + 8 * PAGE_SIZE;

    // 分配用户栈 (分配物理页，并映射到用户空间)
    // A-core USER_STACK_ADDR = 0xf00010000
    // alloc_page_helper 会在 pcb[1].pgdir 里建立映射
    uintptr_t user_stack_physical_kva = alloc_page_helper(USER_STACK_ADDR - PAGE_SIZE, pcb[pcb_idx].pgdir);
    pcb[pcb_idx].user_stack_base = user_stack_physical_kva;

    // Shell 的用户栈顶 (固定为用户虚地址)
    ptr_t final_user_sp = USER_STACK_ADDR;

    // 初始化 PCB 基本信息
    pcb[pcb_idx].pid = process_id++;
    pcb[pcb_idx].status = TASK_READY;
    pcb[pcb_idx].cursor_x = 0;
    pcb[pcb_idx].cursor_y = 0;
    pcb[pcb_idx].mask = 0x3; 
    pcb[pcb_idx].cwd_ino = 1; // [P6-Task1] Set cwd to Root Inode (1)
    list_init(&pcb[pcb_idx].list);
    list_init(&pcb[pcb_idx].wait_list);

    // =============================================================
    // [修改 5] 初始化 TrapFrame
    // =============================================================
    // Shell 启动不需要参数，argc=0, argv=NULL
    init_pcb_stack(kernel_stack, final_user_sp, entry_point, &pcb[pcb_idx], 0, NULL);

    // 加入就绪队列
    list_add_tail(&pcb[pcb_idx].list, &ready_queue);

    // -------------------------------------------------------------
    // 初始化 Idle 任务 (Core 0 & Core 1)
    // -------------------------------------------------------------
    
    // Core 0 Idle
    pid0_pcb[0].pid = 0;
    pid0_pcb[0].status = TASK_RUNNING;
    pid0_pcb[0].kernel_sp = (reg_t)pid0_stack_core0;
    // Idle 任务运行在内核态，直接使用内核根页表
    pid0_pcb[0].pgdir = pa2kva(PGDIR_PA); 
    list_init(&pid0_pcb[0].list);

    // Core 1 Idle
    pid0_pcb[1].pid = 0;
    pid0_pcb[1].status = TASK_RUNNING;
    pid0_pcb[1].kernel_sp = (reg_t)pid0_stack_core1;
    pid0_pcb[1].pgdir = pa2kva(PGDIR_PA);
    list_init(&pid0_pcb[1].list);

    // 设置当前运行进程
    uint64_t cpu_id = get_current_cpu_id();
    current_running = &pid0_pcb[cpu_id];
}

static void init_syscall(void)
{
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
    syscall[SYSCALL_MBOX_OPEN]      = (long (*)())do_mbox_open;
    syscall[SYSCALL_MBOX_CLOSE]     = (long (*)())do_mbox_close;
    syscall[SYSCALL_MBOX_SEND]      = (long (*)())do_mbox_send;
    syscall[SYSCALL_MBOX_RECV]      = (long (*)())do_mbox_recv;

    syscall[SYSCALL_TASKSET]        = (long (*)())do_taskset;

    // Pro4 新增
    syscall[SYSCALL_PIPE_OPEN] = (long (*)())do_pipe_open; // 如果 pipe 实际上就是 mailbox
    syscall[SYSCALL_PIPE_TAKE] = (long (*)())do_pipe_take_pages; // 对应 take
    syscall[SYSCALL_PIPE_GIVE] = (long (*)())do_pipe_give_pages;  // 对应 give (detach)
    
    // P4 Task4 新增
    syscall[SYSCALL_FREE_MEM] = (long (*)())do_get_free_memory;
    
    // P5 新增
    syscall[SYSCALL_NET_SEND] = (long (*)())do_net_send;
    syscall[SYSCALL_NET_RECV] = (long (*)())do_net_recv;
    syscall[SYSCALL_NET_RECV_STREAM] = (long (*)())do_net_recv_stream;
    syscall[SYSCALL_NET_RESET] = (long (*)())init_reliable_layer;

    // P6 新增
    syscall[SYSCALL_FS_MKFS] = (long (*)())do_mkfs;
    syscall[SYSCALL_FS_STATFS] = (long (*)())do_statfs;
    syscall[SYSCALL_FS_CD] = (long (*)())do_cd;
    syscall[SYSCALL_FS_MKDIR] = (long (*)())do_mkdir;
    syscall[SYSCALL_FS_RMDIR] = (long (*)())do_rmdir;
    syscall[SYSCALL_FS_LS] = (long (*)())do_ls;
    // syscall[SYSCALL_FS_TOUCH] = (long (*)())do_touch;
    // syscall[SYSCALL_FS_CAT] = (long (*)())do_cat;
    syscall[SYSCALL_FS_OPEN] = (long (*)())do_open;
    syscall[SYSCALL_FS_READ] = (long (*)())do_read;
    syscall[SYSCALL_FS_WRITE] = (long (*)())do_write;
    syscall[SYSCALL_FS_CLOSE] = (long (*)())do_close;
    syscall[SYSCALL_FS_LN] = (long (*)())do_ln;
    syscall[SYSCALL_FS_RM] = (long (*)())do_rm;
    syscall[SYSCALL_FS_LSEEK] = (long (*)())do_lseek;
    syscall[SYSCALL_FS_SYNC] = (long (*)())do_fs_sync;
    
}

/* [P4-Task1] 新增: 取消临时映射的辅助函数 */
void cancel_identity_mapping(void)
{
    /* 
     * 在 Sv39 模式下，0x50200000 对应的虚拟地址通常位于顶级页表(Page Directory)的第 0 项。
     * 因为 0 ~ 1GB 的地址都在第 0 项覆盖范围内 (1GB = 1 << 30)。
     * 计算索引: VPN2 = (0x50200000 >> 30) & 0x1FF = 0
     */
     
    // 获取内核页表基地址 (必须转换为虚拟地址访问!)
    PTE *pgdir = (PTE *)pa2kva(PGDIR_PA); 
    
    // 清除第 0 项 (覆盖 0-1GB 的恒等映射)
    pgdir[0] = 0; 
    
    // 刷新 TLB，确保 CPU 不再缓存旧的映射
    local_flush_tlb_all();
}

/************************************************************/

/*
 * Once a CPU core calls this function,
 * it will stop executing!
 */
static void kernel_brake(void)
{
    disable_interrupt();
    printk("> [INIT] task1_1 OK.\n");
    while (1)
        __asm__ volatile("wfi");
}

int main(/*int argc, char *argv[]*/)
{   
    // [P3-TASK3] 获取当前核心 ID
    uint64_t cpu_id = get_current_cpu_id();

    /* 
     * [P4-Task1] 关键修改: 全局签到 
     * 每个核进入 main 函数，先原子加一，表示“我已经到达高虚拟地址空间”
     */
    __sync_fetch_and_add(&cpu_sync_barrier, 1);

    if (cpu_id == 0) {
        // ============ Core 0 初始化流程 ============
        
        // 1. 初始化全局数据结构
        smp_init(); // 初始化大内核锁
        init_jmptab(); 
        
        // current_running = pid0_pcb; // 指向 Core 0 的 idle PCB   --- debug use    
        lock_kernel(); // 上大内核锁，防止 Core 1 提前运行
        // current_running = NULL; // 避免误用

        // 强制转换为 short 指针并取值
        short num_tasks = *(volatile short *)TASK_NUM_ADDR;
        short task_info_start_sector = *(volatile short *)TASK_INFO_START_ADDR;
        
        /* [P4-Task1] 关键修改: 初始化内存管理器 */
        // 必须在分配内存之前调用，初始化位图和Swap链表
        init_memory_manager();
        // 如果有 Swap 链表初始化，也在这里 (init_uva_alloc)
        // init_uva_alloc(); 
        
        bss_check();
        

        init_task_info(num_tasks, task_info_start_sector);
        

        
        // 设置 tp 寄存器指向 Core 0 的 current_running
        asm volatile("mv tp, %0" : : "r" (current_running));
        
        time_base = bios_read_fdt(TIMEBASE);

        // e1000 = (volatile uint8_t *)bios_read_fdt(ETHERNET_ADDR);
        // uint64_t plic_addr = bios_read_fdt(PLIC_ADDR);
        // uint32_t nr_irqs = (uint32_t)bios_read_fdt(NR_IRQS);
        // // printk("> [INIT] e1000: %lx, plic_addr: %lx, nr_irqs: %lx.\n", e1000, plic_addr, nr_irqs);

        // // IOremap
        // plic_addr = (uintptr_t)ioremap((uint64_t)plic_addr, 0x4000 * NORMAL_PAGE_SIZE);
        // e1000 = (uint8_t *)ioremap((uint64_t)e1000, 8 * NORMAL_PAGE_SIZE);

        // init_pcb 内部现在使用 allocPage 
        // P5 task3 ---------------------- 这个位置要改！！！！！！要在ioremap之后！！！！！！
        init_pcb();         

        printk("> [INIT] IOremap initialization succeeded.\n");
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
        init_fs(); // [P6-Task1] Init File System
        printk("> [INIT] File System initialized successfully.\n");
        init_screen();
        printk("> [INIT] SCREEN initialization succeeded.\n");

        // TODO: [p5-task4] Init plic
        // plic_init(plic_addr, nr_irqs);
        // printk("> [INIT] PLIC initialized successfully. addr = 0x%lx, nr_irqs=0x%x\n", plic_addr, nr_irqs);

        // Init network device(-_-)
        // e1000_init();
        // printk("> [INIT] E1000 device initialized successfully.\n");

        printk("> [INIT] CPU #%u has entered kernel with VM!\n",
            (unsigned int)get_current_cpu_id());
       
        /* [P4-Task1] task1前半部分启用 kernel_brake，之后注释掉即可 */
        //kernel_brake();
 
        // 2. 唤醒 Core 1
        unlock_kernel(); // 释放大内核锁，允许 Core 1 继续执行
        wakeup_other_hart(); // 发送 IPI 唤醒 Core 1
        
        /* 
         * [P4-Task1] 关键修改: 等待 Core 1 到达 
         * 在取消映射之前，必须确保 Core 1 也已经进入了 main 函数（cpu_sync_barrier >= 2）
         */
        while (cpu_sync_barrier < 2) {
             asm volatile("nop"); // 自旋等待
        }
        
        /* [P4-Task1] 安全时刻: 现在取消临时映射是安全的 */
        cancel_identity_mapping();

        // 3. 标记初始化完成，允许 Core 1 继续后续初始化逻辑
        is_init_finished = 0; 
        
        /* 通知 Core 1：全局初始化已完成，你可以继续了 */
        is_init_finished = 1; 
        asm volatile("fence w, w" ::: "memory"); // 写屏障

    } else {
        // ============ Core 1 初始化流程 ============
        
        lock_kernel(); // 尝试获取锁，会在这里阻塞直到 Core 0 释放锁
        
        /* 
         * 等待 Core 0 完成所有全局初始化 
         */
        while (is_init_finished == 0) {
            unlock_kernel(); // 释放锁让 Core 0 有机会跑 (如果需要)
            asm volatile("nop");
            lock_kernel();   // 重新抢锁
        }

        asm volatile("fence r, r" ::: "memory"); 

        // 2. 设置 Core 1 的环境
        current_running = &pid0_pcb[1];
        asm volatile("mv tp, %0" : : "r" (current_running));
        printk("> [INIT] Core 1 ready to started.\n");
        asm volatile("csrs sstatus, %[mask]" :: [mask] "r" (SR_SUM));

        printk("> [INIT] Core 1 started.\n"); 
        
        unlock_kernel();
    }


    // 设置初始时钟中断
    bios_set_timer(get_ticks()+TIMER_INTERVAL);
    // 设置异常入口 stvec,并开全局中断
    setup_exception();

    // Infinite while loop
    while (1)
    {
        enable_preempt();
        asm volatile("wfi");
    }

    return 0;
}

#include <unistd.h>
#include <stdio.h>
#include <string.h>

/* ==========================================
 * 1. 基础配置与宏定义
 * ========================================== */
#ifndef NULL
#define NULL ((void *)0)
#endif

// 必须匹配内核 mailbox 大小
#define MBOX_CAPACITY 64  
#define MSG_LEN 1

// 定义线程任务的枚举（通过命令行参数传递）
typedef enum {
    MODE_MAIN,      // 主控程序
    MODE_MANAGER_A, // 进程 A (线程管理者)
    MODE_MANAGER_B, // 进程 B (线程管理者)
    MODE_A_SENDER,  // 线程：A 的发送者
    MODE_A_RECVER,  // 线程：A 的接收者
    MODE_B_SENDER,  // 线程：B 的发送者
    MODE_B_RECVER   // 线程：B 的接收者
} Mode;

/* ==========================================
 * 2. 简易线程框架 (基于 PCB/exec 实现)
 *    老师说：不用写 TCB，用 PCB 就好
 * ========================================== */

// 模拟 thread_create
// 实际上是 exec 自己，并传入特定的参数让新进程跳转到对应的函数
int my_thread_create(char *func_name)
{
    char *args[] = {"nodeadlock_test", func_name, NULL};
    // 启动一个新进程来充当“线程”
    // 因为它们操作的是内核对象(Mailbox)，所以不共享内存也没关系
    int pid = sys_exec("nodeadlock_test", 2, args);
    return pid;
}

// 模拟 thread_join
void my_thread_join(int pid)
{
    sys_waitpid(pid);
}

/* ==========================================
 * 3. 具体的业务逻辑 (Send/Recv 分离)
 * ========================================== */

// 线程 A_Sender: 只负责往 mbox1 发数据
void thread_a_sender(void)
{
    int m1 = sys_mbox_open("mbox1");
    char msg[] = "A";
    printf("  [Thread A_TX] Trying to send to mbox1...\n");
    
    // 如果满了，这里会阻塞。但只要 A_RX 或 B_RX 在运行，这里最终会通
    sys_mbox_send(m1, msg, MSG_LEN);
    
    printf("  [Thread A_TX] Send Success!\n");
}

// 线程 A_Recver: 只负责从 mbox2 收数据
void thread_a_recver(void)
{
    int m2 = sys_mbox_open("mbox2");
    char buf[10];
    printf("  [Thread A_RX] Trying to recv from mbox2...\n");
    
    // 这里会把 mbox2 的数据读走，从而解救被阻塞的 B_Sender
    sys_mbox_recv(m2, buf, MSG_LEN);
    
    printf("  [Thread A_RX] Recv Success! Got: %d bytes\n", MSG_LEN);
}

// 线程 B_Sender: 只负责往 mbox2 发数据
void thread_b_sender(void)
{
    int m2 = sys_mbox_open("mbox2");
    char msg[] = "B";
    printf("  [Thread B_TX] Trying to send to mbox2...\n");
    sys_mbox_send(m2, msg, MSG_LEN);
    printf("  [Thread B_TX] Send Success!\n");
}

// 线程 B_Recver: 只负责从 mbox1 收数据
void thread_b_recver(void)
{
    int m1 = sys_mbox_open("mbox1");
    char buf[10];
    printf("  [Thread B_RX] Trying to recv from mbox1...\n");
    sys_mbox_recv(m1, buf, MSG_LEN);
    printf("  [Thread B_RX] Recv Success! Got: %d bytes\n", MSG_LEN);
}

/* ==========================================
 * 4. 进程/线程 管理者逻辑
 * ========================================== */

// 进程 A：不再自己干活，而是创建两个线程
void manager_a(void)
{
    printf(" [Manager A] Spawning threads...\n");
    int t1 = my_thread_create("A_TX");
    int t2 = my_thread_create("A_RX");
    
    my_thread_join(t1);
    my_thread_join(t2);
    printf(" [Manager A] All threads done. Exiting.\n");
}

// 进程 B：同上
void manager_b(void)
{
    printf(" [Manager B] Spawning threads...\n");
    int t1 = my_thread_create("B_TX");
    int t2 = my_thread_create("B_RX");
    
    my_thread_join(t1);
    my_thread_join(t2);
    printf(" [Manager B] All threads done. Exiting.\n");
}

/* ==========================================
 * 5. 主入口 (分发器)
 * ========================================== */
int main(int argc, char *argv[])
{
    // --- 路由分发逻辑 ---
    Mode mode = MODE_MAIN;
    if (argc > 1) {
        if (strcmp(argv[1], "A") == 0) mode = MODE_MANAGER_A;
        else if (strcmp(argv[1], "B") == 0) mode = MODE_MANAGER_B;
        else if (strcmp(argv[1], "A_TX") == 0) mode = MODE_A_SENDER;
        else if (strcmp(argv[1], "A_RX") == 0) mode = MODE_A_RECVER;
        else if (strcmp(argv[1], "B_TX") == 0) mode = MODE_B_SENDER;
        else if (strcmp(argv[1], "B_RX") == 0) mode = MODE_B_RECVER;
    }

    switch (mode) {
        case MODE_A_SENDER: thread_a_sender(); return 0;
        case MODE_A_RECVER: thread_a_recver(); return 0;
        case MODE_B_SENDER: thread_b_sender(); return 0;
        case MODE_B_RECVER: thread_b_recver(); return 0;
        case MODE_MANAGER_A: manager_a(); return 0;
        case MODE_MANAGER_B: manager_b(); return 0;
        case MODE_MAIN: break; // 继续执行下方主控逻辑
    }

    // --- 主控逻辑 (初始化环境) ---
    printf("--- No-Deadlock Test Start ---\n");


    int prem1 = sys_mbox_open("mbox1");
    int prem2 = sys_mbox_open("mbox2");
    sys_mbox_close(prem1);
    sys_mbox_close(prem2); // 先删掉之前的，重新建立mbox
    int m1 = sys_mbox_open("mbox1");
    int m2 = sys_mbox_open("mbox2");

    char fill = 'F';

    // 1. 同样，先填满邮箱
    printf("--- Main: Filling mailboxes (%d bytes)... ---\n", MBOX_CAPACITY);
    for (int i = 0; i < MBOX_CAPACITY; i++) {
        sys_mbox_send(m1, &fill, 1);
        sys_mbox_send(m2, &fill, 1);
    }
    printf("--- Main: Filled. Starting Managers A and B ---\n");

    // 2. 启动两个管理者
    int pid_a = my_thread_create("A");
    int pid_b = my_thread_create("B");

    // 3. 等待结果
    sys_waitpid(pid_a);
    sys_waitpid(pid_b);

    printf("--- No-Deadlock Test Passed! (If see this) ---\n"); // 只有
    return 0;
}
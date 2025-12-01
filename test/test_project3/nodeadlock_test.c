#include <unistd.h>
#include <stdio.h>
#include <string.h>

/* ==========================================
 * 1. 基础配置与宏定义
 * ========================================== */
#ifndef NULL
#define NULL ((void *)0)
#endif

#define MBOX_CAPACITY 64  
#define MSG_LEN 1
#define TEST_LOOP 50

// --- 屏幕布局定义 ---
#define LINE_MAIN   1   
#define LINE_A_TX   3   
#define LINE_A_RX   4   
#define LINE_B_TX   5   
#define LINE_B_RX   6   

// 模式定义
typedef enum {
    MODE_MAIN,      
    MODE_MANAGER_A, 
    MODE_MANAGER_B, 
    MODE_A_SENDER,  
    MODE_A_RECVER,  
    MODE_B_SENDER,  
    MODE_B_RECVER,
    MODE_INIT_FILL  // <--- 新增：专门用于初始化的填充模式
} Mode;

/* ==========================================
 * 2. 线程创建封装
 * ========================================== */
int my_thread_create(char *func_name)
{
    char *args[] = {"nodeadlock_test", func_name, NULL};
    return sys_exec("nodeadlock_test", 2, args);
}

void my_thread_join(int pid)
{
    sys_waitpid(pid);
}

/* ==========================================
 * 3. 核心业务逻辑
 * ========================================== */

// --- 新增：专门的填充任务 ---
// 它的工作就是尝试往信箱里塞数据。
// 如果信箱是空的，它会迅速填满并退出。
// 如果信箱已经是满的，它会卡在 sys_mbox_send，但这没关系，
// 因为它是在子进程里卡的，不会影响主程序！
void task_init_fill(void)
{
    int m1 = sys_mbox_open("mbox1");
    int m2 = sys_mbox_open("mbox2");
    char fill = 'F';

    // 尝试填满两个信箱
    for (int i = 0; i < MBOX_CAPACITY; i++) {
        sys_mbox_send(m1, &fill, 1);
        sys_mbox_send(m2, &fill, 1);
    }
    // 如果能运行到这里，说明填充完毕
}

void thread_a_sender(void)
{
    int m1 = sys_mbox_open("mbox1");
    char msg[] = "A";
    
    sys_move_cursor(0, LINE_A_TX);
    printf("[A Send]: Ready...");

    for (int i = 0; i < TEST_LOOP; i++) {
        sys_mbox_send(m1, msg, MSG_LEN);
        sys_move_cursor(0, LINE_A_TX);
        printf("[A Send]: Sending... %d/%d (Write OK)", i + 1, TEST_LOOP);
        sys_sleep(1);
    }
    sys_move_cursor(0, LINE_A_TX);
    printf("[A Send]: Done.                         "); 
}

void thread_a_recver(void)
{
    int m2 = sys_mbox_open("mbox2");
    char buf[10];

    sys_move_cursor(0, LINE_A_RX);
    printf("[A Recv]: Ready...");

    for (int i = 0; i < TEST_LOOP; i++) {
        sys_mbox_recv(m2, buf, MSG_LEN);
        sys_move_cursor(0, LINE_A_RX);
        printf("[A Recv]: Recving... %d/%d (Read OK)", i + 1, TEST_LOOP);
        sys_sleep(1);
    }
    sys_move_cursor(0, LINE_A_RX);
    printf("[A Recv]: Done.                         ");
}

void thread_b_sender(void)
{
    int m2 = sys_mbox_open("mbox2");
    char msg[] = "B";

    sys_move_cursor(0, LINE_B_TX);
    printf("[B Send]: Ready...");

    for (int i = 0; i < TEST_LOOP; i++) {
        sys_mbox_send(m2, msg, MSG_LEN);
        sys_move_cursor(0, LINE_B_TX);
        printf("[B Send]: Sending... %d/%d (Write OK)", i + 1, TEST_LOOP);
        sys_sleep(1);
    }
    sys_move_cursor(0, LINE_B_TX);
    printf("[B Send]: Done.                         ");
}

void thread_b_recver(void)
{
    int m1 = sys_mbox_open("mbox1");
    char buf[10];

    sys_move_cursor(0, LINE_B_RX);
    printf("[B Recv]: Ready...");

    for (int i = 0; i < TEST_LOOP; i++) {
        sys_mbox_recv(m1, buf, MSG_LEN);
        sys_move_cursor(0, LINE_B_RX);
        printf("[B Recv]: Recving... %d/%d (Read OK)", i + 1, TEST_LOOP);
        sys_sleep(1);
    }
    sys_move_cursor(0, LINE_B_RX);
    printf("[B Recv]: Done.                         ");
}

/* ==========================================
 * 4. 管理者逻辑
 * ========================================== */
void manager_a(void)
{
    int t1 = my_thread_create("A_TX");
    int t2 = my_thread_create("A_RX");
    my_thread_join(t1);
    my_thread_join(t2);
}

void manager_b(void)
{
    int t1 = my_thread_create("B_TX");
    int t2 = my_thread_create("B_RX");
    my_thread_join(t1);
    my_thread_join(t2);
}

/* ==========================================
 * 5. 主入口
 * ========================================== */
int main(int argc, char *argv[])
{
    // --- 路由分发 ---
    Mode mode = MODE_MAIN;
    if (argc > 1) {
        if (strcmp(argv[1], "A") == 0) mode = MODE_MANAGER_A;
        else if (strcmp(argv[1], "B") == 0) mode = MODE_MANAGER_B;
        else if (strcmp(argv[1], "A_TX") == 0) mode = MODE_A_SENDER;
        else if (strcmp(argv[1], "A_RX") == 0) mode = MODE_A_RECVER;
        else if (strcmp(argv[1], "B_TX") == 0) mode = MODE_B_SENDER;
        else if (strcmp(argv[1], "B_RX") == 0) mode = MODE_B_RECVER;
        // 新增的分支
        else if (strcmp(argv[1], "INIT_FILL") == 0) mode = MODE_INIT_FILL;
    }

    switch (mode) {
        case MODE_A_SENDER: thread_a_sender(); return 0;
        case MODE_A_RECVER: thread_a_recver(); return 0;
        case MODE_B_SENDER: thread_b_sender(); return 0;
        case MODE_B_RECVER: thread_b_recver(); return 0;
        case MODE_MANAGER_A: manager_a(); return 0;
        case MODE_MANAGER_B: manager_b(); return 0;
        case MODE_INIT_FILL: task_init_fill(); return 0; // 执行填充
        case MODE_MAIN: break;
    }

    // --- 主控逻辑 ---

    sys_move_cursor(0, LINE_MAIN);
    printf("--- Deadlock Test (50 Loops) | Status: Pre-filling...       ");

    // 2. 异步填充 (Async Fill Strategy)
    // 我们启动一个子进程去填信箱。
    // - 如果信箱是空的：它会瞬间填满然后退出。
    // - 如果信箱是满的：它会卡住。
    // 关键点：我们主程序只 sleep(1)，不管它有没有卡住，我们都继续往下跑！
    int pid_fill = my_thread_create("INIT_FILL");
    
    // 给它一点时间工作，如果它卡住了，sys_sleep 结束后我们照样继续
    sys_sleep(1); 

    sys_move_cursor(0, LINE_MAIN);
    printf("--- Status: Mailboxes Ready. Starting Managers A & B...     ");

    // 3. 启动测试进程
    int pid_a = my_thread_create("A");
    int pid_b = my_thread_create("B");

    // 4. 等待测试结束
    sys_waitpid(pid_a);
    sys_waitpid(pid_b);

    // 5. 收尾
    // 这一步是非阻塞的清理。如果之前的填充进程因为信箱满卡住了，
    // 现在测试跑完了（消费了数据），它应该早就通了并退出了。
    sys_waitpid(pid_fill); 

    sys_move_cursor(0, LINE_MAIN + 7);
    printf("--- All Tasks Finished. Test Passed! ---\n");
    return 0;
}
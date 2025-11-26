#include <unistd.h>
#include <stdio.h>
#include <string.h>

/* ==========================================
 * 1. 解决 NULL 未定义的问题
 * ========================================== */
#ifndef NULL
#define NULL ((void *)0)
#endif

#define MBOX_CAPACITY 64  // 内核设定mbox容量为 64 字节
#define MSG_LEN 1

/* ==========================================
 * 3. 进程 A 的逻辑 (子程序 1)
 * ========================================== */
void task_a(void)
{
    int m1 = sys_mbox_open("mbox1");
    int m2 = sys_mbox_open("mbox2");
    char msg[] = "A";
    char buf[10];

    printf("[Child A] Start. I hold mbox2(reader), need mbox1(writer).\n");
    
    // 此时 mbox1 已经被主程序填满了
    // 1. 尝试向 mbox1 发送 -> 阻塞 (Deadlock!)
    //    因为它在等 mbox1 有空位，而空位需要 B 来读取
    printf("[Child A] Trying to send to mbox1 (Full)...\n");
    sys_mbox_send(m1, msg, MSG_LEN); 
    
    // 2. 如果发送成功（说明没死锁），则尝试读取 mbox2
    printf("[Child A] Sent success! (Deadlock failed)\n");
    sys_mbox_recv(m2, buf, MSG_LEN);
}

/* ==========================================
 * 4. 进程 B 的逻辑 (子程序 2)
 * ========================================== */
void task_b(void)
{
    int m1 = sys_mbox_open("mbox1");
    int m2 = sys_mbox_open("mbox2");
    char msg[] = "B";
    char buf[10];

    printf("[Child B] Start. I hold mbox1(reader), need mbox2(writer).\n");

    // 此时 mbox2 已经被主程序填满了
    // 1. 尝试向 mbox2 发送 -> 阻塞 (Deadlock!)
    //    因为它在等 mbox2 有空位，而空位需要 A 来读取
    printf("[Child B] Trying to send to mbox2 (Full)...\n");
    sys_mbox_send(m2, msg, MSG_LEN);

    // 2. 如果发送成功，尝试读取 mbox1
    printf("[Child B] Sent success! (Deadlock failed)\n");
    sys_mbox_recv(m1, buf, MSG_LEN);
}

/* ==========================================
 * 5. 主程序逻辑
 * ========================================== */
int main(int argc, char *argv[])
{
    // --- 分流逻辑：根据参数判断是变成 A 还是 B ---
    if (argc > 1) {
        if (strcmp(argv[1], "A") == 0) {
            task_a();
            return 0;
        } else if (strcmp(argv[1], "B") == 0) {
            task_b();
            return 0;
        }
    }

    // --- 主控逻辑：初始化环境并启动子进程 ---
    printf("--- Deadlock Test: Init ---\n");

    int m1 = sys_mbox_open("mbox1");
    int m2 = sys_mbox_open("mbox2");
    char fill_char = 'F';

    // 1. 先把两个邮箱彻底填满！
    printf("--- Main: Filling mailboxes (%d bytes)... ---\n", MBOX_CAPACITY);
    for (int i = 0; i < MBOX_CAPACITY; i++) {
        sys_mbox_send(m1, &fill_char, 1);
        sys_mbox_send(m2, &fill_char, 1);
    }
    printf("--- Main: Mailboxes filled! ---\n");

    // 2. 准备启动参数
    // 注意：这里的第一个参数通常是程序名，后面是参数，最后必须是 NULL
    char *args_a[] = {"deadlock_test", "A", NULL};
    char *args_b[] = {"deadlock_test", "B", NULL};

    // 3. 启动两个子进程
    // sys_exec 返回的是 pid
    printf("--- Main: Launching Child A and Child B ---\n");
    int pid_a = sys_exec("deadlock_test", 2, args_a);
    int pid_b = sys_exec("deadlock_test", 2, args_b);

    // 4. 等待 (实际上会永远等待)
    printf("--- Main: Waiting for children (Should Hang) ---\n");
    sys_waitpid(pid_a);
    sys_waitpid(pid_b);

    return 0;
}
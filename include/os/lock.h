/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                   Thread Lock
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#ifndef INCLUDE_LOCK_H_
#define INCLUDE_LOCK_H_

#include <os/list.h>

#define LOCK_NUM 16

typedef enum {
    UNLOCKED,
    LOCKED,
} lock_status_t;

typedef struct spin_lock
{
    volatile lock_status_t status;
} spin_lock_t;

typedef struct mutex_lock
{
    spin_lock_t lock;
    list_head block_queue;
    int key;
    int pid;
} mutex_lock_t;


void init_locks(void);

void spin_lock_init(spin_lock_t *lock);
int spin_lock_try_acquire(spin_lock_t *lock);
void spin_lock_acquire(spin_lock_t *lock);
void spin_lock_release(spin_lock_t *lock);

int do_mutex_lock_init(int key);
void do_mutex_lock_acquire(int mlock_idx);
void do_mutex_lock_release(int mlock_idx);
void check_and_release_locks(pid_t pid);

/************************************************************/
typedef struct barrier
{
    // TODO [P3-TASK2 barrier]
    int key;            // 唯一标识符
    int goal;           // 目标到达的进程数量
    int current_count;  // 当前已到达的进程数量
    list_head wait_queue;
} barrier_t;

#define BARRIER_NUM 16

void init_barriers(void);
int do_barrier_init(int key, int goal);
void do_barrier_wait(int bar_idx);
void do_barrier_destroy(int bar_idx);

typedef struct condition
{
    // TODO [P3-TASK2 condition]
    int key;            // 唯一标识符
    list_head wait_queue;
} condition_t;

#define CONDITION_NUM 16

void init_conditions(void);
int do_condition_init(int key);
void do_condition_wait(int cond_idx, int mutex_idx);
void do_condition_signal(int cond_idx);
void do_condition_broadcast(int cond_idx);
void do_condition_destroy(int cond_idx);

typedef struct semaphore
{
    // TODO [P3-TASK2 semaphore]
} semaphore_t;

#define SEMAPHORE_NUM 16

void init_semaphores(void);
int do_semaphore_init(int key, int init);
void do_semaphore_up(int sema_idx);
void do_semaphore_down(int sema_idx);
void do_semaphore_destroy(int sema_idx);

#define MAX_MBOX_LENGTH (64)
#define MBOX_NAME_LEN (32)
typedef struct mailbox
{
    // TODO [P3-TASK2 mailbox]
    char name[MBOX_NAME_LEN];  // 信箱名字
    char buf[MAX_MBOX_LENGTH]; // 数据缓冲区
    int head;                  // 读指针
    int tail;                  // 写指针
    int count;                 // 当前缓冲区内数据字节数
    int open_refs;             // 引用计数，0 表示未使用
    
    spin_lock_t lock;          // 保护该信箱的自旋锁
    
    list_head send_wait_queue; // 发送等待队列 (满时等待)
    list_head recv_wait_queue;
} mailbox_t;

#define MBOX_NUM 16
void init_mbox();
int do_mbox_open(char *name);
void do_mbox_close(int mbox_idx);
int do_mbox_send(int mbox_idx, void * msg, int msg_length);
int do_mbox_recv(int mbox_idx, void * msg, int msg_length);

/************************************************************/

// P4 Task5 ----------------- pipe 管道 -----------------
#define PIPE_NAME_LEN 32
#define PIPE_BUFFER_SIZE 1024 // 缓冲区大小，单位是“页” (即支持缓存 1024 * 4KB 数据)
#define MAX_PIPES 16

typedef struct pipe {
    int valid;                  // 是否有效/被占用
    char name[PIPE_NAME_LEN];   // 管道名称
    
    // 循环队列，存储的是物理页号 (PPN)
    uint64_t page_buffer[PIPE_BUFFER_SIZE]; 
    int head;                   // 写索引
    int tail;                   // 读索引
    int count;                  // 当前缓冲区内的页数

    spin_lock_t lock;           // 自旋锁
    
    list_head send_wait_queue;  // 管道满时，发送者等待队列
    list_head recv_wait_queue;  // 管道空时，接收者等待队列
} pipe_t;

void init_pipes(void);
int do_pipe_open(const char *name);
long do_pipe_take_pages(int pipe_idx, void *src, size_t length);
long do_pipe_give_pages(int pipe_idx, void *dst, size_t length);


#endif

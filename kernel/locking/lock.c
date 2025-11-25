#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>
#include <os/string.h>
mutex_lock_t mlocks[LOCK_NUM];
// 由于 mutex_lock_t 结构体没有状态字段，我们创建一个并行的状态数组来跟踪每个锁的状态
static lock_status_t mlock_status[LOCK_NUM];


// 用于保护 mlocks 数组在初始化/分配过程中的原子性
static spin_lock_t mlock_alloc_lock;


void init_locks(void)
{
    /* TODO: [p2-task2] initialize mlocks */

    spin_lock_init(&mlock_alloc_lock);
    for (int i = 0; i < LOCK_NUM; i++)
    {
        spin_lock_init(&mlocks[i].lock);
        list_init(&mlocks[i].block_queue);
        mlocks[i].key = -1; // -1 表示该锁槽位未使用
        mlock_status[i] = UNLOCKED;
    }
}

void spin_lock_init(spin_lock_t *lock)
{
    /* TODO: [p2-task2] initialize spin lock */
    lock->status = UNLOCKED;
}

int spin_lock_try_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] try to acquire spin lock */
    return (atomic_swap(LOCKED, (ptr_t)&lock->status) == UNLOCKED);
}

void spin_lock_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] acquire spin lock */
    while (!spin_lock_try_acquire(lock))
    {
        // loop and wait
    }
}

void spin_lock_release(spin_lock_t *lock)
{
    /* TODO: [p2-task2] release spin lock */
    lock->status = UNLOCKED;
}

int do_mutex_lock_init(int key)
{
    /* TODO: [p2-task2] initialize mutex lock */
    spin_lock_acquire(&mlock_alloc_lock);

    // 查找是否已存在基于此 key 的锁
    for (int i = 0; i < LOCK_NUM; i++)
    {
        if (mlocks[i].key == key)
        {
            spin_lock_release(&mlock_alloc_lock);
            return i; // 返回已存在的锁的句柄 (索引)
        }
    }

    // 查找一个未使用的锁槽位进行初始化
    int lock_idx = -1;
    for (int i = 0; i < LOCK_NUM; i++)
    {
        if (mlocks[i].key == -1)
        {
            mlocks[i].key = key;
            lock_idx = i;
            break;
        }
    }

    spin_lock_release(&mlock_alloc_lock);
    return lock_idx; // 返回新锁的句柄，如果没找到则返回-1
 
}

void do_mutex_lock_acquire(int mlock_idx)
{
    /* TODO: [p2-task2] acquire mutex lock */
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) return;

    mutex_lock_t *lock = &mlocks[mlock_idx];

    while (1) {
        // 使用自旋锁保护对锁状态和阻塞队列的访问
        spin_lock_acquire(&lock->lock);

        if (mlock_status[mlock_idx] == UNLOCKED)
        {
            // 锁是可用的，获取它并立即返回
            mlock_status[mlock_idx] = LOCKED;
            lock->pid = current_running->pid;
            spin_lock_release(&lock->lock);
            return;
        }
        else
        {
            // 锁被占用，将当前进程阻塞
            do_block(&current_running->list, &lock->block_queue);
            // 释放自旋锁，允许其他进程（特别是持有锁的进程）运行
            spin_lock_release(&lock->lock);
            // 调用调度器切换进程
            do_scheduler();
            // 被唤醒后，循环再次尝试获取锁
        }
    }
}

void do_mutex_lock_release(int mlock_idx)
{
    /* TODO: [p2-task2] release mutex lock */
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) return;

    mutex_lock_t *lock = &mlocks[mlock_idx];

    spin_lock_acquire(&lock->lock);

    // 1. 无论如何，都将锁的状态设为 UNLOCKED
    // mlock_status[mlock_idx] = UNLOCKED;

    // // 2. 如果有等待者，唤醒队首的一个
    // if (!list_empty(&lock->block_queue))
    // {
    //     do_unblock(lock->block_queue.next);
    // }
    
    if (lock->pid == current_running->pid) { // 安全检查：只有持有者才能释放锁
    mlock_status[mlock_idx] = UNLOCKED;
    lock->pid = -1; // <<-- 清除持有者！
    if (!list_empty(&lock->block_queue)) {
        do_unblock(lock->block_queue.next);
    }
}
    spin_lock_release(&lock->lock);
}



barrier_t barriers[BARRIER_NUM];

void init_barriers(void)
{
    for (int i = 0; i < BARRIER_NUM; i++)
    {
        barriers[i].key = -1; // -1 表示未使用
        barriers[i].goal = 0;
        barriers[i].current_count = 0;
        list_init(&barriers[i].wait_queue);
    }
}

int do_barrier_init(int key, int goal)
{
    // 1. 检查是否已经存在该 key 的 barrier
    for (int i = 0; i < BARRIER_NUM; i++)
    {
        if (barriers[i].key == key)
        {
            return i;
        }
    }

    // 2. 寻找一个空的槽位
    for (int i = 0; i < BARRIER_NUM; i++)
    {
        if (barriers[i].key == -1)
        {
            barriers[i].key = key;
            barriers[i].goal = goal;
            barriers[i].current_count = 0;
            list_init(&barriers[i].wait_queue);
            return i;
        }
    }

    return -1; // 没有空闲的 barrier
}

void do_barrier_wait(int bar_idx)
{
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM) return;
    
    barrier_t *bar = &barriers[bar_idx];

    // 增加到达计数
    bar->current_count++;

    if (bar->current_count >= bar->goal)
    {
        // 最后一个到达的进程：重置计数器并唤醒所有人
        bar->current_count = 0;
        
        while (!list_empty(&bar->wait_queue))
        {
            // 唤醒队首
            do_unblock(bar->wait_queue.next);
        }
    }
    else
    {
        // 不是最后一个：阻塞自己，等待被唤醒
        do_block(&current_running->list, &bar->wait_queue);
        do_scheduler();
    }
}

void do_barrier_destroy(int bar_idx)
{
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM) return;
    
    // 简单地标记为未使用，实际工程中可能需要处理还未被唤醒的进程
    barriers[bar_idx].key = -1;
    barriers[bar_idx].goal = 0;
    barriers[bar_idx].current_count = 0;
    // list_init(&barriers[bar_idx].wait_queue);
}


condition_t conditions[CONDITION_NUM];

void init_conditions(void)
{
    for (int i = 0; i < CONDITION_NUM; i++)
    {
        conditions[i].key = -1;
        list_init(&conditions[i].wait_queue);
    }
}

int do_condition_init(int key)
{
    // 1. 查找是否存在
    for (int i = 0; i < CONDITION_NUM; i++)
    {
        if (conditions[i].key == key)
        {
            return i;
        }
    }

    // 2. 寻找空位
    for (int i = 0; i < CONDITION_NUM; i++)
    {
        if (conditions[i].key == -1)
        {
            conditions[i].key = key;
            list_init(&conditions[i].wait_queue);
            return i;
        }
    }

    return -1;
}

void do_condition_wait(int cond_idx, int mutex_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;

    condition_t *cond = &conditions[cond_idx];

  
    // 2. 阻塞当前进程
    do_block(&current_running->list, &cond->wait_queue);

    do_mutex_lock_release(mutex_idx);

    // 3. 调度 (让出 CPU)
    do_scheduler();

    // 4. 被唤醒后，重新获取互斥锁
    do_mutex_lock_acquire(mutex_idx);
}

void do_condition_signal(int cond_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;

    condition_t *cond = &conditions[cond_idx];

    // 唤醒队列中的第一个等待者
    if (!list_empty(&cond->wait_queue))
    {
        do_unblock(cond->wait_queue.next);
    }
}

void do_condition_broadcast(int cond_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;

    condition_t *cond = &conditions[cond_idx];

    // 唤醒队列中所有的等待者
    while (!list_empty(&cond->wait_queue))
    {
        do_unblock(cond->wait_queue.next);
    }
}

void do_condition_destroy(int cond_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;

    conditions[cond_idx].key = -1;
}


mailbox_t mboxes[MBOX_NUM];

void init_mbox()
{
    for (int i = 0; i < MBOX_NUM; i++)
    {
        mboxes[i].open_refs = 0;
        mboxes[i].name[0] = '\0';
        mboxes[i].head = 0;
        mboxes[i].tail = 0;
        mboxes[i].count = 0;
        
        spin_lock_init(&mboxes[i].lock);
        list_init(&mboxes[i].send_wait_queue);
        list_init(&mboxes[i].recv_wait_queue);
    }
}

int do_mbox_open(char *name)
{
    // 1. 查找是否已存在同名信箱
    for (int i = 0; i < MBOX_NUM; i++)
    {
        if (mboxes[i].open_refs > 0 && strcmp(mboxes[i].name, name) == 0)
        {
            spin_lock_acquire(&mboxes[i].lock);
            mboxes[i].open_refs++;
            spin_lock_release(&mboxes[i].lock);
            return i;
        }
    }

    // 2. 如果不存在，找一个空闲槽位创建
    for (int i = 0; i < MBOX_NUM; i++)
    {
        if (mboxes[i].open_refs == 0)
        {
            spin_lock_acquire(&mboxes[i].lock);
            // 双重检查，防止并发竞争
            if (mboxes[i].open_refs == 0) 
            {
                strcpy(mboxes[i].name, name);
                mboxes[i].open_refs = 1;
                mboxes[i].head = 0;
                mboxes[i].tail = 0;
                mboxes[i].count = 0;
                // 确保队列被清空 (虽然 init_mbox 做过，但为了安全)
                list_init(&mboxes[i].send_wait_queue);
                list_init(&mboxes[i].recv_wait_queue);
                
                spin_lock_release(&mboxes[i].lock);
                return i;
            }
            spin_lock_release(&mboxes[i].lock);
        }
    }

    // 没有空闲信箱
    return -1;
}

void do_mbox_close(int mbox_idx)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return;
    
    mailbox_t *mbox = &mboxes[mbox_idx];
    
    spin_lock_acquire(&mbox->lock);
    
    if (mbox->open_refs > 0)
    {
        mbox->open_refs--;
        if (mbox->open_refs == 0)
        {

            mbox->name[0] = '\0';
            mbox->count = 0;
            mbox->head = 0;
            mbox->tail = 0;
        }
    }
    
    spin_lock_release(&mbox->lock);
}

int do_mbox_send(int mbox_idx, void * msg, int msg_length)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return 0;
    if (msg_length <= 0) return 0;
    // 如果单条消息超过信箱总容量，永远不可能发送成功，直接返回失败
    if (msg_length > MAX_MBOX_LENGTH) return 0; 

    mailbox_t *mbox = &mboxes[mbox_idx];
    char *data = (char *)msg;

    spin_lock_acquire(&mbox->lock);


    while ( (MAX_MBOX_LENGTH - mbox->count) < msg_length )
    {
        // 空间不足以放下**整条**消息，进入发送等待队列
        do_block(&current_running->list, &mbox->send_wait_queue);
        
        spin_lock_release(&mbox->lock);
        do_scheduler(); // 调度出去
        spin_lock_acquire(&mbox->lock); // 醒来后重新抢锁检查条件
        
        // 唤醒后检查信箱是否被非法关闭
        if (mbox->open_refs == 0) {
            spin_lock_release(&mbox->lock);
            return 0;
        }
    }

    // 程序走到这里，说明持有锁，且空间足够一次性写入 msg_length
    for (int i = 0; i < msg_length; i++)
    {
        mbox->buf[mbox->tail] = data[i];
        mbox->tail = (mbox->tail + 1) % MAX_MBOX_LENGTH;
    }
    
    mbox->count += msg_length;

    // 写入了数据，唤醒所有等待接收的进程
    while (!list_empty(&mbox->recv_wait_queue))
    {
        do_unblock(mbox->recv_wait_queue.next);
    }

    spin_lock_release(&mbox->lock);
    return msg_length; // 返回成功发送的字节数
}


int do_mbox_recv(int mbox_idx, void * msg, int msg_length)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return 0;
    if (msg_length <= 0) return 0;
    
    mailbox_t *mbox = &mboxes[mbox_idx];
    char *data = (char *)msg;

    spin_lock_acquire(&mbox->lock);

    while (mbox->count < msg_length)
    {
        // 数据量不足，进入接收等待队列
        do_block(&current_running->list, &mbox->recv_wait_queue);
        
        spin_lock_release(&mbox->lock);
        do_scheduler();
        spin_lock_acquire(&mbox->lock);
        
        if (mbox->open_refs == 0) {
            spin_lock_release(&mbox->lock);
            return 0;
        }
    }

    // 开始原子读取
    for (int i = 0; i < msg_length; i++)
    {
        data[i] = mbox->buf[mbox->head];
        mbox->head = (mbox->head + 1) % MAX_MBOX_LENGTH;
    }
    
    mbox->count -= msg_length;

    // 读走了数据，腾出了空间，唤醒所有等待发送的进程
    while (!list_empty(&mbox->send_wait_queue))
    {
        do_unblock(mbox->send_wait_queue.next);
    }

    spin_lock_release(&mbox->lock);
    return msg_length;
}

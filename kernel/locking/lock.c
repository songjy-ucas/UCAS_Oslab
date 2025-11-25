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
    if (lock->pid == current_running->pid) { // 安全检查：只有持有者才能释放锁
    mlock_status[mlock_idx] = UNLOCKED;
    lock->pid = -1; // <<-- 清除持有者！
    if (!list_empty(&lock->block_queue)) {
        do_unblock(lock->block_queue.next);
    }
}
    spin_lock_release(&lock->lock);
}

// 辅助函数：释放锁
void check_and_release_locks(pid_t pid)
{
    // 遍历所有可能的互斥锁
    for (int i = 0; i < LOCK_NUM; ++i) {
        mutex_lock_t *lock = &mlocks[i];
        
        // 1. 获取自旋锁，保证操作原子性
        // 虽然被杀的进程不会再动，但其他进程可能正在访问这个锁的队列
        spin_lock_acquire(&lock->lock);

        // 2. 检查条件：锁必须是 LOCKED 状态，且持有者 PID 必须匹配
        if (mlock_status[i] == LOCKED && lock->pid == pid) {
            
            // 3. 强制释放锁
            mlock_status[i] = UNLOCKED;
            lock->pid = -1; // 清除持有者

            // 4. 检查等待队列，如果有等待者，唤醒它
            if (!list_empty(&lock->block_queue)) {
                // 取出等待队列的第一个节点
                list_node_t *waiter = lock->block_queue.next;
                
                // 调用 sched.c 中的 unblock 将其移回就绪队列
                // 注意：在 Mesa 管程语义下，唤醒的进程会再次尝试 atomic_swap 获取锁
                // 因为我们将状态设为了 UNLOCKED，它应该能成功
                do_unblock(waiter);
            }
        }
        
        // 5. 释放自旋锁
        spin_lock_release(&lock->lock);
    }
}
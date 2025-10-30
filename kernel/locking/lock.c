#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>
#include <printk.h> // 引入 printk 用于调试

mutex_lock_t mlocks[LOCK_NUM];

void init_locks(void)
{
    /* TODO: [p2-task2] initialize mlocks */
    for (int i = 0; i < LOCK_NUM; i++) {
        // 初始化每个互斥锁
        spin_lock_init(&mlocks[i].lock);    // 初始化内部的自旋锁
        list_init(&mlocks[i].block_queue);  // 初始化阻塞队列
        mlocks[i].key = -1;                 // -1 表示这个锁槽位当前是空闲的，未被任何 key 占用
    }
}

void spin_lock_init(spin_lock_t *lock)
{
    /* TODO: [p2-task2] initialize spin lock */
    lock->status = UNLOCKED;
}

int spin_lock_try_acquire(spin_lock_t *lock) //尝试获取一次锁，但如果失败了，它不会等待，而是立即返回
{
    /* TODO: [p2-task2] try to acquire spin lock */
    // 使用“比较并交换”：尝试将锁从 UNLOCKED 状态变为 LOCKED 状态，`atomic_cmpxchg` 会返回操作前内存中的原始值
    uint32_t old_status = atomic_cmpxchg(UNLOCKED, LOCKED, (ptr_t)&lock->status);

    // 如果返回的原始值是 UNLOCKED，说明成功地完成了“比较并交换”，锁已到手
    return (old_status == UNLOCKED); // 成功返回 1 ，失败返回 0 
}

void spin_lock_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] acquire spin lock */
    // 使用一个 while 循环，不断地尝试获取锁，直到成功为止
    while (atomic_swap(LOCKED, (ptr_t)&lock->status) == LOCKED){
    
    }
}

void spin_lock_release(spin_lock_t *lock)
{
    /* TODO: [p2-task2] release spin lock */
    // 原子地将锁的状态设置回 UNLOCKED
    atomic_swap(UNLOCKED, (ptr_t)&lock->status);
}

int do_mutex_lock_init(int key) // 初始化一个互斥锁，并返回其handle
{
    /* TODO: [p2-task2] initialize mutex lock */
    int free_slot = -1;

    // 遍历全局锁数组，寻找与 key 匹配的锁或一个空闲槽位
    for (int i = 0; i < LOCK_NUM; i++) {
        if (mlocks[i].key == key) {
            // 如果已经存在一个锁对应这个 key，直接返回它的句柄 (即数组下标)
            return i;
        }
        // 记录下第一个遇到的空闲槽位
        if (mlocks[i].key == -1 && free_slot == -1) {
            free_slot = i;
        }
    }

    // 如果没有找到匹配的 key，但找到了空闲槽位
    if (free_slot != -1) {
        mlocks[free_slot].key = key; // 将这个 key 与空闲槽位绑定
        return free_slot;            // 返回新创建的锁的句柄
    }

    // 所有锁槽位都已被占用，且没有与 key 匹配的
    printk("Error: No free mutex lock available.\n");
    return -1; // 返回 -1 表示失败
}

void do_mutex_lock_acquire(int mlock_idx) //获取一个互斥锁
{
    /* TODO: [p2-task2] acquire mutex lock */
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) return; // 检查handle合法性

    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) return;

    // 尝试获取自旋锁，如果成功，就代表获取了互斥锁，直接返回
    if (spin_lock_try_acquire(&mlocks[mlock_idx].lock)) {
        return;
    }

    // 获取锁失败，说明锁被占用，需要阻塞当前进程，do_block 会将当前进程放入阻塞队列并调度走
    do_block(&current_running->list, &mlocks[mlock_idx].block_queue);
    do_scheduler(); // 在这里去调度，不要在do_block里面调度，因为do_block不一定是current_running
}

void do_mutex_lock_release(int mlock_idx) // 释放一个互斥锁
{
    /* TODO: [p2-task2] release mutex lock */
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) return; // 检查handle合法性

    list_head *queue = &mlocks[mlock_idx].block_queue;

    // 检查阻塞队列是否为空
    if (list_is_empty(queue)) {
        // 如果没有进程在等待，就直接释放自旋锁（即释放互斥锁）
        spin_lock_release(&mlocks[mlock_idx].lock);
    } else {
        // 如果有进程在等待，不释放锁，而是直接唤醒队首的进程
        do_unblock(queue->next);
    }
}

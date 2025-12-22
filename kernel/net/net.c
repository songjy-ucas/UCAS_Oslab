#include <e1000.h>
#include <type.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/smp.h>
#include <os/irq.h>
#include <os/debug.h>

static spin_lock_t net_queue_lock;

void init_net_lock(void)
{
    spin_lock_init(&net_queue_lock);
}

static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

// 【辅助函数】：清空并唤醒队列中的所有进程
// 适配 do_unblock(list_node_t *node)
static void unblock_queue(list_head *queue)
{
    while (!list_empty(queue)) {
        // 1. 获取队列中的第一个节点（队首）
        list_node_t *node = queue->next;
        
        // 2. 调用你的 do_unblock
        // 注意：do_unblock 内部执行了 list_del(node)，
        // 这会将 node 从 queue 中移除，所以 queue 的长度会减 1
        do_unblock(node);
    }
}

int do_net_send(void *txpacket, int length)
{
    int trans_len = 0;
    
    while (1) {
        // 1. 尝试发送
        // 必须确保 drivers/e1000.c 中的 e1000_transmit 是非阻塞的
        // 如果队列满，它应该返回 0
        trans_len = e1000_transmit(txpacket, length);
        
        // 2. 发送成功 (>0)，直接返回
        if (trans_len > 0) {
            break;
        }
        
        // 3. 发送失败 (返回0)，说明硬件队列满
        // 启用"按需中断"机制
        
        // A. 开启 TXQE (发送队列空) 中断
        uint32_t ims = e1000_read_reg(e1000, E1000_IMS);
        e1000_write_reg(e1000, E1000_IMS, ims | E1000_IMS_TXQE);
        local_flush_dcache(); // 确保写寄存器生效
        
        // B. 阻塞当前进程
        // 使用你的 do_block 接口：传入当前进程节点和目标队列
        spin_lock_acquire(&net_queue_lock);
        do_block(&current_running->list, &send_block_queue);
        spin_lock_release(&net_queue_lock);
        
        // C. 【必须】主动触发调度，让出 CPU
        // 因为你的 do_block 只改状态不调度
        do_scheduler();
        
        // D. 进程被唤醒后（此时中断已在处理函数中关闭），
        // 回到 while(1) 循环头部，再次尝试调用 e1000_transmit
    }
    
    return trans_len;
}

int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens)
{
    int total_len = 0;
    
    for (int i = 0; i < pkt_num; ) {
        // 1. 尝试轮询获取一个包
        int len = e1000_poll(rxbuffer + total_len);
        
        if (len > 0) {
            // 成功收到包
            pkt_lens[i] = len;
            total_len += len;
            i++; // 处理下一个包
        } else {
            // 没收到包，说明硬件队列空
            
            // A. 开启 RXDMT0 (接收描述符阈值) 中断
            uint32_t ims = e1000_read_reg(e1000, E1000_IMS);
            e1000_write_reg(e1000, E1000_IMS, ims | E1000_IMS_RXDMT0);
            local_flush_dcache();
            
            // B. 阻塞当前进程
            spin_lock_acquire(&net_queue_lock);
            do_block(&current_running->list, &recv_block_queue);
            spin_lock_release(&net_queue_lock);
            
            // C. 【必须】主动触发调度
            do_scheduler();
            
            // D. 被唤醒后，直接 continue，重新 while 循环尝试 e1000_poll
        }
    }

    return total_len;
}

// 具体的中断处理函数：TXQE
static void handle_e1000_txqe(void)
{
    // 1. 唤醒所有等待发送的进程
    spin_lock_acquire(&net_queue_lock);
    unblock_queue(&send_block_queue);
    spin_lock_release(&net_queue_lock);
    
    // 2. 关闭 TXQE 中断 (写入 IMC)
    // 既然进程醒了会去轮询，就不需要中断频繁打扰了
    e1000_write_reg(e1000, E1000_IMC, E1000_IMC_TXQE);
    local_flush_dcache();
}

// 具体的中断处理函数：RXDMT0
static void handle_e1000_rxdmt0(void)
{
    // 1. 唤醒所有等待接收的进程
    spin_lock_acquire(&net_queue_lock);
    unblock_queue(&recv_block_queue);
    spin_lock_release(&net_queue_lock);
    
    // 2. 关闭 RXDMT0 中断 (写入 IMC)
    e1000_write_reg(e1000, E1000_IMC, E1000_IMC_RXDMT0);
    local_flush_dcache();
}

// 总入口：net_handle_irq
void net_handle_irq(void)
{
    local_flush_dcache();
    // 读取 ICR，读操作会自动清除 ICR 中的标志位
    uint32_t icr = e1000_read_reg(e1000, E1000_ICR);
    
    // 检查是否是 TXQE
    if (icr & E1000_ICR_TXQE) {
        handle_e1000_txqe();
        klog("[E1000] Tx Done Interrupt!\n");
    }
    
    // 检查是否是 RXDMT0
    if (icr & E1000_ICR_RXDMT0) {
        handle_e1000_rxdmt0();
        klog("[E1000] Rx Interrupt!\n");
    }
}
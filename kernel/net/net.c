#include <e1000.h>
#include <type.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/list.h>
#include <os/smp.h>

static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

int do_net_send(void *txpacket, int length)
{
    // TODO: [p5-task1] Transmit one network packet via e1000 device
    // 直接调用 e1000 发送函数
    // e1000_transmit 内部处理了判满阻塞（轮询）和地址转换
    return e1000_transmit(txpacket, length);
        
    // TODO: [p5-task3] Call do_block when e1000 transmit queue is full
    // TODO: [p5-task4] Enable TXQE interrupt if transmit queue is full

    return 0;  // Bytes it has transmitted
}

int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens)
{
    // TODO: [p5-task2] Receive one network packet via e1000 device
    int total_len = 0; // 记录总共收到了多少字节
    
    // 循环接收 pkt_num 个包
    for (int i = 0; i < pkt_num; i++) {
        int len = 0;
        
        // 【关键点】：轮询等待 (Busy Wait)
        // 如果 e1000_poll 返回 0，说明还没收到包，就一直循环查，直到收到为止
        while ((len = e1000_poll(rxbuffer + total_len)) == 0) {
            // Task 2 允许死循环等待，也可以在这里加个 sys_yield() 让出 CPU
            do_scheduler(); 
        }

        // 收到包了，记录长度
        pkt_lens[i] = len;
        
        // 更新总长度，以便下一个包存在 rxbuffer 的后面
        total_len += len;
    }

    return total_len;  // 返回接收到的总字节数
    // TODO: [p5-task3] Call do_block when there is no packet on the way

    // return 0;  // Bytes it has received
}

void net_handle_irq(void)
{
    // TODO: [p5-task4] Handle interrupts from network device
}
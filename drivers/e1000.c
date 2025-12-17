#include <e1000.h>
#include <type.h>
#include <os/string.h>
#include <os/time.h>
#include <assert.h>
#include <pgtable.h>

// E1000 Registers Base Pointer
volatile uint8_t *e1000;  // use virtual memory address

// E1000 Tx & Rx Descriptors
static struct e1000_tx_desc tx_desc_array[TXDESCS] __attribute__((aligned(16)));
static struct e1000_rx_desc rx_desc_array[RXDESCS] __attribute__((aligned(16)));

// E1000 Tx & Rx packet buffer
static char tx_pkt_buffer[TXDESCS][TX_PKT_SIZE];
static char rx_pkt_buffer[RXDESCS][RX_PKT_SIZE];

// Fixed Ethernet MAC Address of E1000
static const uint8_t enetaddr[6] = {0x00, 0x0a, 0x35, 0x00, 0x1e, 0x53};

/**
 * e1000_reset - Reset Tx and Rx Units; mask and clear all interrupts.
 **/
static void e1000_reset(void)
{
	/* Turn off the ethernet interface */
    e1000_write_reg(e1000, E1000_RCTL, 0);
    e1000_write_reg(e1000, E1000_TCTL, 0);

	/* Clear the transmit ring */
    e1000_write_reg(e1000, E1000_TDH, 0);
    e1000_write_reg(e1000, E1000_TDT, 0);

	/* Clear the receive ring */
    e1000_write_reg(e1000, E1000_RDH, 0);
    e1000_write_reg(e1000, E1000_RDT, 0);

	/**
     * Delay to allow any outstanding PCI transactions to complete before
	 * resetting the device
	 */
    latency(1);

	/* Clear interrupt mask to stop board from generating interrupts */
    e1000_write_reg(e1000, E1000_IMC, 0xffffffff);

    /* Clear any pending interrupt events. */
    while (0 != e1000_read_reg(e1000, E1000_ICR)) ;
}

/**
 * e1000_configure_tx - Configure 8254x Transmit Unit after Reset
 **/
static void e1000_configure_tx(void)    // 该函数用于 E1000 发送初始化
{
    /* TODO: [p5-task1] Initialize tx descriptors */
    // 1. 清零所有发送描述符
    for (int i = 0; i < TXDESCS; i++) {
        bzero(&tx_desc_array[i], sizeof(struct e1000_tx_desc));
        // 这里不需要预先填 Buffer Address，发送时动态填入即可
    }

    /* TODO: [p5-task1] Set up the Tx descriptor base address and length */
    // 2. 将描述符数组的【物理地址】写入 TDBAL 和 TDBAH
    // 注意：必须使用 kva2pa 将内核虚地址转换为物理地址
    uint64_t tx_desc_pa = kva2pa((uintptr_t)tx_desc_array);
    e1000_write_reg(e1000, E1000_TDBAL, (uint32_t)(tx_desc_pa & 0xffffffff));
    e1000_write_reg(e1000, E1000_TDBAH, (uint32_t)(tx_desc_pa >> 32));

    // 3. 设置数组长度 (字节数 = 描述符个数 * 16)
    e1000_write_reg(e1000, E1000_TDLEN, TXDESCS * sizeof(struct e1000_tx_desc));

	/* TODO: [p5-task1] Set up the HW Tx Head and Tail descriptor pointers */
    // 4. 初始化 Head 和 Tail 指针为 0
    e1000_write_reg(e1000, E1000_TDH, 0);
    e1000_write_reg(e1000, E1000_TDT, 0);

    /* TODO: [p5-task1] Program the Transmit Control Register */
    // 5. 设置 TCTL 寄存器
    // EN: Enable (bit 1)
    // PSP: Pad Short Packets (bit 3)
    // CT: Collision Threshold (bit 4-11), set to 0x10 (16)
    // COLD: Collision Distance (bit 12-21), set to 0x40 (64)
    uint32_t tctl = 0;
    tctl |= E1000_TCTL_EN;      // 使能发送
    tctl |= E1000_TCTL_PSP;     // 填充短包
    tctl |= (0x10 << 4);        // CT = 0x10
    tctl |= (0x40 << 12);       // COLD = 0x40
    
    e1000_write_reg(e1000, E1000_TCTL, tctl);
}

/**
 * e1000_configure_rx - Configure 8254x Receive Unit after Reset
 **/
static void e1000_configure_rx(void)
{
    /* TODO: [p5-task2] Set e1000 MAC Address to RAR[0] */

    /* TODO: [p5-task2] Initialize rx descriptors */

    /* TODO: [p5-task2] Set up the Rx descriptor base address and length */

    /* TODO: [p5-task2] Set up the HW Rx Head and Tail descriptor pointers */

    /* TODO: [p5-task2] Program the Receive Control Register */

    /* TODO: [p5-task4] Enable RXDMT0 Interrupt */
}

/**
 * e1000_init - Initialize e1000 device and descriptors
 **/
void e1000_init(void)
{
    /* Reset E1000 Tx & Rx Units; mask & clear all interrupts */
    e1000_reset();

    /* Configure E1000 Tx Unit */
    e1000_configure_tx();

    /* Configure E1000 Rx Unit */
    e1000_configure_rx();
}

/**
 * e1000_transmit - Transmit packet through e1000 net device
 * @param txpacket - The buffer address of packet to be transmitted
 * @param length - Length of this packet
 * @return - Number of bytes that are transmitted successfully
 **/
int e1000_transmit(void *txpacket, int length) // 该函数用于数据帧的发送
{
    /* TODO: [p5-task1] Transmit one packet from txpacket */
    
    // 1. 获取当前 Tail 指针位置 (软件写入的位置)
    uint32_t tx_tail = e1000_read_reg(e1000, E1000_TDT);
    
    // 2. 获取当前 Head 指针位置 (硬件处理到的位置)
    uint32_t tx_head = e1000_read_reg(e1000, E1000_TDH);

    // 3. 检查队列是否已满
    // 满的条件：(Tail + 1) % Size == Head (保留一个空位)
    uint32_t next_tail = (tx_tail + 1) % TXDESCS;
    
    // 对于 Task 1，如果满了，我们需要轮询等待（Busy Wait），直到硬件腾出空间
    while (next_tail == tx_head) {
        // 重新读取 Head，看硬件有没有往前走
        tx_head = e1000_read_reg(e1000, E1000_TDH);
        // 可以插入 yield 让出 CPU，防止死锁，但 Task 1 要求轮询
        // sys_yield(); 
    }

    // 4. 将用户数据拷贝到内核的专用发送缓冲区
    // 为什么要拷贝？因为 txpacket 可能是用户态地址，或者如果不拷贝直接DMA，
    // 用户可能会在DMA完成前修改数据。使用内核 buffer 更安全。
    // tx_pkt_buffer 也是静态分配的，kva2pa 转换很方便。
    if (length > TX_PKT_SIZE) length = TX_PKT_SIZE; // 截断防止溢出
    memcpy(tx_pkt_buffer[tx_tail], txpacket, length);

    // 5. 填写描述符
    // Buffer Address: 必须是【物理地址】
    tx_desc_array[tx_tail].addr = kva2pa((uintptr_t)tx_pkt_buffer[tx_tail]);
    // Length: 数据长度
    tx_desc_array[tx_tail].length = (uint16_t)length;
    // CMD: EOP (包结束) | RS (报告状态，让硬件处理完后写回 DD 位)
    tx_desc_array[tx_tail].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
    // Status: 清零，以便我们(可选地)后续检查 DD 位
    tx_desc_array[tx_tail].status = 0;

    // 6. 内存屏障 / 刷 Cache
    // 确保上面的描述符写操作真正进入内存，在通知 DMA 之前
    local_flush_dcache();

    // 7. 更新 Tail 指针，通知硬件开始发送
    e1000_write_reg(e1000, E1000_TDT, next_tail);

    // 成功发送
    return length;
}

/**
 * e1000_poll - Receive packet through e1000 net device
 * @param rxbuffer - The address of buffer to store received packet
 * @return - Length of received packet
 **/
int e1000_poll(void *rxbuffer)
{
    /* TODO: [p5-task2] Receive one packet and put it into rxbuffer */

    return 0;
}
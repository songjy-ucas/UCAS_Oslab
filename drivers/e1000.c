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
    // 1. 设置 MAC 地址 (Receive Address Register)
    // RAR[0] 由两个寄存器组成：RAL0 (低32位) 和 RAH0 (高16位 + 有效位)
    // MAC 地址是 {00, 0a, 35, 00, 1e, 53}
    // RAL0 存放：00:0a:35:00 (注意大小端，通常是低位在前)
    // RAH0 存放：1e:53 并置位 AV (Address Valid)
    
    // 组合 RAL0: 0x00350a00 (取决于 enetaddr 定义顺序，这里按字节序拼装)
    uint32_t ral = (enetaddr[3] << 24) | (enetaddr[2] << 16) | (enetaddr[1] << 8) | enetaddr[0];
    // 组合 RAH0: AV bit (31) | 0x531e
    uint32_t rah = E1000_RAH_AV | (enetaddr[5] << 8) | enetaddr[4];
    
    e1000_write_reg_array(e1000, E1000_RA, 0, ral);
    e1000_write_reg_array(e1000, E1000_RA, 1, rah);

    /* TODO: [p5-task2] Initialize rx descriptors */
    // 2. 初始化接收描述符
    // 我们需要把所有的接收 Buffer 的物理地址填给硬件，这样硬件一收到包就有地方放
    for (int i = 0; i < RXDESCS; i++) {
        bzero(&rx_desc_array[i], sizeof(struct e1000_rx_desc));
        // 关键：预先填入 buffer 的物理地址
        rx_desc_array[i].addr = kva2pa((uintptr_t)rx_pkt_buffer[i]);
        // 状态清零，之后如果 status 变了说明收到包了
        rx_desc_array[i].status = 0;
    }

    /* TODO: [p5-task2] Set up the Rx descriptor base address and length */
    // 3. 告诉硬件接收描述符环形队列在哪
    uint64_t rx_desc_pa = kva2pa((uintptr_t)rx_desc_array);
    e1000_write_reg(e1000, E1000_RDBAL, (uint32_t)(rx_desc_pa & 0xFFFFFFFF));
    e1000_write_reg(e1000, E1000_RDBAH, (uint32_t)(rx_desc_pa >> 32));
    e1000_write_reg(e1000, E1000_RDLEN, RXDESCS * sizeof(struct e1000_rx_desc));

    /* TODO: [p5-task2] Set up the HW Rx Head and Tail descriptor pointers */
    // 4. 初始化头尾指针
    // RDH (Head): 硬件当前处理到的位置，初始为 0
    e1000_write_reg(e1000, E1000_RDH, 0);
    
    // RDT (Tail): 软件告诉硬件“可以用到哪里”。
    // 初始状态下，所有描述符都是空的，都归硬件管。
    // 按照“Head 到 Tail 之间（不含 Tail）是硬件可用区”的规则，
    // 我们把 Tail 指向最后一个描述符，这样 0 到 RXDESCS-1 都是硬件可用的。
    e1000_write_reg(e1000, E1000_RDT, RXDESCS - 1);

    /* TODO: [p5-task2] Program the Receive Control Register */
    // 5. 设置 RCTL 寄存器
    // EN: Enable (开启接收)
    // BAM: Broadcast Accept Mode (接收广播包)
    // BSEX = 0, BSIZE = 0: 设置接收缓冲区大小为 2048 字节 (默认)
    // 【修改点】：添加 E1000_RCTL_RDMTS_HALF，为 Task 3 中断做准备
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_RDMTS_HALF;
    e1000_write_reg(e1000, E1000_RCTL, rctl);

    /* TODO: [p5-task4] Enable RXDMT0 Interrupt */
    // Task 2 不需要中断，暂时留空
    
    // 刷 Cache 确保配置生效
    local_flush_dcache();
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
    
    // // 对于 Task 1，如果满了，我们需要轮询等待（Busy Wait），直到硬件腾出空间
    // while (next_tail == tx_head) {
    //     // 重新读取 Head，看硬件有没有往前走
    //     tx_head = e1000_read_reg(e1000, E1000_TDH);
    //     // 可以插入 yield 让出 CPU，防止死锁，但 Task 1 要求轮询
    //     // sys_yield(); 
    // }

    // 【修改点】：移除死循环，如果满了直接返回 0
    if (next_tail == tx_head) {
        return 0; 
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
    
    // 1. 获取当前 RDT (Tail) 指针的位置
    // RDT 指向的是“最后一个空闲描述符”。
    // 那么 (RDT + 1) 就是“硬件应该存放下一个数据包的位置”。
    uint32_t tail = e1000_read_reg(e1000, E1000_RDT);
    uint32_t next_check = (tail + 1) % RXDESCS;

    // 2. 读取描述符状态前，先刷 Cache，防止读到旧数据
    local_flush_dcache();

    // 3. 检查 DD (Descriptor Done) 位
    // 如果 DD 位是 1，说明硬件已经把包放进来了，并且更新了状态。
    // 如果 DD 位是 0，说明还没收到包，直接返回。
    if (!(rx_desc_array[next_check].status & E1000_RXD_STAT_DD)) {
        return 0; // 没收到包
    }

    // 4. 收到包,读取数据长度
    uint16_t length = rx_desc_array[next_check].length;

    // 5. 将数据拷贝给用户 buffer
    // 注意：如果 rxbuffer 为空，说明用户只想查询是否有包（Peek），不想取走
    // 但通常这个函数设计为“取走一个包”。
    if (rxbuffer) {
        memcpy(rxbuffer, rx_pkt_buffer[next_check], length);
    }

    // 6. 重置描述符状态
    // 把 DD 位清零，把状态清空，准备让硬件下次再用这个坑位
    rx_desc_array[next_check].status = 0;
    rx_desc_array[next_check].length = 0;

    // 7. 更新 RDT 指针 (归还描述符)
    // 告诉硬件：“next_check 这个位置我（软件）已经处理完了，数据拿走了，
    // 现在这个坑位是空的了，交还给你（硬件）继续收新包用。”
    e1000_write_reg(e1000, E1000_RDT, next_check);
    
    // 更新后刷一下 Cache 
    local_flush_dcache();

    return length;
}
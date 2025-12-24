#include <e1000.h>
#include <type.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/list.h>
#include <os/smp.h>
#include <os/irq.h>
#include <os/mm.h>
#include <os/time.h> // 需要 get_ticks
#include <os/net.h>
#include <printk.h>  // 用于调试打印

// ================= 大小端转换宏 =================
static inline uint16_t htons(uint16_t v) { return (v << 8) | (v >> 8); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v) {
    return ((v & 0xff) << 24) | ((v & 0xff00) << 8) | 
           ((v & 0xff0000) >> 8) | ((v >> 24) & 0xff);
}
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }

// ================= 全局变量 =================
static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

// Task 4 全局状态
static uint32_t current_seq = 0;              // 当前期望的序列号
static reorder_slot_t reorder_buf[REORDER_BUF_SIZE]; // 静态乱序缓冲区
static char control_packet[128];              // 发送控制包的专用buffer

// ================= 辅助函数 =================

static void unblock_queue(list_head *queue) {
    while (!list_empty(queue)) {
        list_node_t *node = queue->next;
        do_unblock(node);
    }
}

// 简单的 Checksum 计算 (用于计算 IP 头校验和)
static uint16_t calc_checksum(void *data, int len) {
    uint32_t sum = 0;
    uint16_t *ptr = (uint16_t *)data;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len) sum += *(uint8_t *)ptr;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

// 初始化可靠传输层
void init_reliable_layer(void) {
    for(int i=0; i<REORDER_BUF_SIZE; i++) 
        reorder_buf[i].valid = 0;
    current_seq = 0;
}

// ================= Task 1 & 3: 发送逻辑 =================

int do_net_send(void *txpacket, int length) {
    int trans_len = 0;
    while (1) {
        trans_len = e1000_transmit(txpacket, length);
        if (trans_len > 0) break;

        // 队列满，开中断并阻塞
        e1000_write_reg(e1000, E1000_IMS, E1000_IMS_TXQE);
        local_flush_dcache();
        do_block(&current_running->list, &send_block_queue);
        do_scheduler();
    }
    return trans_len;
}

static void handle_e1000_txqe(void) {
    unblock_queue(&send_block_queue);
    e1000_write_reg(e1000, E1000_IMC, E1000_IMC_TXQE);
    local_flush_dcache();
}

// ================= Task 4: 发送控制包 (核心修复) =================
// 完全复刻同学的代码，伪造完整的协议栈头部
static void send_control(uint8_t flags, uint32_t seq) {
    // 1. 清空 buffer
    // 注意：这里使用 memset 0 很重要
    for(int i=0; i<128; i++) control_packet[i] = 0;

    /* --- 1. Ethernet Header (14 bytes) --- */
    struct ethhdr *eth = (struct ethhdr *)control_packet;
    // 目的 MAC: 宿主机 (根据同学代码硬编码)
    uint8_t dst_mac[6] = {0x80, 0xfa, 0x5b, 0x33, 0x56, 0xef}; 
    memcpy(eth->ether_dmac, dst_mac, 6); 
    // 源 MAC: 板卡 (伪造)
    uint8_t src_mac[6] = {0x00, 0x10, 0x53, 0x00, 0x30, 0x83};
    memcpy(eth->ether_smac, src_mac, 6);
    eth->ether_type = htons(0x0800); /* IPv4 */

    /* --- 2. IPv4 Header (20 bytes) --- */
    uint8_t *ip = (uint8_t *)(control_packet + 14);
    ip[0] = 0x45; // Ver=4, IHL=5
    ip[1] = 0x00; // TOS
    // Total Len = 62 (不包含以太网头)
    // IP(20) + TCP(20) + Reliable(8) + Pad(14) = 62 左右，这里填 0x30(48) + 14?
    // 同学代码填的是 0x0030 (48 bytes: 20IP + 20TCP + 8Reliable)
    ip[2] = 0x00; ip[3] = 0x30; 
    ip[4] = 0x00; ip[5] = 0x00; // ID
    ip[6] = 0x40; ip[7] = 0x00; // Flags=DF
    ip[8] = 0x40; // TTL=64
    ip[9] = 0x06; // Proto=TCP (关键！)
    ip[10] = 0; ip[11] = 0; // Checksum 先置0
    // Src IP: 10.0.0.2
    ip[12] = 10; ip[13] = 0; ip[14] = 0; ip[15] = 2;
    // Dst IP: 10.0.0.67
    ip[16] = 10; ip[17] = 0; ip[18] = 0; ip[19] = 67;
    
    // 计算 IP 校验和
    uint16_t csum = calc_checksum(ip, 20);
    ip[10] = csum & 0xff;
    ip[11] = csum >> 8;

    /* --- 3. TCP Header (20 bytes) --- */
    uint8_t *tcp = (uint8_t *)(control_packet + 34);
    tcp[0] = 0x04; tcp[1] = 0xd2; // Port 1234
    tcp[2] = 0x16; tcp[3] = 0x2e; // Port 5678
    // Offset=5 (20 bytes), Flags=ACK(0x10)
    tcp[12] = 0x50; tcp[13] = 0x10;

    /* --- 4. Reliable Header (8 bytes) --- */
    // 偏移量 14+20+20 = 54
    struct reliable_hdr *rh = (struct reliable_hdr *)(control_packet + RELIABLE_HDR_OFFSET);
    rh->magic = NET_MAGIC;
    rh->flags = flags;
    rh->len   = 0;
    rh->seq   = htonl(seq);

    /* --- 5. 发送 --- */
    int pkt_len = 64; // 最小以太网帧长度
    // 循环直到发送成功（这里 control packet 不允许阻塞）
    while (e1000_transmit(control_packet, pkt_len) == 0) ;
}

// ================= Task 2 & 3: 接收逻辑 =================

int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens) {
    int total_bytes = 0;
    for (int i = 0; i < pkt_num; i++) {
        int len = e1000_poll((char*)rxbuffer + total_bytes);
        if (len > 0) {
            pkt_lens[i] = len;
            total_bytes += len;
        } else {
            e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0 | E1000_IMS_RXT0);
            local_flush_dcache();
            do_block(&current_running->list, &recv_block_queue);
            do_scheduler();
            i--; // 没收到，重试这一次循环
        }
    }
    return total_bytes;
}

static void handle_e1000_rxdmt0(void) {
    unblock_queue(&recv_block_queue);
    e1000_write_reg(e1000, E1000_IMC, E1000_IMC_RXDMT0 | E1000_IMC_RXT0);
    local_flush_dcache();
}

// ================= Task 4: 可靠流接收 (核心) =================

int do_net_recv_stream(void *buffer, int *nbytes) {
    int wanted = *nbytes;
    int received = 0;
    uint8_t *user_ptr = (uint8_t *)buffer;
    uint64_t last_recv_time = get_ticks();
    // 超时阈值，根据你的时钟频率调整，假设 timer_freq 是一千万，这里约 0.1秒
    const uint64_t RECV_TIMEOUT = 1000000; 

    // 逻辑：一直循环，直到至少收到 1 个字节
    while (received == 0) { 
        /* --- 步骤 1: 检查重排缓冲区 (Cache Hit) --- */
        for (int i = 0; i < REORDER_BUF_SIZE; i++) {
            if (reorder_buf[i].valid && reorder_buf[i].seq == current_seq) {
                int dlen = reorder_buf[i].len;
                int copy_len = (dlen > wanted) ? wanted : dlen;
                
                memcpy(user_ptr, reorder_buf[i].data, copy_len);
                received = copy_len;
                
                current_seq += dlen; 
                reorder_buf[i].valid = 0;
                
                // 收到预期数据，立即 ACK
                send_control(NET_OP_ACK, current_seq);
                break; 
            }
        }
        // 如果从缓存里取到了数据，就直接返回（非阻塞风格）
        if (received > 0) break;

        /* --- 步骤 2: 轮询硬件 --- */
        static char rx_raw[2048]; // 静态分配避免爆栈
        int len = e1000_poll(rx_raw);

        if (len <= 0) {
            // 没有数据：开启中断并阻塞
            e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0 | E1000_IMS_RXT0);
            local_flush_dcache();

            // >>>>> KERNEL BUG FIX <<<<<
            // 在开启中断和阻塞之间，必须再次检查，防止竞态
            len = e1000_poll(rx_raw);
            if (len <= 0) {
                // 复用 recv_block_queue，因为逻辑一样
                do_block(&current_running->list, &recv_block_queue);
                do_scheduler();

                // 醒来后检查是否超时
                if ((get_ticks() - last_recv_time) > RECV_TIMEOUT) {
                    // 超时了还没收到 current_seq，发送 RSD 催促
                    send_control(NET_OP_RSD, current_seq);
                    last_recv_time = get_ticks(); 
                }
                continue; // 继续循环
            }
            // 如果在竞态窗口中收到了包，则关闭中断，继续向下执行
            e1000_write_reg(e1000, E1000_IMC, E1000_IMC_RXDMT0 | E1000_IMC_RXT0);
            local_flush_dcache();
        }
        
        // 收到包了，更新时间
        last_recv_time = get_ticks();

        /* --- 步骤 3: 解析数据包 --- */
        // 长度校验
        if (len < RELIABLE_HDR_OFFSET + sizeof(struct reliable_hdr)) continue;
        
        struct reliable_hdr *rh = (struct reliable_hdr *)(rx_raw + RELIABLE_HDR_OFFSET);
        
        // 校验 Magic 和 Flags
        if (rh->magic != NET_MAGIC || rh->flags != NET_OP_DAT) continue;

        uint32_t seq = ntohl(rh->seq);
        uint16_t dlen = ntohs(rh->len);
        uint8_t *data_ptr = (uint8_t *)(rx_raw + RELIABLE_HDR_OFFSET + sizeof(struct reliable_hdr));

        if (seq == current_seq) {
            // A. 正好是期望的包 (In-Order)
            int copy_len = (dlen > wanted) ? wanted : dlen;
            memcpy(user_ptr, data_ptr, copy_len);
            received = copy_len;
            
            current_seq += dlen;
            send_control(NET_OP_ACK, current_seq);
        } 
        else if (seq > current_seq) {
            // B. 乱序包 (Out-of-Order): 存入静态缓存
            for (int i = 0; i < REORDER_BUF_SIZE; i++) {
                if (!reorder_buf[i].valid) {
                    reorder_buf[i].valid = 1;
                    reorder_buf[i].seq = seq;
                    reorder_buf[i].len = dlen;
                    // 防止数据溢出
                    int save_len = (dlen > MAX_DATA_PER_PKT) ? MAX_DATA_PER_PKT : dlen;
                    memcpy(reorder_buf[i].data, data_ptr, save_len);
                    break;
                }
            }
            // 收到乱序包，立即发送 RSD 催促中间缺少的包
            send_control(NET_OP_RSD, current_seq);
        } 
        else {
            // C. 旧包 (Duplicate/Old): 重发 ACK
            send_control(NET_OP_ACK, current_seq);
        }
    }

    *nbytes = received;
    return 0;
}

// ================= 中断处理入口 =================

void net_handle_irq(void) {
    local_flush_dcache();
    uint32_t icr = e1000_read_reg(e1000, E1000_ICR);

    if (icr & E1000_ICR_TXQE) {
        handle_e1000_txqe();
    }
    if ((icr & E1000_ICR_RXDMT0) || (icr & E1000_ICR_RXT0)) {
        handle_e1000_rxdmt0();
    }
}
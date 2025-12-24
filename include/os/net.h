#ifndef __INCLUDE_NET_H__
#define __INCLUDE_NET_H__

#include <type.h>
#include <os/list.h>

#define PKT_NUM 32

// E1000 实验常量
#define ETH_ALEN 6u
#define ETH_P_IP 0x0800u

// Task 4 协议常量 (参考同学代码)
#define NET_MAGIC       0x45       // 协议魔数
#define NET_OP_DAT      0x1        // 数据包
#define NET_OP_ACK      0x4        // 确认包
#define NET_OP_RSD      0x2        // 请求重传

#define RELIABLE_HDR_OFFSET 54     // 协议头偏移 (14 Eth + 20 IP + 20 TCP)
#define MAX_DATA_PER_PKT    1024   // 每个包最大数据量
#define REORDER_BUF_SIZE    32     // 乱序缓冲大小

// 协议头结构 (8字节)
struct reliable_hdr {
    uint8_t magic;
    uint8_t flags;
    uint16_t len;
    uint32_t seq;
} __attribute__((packed));

// 静态乱序缓冲槽位
typedef struct {
    int valid;             // 是否有效
    uint32_t seq;          // 序列号
    int len;               // 数据长度
    uint8_t data[MAX_DATA_PER_PKT]; // 数据本体
} reorder_slot_t;

// Ethernet header
struct ethhdr {
    uint8_t ether_dmac[ETH_ALEN];
    uint8_t ether_smac[ETH_ALEN];
    uint16_t ether_type;
};

// 函数声明
void net_handle_irq(void);
int do_net_send(void *txpacket, int length);
int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens);
int do_net_recv_stream(void *buffer, int *nbytes);
void init_reliable_layer(void); // 初始化函数

#endif
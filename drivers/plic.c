// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 SiFive
 * Copyright (C) 2018 Christoph Hellwig
 */

/*
 * This driver implements a version of the RISC-V PLIC with the actual layout
 * specified in chapter 8 of the SiFive U5 Coreplex Series Manual:
 *
 *     https://static.dev.sifive.com/U54-MC-RVCoreIP.pdf
 *
 * The largest number supported by devices marked as 'sifive,plic-1.0.0', is
 * 1024, of which device 0 is defined as non-existent by the RISC-V Privileged
 * Spec.
 */

#include <type.h>
#include <io.h>
#include <csr.h>
#include <os/irq.h>
#include <printk.h>

#include <plic.h>

// 全局变量：PLIC 寄存器基地址（虚拟地址）
static void *plic_regs;
// 全局变量：用于存储当前 Hart 的 Context 信息（基地址等）
struct plic_handler plic_handlers;

/**
 * [辅助函数] plic_toggle
 * 功能：在 Enable 寄存器中，开启或关闭指定中断源 (hwirq)
 * @param handler: 包含 enable_base 的结构体
 * @param hwirq: 中断源 ID (例如 33)
 * @param enable: 1 为开启，0 为关闭
 */
static void plic_toggle(struct plic_handler *handler, int hwirq, int enable)
{
    // 计算该中断 ID 对应的寄存器地址（每 32 个 ID 共用一个 32位寄存器）
	uint32_t *reg = handler->enable_base + (hwirq / 32) * sizeof(uint32_t);
    // 计算该中断 ID 在寄存器中的位掩码
	uint32_t hwirq_mask = 1 << (hwirq % 32);

	if (enable)
        // 读出 -> 置位 -> 写回
		writel(readl(reg) | hwirq_mask, reg);
	else
        // 读出 -> 清零 -> 写回
		writel(readl(reg) & ~hwirq_mask, reg);
}

/**
 * [辅助函数] plic_irq_unmask
 * 功能：设置指定中断源的优先级，并开启该中断
 * @param hwirq: 中断源 ID
 */
static void plic_irq_unmask(int hwirq)
{
	int enable = 1;
    // 1. 设置优先级寄存器
    // 将该 ID 的优先级设为 1 (只要大于阈值 0 即可触发)
    // 注意：PRIORITY_BASE 等宏定义在 plic.h 中
	writel(enable, plic_regs + PRIORITY_BASE + hwirq * PRIORITY_PER_ID);
    
    struct plic_handler *handler = &plic_handlers;

    // 2. 在 Enable 寄存器中开启该中断
    if (handler->present) plic_toggle(handler, hwirq, enable);
}

/*
 * 处理中断的两步走：
 * 1. Claim: 读取 claim 寄存器获取中断 ID。
 * 2. Complete: 将 ID 写回 claim 寄存器表示处理完毕。
 */

/**
 * [核心函数] plic_claim
 * 功能：获取当前发生的最高优先级中断的 ID
 * 返回值：中断 ID (如果没有中断则返回 0)
 */
uint32_t plic_claim(void)
{
	struct plic_handler *handler = &plic_handlers;
    // 计算 Claim 寄存器地址
	void *claim = handler->hart_base + CONTEXT_CLAIM;

    // 读取并返回
	return readl(claim);
}

/**
 * [核心函数] plic_complete
 * 功能：通知 PLIC 该中断已处理完毕
 * @param hwirq: 刚刚处理完的中断 ID
 */
void plic_complete(int hwirq)
{
	struct plic_handler *handler = &plic_handlers;
    // 将 ID 写回 Claim/Complete 寄存器
	writel(hwirq, handler->hart_base + CONTEXT_CLAIM);
}

/**
 * [核心函数] plic_init
 * 功能：初始化 PLIC 控制器
 * @param plic_regs_addr: PLIC 基地址的【虚拟地址】(即 main 中 ioremap 后的值)
 * @param nr_irqs: 系统支持的最大中断号数量
 */
int plic_init(uint64_t plic_regs_addr, uint32_t nr_irqs)
{
    // 保存基地址到全局变量
    plic_regs = (void *)plic_regs_addr;

    struct plic_handler *handler;
    int hwirq;
    uint32_t threshold = 0; // 阈值设为 0，允许所有优先级 > 0 的中断

    handler = &plic_handlers;
    
    // 防止重复初始化（如果多核同时调可能需要注意，但这里逻辑比较简单）
    if (handler->present) {
        printk("handler already present.\n");
        threshold = 0xffffffff; // 如果重复初始化，设为最高阈值屏蔽中断
        goto done;
    }

    handler->present     = true;
    // 设置 S 态 Context 的基地址 (CONTEXT_BASE 对应 Hart 0 S-Mode 的偏移)
    handler->hart_base   = plic_regs + CONTEXT_BASE + CONTEXT_PER_HART;
    // 设置 S 态 Enable 的基地址
    handler->enable_base = plic_regs + ENABLE_BASE + ENABLE_PER_HART;

done:
    /* 
     * 关键步骤：设置阈值 (Threshold)
     * 只有 Priority > Threshold 的中断才会被 CPU 感知
     * 这里我们将 Threshold 设为 0
     */
    writel(threshold, handler->hart_base + CONTEXT_THRESHOLD);
    
    // 先把所有中断都关掉 (Mask)，防止意外触发
    for (hwirq = 1; hwirq <= nr_irqs; hwirq++) plic_toggle(handler, hwirq, 0);

    /*
     * 关键步骤：开启 E1000 网卡中断
     * 根据最大中断号判断是 QEMU 环境还是 FPGA/PYNQ 环境
     * 并调用 plic_irq_unmask 开启对应的 ID
     */
	if (hwirq > PLIC_E1000_QEMU_IRQ) // 如果系统中断数够大，说明是 QEMU (ID 33)
	{
		// 开启 ID 为 33 的中断
		plic_irq_unmask(PLIC_E1000_QEMU_IRQ);		
	}
	else
	{
		// 否则认为是 FPGA 开发板，开启 ID 为 3 的中断
		plic_irq_unmask(PLIC_E1000_PYNQ_IRQ);		
	}

    return 0;
}
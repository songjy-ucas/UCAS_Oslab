/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                   Memory Management
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */
#ifndef MM_H
#define MM_H

#include <type.h>
#include <pgtable.h>
#include <os/list.h>
#include <os/irq.h> 

#define MAP_KERNEL 1
#define MAP_USER 2
#define MEM_SIZE 32
#define PAGE_SIZE 4096 // 4K
#define INIT_KERNEL_STACK 0xffffffc052000000
#define INIT_USER_STACK   0xffffffc052500000
#define FREEMEM_KERNEL (INIT_KERNEL_STACK+ 2*PAGE_SIZE)
#define FREEMEM_USER INIT_USER_STACK

#define USER_PAGE_MAX_NUM 1024  // 最大管理的换页元数据数量
#define KERN_PAGE_MAX_NUM 512   // 触发换出的物理页阈值

/* Rounding; only works for n = power of two */
#define ROUND(a, n)     (((((uint64_t)(a))+(n)-1)) & ~((n)-1))
#define ROUNDDOWN(a, n) (((uint64_t)(a)) & ~((n)-1))

extern ptr_t allocPage(int numPage);
// TODO [P4-task1] */
void freePage(ptr_t baseAddr);

// #define S_CORE
// NOTE: only need for S-core to alloc 2MB large page
#ifdef S_CORE
#define LARGE_PAGE_FREEMEM 0xffffffc056000000
#define USER_STACK_ADDR 0x400000
extern ptr_t allocLargePage(int numPage);
#else
// NOTE: A/C-core
#define USER_STACK_ADDR 0xf00010000
#endif

// TODO [P4-task1] 
extern void* kmalloc(size_t size);
extern void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir);
extern uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir);
void init_memory_manager();
void init_uva_alloc();

// TODO [P4-task2]
// extern void do_page_fault(regs_context_t *regs/*, uint64_t stval, uint64_t scause*/);
extern void do_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause);

// TODO [P4-task4]: shm_page_get/dt */
uintptr_t shm_page_get(int key);
void shm_page_dt(uintptr_t addr);


// [P4-Task3] 换页机制相关数据结构和函数声明
// 标准页表无法查出某一个PA对应哪一个进程和虚拟地址，所以需要一个额外的数据结构
typedef struct {
    list_node_t lnode;    // 链表节点
    uintptr_t uva;        // 用户虚拟地址
    uintptr_t pa;         // 物理地址
    uint32_t on_disk_sec; // 在磁盘 swap 区的扇区号
    int pgdir_id;         // 所属页表ID/进程ID
} alloc_info_t;

void free_process_memory(pcb_t *proc);

extern alloc_info_t alloc_info[USER_PAGE_MAX_NUM];
extern uint64_t image_end_sec; // Swap 区起始扇区

// [P4-Task4] 获取当前系统剩余内存大小
size_t do_get_free_memory();

#endif /* MM_H */


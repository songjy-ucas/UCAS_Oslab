#ifndef INCLUDE_DEBUG_H_
#define INCLUDE_DEBUG_H_

#include <os/smp.h>
#include <printk.h>
#include <csr.h>    // 包含 CSR 定义

#define KERNEL_LOG

#ifdef KERNEL_LOG
    #define klog(fmt, ...) \
        printl("[CORE %d] " fmt, get_current_cpu_id(), ##__VA_ARGS__)
#else
    #define klog(fmt, ...) do {} while(0)
#endif

void print_entering_exception(void);
void print_leaving_exception(void);

#endif
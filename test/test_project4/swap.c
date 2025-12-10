#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
// ======================= 配置常量 =======================
#define NUM_PAGES_TO_TEST   64      // 测试的虚拟页面总数
#define MAX_WRITES_PER_PAGE 20      // 每个页面最多记录的写操作次数
#define VIRT_ADDR_BASE      0x10000000UL // 自定义的用户态虚拟地址起始点
#define PAGE_SHIFT          12      // 页面大小为 2^12 = 4KB
#define PAGE_SIZE           (1 << PAGE_SHIFT)

// ======================= 数据结构定义 =======================
// 记录对某个内存位置的最后一次写入操作
typedef struct {
    int index_in_page;   // 在页面内的偏移（以int为单位）
    int value;           // 写入的值
} write_record_t;

// 每个虚拟页面对应一个历史记录数组
write_record_t g_write_history[NUM_PAGES_TO_TEST][MAX_WRITES_PER_PAGE];
// 记录每个页面的历史记录条数
int g_history_count[NUM_PAGES_TO_TEST] = {0};

int main() {
    printf("====== Paging Test: Random Read/Write Verification ======\n\n");

    // =======================================================================
    // [修改] 使用 clock() 或 get_ticks() 作为随机种子
    // =======================================================================
    // srand(time(NULL)); // 替换掉这一行
    srand(sys_get_tick()); // 使用 get_ticks() 或 clock()
    // =======================================================================

    int ints_per_page = PAGE_SIZE / sizeof(int);

    // ---------------------- 随机写入阶段 ----------------------
    printf("[PHASE 1] Start random writes to virtual pages...\n");
    for (int i = 0; i < NUM_PAGES_TO_TEST * 2; ++i) { // 增加总写入次数以更好地测试
        
        // 随机选择一个要操作的虚拟页面
        int page_idx = rand() % NUM_PAGES_TO_TEST;
        uint64_t target_va = VIRT_ADDR_BASE + ((uint64_t)page_idx << PAGE_SHIFT);
        int* page_ptr = (int*)target_va;

        // 随机决定写入多少次
        int num_writes = (rand() % 5) + 1; // 每次写入1到5个整数
        // printf("  -> Accessing page %d (VA: 0x%x), performing %d writes.\n", page_idx, target_va, num_writes);

        for (int j = 0; j < num_writes; ++j) {
            int random_value = rand();
            int random_index = rand() % ints_per_page;

            // 执行写入
            page_ptr[random_index] = random_value;

            // 更新历史记录
            int k;
            for (k = 0; k < g_history_count[page_idx]; ++k) {
                if (g_write_history[page_idx][k].index_in_page == random_index) {
                    // 如果这个位置之前写过，就更新记录
                    g_write_history[page_idx][k].value = random_value;
                    break;
                }
            }

            if (k == g_history_count[page_idx]) {
                // 如果是第一次写这个位置，并且还有空间
                if (g_history_count[page_idx] < MAX_WRITES_PER_PAGE) {
                    g_write_history[page_idx][k].index_in_page = random_index;
                    g_write_history[page_idx][k].value = random_value;
                    g_history_count[page_idx]++;
                }
            }
        }
    }
    printf("[PHASE 1] Write phase completed.\n\n");


    // ---------------------- 校验阶段 ----------------------
    printf("[PHASE 2] Start verifying the contents of all written pages...\n");
    int error_found = 0;

    for (int i = 0; i < NUM_PAGES_TO_TEST; ++i) {
        if (g_history_count[i] == 0) {
            // 这个页面从未被写入过，跳过
            continue;
        }

        uint64_t target_va = VIRT_ADDR_BASE + ((uint64_t)i << PAGE_SHIFT);
        int* page_ptr = (int*)target_va;
        
        // printf("  -> Checking page %d (VA: 0x%x)... ", i, target_va);

        for (int j = 0; j < g_history_count[i]; ++j) {
            int recorded_index = g_write_history[i][j].index_in_page;
            int recorded_value = g_write_history[i][j].value;

            // 从内存中读回值
            int current_value = page_ptr[recorded_index];

            if (current_value != recorded_value) {
                printf("\n\n!! VERIFICATION FAILED !!\n");
                printf("   - On virtual page: %d (Base VA: 0x%lx)\n", i, target_va);
                printf("   - At index:        %d\n", recorded_index);
                printf("   - Expected value:  0x%x (%d)\n", recorded_value, recorded_value);
                printf("   - Got value:       0x%x (%d)\n\n", current_value, current_value);
                error_found = 1;
                goto end_test; // 发现一个错误就停止
            }
        }
        // printf("OK.\n");
    }

end_test:
    printf("[PHASE 2] Verification phase completed.\n\n");
    if (error_found) {
        printf("====== TEST RESULT: FAILED ======\n");
    } else {
        printf("====== TEST RESULT: ALL PASSED! ======\n");
    }

    return 0;
}
#include <stdio.h>
#include <unistd.h>
// #include <kernel.h>

/**
 * The ascii airplane is designed by Joan Stark
 * from: https://www.asciiart.eu/vehicles/airplanes
 */

#define CYCLE_PER_MOVE 50 // 定义了飞机每移动一格，内层循环需要迭代多少次,控制动画的速度。值越大，内层循环执行次数越多，飞机移动越慢。
#define LENGTH 60 // 飞机移动总长度
#define CHECK_POINT 50 // 检查点位置

static char blank[] = {"                                                                               "};
static char plane1[] = {"    \\\\   "};
static char plane2[] = {" \\====== "};
static char plane3[] = {"    //   "};

int main(void)
{
    int j = 17;
    int remain_length;
    int round = 1; // 轮次数

    // [Task 5 Change] 增加一个 phase 变量来跟踪当前阶段
    int phase; // 区分到达检查点之前和之后，0 or 1
    int current_pos; // 飞机当前位置

    sys_set_sche_workload(1,0,LENGTH-CHECK_POINT);

    while (1)
    {
        // [Task 5 Change] 在每轮开始时重置 phase,length
        phase = 0;
        remain_length = LENGTH;
        int clk = sys_get_tick();

	sys_move_cursor(CHECK_POINT + 8, j);
	printf("%c", '|');
	sys_move_cursor(CHECK_POINT + 8, j + 1);
	printf("%c", '|');
	sys_move_cursor(CHECK_POINT + 8, j + 2);
	printf("%c", '|');

        for (int i = (60 - LENGTH) * CYCLE_PER_MOVE; i < 60 * CYCLE_PER_MOVE; i++)
        {
            /* move */
            if(i % CYCLE_PER_MOVE == 0) // 只有当 i 是 CYCLE_PER_MOVE 的整数倍时，才执行一次绘制操作。实现了循环 CYCLE_PER_MOVE 次，飞机移动一格的效果。
            {
                sys_move_cursor(i/CYCLE_PER_MOVE, j + 0);
                printf("%s", plane1);

                sys_move_cursor(i/CYCLE_PER_MOVE, j + 1);
                printf("%s", plane2);

                sys_move_cursor(i/CYCLE_PER_MOVE, j + 2);
                printf("%s", plane3);
                // sys_yield();
                // for (int j=0;j<200000;j++); // wait
                
                current_pos = i / CYCLE_PER_MOVE;
                // 分段计算remain_length
                if (phase == 0) {
                    // 阶段一：目标是检查点
                    remain_length = CHECK_POINT - current_pos;
                    if (remain_length <= 0) {
                        remain_length = 0; // 确保报告0，以便内核检测状态切换
                        phase = 1;
                        // 刚切换到 phase 1，立即计算一次到终点的距离并报告
                        remain_length = LENGTH - current_pos;
                    }
                } else { // phase == 1
                    // 阶段二：目标是终点
                    remain_length = LENGTH - current_pos;
                }

                if (remain_length < 0) {
                    remain_length = 0;
                }
                
                // 使用新的系统调用报告所有状态
                sys_set_sche_workload(round, phase, remain_length);
                sys_move_cursor(0, 29);
                printf("[fly5] position: %d path.%d phase",current_pos,phase);
            }
        }
        
        // 一轮结束，进入下一轮
        round++;
        sys_set_sche_workload(round, 0, LENGTH-CHECK_POINT);

        // sys_yield();
        sys_move_cursor(0, j);
        printf("%s", blank);
        sys_move_cursor(0, j + 1);
        printf("%s", blank);
        sys_move_cursor(0, j + 2);
        printf("%s", blank);

        clk = sys_get_tick() - clk;
        sys_move_cursor(0, 24);
        printf("[fly5] used time per round: %d tick.%d round",clk,round);
    }
}

// int main(void)
// {
//     int j = 10;

//     while (1)
//     {
//         for (int i = 0; i < 50; i++)
//         {
//             /* move */
//             kernel_move_cursor(i, j + 0);
//             kernel_print("%s", (long)plane1, 0);

//             kernel_move_cursor(i, j + 1);
//             kernel_print("%s", (long)plane2, 0);

//             kernel_move_cursor(i, j + 2);
//             kernel_print("%s", (long)plane3, 0);

//             kernel_move_cursor(i, j + 3);
//             kernel_print("%s", (long)plane4, 0);

//             kernel_move_cursor(i, j + 4);
//             kernel_print("%s", (long)plane5, 0);

//             kernel_move_cursor(i, j + 5);
//             kernel_print("%s", (long)plane6, 0);

//             kernel_move_cursor(i, j + 6);
//             kernel_print("%s", (long)plane7, 0);
//         }
//         kernel_yield();

//         kernel_move_cursor(0, j + 0);
//         kernel_print("%s", (long)blank, 0);

//         kernel_move_cursor(0, j + 1);
//         kernel_print("%s", (long)blank, 0);

//         kernel_move_cursor(0, j + 2);
//         kernel_print("%s", (long)blank, 0);

//         kernel_move_cursor(0, j + 3);
//         kernel_print("%s", (long)blank, 0);

//         kernel_move_cursor(0, j + 4);
//         kernel_print("%s", (long)blank, 0);

//         kernel_move_cursor(0, j + 5);
//         kernel_print("%s", (long)blank, 0);

//         kernel_move_cursor(0, j + 6);
//         kernel_print("%s", (long)blank, 0);
//     }
// }

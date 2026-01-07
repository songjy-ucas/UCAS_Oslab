/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                  The shell acts as a task running in user mode.
 *       The main function is to make system calls through the user's output.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
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

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>

#define SHELL_BEGIN 20
#define CMD_MAX_LENGTH 128
#define MAX_ARGV 16

/* ================= 保持我Pro2之前的把保存命令行的功能 ================= */
#define HISTORY_MAX 10
static char history_buffer[HISTORY_MAX][CMD_MAX_LENGTH];
static int history_count = 0;
static int history_current = 0;

static void add_to_history(const char *command)
{
    if (command[0] == '\0' || 
       (history_count > 0 && strcmp(command, history_buffer[(history_count - 1) % HISTORY_MAX]) == 0)) 
    {
        return;
    }
    strcpy(history_buffer[history_count % HISTORY_MAX], command);
    history_count++;
    history_current = history_count; 
}
/* ============================================================== */

/* ================= [Task 4] 辅助函数：十六进制字符串转整数 ================= */
int xtoi(const char *str) {
    int val = 0;
    // 跳过 "0x" 或 "0X" 前缀
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    }
    while (*str) {
        char c = *str;
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else break; // 非法字符停止解析
        val = val * 16 + digit;
        str++;
    }
    return val;
}


int parse_command(char *buffer, char *argv[])
{
    int argc = 0;
    char *p = buffer;
    while (*p && argc < MAX_ARGV) {
        // Skip leading whitespace
        while (*p && isspace(*p)) {
            *p++ = '\0';
        }
        if (*p == '\0') break;

        // Save argument
        argv[argc++] = p;

        // Find end of argument
        while (*p && !isspace(*p)) {
            p++;
        }
    }
    argv[argc] = NULL;
    return argc;
}

void print_logo() {
    printf(" ::::::::   ::::::::  :::    ::: :::::::: :::      :::       \r\n");
    printf(":+:    :+: :+:    :+: :+:    :+: :+:      :+:      :+:       \r\n");
    printf("+:+        +:+        +:+    +:+ +:+      +:+      +:+       \r\n");
    printf("+#++:++#++ +#++:++#++ +#++:++#++ +++++#   +#+      +#+       \r\n");
    printf("       +#+        +#+ +#+    +#+ +#+      +#+      +#+       \r\n");
    printf("#+#    #+# #+#    #+# #+#    #+# #+#      #+#      #+#       \r\n");
    printf(" ########   ########  ###    ### ######## ######## ########  \r\n");
    printf("                                                             \r\n");
}

// 2. 实现 free 命令的处理函数
void exec_free(int argc, char *argv[])
{
    // 调用系统调用获取空闲字节数
    size_t free_bytes = sys_get_free_memory();
    
    // 检查是否包含 -h 选项
    int human_readable = 0;
    if (argc > 1 && strcmp(argv[1], "-h") == 0) {
        human_readable = 1;
    }

    // 输出结果
    printf("System Memory Status:\n");
    if (!human_readable) {
        // 默认格式：直接输出字节
        printf("  Free Memory: %ld Bytes\n", free_bytes);
    } else {
        // -h 格式：自动转换单位 (B -> KiB -> MiB)
        if (free_bytes < 1024) {
            printf("  Free Memory: %ld B\n", free_bytes);
        } else if (free_bytes < 1024 * 1024) {
            printf("  Free Memory: %ld KiB\n", free_bytes / 1024);
        } else {
            // 保留一位小数，例如 12.5 MiB
            size_t mib = free_bytes / (1024 * 1024);
            size_t remainder = (free_bytes % (1024 * 1024)) * 10 / (1024 * 1024);
            printf("  Free Memory: %ld.%ld MiB\n", mib, remainder);
        }
    }
}

// File System Commands
void exec_mkfs(int argc, char *argv[]) {
    sys_fs_mkfs();
}

void exec_statfs(int argc, char *argv[]) {
    sys_fs_statfs();
}

void exec_cd(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: cd <path>\n");
        return;
    }
    if (sys_fs_cd(argv[1]) != 0) {
        printf("Error: cd failed\n");
    }
}

void exec_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: mkdir <path>\n");
        return;
    }
    if (sys_fs_mkdir(argv[1]) != 0) {
        printf("Error: mkdir failed\n");
    }
}

void exec_rmdir(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: rmdir <path>\n");
        return;
    }
    if (sys_fs_rmdir(argv[1]) != 0) {
        printf("Error: rmdir failed\n");
    }
}

void exec_ls(int argc, char *argv[]) {
    int option = 0;
    char *path = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            option = 1;
        } else {
            path = argv[i];
        }
    }
    sys_fs_ls(path, option);
}

void exec_touch(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: touch <file>\n");
        return;
    }
    // O_RDWR (3) | O_CREAT logic handled in kernel if open mode allows
    // Assuming 3 = O_RDWR. If kernel implements O_CREAT logic for open, this works.
    // Guidebook says "open(char* name, int access)". 
    // Usually access: O_RDONLY=1, O_WRONLY=2, O_RDWR=3.
    // If not exist, open should create if writing is involved? 
    // The student implementation of do_open creates file if not found and writable.
    int fd = sys_fs_open(argv[1], 3); 
    if (fd >= 0) {
        sys_fs_close(fd);
    } else {
        printf("Error: Failed to touch %s\n", argv[1]);
    }
}

void exec_cat(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return;
    }
    int fd = sys_fs_open(argv[1], 1); // O_RDONLY
    if (fd < 0) {
        printf("Error: Cannot open %s\n", argv[1]);
        return;
    }
    char buf[129];
    int n;
    while ((n = sys_fs_read(fd, buf, 128)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    printf("\n");
    sys_fs_close(fd);
}

void exec_ln(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: ln <src> <dst>\n");
        return;
    }
    if (sys_fs_ln(argv[1], argv[2]) != 0) {
        printf("Error: ln failed\n");
    }
}

void exec_rm(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: rm <file>\n");
        return;
    }
    if (sys_fs_rm(argv[1]) != 0) {
        printf("Error: rm failed\n");
    }
}

int main(void)
{
    char cmd_buffer[CMD_MAX_LENGTH];
    int buffer_ptr;
    sys_move_cursor(0, SHELL_BEGIN);
    print_logo();
    printf("------------------- COMMAND -------------------\n");
    sys_reflush();
    while (1)
    {
        // 独立的行输入循环
        
        // [HISTORY] 每次新循环开始时，重置当前历史指针到最新位置
        history_current = history_count;

        printf("> root@UCAS_OS: ");
        sys_reflush();
        buffer_ptr = 0;
        
        // [Fix] 确保缓冲区初始化，避免脏数据影响
        cmd_buffer[0] = '\0'; 

        while (1) {
            int c;
            while((c = sys_getchar()) == -1){
            ; // 等待命令行输入    
            } 
            
            /* ================= ADDED: ARROW KEYS HANDLING ================= */
            if (c == 27) { // ESC sequence
                int c2, c3;
                // 【修复】必须循环等待第二个字符，不能假设它已经到了
                while((c2 = sys_getchar()) == -1); 
                // 【修复】必须循环等待第三个字符
                while((c3 = sys_getchar()) == -1); 
                if (c2 == 91) { // '['
                    if (c3 == 65) { // 'A' -> Up Arrow
                         if (history_count > 0 && history_current > 0) {
                            history_current--;
                            // 清除当前行
                            while (buffer_ptr > 0) {
                                buffer_ptr--;
                                sys_write_ch('\b'); sys_write_ch(' '); sys_write_ch('\b');
                            }
                            // 复制历史命令
                            strcpy(cmd_buffer, history_buffer[history_current % HISTORY_MAX]);
                            buffer_ptr = strlen(cmd_buffer);
                            // 显示
                            printf("%s", cmd_buffer);
                            sys_reflush();
                        }
                    } else if (c3 == 66) { // 'B' -> Down Arrow
                        if (history_count > 0 && history_current < history_count) {
                            // 清除当前行
                            while (buffer_ptr > 0) {
                                buffer_ptr--;
                                sys_write_ch('\b'); sys_write_ch(' '); sys_write_ch('\b');
                            }
                            
                            if (history_current < history_count - 1) {
                                history_current++;
                                strcpy(cmd_buffer, history_buffer[history_current % HISTORY_MAX]);
                            } else {
                                // 已经是最后一条，再按向下则清空（回到新行状态）
                                history_current = history_count;
                                cmd_buffer[0] = '\0';
                            }

                            buffer_ptr = strlen(cmd_buffer);
                            printf("%s", cmd_buffer);
                            sys_reflush();
                        }
                    }
                }
                continue; // 处理完方向键，跳过后续常规字符处理
            }
            /* ============================================================== */

            if (c == '\r' || c == '\n') {
                // 回车，结束行输入
                sys_write_ch('\n');
                sys_reflush();
                cmd_buffer[buffer_ptr] = '\0';
                break;
            } else if (c == 8 || c == 127) {
                if (buffer_ptr > 0) {
                    buffer_ptr--;
                    // 使用 sys_write_ch 实现删除效果
                    sys_write_ch('\b');
                    sys_write_ch(' ');
                    sys_write_ch('\b');
                    sys_reflush();
                }
            } else { // Normal character
                if (buffer_ptr < CMD_MAX_LENGTH - 1) {
                    cmd_buffer[buffer_ptr++] = c;
                    cmd_buffer[buffer_ptr] = '\0'; // [Fix] 动态保持字符串结尾，以便方向键切换时逻辑正确
                    sys_write_ch(c);
                    sys_reflush();
                }
            }
        }

        /* ================= ADDED: SAVE HISTORY ================= */
        // 在解析和修改 buffer 之前，保存到历史记录
        add_to_history(cmd_buffer);
        /* ======================================================= */

        // 行输入结束，开始解析和执行命令 
        int is_background = 0;
        if (buffer_ptr > 0 && cmd_buffer[buffer_ptr - 1] == '&') { //根据是否有 & ,判断要不要默认执行sys_waitpid
            is_background = 1;
            cmd_buffer[--buffer_ptr] = '\0';
        }

        char *argv[MAX_ARGV];
        int argc = parse_command(cmd_buffer, argv);

        if (argc == 0) {
            continue;
        }

        // 交互界面，识别命令行
        if (strcmp(argv[0], "ps") == 0) {
            sys_ps();
        } else if(strcmp(argv[0], "free") == 0) {
            exec_free(argc, argv);
        } else if (strcmp(argv[0], "clear") == 0) {
            sys_clear();
            sys_move_cursor(0, SHELL_BEGIN);
            printf("------------------- COMMAND -------------------\n");
            sys_reflush();
        } else if (strcmp(argv[0], "exec") == 0) {
            if (argc < 2) {
                printf("Usage: exec <task_name> [args...]\n");
            } else {
                pid_t pid = sys_exec(argv[1], argc - 1, &argv[1]);
                if (pid != -1) {
                    printf("Info: execute %s successfully, pid = %d\n", argv[1], pid);
                    if (!is_background) {
                        sys_waitpid(pid);
                    }
                } else {
                    printf("Error: failed to exec %s\n", argv[1]);
                }
            }
        } else if (strcmp(argv[0], "kill") == 0) {
            if (argc < 2) {
                printf("Usage: kill <pid>\n");
            } else {
                int pid_to_kill = atoi(argv[1]);
                if (sys_kill(pid_to_kill)) {
                    printf("Info: kill process %d successfully.\n", pid_to_kill);
                } else {
                    printf("Info: Cannot find process with pid %d!\n", pid_to_kill);
                }
            }
        } else if(strcmp(argv[0], "taskset") == 0) {
            if (argc < 3) {
                printf("Usage: taskset -p mask pid OR taskset mask name [args...]\n");
            } else {
                // 模式 1: taskset -p mask pid
                if (strcmp(argv[1], "-p") == 0) {
                    if (argc < 4) {
                         printf("Usage: taskset -p mask pid\n");
                    } else {
                        int mask = xtoi(argv[2]);
                        int pid = atoi(argv[3]);
                        sys_taskset(mask, pid);
                        printf("Info: set pid %d mask to 0x%x\n", pid, mask);
                    }
                } 
                // 模式 2: taskset mask name [args...]
                else {
                    int mask = xtoi(argv[1]);
                    // argv[2] 是程序名，参数从 argv[2] 开始传
                    // 计算传递给 sys_exec 的 argc: 总参数减去 "taskset" 和 "mask" 两个
                    pid_t pid = sys_exec(argv[2], argc - 2, &argv[2]);
                    
                    if (pid != -1) {
                        // [关键点] 进程创建后立即设置亲和性
                        sys_taskset(mask, pid);
                        printf("Info: execute taskset successfully, pid = %d, mask = 0x%x\n", pid, mask);
                        
                        // 依然遵循 & 后台运行规则
                        if (!is_background) {
                            sys_waitpid(pid);
                        }
                    } else {
                        printf("Error: failed to exec %s\n", argv[2]);
                    }
                }
            }
        } else if (strcmp(argv[0], "mkfs") == 0) {
            exec_mkfs(argc, argv);
        } else if (strcmp(argv[0], "statfs") == 0) {
            exec_statfs(argc, argv);
        } else if (strcmp(argv[0], "cd") == 0) {
            exec_cd(argc, argv);
        } else if (strcmp(argv[0], "mkdir") == 0) {
            exec_mkdir(argc, argv);
        } else if (strcmp(argv[0], "rmdir") == 0) {
            exec_rmdir(argc, argv);
        } else if (strcmp(argv[0], "ls") == 0) {
            exec_ls(argc, argv);
        } else if (strcmp(argv[0], "touch") == 0) {
            exec_touch(argc, argv);
        } else if (strcmp(argv[0], "cat") == 0) {
            exec_cat(argc, argv);
        } else if (strcmp(argv[0], "ln") == 0) {
            exec_ln(argc, argv);
        } else if (strcmp(argv[0], "rm") == 0) {
            exec_rm(argc, argv);
        }
        else {
            printf("Error: Unknown command '%s'\n", argv[0]);
        }

        /************************************************************/
        /* Do not touch this comment. Reserved for future projects. */
        /************************************************************/    
    }

    return 0;
}

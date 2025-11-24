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


int main(void)
{
    char cmd_buffer[CMD_MAX_LENGTH];
    int buffer_ptr;
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
    sys_reflush();
    while (1)
    {
        // TODO [P3-task1]: call syscall to read UART port
        
        // TODO [P3-task1]: parse input
        // note: backspace maybe 8('\b') or 127(delete)

        // TODO [P3-task1]: ps, exec, kill, clear 
    
        // [OPTIMIZED] 采用参考代码中更健壮的、独立的行输入循环
        
        printf("> root@UCAS_OS: ");
        sys_reflush();
        buffer_ptr = 0;
        while (1) {
            // [SIMPLIFIED] sys_getchar() 现在是阻塞的，直接调用即可
            int c = sys_getchar();

            if (c == '\r' || c == '\n') {
                // 回车，结束行输入
                sys_write_ch('\n');
                sys_reflush();
                cmd_buffer[buffer_ptr] = '\0';
                break;
            } else if (c == 8 || c == 127) { // Backspace
                if (buffer_ptr > 0) {
                    buffer_ptr--;
                    // [EFFICIENT] 使用新的 sys_write_ch 实现完美的视觉删除效果
                    sys_write_ch('\b');
                    sys_write_ch(' ');
                    sys_write_ch('\b');
                    sys_reflush();
                }
            } else { // Normal character
                if (buffer_ptr < CMD_MAX_LENGTH - 1) {
                    cmd_buffer[buffer_ptr++] = c;
                    // [EFFICIENT] 使用新的 sys_write_ch 直接回显，效率更高
                    sys_write_ch(c);
                    sys_reflush();
                }
            }
        }

        // 行输入结束，开始解析和执行命令
        
        int is_background = 0;
        if (buffer_ptr > 0 && cmd_buffer[buffer_ptr - 1] == '&') {
            is_background = 1;
            cmd_buffer[--buffer_ptr] = '\0';
        }

        char *argv[MAX_ARGV];
        int argc = parse_command(cmd_buffer, argv);

        if (argc == 0) {
            continue;
        }

        // 命令分发逻辑保持不变
        if (strcmp(argv[0], "ps") == 0) {
            sys_ps();
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
        } else {
            printf("Error: Unknown command '%s'\n", argv[0]);
        }

        /************************************************************/
        /* Do not touch this comment. Reserved for future projects. */
        /************************************************************/    
    }

    return 0;
}

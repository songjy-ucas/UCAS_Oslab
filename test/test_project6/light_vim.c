#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_BUFFER_SIZE 4096
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 24

// 模式定义
#define MODE_NORMAL 0   // 普通模式：移动、浏览
#define MODE_INSERT 1   // 插入模式：输入文字
#define MODE_COMMAND 2  // 命令模式：输入 :w, :q 等
#define MODE_HELP 3     // 新增：帮助模式

// 全局变量
char buffer[MAX_BUFFER_SIZE]; // 文件内容缓冲区
int buffer_len = 0;           // 当前文件内容长度
int cursor_pos = 0;           // 光标在缓冲区中的线性位置（0 ~ buffer_len）
int mode = MODE_NORMAL;       // 当前编辑器模式
char filename[64];            // 打开的文件名
char command_buffer[16];      // 命令模式下的输入缓冲
int command_len = 0;          // 命令长度

// 计算光标在屏幕上的 (x, y) 坐标
// 逻辑保持不变：遍历缓冲区，遇到换行符y+1，x归零；超过屏幕宽度自动换行
void get_screen_pos(int pos, int *x, int *y) {
    *x = 0;
    *y = 0;
    for (int i = 0; i < pos && i < buffer_len; i++) {
        if (buffer[i] == '\n') {
            (*y)++;
            *x = 0;
        } else {
            (*x)++;
            if (*x >= SCREEN_WIDTH) {
                (*y)++;
                *x = 0;
            }
        }
    }
}

// 加载文件
void load_file() {
    int fd = sys_fs_open(filename, 1); // O_RDONLY: 只读方式打开
    if (fd < 0) {
        buffer_len = 0; // 文件不存在则新建空缓冲区
        return;
    }
    // 读取文件内容到 buffer
    buffer_len = sys_fs_read(fd, buffer, MAX_BUFFER_SIZE - 1);
    if (buffer_len < 0) buffer_len = 0;
    buffer[buffer_len] = '\0'; // 确保字符串结束符
    sys_fs_close(fd);
}

// 保存文件
void save_file() {
    int fd = sys_fs_open(filename, 3); // O_RDWR: 读写方式打开（通常含创建权限）
    if (fd >= 0) {
        sys_fs_write(fd, buffer, buffer_len);
        sys_fs_close(fd);
    }
}

// 渲染屏幕
void render() {
    sys_clear(); // 清屏

    // 新增：如果是帮助模式，显示帮助信息并直接返回，不渲染文件内容
    if (mode == MODE_HELP) {
        sys_move_cursor(0, 0);
        printf("--- VIM HELP ---\n\n");
        printf("i       : change to insert\n");
        printf("esc     : quit to normal\n");
        printf("h,j,k,l : move cursor (left, down, up, right)\n");
        printf(":w      : save file\n");
        printf(":q      : quit editor\n");
        printf(":help   : show this message\n");
        printf("\n[Press any key to return]");
        return;
    }

    // 1. 输出文件内容
    printf("%s", buffer);

    // 2. 绘制光标（逻辑不变：计算位置，移动过去打印'$'，这可能是为了调试或简单模拟光标）
    int cx, cy;
    get_screen_pos(cursor_pos, &cx, &cy);
    sys_move_cursor(cx, cy);
    printf("$"); 

    // 3. 绘制底部状态栏
    sys_move_cursor(0, SCREEN_HEIGHT - 1);
    if (mode == MODE_NORMAL) {
        printf("-- NORMAL --  %s", filename);
    } else if (mode == MODE_INSERT) {
        printf("-- INSERT --  %s", filename);
    } else if (mode == MODE_COMMAND) {
        printf(":%s", command_buffer); // 显示当前正在输入的命令
    }
    
    // 4. 将物理光标移回文本光标位置
    sys_move_cursor(cx, cy);
}

// 处理 Normal 模式下的输入（逻辑完全保持原样）
void handle_normal_input(char c) {
    if (c == 'i') {
        mode = MODE_INSERT; // 切换到插入模式
    } else if (c == ':') {
        mode = MODE_COMMAND; // 切换到命令模式
        command_len = 0;
        memset(command_buffer, 0, sizeof(command_buffer));
    } else if (c == 'h') { // 左移
        if (cursor_pos > 0) cursor_pos--;
    } else if (c == 'l') { // 右移
        if (cursor_pos < buffer_len) cursor_pos++;
    } else if (c == 'k') { // 上移 (UP)
        // 逻辑不变：寻找当前行起始位置
        int curr_line_start = cursor_pos;
        while (curr_line_start > 0 && buffer[curr_line_start - 1] != '\n') {
            curr_line_start--;
        }
        int col = cursor_pos - curr_line_start; // 当前列偏移量

        if (curr_line_start > 0) {
            // 寻找上一行的结束位置
            int prev_line_end = curr_line_start - 1;
            int prev_line_start = prev_line_end;
            // 寻找上一行的起始位置
            while (prev_line_start > 0 && buffer[prev_line_start - 1] != '\n') {
                prev_line_start--;
            }
            // 确保光标不超过上一行的长度
            int prev_line_len = prev_line_end - prev_line_start;
            if (col > prev_line_len) col = prev_line_len;
            cursor_pos = prev_line_start + col;
        }
    } else if (c == 'j') { // 下移 (DOWN)
        // 逻辑不变：寻找当前行起始位置
        int curr_line_start = cursor_pos;
        while (curr_line_start > 0 && buffer[curr_line_start - 1] != '\n') {
            curr_line_start--;
        }
        int col = cursor_pos - curr_line_start;

        // 寻找当前行结束位置
        int curr_line_end = cursor_pos;
        while (curr_line_end < buffer_len && buffer[curr_line_end] != '\n') {
            curr_line_end++;
        }

        if (curr_line_end < buffer_len) { // 如果存在下一行
            int next_line_start = curr_line_end + 1;
            int next_line_end = next_line_start;
            // 寻找下一行的结束位置
            while (next_line_end < buffer_len && buffer[next_line_end] != '\n') {
                next_line_end++;
            }
            // 确保光标不超过下一行的长度
            int next_line_len = next_line_end - next_line_start;
            if (col > next_line_len) col = next_line_len;
            cursor_pos = next_line_start + col;
        }
    } else if (c == 'x') { // 删除字符
        if (cursor_pos < buffer_len) {
            for (int i = cursor_pos; i < buffer_len - 1; i++) {
                buffer[i] = buffer[i+1];
            }
            buffer_len--;
            buffer[buffer_len] = '\0';
        }
    }
}

// 处理 Insert 模式下的输入（逻辑完全保持原样）
void handle_insert_input(char c) {
    if (c == 27) { // ESC 键
        mode = MODE_NORMAL; // 回到普通模式
    } else if (c == 127 || c == '\b') { // Backspace
        if (cursor_pos > 0) {
            // 删除光标前一个字符
            for (int i = cursor_pos - 1; i < buffer_len - 1; i++) {
                buffer[i] = buffer[i+1];
            }
            buffer_len--;
            cursor_pos--;
            buffer[buffer_len] = '\0';
        }
    } else {
        // 插入字符
        if (buffer_len < MAX_BUFFER_SIZE - 1) {
            // 将光标后字符后移
            for (int i = buffer_len; i > cursor_pos; i--) {
                buffer[i] = buffer[i-1];
            }
            buffer[cursor_pos] = (c == '\r') ? '\n' : c; // 处理回车
            buffer_len++;
            cursor_pos++;
            buffer[buffer_len] = '\0';
        }
    }
}

// 处理 Command 模式下的输入
void handle_command_input(char c) {
    if (c == '\r' || c == '\n') { // 按下回车，执行命令
        if (strcmp(command_buffer, "w") == 0) {
            save_file();
            mode = MODE_NORMAL;
        } else if (strcmp(command_buffer, "q") == 0) {
            sys_clear();
            sys_move_cursor(0, 0);
            sys_exit();
        } else if (strcmp(command_buffer, "wq") == 0) {
            save_file();
            sys_clear();
            sys_move_cursor(0, 0);
            sys_exit();
        } else if (strcmp(command_buffer, "help") == 0) { 
            // 新增：识别 help 命令，进入帮助模式
            mode = MODE_HELP;
        } else {
            // 未知命令，回到 Normal
            mode = MODE_NORMAL; 
        }
    } else if (c == 27) { // ESC 键，取消命令
        mode = MODE_NORMAL;
    } else if (c == 127 || c == '\b') { // Backspace
        if (command_len > 0) {
            command_len--;
            command_buffer[command_len] = '\0';
        }
    } else {
        // 记录命令字符
        if (command_len < 15) {
            command_buffer[command_len++] = c;
            command_buffer[command_len] = '\0';
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: vim <filename>\n");
        return 0;
    }

    strcpy(filename, argv[1]);
    load_file();
    render();

    while (1) {
        int c = sys_getchar();
        if (c <= 0 || c == 255) continue; 

        if (mode == MODE_NORMAL) {
            handle_normal_input((char)c);
        } else if (mode == MODE_INSERT) {
            handle_insert_input((char)c);
        } else if (mode == MODE_COMMAND) {
            handle_command_input((char)c);
        } else if (mode == MODE_HELP) {
            // 新增：在帮助模式下，按任意键返回 Normal 模式
            mode = MODE_NORMAL;
        }
        
        render();
        sys_reflush();
    }
    return 0;
}
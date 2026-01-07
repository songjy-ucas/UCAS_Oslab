#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_BUFFER_SIZE 4096
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 24

// Modes
#define MODE_NORMAL 0
#define MODE_INSERT 1
#define MODE_COMMAND 2 

char buffer[MAX_BUFFER_SIZE];
int buffer_len = 0;
int cursor_pos = 0;
int mode = MODE_NORMAL;
char filename[64];
char command_buffer[16];
int command_len = 0;

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

void load_file() {
    int fd = sys_fs_open(filename, 1); // O_RDONLY
    if (fd < 0) {
        buffer_len = 0;
        return;
    }
    buffer_len = sys_fs_read(fd, buffer, MAX_BUFFER_SIZE - 1);
    if (buffer_len < 0) buffer_len = 0;
    buffer[buffer_len] = '\0';
    sys_fs_close(fd);
}

void save_file() {
    int fd = sys_fs_open(filename, 3); // O_RDWR
    if (fd >= 0) {
        sys_fs_write(fd, buffer, buffer_len);
        sys_fs_close(fd);
    }
}

void render() {
    sys_clear();
    printf("%s", buffer);

    int cx, cy;
    get_screen_pos(cursor_pos, &cx, &cy);
    sys_move_cursor(cx, cy);
    printf("$"); 

    sys_move_cursor(0, SCREEN_HEIGHT - 1);
    if (mode == MODE_NORMAL) {
        printf("-- NORMAL --  %s", filename);
    } else if (mode == MODE_INSERT) {
        printf("-- INSERT --  %s", filename);
    } else if (mode == MODE_COMMAND) {
        printf(":%s", command_buffer);
    }
    sys_move_cursor(cx, cy);
}

void handle_normal_input(char c) {
    if (c == 'i') {
        mode = MODE_INSERT;
    } else if (c == ':') {
        mode = MODE_COMMAND;
        command_len = 0;
        memset(command_buffer, 0, sizeof(command_buffer));
    } else if (c == 'h') { 
        if (cursor_pos > 0) cursor_pos--;
    } else if (c == 'l') { 
        if (cursor_pos < buffer_len) cursor_pos++;
    } else if (c == 'k') { // UP
        // Find start of current line
        int curr_line_start = cursor_pos;
        while (curr_line_start > 0 && buffer[curr_line_start - 1] != '\n') {
            curr_line_start--;
        }
        int col = cursor_pos - curr_line_start;

        if (curr_line_start > 0) {
            // Find start of previous line
            int prev_line_end = curr_line_start - 1;
            int prev_line_start = prev_line_end;
            while (prev_line_start > 0 && buffer[prev_line_start - 1] != '\n') {
                prev_line_start--;
            }
            int prev_line_len = prev_line_end - prev_line_start;
            if (col > prev_line_len) col = prev_line_len;
            cursor_pos = prev_line_start + col;
        }
    } else if (c == 'j') { // DOWN
        // Find start of current line
        int curr_line_start = cursor_pos;
        while (curr_line_start > 0 && buffer[curr_line_start - 1] != '\n') {
            curr_line_start--;
        }
        int col = cursor_pos - curr_line_start;

        // Find end of current line
        int curr_line_end = cursor_pos;
        while (curr_line_end < buffer_len && buffer[curr_line_end] != '\n') {
            curr_line_end++;
        }

        if (curr_line_end < buffer_len) { // There is a next line
            int next_line_start = curr_line_end + 1;
            int next_line_end = next_line_start;
            while (next_line_end < buffer_len && buffer[next_line_end] != '\n') {
                next_line_end++;
            }
            int next_line_len = next_line_end - next_line_start;
            if (col > next_line_len) col = next_line_len;
            cursor_pos = next_line_start + col;
        }
    } else if (c == 'x') { 
        if (cursor_pos < buffer_len) {
            for (int i = cursor_pos; i < buffer_len - 1; i++) {
                buffer[i] = buffer[i+1];
            }
            buffer_len--;
            buffer[buffer_len] = '\0';
        }
    }
}

void handle_insert_input(char c) {
    if (c == 27) { // ESC
        mode = MODE_NORMAL;
    } else if (c == 127 || c == '\b') { 
        if (cursor_pos > 0) {
            for (int i = cursor_pos - 1; i < buffer_len - 1; i++) {
                buffer[i] = buffer[i+1];
            }
            buffer_len--;
            cursor_pos--;
            buffer[buffer_len] = '\0';
        }
    } else {
        if (buffer_len < MAX_BUFFER_SIZE - 1) {
            for (int i = buffer_len; i > cursor_pos; i--) {
                buffer[i] = buffer[i-1];
            }
            buffer[cursor_pos] = (c == '\r') ? '\n' : c; 
            buffer_len++;
            cursor_pos++;
            buffer[buffer_len] = '\0';
        }
    }
}

void handle_command_input(char c) {
    if (c == '\r' || c == '\n') {
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
        } else {
            mode = MODE_NORMAL; 
        }
    } else if (c == 27) { 
        mode = MODE_NORMAL;
    } else if (c == 127 || c == '\b') { // Backspace
        if (command_len > 0) {
            command_len--;
            command_buffer[command_len] = '\0';
        }
    } else {
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
        }
        
        render();
        sys_reflush();
    }
    return 0;
}

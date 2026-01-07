#ifndef __UNISTD_H__
#define __UNISTD_H__

#include <stdint.h>
typedef int32_t pid_t;
typedef pid_t pthread_t;
typedef uint64_t size_t;

void sys_sleep(uint32_t time);
void sys_yield(void);
void sys_write(char *buff);
void sys_move_cursor(int x, int y);
void sys_reflush(void);
long sys_get_timebase(void);
long sys_get_tick(void);
int sys_mutex_init(int key);
void sys_mutex_acquire(int mutex_idx);
void sys_mutex_release(int mutex_idx);
void sys_set_sche_workload(int workload);
/************************************************************/
/* TODO: [P3 task1] ps, getchar */
void sys_ps(void);
int  sys_getchar(void);

/* TODO: [P3 task1] exec, exit, kill waitpid */
// S-core
//pid_t  sys_exec(int id, int argc, uint64_t arg0, uint64_t arg1, uint64_t arg2);
// A/C-core
 pid_t  sys_exec(char *name, int argc, char **argv);

void sys_exit(void);
int  sys_kill(pid_t pid);
int  sys_waitpid(pid_t pid);
pid_t sys_getpid();
void sys_clear(void);
void sys_write_ch(char);

/* TODO: [P3 task2] barrier */ 
int  sys_barrier_init(int key, int goal);
void sys_barrier_wait(int bar_idx);
void sys_barrier_destroy(int bar_idx);

/* TODO: [P3 task2] condition */ 
int sys_condition_init(int key);
void sys_condition_wait(int cond_idx, int mutex_idx);
void sys_condition_signal(int cond_idx);
void sys_condition_broadcast(int cond_idx);
void sys_condition_destroy(int cond_idx);

/* TODO: [P3 task2] mailbox */ 
int sys_mbox_open(char * name);
void sys_mbox_close(int mbox_id);
int sys_mbox_send(int mbox_idx, void *msg, int msg_length);
int sys_mbox_recv(int mbox_idx, void *msg, int msg_length);
/************************************************************/

/* Pro3 TASK4 ADD*/
void sys_taskset(int mask, int pid);

/* TODO: [P4 task4] free memory*/
size_t sys_get_free_memory(void);
/* TODO: [P4 task5] pipe*/
int sys_pipe_open(const char *name);
long sys_pipe_give_pages(int pipe_idx, void *src, size_t length);
long sys_pipe_take_pages(int pipe_idx, void *dst, size_t length);

/* TODO: [P5-task1&5] net send and recv */
int sys_net_send(void *txpacket, int length);
int sys_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens);
int sys_net_recv_stream(void *buffer, int *nbytes);
void sys_net_reset();

/* file system operations */
int sys_fs_mkfs(void);
int sys_fs_statfs(void);
int sys_fs_cd(char *path);
int sys_fs_mkdir(char *path);
int sys_fs_rmdir(char *path);
int sys_fs_ls(char *path, int option);
int sys_fs_open(char *path, int mode);
int sys_fs_read(int fd, char *buff, int length);
int sys_fs_write(int fd, char *buff, int length);
int sys_fs_close(int fd);
int sys_fs_ln(char *src_path, char *dst_path);
int sys_fs_rm(char *path);
int sys_fs_lseek(int fd, int offset, int whence);
/************************************************************/


#endif

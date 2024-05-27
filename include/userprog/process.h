#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/*추가한 부분
* parse: 파싱된 인자들, count: 인자의 개수, rsp: 스택 포인터를 가리키는 주소
*/
// void argument_stack(char **parse, int count, void **rsp);
static void argument_stack(char *argv[], int argc, struct intr_frame *if_);
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
void process_close_fid(int fd);
struct thread *get_child_process(int pid);

//추가한 부분
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);
struct thread *get_child_process(int pid);
#endif /* userprog/process.h */

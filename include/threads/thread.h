#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
// #include "user/syscall.h"
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status
{
	THREAD_RUNNING, /* Running thread. */
	THREAD_READY,	/* Not running but ready to run. */
	THREAD_BLOCKED, /* Waiting for an event to trigger. */
	THREAD_DYING	/* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
// 스레드 식별자 유형. 원하는 유형으로 재정의할 수 있다. 커널이 돌아가는 내내 유일하게 유지되는 tid를 가지고 있어야한다.
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0	   /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63	   /* Highest priority. */
#define MAX_FD 64		// 테이블 최대 크기   

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread
{
	/* Owned by thread.c. */
	tid_t tid;				   /* Thread identifier. */
	enum thread_status status; /* Thread state. */
	char name[16];			   /* Name (for debugging purposes). */
	int priority;			   /* Priority. */

	int org_priority;

	int64_t local_tick; /*add local_tick*/

	struct lock *wait_on_lock; /* add wait_on_lock */
	struct list_elem d_elem;   /* add d_elem */
	struct list_elem all_elem; /* all_elem*/
	struct list donations;	   /* add donations */
	int nice;				   /*add struct*/
	int recent_cpu;			   /*add struct*/

	/* Shared between thread.c and synch.c. */
	struct list_elem elem; /* List element. */
	/* add code_pro2*/

	// pid_t pid;
	struct thread *parent_process; // 부모 프로세스 디스크립터를 가리키는 필드 추가
	struct list_elem c_elem;	   // 자식 리스트 element
	struct list child_list;		   // 자식 리스트
	bool is_program_loaded;		   // 프로세스의 프로그램 메모리 적재 유무
	bool is_program_exit;		   // 프로세스 종료 유무 확인
	struct semaphore sema_load;	   // load 세마포어		// fork에 사용 
	struct semaphore sema_exit;	   // exit 세마포어
	struct semaphore sema_wait;    // wait  세마포어  
	int exit_status;			   // exit 호출 시 종료 status

	struct file *fd_table[MAX_FD]; // 파일 디스크립터 테이블
	int max_fd;					// 현재 테이블에 존재하는 fd값의 최대값 +1;
	struct file *running;
	struct intr_frame *parent_if;

	/* add code_pro2*/

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4; /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf; /* Information for switching */
	unsigned magic;		  /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame *tf);

/* add function - gdy*/
void wake_up(int64_t ticks);
void thread_sleep(int64_t ticks);
int64_t get_global_tick(void);
bool cmp_priority(const struct list_elem *a_, const struct list_elem *b_,
				  void *aux UNUSED);
void thread_preemtive(void);
bool cmp_priority_donation(const struct list_elem *a_, const struct list_elem *b_,
						   void *aux UNUSED);

void calculate_priority(struct thread *t);
void calculate_recent_cpu(struct thread *t);
int calculate_load_average();
void up_recent_cpu(struct thread *t);
void recalculate_all();
void recalculate_priority();
/* add function - gdy*/

#endif /* threads/thread.h */

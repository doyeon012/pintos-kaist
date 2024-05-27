#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include "synch.h" //semaphore 관련 헤더 파일
#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "/pintos-kaist/include/vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
/* 파일 디스크립터 테이블 초기화를 위한 매크로*/
#define FDT_PAGES 2 //fdt에 할당할 페이지 수
#define FDT_COUNT_LIMIT 128 // fdt의 최대 크기
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
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	int64_t wakeup; //추가한 부분. 꺠어나야 하는 ticks 값

	/* 스레드마다 양도받은 내역을 관리할 수 있는 내역*/
	int init_priority; //추가한 부분. 초기 우선순위값. 우선순위 복원 시 사용
	struct lock *wait_on_lock; //추가한 부분. 스레드가 기다리는 lock
	struct list donations; //추가한 부분. 자신에게 priority를 나눠준 thread list
	struct list_elem donation_elem; //추가한 부분. 우선순위 기부 리스트의 element

	int nice;//추가한 부분. MLFQS
	int recent_cpu;//추가한 부분. MLFQS
	struct list_elem allelem; //추가한 부분. 모든 스레드들의 리스트

	int exit_status;
	struct file **fdt;//파일 디스크립터 테이블. 각 프로세스가 가지고 있는 파일 객체와 연결된 fd의 배열.
	int next_fd;//다음에 할당할 파일 디스크립터 번호

	struct intr_frame parent_if;
	struct list child_list;
	struct list_elem child_elem;

	// 새로운 스레드가 실행 파일을 성공적으로 load할 때까지 부모 스레드가 기다리도록 함
	// 스레드가 실행 파일을 성공적으로 load했을 때 sema_up(&child->load_sema);를 호출, 부모 스레드를 깨움.
	struct semaphore load_sema;
	// 자식 스레드가 종료될 때, 부모 스레드가 자식 스레드의 종료를 처리할 시간을 주기 위해 사용.
	// sema_down(&curr->exit_sema);를 호출해 대기 상태로 들어감, 
	// 부모 스레드가 sema_up(&child->exit_sema);를 호출해 자식 스레드를 깨움.
	struct semaphore exit_sema;
	// 부모 스레드가 자식 스레드의 종료를 기다릴 때 사용. 자식 스레드가 종료될 때
	// sema_up(&curr->wait_sema); 를 호출, 부모 스레드를 깨움.
	struct semaphore wait_sema;

	struct file *running; // 실행 중인 파일


#ifdef USERPROG 
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;//추가한 부분
	void* rsp;//추가한 부분
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs; // -mlfqs 옵션을 주면 해당 값이 true가 된다.

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

//추가한 부분. prototype 선언.
void thread_sleep(int64_t ticks); // ticks 만큼 sleep
void thread_awake(int64_t ticks); // ticks 만큼 깨움

bool cmp_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED);//추가한 부분
void thread_preemptive(void);//추가한 부분
bool cmp_donate_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void donate_priority(void);
void remove_with_lock(struct lock *lock);//추가한 부분
void revert_priority(void);//추가한 부분 

//mlfqs 추가해준 부분
void mlfqs_calculate_priority(struct thread *t);
void mlfqs_calculate_recent_cpu(struct thread *t);
void mlfqs_calculate_load_avg (void);
void mlfqs_increment_recent_cpu (void);
void mlfqs_recalculate_recent_cpu (void);
void mlfqs_recalculate_priority (void);
#endif /* threads/thread.h */

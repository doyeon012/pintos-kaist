#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */

static struct list ready_list;
static struct list sleep_list;
static struct list all_list; // add code
/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4		  /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* add code - gdy*/
static int64_t global_tick = INT64_MAX; // global_tick을 최대값으로 초기화//
/* add code - gdy*/

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* add function - gdy*/
void thread_sleep(int64_t ticks);
static bool find_less_tick(const struct list_elem *a_, const struct list_elem *b_,
						   void *aux UNUSED);
void wake_up(int64_t ticks);
/* add function - gdy*/

/* add function - gdy2*/
bool cmp_priority(const struct list_elem *a_, const struct list_elem *b_,
				  void *aux UNUSED);
void thread_preemtive(void);
bool cmp_priority_donation(const struct list_elem *a_, const struct list_elem *b_,
						   void *aux UNUSED);
int load_average;
/* add function - gdy2*/

/* add function - gdy3*/

void calculate_priority(struct thread *t);
void calculate_recent_cpu(struct thread *t);
int calculate_load_average();
void up_recent_cpu(struct thread *t);
void recalculate_all();
void recalculate_priority();

/* add function - gdy3*/

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	lock_init(&tid_lock);
	list_init(&ready_list);
	/* add function - gdy*/
	list_init(&sleep_list);
	/* add function - gdy*/
	list_init(&all_list); // add cdoe

	list_init(&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
	load_average = 0;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
// 인터럽트가 활성화 되면 선점형 스레드 스케줄링을 시작하고, idle 스레드를 생성한다.
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
// 커널 내에서 새로운 스레드를 생성하는 함수
// 주어진 이름과 우선순위로 새로운 커널 스레드를 생성, 실행하고자 하는 함수를 넣는다.
// thread_create()가 실행되면 쓰레드에 전달된 함수가 실행된다.
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL); // ASSERT : 에러 검출 용 코드, ASSERT 함수에 걸리게 되면 버그 발생위치, call stack등 여러 정보를 알 수 있게 된다.

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR; // 쓰레드 생성에 실패하면 TID_ERROR를 반환

	/* Initialize thread. */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* add code_pro2*/
	// t->pid = tid;									  // pid 값에 tid 값을 넣음
	struct thread *curr = thread_current();		   // 현재 실행중인 프로세스가 부모 프로세스이다.
	t->parent_process = curr;					   // 부모 프로세스 저장
	t->is_program_loaded = 0;					   // 프로그램이 로드되지 않음
	t->is_program_exit = 0;						   // 프로그램이 종료되지 않음
	sema_init(&t->sema_load, 0);				   // load 세마포어 0으로 초기화
	sema_init(&t->sema_exit, 0);				   // exit 세마포어 0으로 초기화
	sema_init(&t->sema_wait, 0);				   // wait 세마포어 0으로 초기화   
	list_push_back(&curr->child_list, &t->c_elem); // 부모 프로세스의 자식리스트에 추가
	t->fd_table[0] = 0;
	t->fd_table[1] = 1;
	t->max_fd = 2; // fd 값 초기화
	t->exit_status = 0;

	/* add code_pro2*/

	/* Add to run queue. */
	thread_unblock(t);

	/* add code - gdy2*/
	thread_preemtive();
	/* add code - gdy2*/

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
/* code modify - gdy*/
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);
	// list_push_back(&ready_list, &t->elem);
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL); // ready_list에 삽입할 때 우선순위 순으로 정렬 (내림차순)
	t->status = THREAD_READY;
	intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
// 실행 중인 스레드를 반환한다.
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable();
	struct thread *curr = thread_current();
	// curr->is_program_exit = 1;	// 프로세스 종료 알림 -> ??이거 어디 반영?
	do_schedule(THREAD_DYING);
	// sema_up(&curr->sema_exit);	// 부모 프로세스가 ready_list에 들어갈 수 있도록 sema_up
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
// cpu 양보를 수행 . 현재 스레드는 sleep되지 않고, 스케줄러에 따라 즉시 다시 실행될 수 있다.
void thread_yield(void)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable();
	if (curr != idle_thread)
		// list_push_back(&ready_list, &curr->elem);
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL);
	do_schedule(THREAD_READY);
	intr_set_level(old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
// 현재 thread의 우선순위를 new_priority로 설정
void thread_set_priority(int new_priority)
{
	if (thread_mlfqs) // thread_set_priority 함수는 mlfqs가 ture일 경우일땐 반영되지 않아야한다.
	{
		return;
	}
	struct thread *curr = thread_current();
	curr->org_priority = new_priority; // 새로 받은 우선순위를 org_priority에 반영
	curr->priority = new_priority;	   // 새로 받은 우선순위를 priority에 반영

	// 우선 순위 기부

	if (!list_empty(&curr->donations)) // donations가 비어있지 않을 경우
	{
		struct thread *donation_first;
		donation_first = list_entry(list_begin(&curr->donations), struct thread, d_elem);
		if (curr->priority < donation_first->priority)
		{
			curr->priority = donation_first->priority;
		}
	}

	//
	struct thread *readey_list_first = list_entry(list_begin(&ready_list), struct thread, elem);
	if (curr->priority < readey_list_first->priority)
	{
		thread_yield();
	}
}

/* Returns the current thread's priority. */
// 현재 쓰레드의 우선순위를 반환한다.
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */

/* thread의 nice값 setting */
void thread_set_nice(int nice UNUSED)
{
	enum intr_level old_level;
	old_level = intr_disable(); // interrupt 비활성화
	struct thread *t = thread_current();
	t->nice = nice;		   // 현재 thread의 nice 값 갱신
	calculate_priority(t); // 우선순위 재 계산
	// 우선순위 비교 후 context switching
	struct thread *readey_list_first = list_entry(list_begin(&ready_list), struct thread, elem);
	if (readey_list_first != NULL)
	{
		if (t->priority < readey_list_first->priority)
		{
			thread_yield();
		}
	}
	intr_set_level(old_level); // interrupt 활성화
}

/* Returns the current thread's nice value. */

/* 현재 thread의 nice 값 return */
int thread_get_nice(void)
{
	enum intr_level old_level;
	old_level = intr_disable(); // interrupt 활성화
	int temp = thread_current()->nice;
	intr_set_level(old_level); // interrupt 비 활성화
	return temp;			   // 현재 thread의 nice 값 반환
}

/* Returns 100 times the system load average. */

/* load_average에 100을 곱한 값을 리턴 */
int thread_get_load_avg(void)
{
	enum intr_level old_level;
	old_level = intr_disable();

	int temp = TO_INTEGER_ROUND(load_average * 100); // 출력되는 값이므로 정수형으로 바꿔준 후 출력한다.

	intr_set_level(old_level);

	return temp;
}

/* Returns 100 times the current thread's recent_cpu value. */

/* recent_cpu에 100을 곱한 값을 리턴 */
int thread_get_recent_cpu(void)
{
	enum intr_level old_level;
	old_level = intr_disable();

	int temp = TO_INTEGER_ROUND(thread_current()->recent_cpu * 100); // 출력되는 값이므로 정수형으로 바꿔준 후 출력한다.

	intr_set_level(old_level);
	return temp;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */

// idle thread는 다른 어떤 스레드도 실행준비가 되지 않았을 때 실행된다.
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);
	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
	t->priority = priority;
	t->wait_on_lock = NULL; // add code _gdy2
	list_init(&t->donations);
	t->org_priority = priority;
	t->magic = THREAD_MAGIC;
	// nice값과 recent_cpu값을 초기화 한다.
	t->nice = 0;							 // add code
	t->recent_cpu = 0;						 // add code
	list_push_back(&all_list, &t->all_elem); // add code	// 모든 thread가 초기화 될 때 all_list에 넣어준다.
	t->running = NULL;
	/* add code_pro2*/
	list_init(&t->child_list); // 자식 리스트 초기화
							   /* add code_pro2*/
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run(void)
{

	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
// 하나의 쓰레드가 do_iret()에서 iret을 실행할 때 다른 스레드가 실행되기 시작한다.
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		: : "g"((uint64_t)tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */

// 문맥교환이 일어나는 방식 정의
// 현재 실행중인 쓰레드의 상태를 저장하고, 스위칭 할 쓰레드(다음으로 진행할 쓰레드)의 상태를 복원한다.
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
// 새로운 프로세스를 스케줄한다. 진입 시 인터럽트는 비활성화되어 있어야한다.
// 현재 스레드의 상태를 status로 변경한 다음, 다른 스레드를 찾아서 그것으로 전환한다.
// context switch
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		list_remove(&victim->all_elem); // 삭제되는 thread = victim이므로 해당 쓰레드의 all_elem값을 삭제
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

static void
schedule(void)
{
	struct thread *curr = running_thread();
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
#endif

	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

/* add function - gdy*/

// thread를 sleep_list에 삽입하는 함수
void thread_sleep(int64_t ticks)
{
	struct thread *curr = thread_current(); // 현재 실행중인 쓰레드
	enum intr_level old_level;
	old_level = intr_disable(); // 인터럽트를 비활성화 하고 old_level에 이전 인터럽트 상태 저장
	if (curr != idle_thread)	// curr 쓰레드가 idle 쓰레드가 아닐 경우
	{
		curr->local_tick = ticks;											 // 먼저 tick값을 해당 쓰레드의 local_tick값에 저장
		list_insert_ordered(&sleep_list, &curr->elem, find_less_tick, NULL); // 오름차순으로 정렬하면서 sleep_list에 해당 쓰레드를 삽입한다.
		struct thread *new_first = list_entry(list_begin(&sleep_list),
											  struct thread, elem); // sleep_list에 존재하는 쓰레드 중 첫번째 쓰레드의 local_tick값을 global_tick에 저장
		global_tick = new_first->local_tick;						// sleep_list는 오름차순으로 정렬되어 있으므로, global_tick 값에 저장되는 값은 sleep_list의 local_tick값 중 최소값이 된다.
		thread_block();												// curr 스레드의 상태를 block 해주고 schedule()을 실행한다.
	}
	intr_set_level(old_level); // 인터럽트 활성화
}

// 리스트에서 local_tick값 크기 비교하기
bool find_less_tick(const struct list_elem *a_, const struct list_elem *b_,
					void *aux UNUSED) // list_insert_ordered 함수에 사용한다.
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);

	return a->local_tick < b->local_tick; // sleep_list를 돌며 현재 local_tick 값보다 큰 값이 있으면 반복문을 종료하고 그 자리에 현재 쓰레드를 삽입한다.
}

// sleep_list에 있던 쓰레드 깨우기(ready_list로 넣기)
void wake_up(int64_t ticks)
{
	if (list_empty(&sleep_list))
	{
		global_tick = INT64_MAX;
		return;
	}
	struct thread *new_wake_thread = list_entry(list_pop_front(&sleep_list), struct thread, elem); // new_wak_thread는 ready_list에 넣을 쓰레드(리스트의 가장 앞에 있다.)
	thread_unblock(new_wake_thread);															   // 인터럽트를 비활성화 해준 이후 ready_list에 쓰레드를 넣고, 해당 쓰레드의 상태도 변경해준다. 이후 인터럽트를 다시 활성화한다.
	if (list_empty(&sleep_list))
	{
		global_tick = INT64_MAX;
		return;
	}
	struct thread *new_first = list_entry(list_begin(&sleep_list), struct thread, elem); // sleep_list가 갱신 되었으므로 global_tick을 초기화한다.
	global_tick = new_first->local_tick;												 // new_first의 local_tick값이 최솟값으로 설정한다.
																						 // schedule()함수는 실행시키지 않아도 된다.
}
// global_tick 값을 넘겨주기 위한 함수
int64_t get_global_tick(void)
{
	return global_tick;
}

/* add function - gdy*/

/* add function - gdy2*/
// 리스트에서 priority값 크기 비교하기
bool cmp_priority(const struct list_elem *a_, const struct list_elem *b_,
				  void *aux UNUSED) // list_insert_ordered 함수에 사용한다.
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);

	return a->priority > b->priority; // ready_list를 돌며 현재 local_tick 값보다 큰 값이 있으면 반복문을 종료하고 그 자리에 현재 쓰레드를 삽입한다.
}

void thread_preemtive(void)
{
	struct thread *curr = thread_current();
	struct thread *readey_list_first = list_entry(list_begin(&ready_list), struct thread, elem);
	if (curr == idle_thread)
		return;
	if (!list_empty(&ready_list) && curr->priority < readey_list_first->priority)
	{
		thread_yield();
	}
}

bool cmp_priority_donation(const struct list_elem *a_, const struct list_elem *b_,
						   void *aux UNUSED) // list_insert_ordered 함수에 사용한다.
{
	const struct thread *a = list_entry(a_, struct thread, d_elem);
	const struct thread *b = list_entry(b_, struct thread, d_elem);

	return a->priority > b->priority; // ready_list를 돌며 현재 local_tick 값보다 큰 값이 있으면 반복문을 종료하고 그 자리에 현재 쓰레드를 삽입한다.
}
/* add function - gdy2*/

/* add function - gdy3 */

/*nice와 cpu_recent를 사용하여 우선순위를 계산한다.*/
void calculate_priority(struct thread *t)
{
	if (t != idle_thread) // 현재 thread가 ide_thread가 아닐 경우
	{
		t->priority = PRI_MAX - TO_INTEGER_ROUND((t->recent_cpu / 4)) - (t->nice * 2);
	}
}

/* recent_cpu를 계산한다.*/
void calculate_recent_cpu(struct thread *t)
{
	if (t != idle_thread)
	{
		// int new_load_average = calculate_load_average();
		// int decay = (2* load_average) / (2* load_average + 1);
		int decay = DIVIDE((2 * load_average), (ADD_INT(2 * load_average, 1)));
		// t->recent_cpu = decay * t->recent_cpu + t->nice;
		t->recent_cpu = ADD_INT(MULTIPLY(decay, t->recent_cpu), t->nice);
	}
}

/* load_avg를 계산하는 함수 */
int calculate_load_average()
{
	int ready_threads;
	if (thread_current() == idle_thread) // 현재 실행준인 thread가 idle_thread일 경우
	{
		ready_threads = list_size(&ready_list); // ready_threads는 ready_list에 들어있는 모든 thread의 개수
	}
	else
	{
		ready_threads = list_size(&ready_list) + 1; // ready_threads는 ready_list에 들어있는 모든 thread의 개수 + 실행중인 thread
	}
	// load_average = (59 / 60) * load_average + (1 / 60) * ready_threads;
	load_average = ADD(MULTIPLY(DIVIDE(TO_FIXED_POINT(59), TO_FIXED_POINT(60)), load_average), MULTIPLY_INT(DIVIDE(TO_FIXED_POINT(1), TO_FIXED_POINT(60)), ready_threads));
	return load_average;
}

/* recent_cpu를 1씩 증가시키는 함수 */
void up_recent_cpu(struct thread *t)
{
	if (t != idle_thread)
	{
		return t->recent_cpu = ADD_INT(t->recent_cpu, 1);
	}
}

/*모든 thread의 우선순위와 recent_cpu 다시 계산한다. */
void recalculate_all()
{
	// all_list라는 새로운 리스트를 만듬, all_list에는 존재하는 모든 thread가 들어가 있다.
	struct list_elem *all_list_first = list_begin(&all_list);
	calculate_load_average(); // load_average를 갱신해 준 이후 pirority와 recent_cpu값을 갱신해준다.
	while (all_list_first != list_end(&all_list))
	{
		struct thread *first_thread = list_entry(all_list_first, struct thread, all_elem);
		calculate_priority(first_thread);	// 수식에 의한 priority값 갱신
		calculate_recent_cpu(first_thread); // recent_cpu 값 갱신
		all_list_first = all_list_first->next;
	}
}
// 모든 thread의 우선순위를 계산한다.
void recalculate_priority()
{
	struct list_elem *all_list_first = list_begin(&all_list);
	while (all_list_first != list_end(&all_list))
	{
		struct thread *first_thread = list_entry(all_list_first, struct thread, all_elem); // all_list의 elem 이므로 all_elem을 사용해야한다.
		calculate_priority(first_thread);
		all_list_first = all_list_first->next;
	}
}
/* add function - gdy3 */
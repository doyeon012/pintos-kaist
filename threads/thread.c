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

static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

#define TIME_SLICE 4		  /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static int64_t global_tick = INT64_MAX; 

static void kernel_thread(thread_func *, void *aux);
void thread_wakeup(ticks);
static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);


void thread_sleep(int64_t ticks);
static bool find_less_tick(const struct list_elem *a_, const struct list_elem *b_,
						   void *aux UNUSED);
bool cmp_priority(const struct list_elem *a_, const struct list_elem *b_,
				  void *aux UNUSED);
void thread_preemtive(void);
bool cmp_priority_donation(const struct list_elem *a_, const struct list_elem *b_,
						   void *aux UNUSED);
int load_average;
void mlfqs_priority(struct thread *t);
void mlfqs_recent_cpu(struct thread *t);
void mlfqs_recalc(void);
void mlfqs_recale_priority(void);
void mlfqs_increment(void);

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

	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&sleep_list);
	list_init(&all_list);
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
// idle thread를 생성하고, 동시에 idle 함수가 실행.
// idle thread는 한 번 schedule을 받고, sema_up을 하여 thread_start()의 마지막 sema_down을 풀어주어
// thread_start가 작업을 끝나고 run_action()이 실행될 수 있도록 해주고, idle 자신은 block이 된다.
// idle thread는 pinots에서 실행 가능한 thread가 하나도 없을 때 wake되어 다시 작동하는데, 이는 무조건 하나의 thread는 실행하고 있는 상태를
// 만들기 위함이다.
// 스레드 스케줄링을 시작하고 유휴 스레드를 생성
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);//세마포어 초기화. 세마포어는 0으로 초기화되어 있음.
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable(); // 인터럽트를 활성화. 인터럽트를 활성화 > 타이머 인터럽트가 발생, 타이머 인터럽트 핸들러가 실행됨.

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started); // sema_down: 세마포어를 내리는 함수. 세마포어가 0이 될 때까지 기다림.
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

// 새로운 커널 스레드 생성
// 생성시 초기 우선 순위 설정
// 새로운 커널 스레드를 생성하고 초기화하여 wait_list큐에 추가
// 스레드는 'kernel_thread'함수로 시작되며, 이 함수는 'function'과 'aux'를 인수로 받아 실행.
// 스레드가 실행될 때 준비가 되면 실행 대기 큐에 추가되어 스케줄러가 이를 실행할 수있게 된다.
tid_t
thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL); // ASSERT : 에러 검출 용 코드, ASSERT 함수에 걸리게 되면 버그 발생위치, call stack등 여러 정보를 알 수 있게 된다.

	/* Allocate thread. */
	// 2. 스레드 메모리 할당
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR; 

	/* Initialize thread. */
	// 3. 스레드 초기화
	init_thread(t, name, priority); // thread 구조체 초기화
	tid = t->tid = allocate_tid(); // tid 할당

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

	/*
		프로세스 계층 구조 구현
		
		부모 프로세스 저장
		프로그램이 로드되지 않음
		프로세스가 종료되지 않음
		exit 세마포어 0으로 초기화
		load 세마포어 0으로 초기화
		자식 리스트에 추가
	*/
	struct thread *cur = thread_current();		  
	t->parent_process = cur;					   
	t->is_program_loaded = false;					 
	t->is_program_exit = false;						  
	
	sema_init(&t->sema_load, 0);				  
	sema_init(&t->sema_exit, 0);				  
	list_push_back(&cur->child_list, &t->child_elem); 
	
	t->fd_table[0] = 0;
	t->fd_table[1] = 1;
	t->max_fd = 2; //


	/* Add to run queue. */
	thread_unblock(t);
	thread_preemtive();
	
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
tid_t
thread_tid(void)
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
// 현재 실행중인 스레드가 CPU를 자발적으로 양보하고, 다른 스레드에 실행 기회를 주기 위해 사용된다.
void
thread_yield(void)
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

void
thread_set_priority (int new_priority) {

	// mlfqs 스케줄러 일때 우선순위를 임의로 변경할 수 없도록 한다.
	if(thread_mlfqs) return;

	// 1. 현재 실행 중인 스레드 가져오기
	struct thread *curr = thread_current();

	// 원래 우선 순위 설정
	curr->org_priority = new_priority;
	// update_priority_for_donations();

	 // 3. 조건 확인 및 우선순위 설정
	 if(list_empty(&curr->donations) || new_priority > curr->priority) // 새로운 우선순위가 현재 우선순위보다 높은지
	 {
	 	// 4. 우선순위 업데이트
	 	curr->priority = new_priority;
	 }
	 else
	 {
	 	curr->priority = list_entry(list_front(&curr->donations), struct thread, d_elem)->priority;
	 }

	thread_preemtive();
}

/* Returns the current thread's priority. */
// 현재 스레드의 우선 순위 반환
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
// 현재 thread의 nice값을 nice로 설정
void
thread_set_nice (int nice UNUSED) {

	// 현재 스레드의 nice 값을 변경하는 함수 구현
	// 해당 작업중에 인터럽트는 비활성화
	// 현재 스레드의 nice 값 변경
	// nice 값 변경 후에 현재 스레드의 우선순위를 재계산 > 우선순위에 의해 스케줄링

	struct thread *t = thread_current();
	// 2. 인터럽트 비활성화
	enum intr_level old_level;
	old_level = intr_disable();

	// 3. 'nice'값 설정
	t->nice = nice;

	// 4. 우선순위 재계산 - 'nice' 값이 바뀌었기 때문에.
	mlfqs_priority(t);

	thread_preemtive();
	
	// 6. 인터럽트 복원
	intr_set_level(old_level);	
}

/* Returns the current thread's nice value. */
// 현재 thread의 nice 값 반환
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	// 현재 스레드의 nice 값을 반환
	// 해당 작업중에 인터럽트는 비활성화.

	struct thread *t = thread_current();
	enum intr_level old_level;

	old_level = intr_disable();
	int nice_val = t->nice;
	intr_set_level(old_level);
	
	return nice_val;
}

/* Returns 100 times the system load average. */
// load_avg에 100을 곱해서 반환
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	// 해당 과정중에 인터럽트는 비활성화
	enum intr_level old_level;

	old_level = intr_disable();
	int new_load_avg = fp_to_int(mult_mixed(load_average, 100));
	intr_set_level(old_level);

	return new_load_avg;
}

/* Returns 100 times the current thread's recent_cpu value. */
// recent_cpu에 100을 곱해서 반환
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	// 해당 과정중에 인터럽트는 비활성화

	enum intr_level old_level;

	old_level = intr_disable();
	int new_recent_cpu = fp_to_int(mult_mixed(thread_current()->recent_cpu, 100));
	intr_set_level(old_level);

	return new_recent_cpu;
}

// recent_cpu and nice값을 이용하여 priority를 계산
void
mlfqs_priority(struct thread *t)
{
	// 해당 스레드가  idle_thread가 아닌지 검사
	// priority계산식 구현(fixed_point.h의 계산함수 이용)
	if(t!= idle_thread)
	{
		int rec_by_4 = div_mixed(t->recent_cpu, 4);// recent_cpu를 4로 나눈 값 계산
		int nice2 = 2 * t->nice; // nice 값을 2배 한 값 계산
		int to_sub = add_mixed(rec_by_4, nice2); // recent_cpu / 4 + 2 * nice 계산
		int tmp = sub_mixed(to_sub, (int)PRI_MAX); // PRI_MAX - (recent_cpu / 4 + 2 *nice) 계산
		int pri_result = fp_to_int(sub_fp(0, tmp)); // priority 값을 고정 소수점에서 정수로 변환 후 보정

		// 계산된 우선순위의 범위 보정
		// 계산된 우선순위가 PRI_MIN 보다 작으면 PRI_MIN으로 설정
		if(pri_result < PRI_MIN)
			pri_result = PRI_MIN;

		// 계산된 우선순위가 PRI_MAX 보다 크면 PRI_MAX로 설정
		if(pri_result > PRI_MAX)
			pri_result = PRI_MAX;
		
		// 스레드의 우선순위 설정
		t->priority = pri_result;
	}

}

// 스레드의 recent_cpu 업데이트
void
mlfqs_recent_cpu(struct thread *t)
{
	// 해당 스레드가 idle_thread가 아닌지 검사
	// recent_cpu계산식 구현(fixed_point.h의 계산함수 이용)

	if(t != idle_thread)
	{
		int load_avg_2 = mult_mixed(load_average, 2); // load_avg의 2배 값 계산
		int load_avg_2_1 = add_mixed(load_avg_2, 1); // load_avg의 2배에 1을 더한 값 계산

		int frac = div_fp(load_avg_2, load_avg_2_1); // load_avg의 2배를 load_avg의 2배+1로 나눈값 계산
		int tmp = mult_fp(frac, t->recent_cpu); // 위에서 구한 값을 현재 스레드의 recent_cpu 값과 곱한 값 계산
		int result = add_mixed(tmp, t->nice); // 위에서 구한 값에 스레드의 nice 값을 더한 값 계산

		// result 값이 음수가 되지 않도록 보정
		if ((result >> 31) == (-1) >> 31)
		{
			result = 0;
		}

		// 계산된 값을 스레드의 recent_cpu 값으로 설정
		t->recent_cpu = result;
	}
}

// load_avg 값 계산
// 평균 cpu 부하를 계산하여 'load_avg' 값을 업데이트('recent_cpu'와 'priority')
void
mlfqs_load_avg(void)
{
	// load_avg계산식을 구현(fixed_point.h의 계산함수 이용)
	// load_avg는 0보다 작아질 수 없다.
	int a = div_fp(int_to_fp(59), int_to_fp(60)); // 59/60 계산
	int b = div_fp(int_to_fp(1), int_to_fp(60)); // 1/60 계산

	int load_avg2 = mult_fp(a, load_average); // a * load_avg 계산
	int ready_thread = (int)list_size(&ready_list); // read_thread 값 계산
	ready_thread = (thread_current() == idle_thread) ? ready_thread : ready_thread + 1;

	int ready_thread2 = mult_mixed(b, ready_thread); // b * ready_thread 계산
	int result = add_fp(load_avg2, ready_thread2); // load_avg2 + ready_thread2 계산

	// load_avg값을 result로 갱신
	load_average = result; 
}

// recent_cpu 값 1증가
void
mlfqs_increment(void)
{
	// 해당 스레드가 idle_thread가 아닌지 검사
	// 현재 스레드 recent_cpu 값을 1증가 시킴

	if(thread_current() != idle_thread)
	{
		int cur_recent_cpu = thread_current()->recent_cpu; // 현재 실행중인 스레드의 'recent_cpu' 값 가져오기
		thread_current()->recent_cpu = add_mixed(cur_recent_cpu, 1); // 고정 수소점 값을 인자로 받아 정수값을 더하는 함수
	}
}

// 모든 스레드의 recent_cpu를 재계산
void
mlfqs_recalc_recent_cpu(void)
{
	// 리스트 순회
	for (struct list_elem *tmp = list_begin(&all_list); tmp != list_end(&all_list); tmp = list_next(tmp))
	{
		mlfqs_recent_cpu(list_entry(tmp, struct thread, all_elem));
	}
}

// 모든 스레드의 priority를 재계산
void
mlfqs_recale_priority(void)
{
	// 리스트 순회
	for (struct list_elem *tmp = list_begin(&all_list); tmp != list_end(&all_list); tmp = list_next(tmp))
	{
		
		mlfqs_priority(list_entry(tmp, struct thread, all_elem));
	}
}

// 모든 thread의 recent_cpu + priority값 재계산
void
mlfqs_recalc(void)
{
	mlfqs_recalc_recent_cpu();
	mlfqs_recale_priority();
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
	t->wait_on_lock = NULL; 
	list_init(&t->donations);
	t->org_priority = priority;
	t->magic = THREAD_MAGIC;
	
	t->nice = 0;							 
	t->recent_cpu = 0;						 
	list_push_back(&all_list, &t->all_elem); 

	list_init(&t->child_list); 
							
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

// 특정 시간 동안 현재 실행 중인 스레들을 재우기 위해
// 주어진 틱 수 동안 스레드를 일시 중단하고, 지정된 시간이 지나면 깨운다.
// 틱(타이머 인터럽트 발생 주기)
void 
thread_sleep(int64_t ticks)
    {
		struct thread *curr;
		enum intr_level old_level;

		// 1. 인터럽트 비활성화
        old_level = intr_disable(); 

		// 2. 현재 스레드 가져오기
        curr = thread_current();     

		// 3. 스레드 검사(현재 스레드가 idle이 아닐때만)
        ASSERT(curr != idle_thread); 

		// 4. 일어날 시각 설정
        curr->wakeup_ticks = ticks;
		
		// 5. sleep_list에 스레드 추가   
        list_insert_ordered(&sleep_list, &curr->elem, find_less_tick, NULL); // sleep_list에 추가
        
		// 6. 스레드 블록
		thread_block(); // 

		// 7. 인터럽트 복원
        intr_set_level(old_level); 
    }



bool find_less_tick(const struct list_elem *a_, const struct list_elem *b_,
					void *aux UNUSED) // list_insert_ordered 함수에 사용한다.
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);

	return a->local_tick < b->local_tick; // sleep_list를 돌며 현재 local_tick 값보다 큰 값이 있으면 반복문을 종료하고 그 자리에 현재 쓰레드를 삽입한다.
}

// sleep_list에 있는 스레드들이 깨어날 시간이 되었는지 확인, 해당 시간이 되었을 때
// 스레드들을 'ready_list'로 이동시키는 역할.
void 
wake_up(int64_t current_ticks)
    {
        enum intr_level old_level;
		// 1. 인터럽트 비활성화
        old_level = intr_disable(); 

        struct list_elem *curr_elem = list_begin(&sleep_list);

		// 2. 'sleep_list' 순회
        while (curr_elem != list_end(&sleep_list))
        {
            struct thread *curr_thread = list_entry(curr_elem, struct thread, elem); // 현재 검사중인 elem의 스레드
			
			// 3. 스레드 깨우기
            if (current_ticks >= curr_thread->wakeup_ticks)
            {	
				// 4. 순회 계속(sleep_list에서 제거, curr_elem에는 다음 elem이 담김)
                curr_elem = list_remove(curr_elem); 
                
				// 3. 스레드 깨우기(ready_list로 이동)
				thread_unblock(curr_thread);       
            }
            else
                break;
        }

		// 5. 인터럽트 복원
        intr_set_level(old_level); // 인터럽트 상태를 원래 상태로 변경
    }

// global_tick 값을 넘겨주기 위한 함수
int64_t get_global_tick(void)
{
	return global_tick;
}

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
						   void *aux UNUSED) 
{
	const struct thread *a = list_entry(a_, struct thread, d_elem);
	const struct thread *b = list_entry(b_, struct thread, d_elem);

	return a->priority > b->priority; 
}

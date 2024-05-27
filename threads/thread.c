#include "threads/fixed_point.h" // fixed-point 연산을 하기 위해 헤더 파일을 선언해줌.
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
// 추가한 부분
static struct list ready_list; 
static struct list sleep_list; 
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
bool cmp_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED);//추가한 부분
void thread_preemptive(void);//추가한 부분
void remove_with_lock(struct lock *lock);//추가한 부분
void revert_priority(void);//추가한 부분
bool cmp_donate_priority(const struct list_elem *a, const struct list_elem *b,  void *aux UNUSED);//추가한 부분
void donate_priority(void);//추가한 부분
void mlfqs_calculate_priority(struct thread *t);
void mlfqs_calculate_recent_cpu(struct thread* t);
void mlfqs_calculate_load_avg(void);
void mlfqs_increment_recent_cpu(void);
void mlfqs_recalculate_recent_cpu(void);
void mlfqs_recalculate_priority(void);


static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))

//fixed_point.h include. scheduler관련 상수 정의. 변수 선언 및 초기화
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

int load_avg;//전역변수로 선언. 최근 1분동안 사용가능한 프로세스 평균 개수.
//여기까지

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

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
//THREAD_RUNNING 상태로 만듦. 한 번에 하나의 스레드만 running 상태를 가질 수 있음.
void
thread_init (void) { 
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list);//추가한 부분.
	list_init (&all_list);//추가한 부분. in mlfqs
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
// idle thread를 생성하고, 동시에 idle 함수가 실행.
void
thread_start (void) {
	/* Create the idle thread. */ //idle thread(메인 스레드)를 생성.
	//본격적으로 thread를 시작함. 

	struct semaphore idle_started;
	sema_init (&idle_started, 0);//세마포어 초기화. 세마포어는 0으로 초기화되어 있음.
	thread_create ("idle", PRI_MIN, idle, &idle_started);
	load_avg = LOAD_AVG_DEFAULT;//load_avg를 초기화. 최초에는 0으로 초기화.

	/* Start preemptive thread scheduling. */
	intr_enable ();// 인터럽트를 활성화. 인터럽트를 활성화하면 타이머 인터럽트가 발생하고, 타이머 인터럽트 핸들러가 실행됨.

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started); // sema_down: 세마포어를 내리는 함수. 세마포어가 0이 될 때까지 기다림.
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

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
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}
/*추가한 부분*/
void
thread_preemptive(void){
	if (thread_current() == idle_thread)
		return;
	if (list_empty(&ready_list))
		return;
	struct thread *curr = thread_current();
	struct thread *ready = list_entry(list_front(&ready_list), struct thread, elem);
	if (curr->priority < ready->priority) // ready_list에 현재 실행중인 스레드보다 우선순위가 높은 스레드가 있으면
		thread_yield();
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
/**
 * name: 새로운 스레드의 이름. 
 * priority: 우선순위
 * function: 함수명
 * aux: 보조 매개변수
*/
tid_t
thread_create (const char *name, int priority, thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	t = palloc_get_page (0); // kernel 공간을 위한 4KB의 싱글 페이지를 할당. 모든 바이트를 0으로 초기화.
	if (t == NULL)
		return TID_ERROR;

	/* 스레드 초기화 */
	init_thread (t, name, priority); // t(4KB의 단일공간)에 스레드 구조체를 초기화. 스레드 구조체는 128byte 또는 64byte.
	tid = t->tid = allocate_tid (); // 스레드의 고유한 tid를 할당.

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t)kernel_thread; //명령어 포인터 레지스터. 스레드가 실행될 함수의 주소를 가리킴.
	t->tf.R.rdi = (uint64_t)function; // 첫 번째 함수 인자를 전달. 'funtion' 포인터를 설정.
	t->tf.R.rsi = (uint64_t)aux; // 두 번째 함수 인자를 전달. 'aux' 포인터를 설정.
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	//현재 스레드의 자식으로 추가. 가장 최근에 추가시켜줌.
	list_push_back(&(thread_current()->child_list), &(t->child_elem));

	t->fdt = palloc_get_multiple(PAL_ZERO, FDT_PAGES);//fdt 할당
	if(t->fdt == NULL) return TID_ERROR;

	/* Add to run queue. */
	thread_unblock (t);
	thread_preemptive(); //ready_list의 앞 부분과 비교해서 ready_list의 값이 더 클 경우 yield

	return tid;
}
/* 스레드가 다시 실행되어야 할 시간(ticks)를 인자로 받음.*/
void
thread_sleep (int64_t ticks)
{
  struct thread *cur;
  enum intr_level old_level;

  old_level = intr_disable ();// 현재 인터럽트를 비활성화. 이전 인터럽트 레벨을 old_level에 저장.
  cur = thread_current ();// 현재 실행중인 스레드를 가져옴.

  ASSERT (cur != idle_thread);// idle 스레드? 대기상태가 아니기 때문에 sleep되지 않아야 한다.

  cur->wakeup = ticks; // 일어날 시간을 저장
  list_push_back (&sleep_list, &cur->elem);	// sleep_list 에 추가
  thread_block (); // block 상태로 변경. 즉 thread를 대기 상태로 만듦.

  intr_set_level (old_level);//이전 인터럽트 레벨을 복원, 이전 상태로 만듦. 스레드가 대기상태로 들어가기 전의 interrupt 상태를 복원.
}

/* 추가한 함수 */
void
thread_awake (int64_t ticks)
{
  struct list_elem *e = list_begin (&sleep_list);//sleep_list의 첫번째 요소를 가리킴.

  while (e != list_end (&sleep_list)){
    struct thread *t = list_entry (e, struct thread, elem); // 대기중인 스레드를 가리킴
    if (t->wakeup <= ticks){	// 스레드가 일어날 시간이 되었는지 확인
      e = list_remove (e);	// sleep list 에서 제거
      thread_unblock (t);	// 스레드 unblock
    }
    else
      e = list_next (e); // 스레드가 아직 깨워져야 할 시간이 아니라면, 다음 스레드를 검사하기 위해 다음으로 이동.
  }
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
// THREAD_BLOCKED. 멈춰서 아직 실행 불가능한 상태.
// thread_unblock()을 통해 실행가능한 상태가 되면 THREAD_READY가 되어 ready_queue에 들어감.
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) { // parameter로 주어진 t를 차단해제하고, ready 상태로 전환, ready_list에 추가하는 기능을 수행.
	enum intr_level old_level;

	ASSERT (is_thread (t));

	// 현재 인터럽트를 비활성화. 이전의 인터럽트 level을 old_level에 저장. 
	// 이 것은 critical section에 들어가는 것으로 스레드가 상태를 변경하는 동안 다른 interrupt가 발생하지 않도록 보장.
	old_level = intr_disable (); 
	ASSERT (t->status == THREAD_BLOCKED); // 현재 t가 차단된 상태인지 확인.
	//list_push_back (&ready_list, &t->elem);
	//수정한 부분
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, 0);// ready_list에 스레드를 추가. ready_list에 있는 스레드들의 priority값을 비교, 내림차순으로 정렬
	//
	t->status = THREAD_READY; 
	intr_set_level (old_level); // 인터럽트 레벨을 이전 상태로 복원한다. 이전에 비활성화된 인터럽트를 다시 활성화하는 것으로, critical section을 빠져나오는 것을 의미한다.
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread (); //현재 실행 중인 스레드를 가져옴.

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
// THREAD_DYING 상태로 만듦. 스레드를 종료시키는 함수.
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	/**
	* fdt의 모든 파일을 닫고 메모리 반환. 현재 실행 중인 파일도 닫음.
	* 자식이 종료되기를 wait하는 부모에게 sema_up 으로 sig를 보냄.
	* 부모가 wait을 마무리하고 나서 sig를 보내줄 때까지 대기.
	* 해당 대기가 풀리고 나면 scheduling이 이어짐.
	*/
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) { 
	if(!intr_context()){

	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread){
		// list_push_back (&ready_list, &curr->elem); //현재 스레드를 ready_list에 넣음.
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, 0);// ready_list에 스레드를 추가. ready_list에 있는 스레드들의 priority값을 비교, 내림차순으로 정렬
	}
	do_schedule (THREAD_READY); // 스케줄러에게 현재 스레드가 ready_list에 추가되었음을 알림.
	intr_set_level (old_level); // 이전 인터럽트 레벨을 다시 복원하여 이전 상태로 복원함.
	}
}
/*수정한 부분. ready_list에 있는 스레드들의 priority값을 비교, 내림차순으로 정렬*/
bool
cmp_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED){
	return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
}
// thread_current와 ready_list의 맨 앞에 있는 priority와 비교하여, 현재 스레드의 priority가 더 낮다면, yield를 수행
/*여기까지*/
/* Sets the current thread's priority to NEW_PRIORITY. 수정 부분*/
void
thread_set_priority (int new_priority) {
	if(thread_mlfqs){//MLFQS를 사용하는 경우
		return;
	}
	thread_current ()->init_priority = new_priority;//추가한 부분
	// thread_current ()->priority = new_priority;
	revert_priority();
	thread_preemptive(); //ready_list의 앞 부분과 비교해서 ready_list의 값이 더 클 경우 yield
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
	// 현재 스레드의 nice 값을 새 값으로 설정
  enum intr_level old_level = intr_disable ();
  thread_current ()->nice = nice;
  mlfqs_calculate_priority (thread_current ());
  thread_preemptive ();
  intr_set_level (old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level = intr_disable ();
  	int nice = thread_current ()-> nice;
  	intr_set_level (old_level);
  	return nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	// 현재 시스템의 load_avg * 100 값을 반환
  enum intr_level old_level = intr_disable ();
  int load_avg_value = fp_to_int_round (mult_mixed (load_avg, 100));
  intr_set_level (old_level);
  return load_avg_value;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level = intr_disable ();
	int recent_cpu= fp_to_int_round (mult_mixed (thread_current ()->recent_cpu, 100));
	intr_set_level (old_level);
	return recent_cpu;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

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
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
// thread 동작의 전부. thread를 생성하고, thread를 실행하는 함수.
// aux? synchronization을 위한 semaphore 등이 나옴.
// 목적? idle thread(main thread) 위에 여러 thread들이 동시에 실행되도록 만드는 것.
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. thread가 종료될 때까지 실행되는 main 함수 역할*/
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
	//donations를 위해 추가한 부분. init_priority, wait_on_lock, donations 초기화
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&(t->donations));
	//MLFQS를 위한 초기화. 추가한 부분.
	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;
	list_push_back(&all_list, &t->allelem);//mlfqs 추가한 부분. all_list

	t->exit_status = 0;//exit_status 초기화
	t->next_fd = 2;//stdin:0, stdout:1 이기 때문에
	//load_sema, exit_sema, wait_sema 초기화
	sema_init(&t->load_sema, 0);
	sema_init(&t->exit_sema, 0);
	sema_init(&t->wait_sema, 0);
	list_init(&(t->child_list));
	
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) { //항상 ready_list의 앞에 있는 값을 가져옴
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
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
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
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
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread (); //현재 running 중인 스레드
	struct thread *next = next_thread_to_run (); //항상 ready_list의 앞에 값을 가져옴

	ASSERT (intr_get_level () == INTR_OFF); //현재 interrupt는 비활성화 되어있어야 함
	ASSERT (curr->status != THREAD_RUNNING); //현재 실행 중인 스레드는 THREAD_RUNNING 상태가 아니어야 함
	ASSERT (is_thread (next)); //다음에 실행할 스레드가 유효한 스레드여야 함
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
//donation을 위해 추가한 함수.
//추가한 부분. lock을 가지고 있는 스레드가 우선순위를 양도하는 함수.
bool
cmp_donate_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	return list_entry(a, struct thread, donation_elem)->priority > list_entry(b, struct thread, donation_elem)->priority;
}
//특정 lock을 기다리는 모든 스레드들을 현재 donations에서 제거
void
remove_with_lock (struct lock *lock)
{
  struct list_elem *e;
  struct thread *cur = thread_current ();

  for (e = list_begin (&cur->donations); e != list_end (&cur->donations); e = list_next (e)){
    struct thread *t = list_entry (e, struct thread, donation_elem);
    if (t->wait_on_lock == lock) // lock을 기다리는 스레드를 찾음
      list_remove (&t->donation_elem);
  }
}
//추가한 부분. 우선순위 되돌리는 함수. nested 해결.
void
revert_priority(void){ 
	struct thread *cur = thread_current (); //실행중인 스레드를 가져옴
	cur->priority = cur->init_priority; //현재 스레드를 초기 우선순위로 되돌림

	if (!list_empty (&cur->donations)) { //donations가 비어있지 않다면
    	struct thread *front = list_entry (list_front (&cur->donations), struct thread, donation_elem);//가장 앞에 있는 스레드를 가져옴
    	//donations에서 가장 높은 우선순위가 현재 우선순위보다 높을 경우 우선순위를 기부
		if (front->priority > cur->priority) cur->priority = front->priority;
  	}
}
//donate_priority 함수. lock을 기다리는 스레드가 lock을 가지고 있는 스레드에게 우선순위를 기부하는 함수
void
donate_priority(void){
	int depth;
	struct thread *cur = thread_current();

	for(depth = 0; depth < 8; depth++){
		if(!cur->wait_on_lock){ //lock을 기다리는 스레드가 없다면 break
			break;
		}
		struct thread *holder = cur->wait_on_lock->holder;//lock을 가지고 있는 스레드를 가져옴
		holder->priority = cur->priority;
		cur = holder;
	}
}
//priority 계산 함수. MLFQS를 위해 추가한 함수.
void
mlfqs_calculate_priority (struct thread *t)
{
  if (t == idle_thread)return;//idle thread의 priority는 고정이기 때문에 제외.
  t->priority = fp_to_int (add_mixed (div_mixed (t->recent_cpu, -4), PRI_MAX - t->nice * 2));
}

//recent_cpu 계산 함수. MLFQS를 위해 추가한 함수.
void
mlfqs_calculate_recent_cpu (struct thread *t)
{
  if (t == idle_thread) return;
  t->recent_cpu = add_mixed (mult_fp (div_fp (mult_mixed (load_avg, 2), add_mixed (mult_mixed (load_avg, 2), 1)), t->recent_cpu), t->nice);
}

//load_avg 계산 함수. MLFQS를 위해 추가한 함수.
/* threads/thread.c */
void
mlfqs_calculate_load_avg (void)
{
  int ready_threads;

  if (thread_current () == idle_thread){
    ready_threads = list_size (&ready_list);
  }
  else{
    ready_threads = list_size (&ready_list) + 1;
  }

  load_avg = add_fp (mult_fp (div_fp (int_to_fp (59), int_to_fp (60)), load_avg),
                     mult_mixed (div_fp (int_to_fp (1), int_to_fp (60)), ready_threads));
}

//1tick 마다 running 스레드의 recent_cpu + 1.
//4ticks마다 thread의 priority 재계산
void
mlfqs_increment_recent_cpu (void)
{
  if (thread_current () != idle_thread)
    thread_current ()->recent_cpu = add_mixed (thread_current ()->recent_cpu, 1);
}

//4ticks 마다 모든 thread의 priority 재계산
void
mlfqs_recalculate_recent_cpu (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    mlfqs_calculate_recent_cpu (t);
  }
}

//1초마다 모든 스레드의 recent_cpu와 load_avg 재계산
void
mlfqs_recalculate_priority (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    mlfqs_calculate_priority (t);
  }
}
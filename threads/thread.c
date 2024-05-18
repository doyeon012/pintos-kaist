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

// ready, sleep list 선언
static struct list ready_list; 
static struct list sleep_list;



// CPU가 유휴 상태일 때 실행되는 스레드
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
// 시스템 초기화시 실행되는 첫 번째 스레드
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
// T
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

bool cmp_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED);
bool donation_cmp_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED);
void thread_preemption(void);

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
// 스레드 초기화 및 시작
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
	// 글로벌 스레드 컨택스트 초기화
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list); // 추가한 부분
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	// 초기 스레드 설정
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
// idle thread를 생성하고, 동시에 idle 함수가 실행.
// idle thread는 한 번 schedule을 받고, sema_up을 하여 thread_start()의 마지막 sema_down을 풀어주어
// thread_start가 작업을 끝나고 run_action()이 실행될 수 있도록 해주고, idle 자신은 block이 된다.
// idle thread는 pinots에서 실행 가능한 thread가 하나도 없을 때 wake되어 다시 작동하는데, 이는 무조건 하나의 thread는 실행하고 있는 상태를
// 만들기 위함이다.
// 스레드 스케줄링을 시작하고 유휴 스레드를 생성
void
thread_start (void) {
	/* Create the idle thread. */ //idle thread(메인 스레드)를 생성.
	//본격적으로 thread를 시작함. 
	struct semaphore idle_started;
	sema_init (&idle_started, 0);//세마포어 초기화. 세마포어는 0으로 초기화되어 있음.
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();// 인터럽트를 활성화. 인터럽트를 활성화 > 타이머 인터럽트가 발생, 타이머 인터럽트 핸들러가 실행됨.

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

void
thread_preemption(void)
{
	if(!list_empty(&ready_list) && (thread_current ()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority))
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

// 새로운 커널 스레드 생성
// 생성시 초기 우선 순위 설정
// 새로운 커널 스레드를 생성하고 초기화하여 wait_list큐에 추가
// 스레드는 'kernel_thread'함수로 시작되며, 이 함수는 'function'과 'aux'를 인수로 받아 실행.
// 스레드가 실행될 때 준비가 되면 실행 대기 큐에 추가되어 스케줄러가 이를 실행할 수있게 된다.
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	// 2. 스레드 메모리 할당
	t = palloc_get_page (PAL_ZERO); // 페이지 할당
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	// 3. 스레드 초기화
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	// 4. 커널 스레드 설정
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */ 

	// 5. 스레드를 ready_list 큐에 추가
	thread_unblock (t);

	thread_preemption();

	return tid;
}

// 스레드들의 priority 값 비교, 내림차순 정렬
bool
cmp_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED){
	return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
}

bool
donation_cmp_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED){
	return list_entry(a, struct thread, d_elem)->priority > list_entry(b, struct thread, d_elem)->priority;
}

// 알람 때 구현한 것 1. 정렬, 2. 재우기, 3. 깨우기
// 슬립 큐에 스레드 추가시 'wakeup_ticks'값(각 스레드가 깨어야 할 시간.)에 따라 올바른 순서로 정렬
// 각 스레드가 깨어야 할 시간 비교, 내림차순
bool cmp_thread_ticks(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
    {	
		// 'list_elen'포인터 and 'struct thread' 구조체 내에서 'list_elem'의 오프셋을 사용하여 'struct thread'포인터 계산
        struct thread *st_a = list_entry(a, struct thread, elem);
        struct thread *st_b = list_entry(b, struct thread, elem);
		return st_a->wakeup_ticks < st_b->wakeup_ticks; // a가 b보다 더 일찍 깨어야 함
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
        list_insert_ordered(&sleep_list, &curr->elem, cmp_thread_ticks, NULL); // sleep_list에 추가
        
		// 6. 스레드 블록
		thread_block(); // 

		// 7. 인터럽트 복원
        intr_set_level(old_level); 
    }

// sleep_list에 있는 스레드들이 깨어날 시간이 되었는지 확인, 해당 시간이 되었을 때
// 스레드들을 'ready_list'로 이동시키는 역할.
void 
thread_wakeup(int64_t current_ticks)
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
// 차단된 스레드를 READY 상태로 전환하고 ready 큐에 추가
void
thread_unblock (struct thread *t) { // parameter로 주어진 t를 차단해제하고, ready 상태로 전환, ready_list에 추가하는 기능을 수행.
	enum intr_level old_level;

	ASSERT (is_thread (t));

	// 현재 인터럽트를 비활성화. 이전의 인터럽트 level을 old_level에 저장. 
	// 이 것은 critical section에 들어가는 것으로 스레드가 상태를 변경하는 동안 다른 interrupt가 발생하지 않도록 보장.
	old_level = intr_disable (); 
	ASSERT (t->status == THREAD_BLOCKED); // 현재 t가 차단된 상태인지 확인.

	// list_push_back (&ready_list, &t->elem);
	// 스레드가 차단 해제되면 ready_list에 우선 순위에 따라 ready_list 삽입
	list_insert_ordered(& ready_list, & t-> elem, cmp_priority, NULL);    

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
// 현재 실행중인 스레드가 CPU를 자발적으로 양보하고, 다른 스레드에 실행 기회를 주기 위해 사용된다.
void
thread_yield (void) { 
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable (); // 인터럽트 비활성화
	if (curr != idle_thread)
	{
		// list_push_back (&ready_list, &curr->elem); //현재 스레드를 ready_list에 넣음.
		list_insert_ordered(&ready_list, & curr->elem, cmp_priority, NULL);
	}

	do_schedule (THREAD_READY); // 스케줄러에게 현재 스레드가 ready_list에 추가되었음을 알림.
	intr_set_level (old_level); // 인터럽트 복원
}


/* Sets the current thread's priority to NEW_PRIORITY. */
// 현재 스레드의 우선순위를 new_priority로 변경
void
thread_set_priority (int new_priority) {

	// 1. 현재 실행 중인 스레드 가져오기
	struct thread *curr = thread_current();

	// 원래 우선 순위 설정
	curr->original_priority = new_priority;
	// update_priority_for_donations();

	 // 3. 조건 확인 및 우선순위 설정
	 if(list_empty(&curr->donations) || new_priority > curr->priority) // 새로운 우선순위가 현재 우선순위보다 높은지
	 {
	 	// 4. 우선순위 업데이트
	 	curr->priority = new_priority;
	 }
	 else
	 {
	 	curr->priority = list_entry(list_front(&curr->donations), struct thread,d_elem)->priority;
	 }

	thread_preemption(); //리스트 앞 부분과 비교해서 read_list의 값이 더 클 경우 yield
}

/* Returns the current thread's priority. */
// 현재 스레드의 우선 순위 반환
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */

// 유휴 스레드 - 시스템에 실행 가능한 다른 스레드가 없을 때 실행됨
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
// 스레드 초기화 함수
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);

	t->priority = priority; // 우선순위 부여
	t->magic = THREAD_MAGIC;

	t->original_priority = priority; // 원래 우선 순위로 되돌리기
	t->wait_on_lock = NULL; // 락을 소유하고 있는 스레드 초기화
	list_init(&t->donations); // 기부 받은 우선순위 리스트 초기화
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
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

// 현재 스레드를 스케쥴링 하고 다음 실행할 스레드 선택
static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
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

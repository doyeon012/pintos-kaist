#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */
// TIMER_FREQ: timer interrupt 주파수.

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) { // then 이후 경과된 시간(ticks)을 반환
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
//thread_yield: 현재 스레드를 ready_list에 넣고 다음 스레드를 실행하는 역할을 함.
//thread_yield() 호출되면, 해당 함수를 호출한 스레드는 다른 스레드에게 CPU를 양보함.
//timer_sleep: 현재 스레드를 ticks 만큼 재우는 역할을 함.
//양보한 thread가 다시 실행될 때는 thread_yield()를 호출한 thread가 다시 실행됨. 아직 일어날 때가 안됐다면 바로 양보하고를 계속 반복. -> 양보해준 후 ready_list로 감.
//일어날 때가 됐는지 계속해서 확인해줌.
//sleep -> awake -> 시간 확인 -> 다시 sleep -> awake -> 시간 확인 -> awake -> 시간 확인(일어날 시간) -> 깨어남
//해결 방법? sleep 상태의 thread를 block state로 두어 깨어날 시간이 되기 전까지는 스케줄링에 포함되지 않게 하고, 꺠어날 시간이 되었을 떄 ready state로 바꿔줌.
//ticks? pintos 내부에서 시간을 나타내기 위한 값. 부팅 이후에 일정 시간마다 1씩 증가.
//1 tick? 1ms
// void
// timer_sleep (int64_t ticks) { 
// 	int64_t start = timer_ticks ();

// 	ASSERT (intr_get_level () == INTR_ON);
// 	while (timer_elapsed (start) < ticks) //start로부터 지난 tick이 ticks보다 작을 때까지 계속 sleep. tick이 ticks보다 작으면 thread_yield를 호출.
// 		thread_yield ();
// 		//ready_list에서 자신의 차례가 된 스레드는 start 이후 경과된 시간이 ticks보다 커질 때까지 thread_yield()를 호출
// 		//ready_list의 맨 뒤로 이동하기를 반복
// }
/* 수정한 함수*/
void
timer_sleep(int64_t ticks)
{
	int64_t start = timer_ticks();
	thread_sleep(start + ticks);
}


/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
// static void
// timer_interrupt (struct intr_frame *args UNUSED) {
// 	ticks++;
// 	thread_tick ();
// }
/* devices/timer.c */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++; // global ticks를 증가. system 부팅 후 전체적인 시간.
  thread_tick (); // 스레드의 우선순위를 관리, 다음 실행할 스레드를 선택하는 역할.
  thread_awake (ticks);	//
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}

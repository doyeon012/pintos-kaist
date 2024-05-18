/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */

bool cmp_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void remove_donor(struct lock *lock);

// 세마포어를 주어진 값으로 초기화
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */


// 세마포어 Request 세마포어를 흭득한 값을 1만큼 decrease
// 값이 0이하이면 현재 스레드를 wait 리스트에 추가하고 블록
void
sema_down (struct semaphore *sema) {
enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable (); // 비활성화

	while (sema->value == 0) {

		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters, &thread_current()->elem , cmp_priority, NULL);
		// 우선 순위에 따라 waiters list에 스레득를 삽입하도록 수정

		thread_block (); // 현재 스레드를 블록 상태로 전환
	}
	sema->value--;
	intr_set_level (old_level); // 활성화
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
// 세마포어의 "P"연산을 시도
// 세마포어 값이 0보다 크면 값을 감소시키고 true를 반환한다.
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
// 세마포어의 "V" 연산 수행
// 세마포어의 값을 증가시키고, 대기 중인 스레드가 있으면 그 스레드를 깨운다.
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	
	list_sort(&sema->waiters, cmp_priority, NULL);
	if (!list_empty (&sema->waiters))
	{
		list_sort(&sema->waiters, cmp_priority, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
	}
	sema->value++;
	thread_preemption();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);
 
/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
// 자체 테스트
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}
/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */

// void remove_donor(struct lock *lock)
// {
//     struct list *donations = &(thread_current()->donations); // 현재 스레드의 donations
//     struct list_elem *donor_elem;    // 현재 스레드의 donations의 요소
//     struct thread *donor_thread;

//     if (list_empty(donations))
//         return;

//     donor_elem = list_front(donations);

//     while (1)
//     {
//         donor_thread = list_entry(donor_elem, struct thread, d_elem);
//         if (donor_thread->wait_on_lock == lock)           // 현재 release될 lock을 기다리던 스레드라면
//             list_remove(&donor_thread->d_elem); // 목록에서 제거
//         donor_elem = list_next(donor_elem);
//         if (donor_elem == list_end(donations))
//             return;
//     }
// }


// 락을 초기화
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);
	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

// 락을 흭득한다.
// 잠금을 사용할 수 없는 경우 잠금 주소를 저장.
// 현재 우선순위를 저장하고 기부한 스레드를 목록에 유지(복수 기부)
void
lock_acquire (struct lock *lock) {
	// 입력 조건 확인
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock)); // 현재 스레드가 이미 

	// 현재 스레드 가져오기
	struct thread *current_thread = thread_current();
	

	// 락 소유자가 있는 경우 우선순위 기부 처리 - nested donataion 처리.
	if(lock->holder != 	NULL) // NULL이 아니다? 그러면 다른 스레드가 소유하고 있다는 뜻이다.
	{
		if(lock->holder->priority < current_thread->priority)
		{	
			current_thread->wait_on_lock = lock; // 현재 스레드는 'wait_on_lock' 필드에 이 락을 저장
			
			// 락 소유자의 'donations' 리스트에 현재 스레드를 우선순위에 따라 삽입
			list_insert_ordered(&lock->holder->donations, &current_thread->d_elem, donation_cmp_priority, NULL);

			// 우선순위 기부 처리(nested donataion)
			while (current_thread->wait_on_lock != NULL) {
			
			// 락 소유자 가져오기
			struct thread *holder = current_thread->wait_on_lock->holder;

			// 우선순위 비교 및 업데이트
			if (holder->priority < current_thread->priority)
			{
			// 락 소유자의 우선순위를 > 현재 스레드의 우선순위에 기부
			holder->priority = current_thread->priority;
			//현재스레드 = 락 소유자로 변경
			current_thread = holder;
			} 
			else
			{
				break; 
			}
	}
		}
	}

	// 락 흭득 시도(이 과정에서 세마포어 값이 0이면 현재 스레드는 블록)
	sema_down (&lock->semaphore);

	// 락 소유자 설정(락의 소유자를 현재 스레드로 설정)
	lock->holder = thread_current (); 

	// 대기 상태 해제(락을 소유 했으므로)
	current_thread->wait_on_lock = NULL;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
// 락 흭득 시도를 한다.
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */

// 락을 해제한다.
// 잠금이 해제되면 기부 목록에서 잠금이 설정된 스레드를 삭제하고 우선 순위를 올바르게 설정
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	// remove_donor(lock);

	struct list_elem *e;
	struct thread *current_thread_donation = &thread_current()->donations;

	// 현재 스레드가 소유한 'donations'리스트를 순회하며, 'wait_on_lockl'이 해제하려는 'lock'인 스레드를 찾기 = 기부 목록에서 기부된 스레드 제거
	for(e = list_begin(current_thread_donation); e != list_end(current_thread_donation); e = list_next(e))
	{	
		// 기부 목록에서 스레드 추출
		struct thread *t = list_entry(e, struct thread, d_elem);

		// 특정 락을 기다리는지 확인
		if (t->wait_on_lock == lock)
		{
			list_remove(&t->d_elem);
		}
	}

	//우선 순위 복원(왜? 단순히 원래 우선순위로 간다면, 남아있는 다른 락이 뺐긴다)
	thread_current()->priority = thread_current()->original_priority;

	// 기부받은 우선순위 확인 및 조정(multiple donation 처리)
	if(!list_empty(current_thread_donation)) //현재 스레드가 기부받은 우선순위가 있는지 확인
	{	
		// 최대 기부 우선순위 찾기.
		struct thread *max_donor = list_entry(list_front(current_thread_donation), struct thread, d_elem);
		
		// 우선순위 조정
		if(max_donor->priority > thread_current()->priority) // 기부받은 스레드의 우선순위가 현재 스레드의 우선순위보다 높은경우
		{
			thread_current()->priority = max_donor->priority; // 현재 스레드의 우선순위를 기부받은 우선 순위로 설정
		}
	}

	
	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
// 현재 스레드가 락을 소유하고 있는지 확인
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

// 조건변수 : 스레드 간의 동기화를 위한 주요 기법. 특정 조건이 충족될 때까지 스레드가 대기할 수 있도록 하며, 다른 스레드가 
// 그 조건이 중촉되었음을 알리면 대기 중인 스레드가 깨어나서 작업을 재개할 수 있게 한다.

// 대기(wait) - 스레드는 조건 변수를 사용하여 특정 조건이 충족될 때까지 대기. 이때 스레드는 대기 목록에 추가되고, 락을 해제한 후 대기 상태로 들어감
// 신호(signal) - 다른 스레드가 조건 변수를 사용하여 대기 중인 스레드에게 특정 조건이 충족되었음을 알린다. 이 신호를 받은 스레드는 깨어나서 다시 락을 획득한 후 작업 재개
// 브로드캐스트(broadcast) - 모든 대기 중인 스레드에게 조건이 충족되었음을 알린다.

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
// 조건 변수 초기화
// 대기 리스트 초기화
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */      
// 조건 변수를 기다린다.
// 현재 락을 해제하고, 조건 변수를 대기한다. 
// 조건 변수가 신호를 받으면 다시 락을 흭득한다.
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	
	// 조건 변수 cond에 있는 wait_list를 정렬해놓고 우선 순위에 따라서 넣을 수 있도록 한다.
	list_insert_ordered(& cond->waiters, & waiter.elem, cmp_sema_priority, NULL);

	// 락 해제
	lock_release (lock);

	// 세마포어 대기
	sema_down (&waiter.semaphore);

	// 락 재획득
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
// 조건 변수의 대기 목록에서 우선순위가 가장 높은 스레드를 깨우는 역할을 한다.
// 조건 변수의 대기 목록을 우선순위에 따라 정렬하고, 가장 높은 우선순위를 가진 스레드를 깨운다.
// 이를 통해 우선 순위가 높은 스레드가 먼저 실행될 수 있도록 보장.
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	// 대기 목록 확인 및 정렬
	if (!list_empty (&cond->waiters)){
		list_sort(&cond->waiters, cmp_sema_priority, NULL);
		
		// 대기 목록에서 스레드 깨우기
		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);

	}
		
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
// 조건 변수를 브로드캐스트한다.
// 재기 중인 모든 스레드를 깨운다
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}


// 두 세마포어 목록 요소의 우선순위를 비교하여 정렬을 한다.
// 각 세마포어의 대기 목록에서 가장 높은 우선순위를 가진 스레드를 추출한 후, 그 우선순위를 비교하여 높은 순서대로 정렬할 수 있도록
bool
cmp_sema_priority(const struct list_elem *a, const struct list_elem *b,  void *aux UNUSED)
{
	// 세마포어 요소 추출
	struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);

	// 기다리는 스레드 목록 추출
	struct list *waiters_a = &(sema_a->semaphore.waiters);
	struct list *waiters_b = &(sema_b->semaphore.waiters);

	// 가장 높은 우선순위 스레드 추출
	struct thread *s_a = list_entry(list_begin(waiters_a), struct thread, elem);// 스레드 목록의 첫 번째 요소 
	struct thread *s_b = list_entry(list_begin(waiters_b), struct thread, elem);// 각 요소를 'thread' 구조체 포인터로 반환


	// 우선순위 비교
	return s_a->priority > s_b->priority;
}


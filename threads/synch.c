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

/* add function- gdy2*/
bool new_cmp_priority(struct list_elem *a_, struct list_elem *b_,
					  void *aux UNUSED);

/* add function- gdy2*/

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
/* 세마포어를 주어진 값으로 초기화한다. */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */

/* 세마포어를 요청한다. 세마포어를 획득하면 값을 1 감소 시킨다. */
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */

// 세마포어의 value 값이 0보다 클 경우 true 반환
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
/* 세마포어를 해제하고 값을 1 증가시킨다.*/
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters)) // sema->waiter 리스트가 비어있지 않을 경우
	{
		list_sort(&sema->waiters, cmp_priority, NULL);
		thread_unblock(list_entry(list_pop_front(&sema->waiters),
								  struct thread, elem));
	}
	sema->value++;
	thread_preemtive(); // wait_list에서 ready_list로 넣어준 thread의 우선순위가 현재 실행중인 thread의 우선순위 보다 높을 수 있으므로 context_switching이 일어나는지 확인한다.
	intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
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
/* lock 데이터 구조 초기화 */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* lock 요청 */
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	if (lock->holder != NULL)
	{											// 해당 lock을 다른 쓰레드가 점유하고 있을 경우
		struct thread *curr = thread_current(); // donations 목록에 들어가야 하는 쓰레드 = curr
		curr->wait_on_lock = lock;
		list_insert_ordered(&lock->holder->donations, &curr->d_elem, cmp_priority_donation, NULL); // 현재 주어진 lock을 기다리는 donation목록에 curr->d_lem 추가(오름차순으로 정렬 됨)
		// 우선순위 기부
		if (!thread_mlfqs)
		{									   // mlfq일 경우 우선순위 기부 적용 x
			struct thread *donation_first;	   // lock을 가지고 있는 쓰레드의 도네이션 목록에 =>>가장 우선순위가 높은 쓰레드
			while (curr->wait_on_lock != NULL) // nested 상황을 고려하여 while문을 사용 wait_on_lock을 가지고 있지 않은 쓰레드가 나올 때 까지 반복
			{
				struct lock *new_lock = curr->wait_on_lock; // 새로운 lock 구조체를 선언하여 lock을 갱신 해준다.		// 새로 선언을 해주지 않고 그냥 lock을 써서 갱신이 되지 않았었다.
				donation_first = list_entry(list_begin(&new_lock->holder->donations), struct thread, d_elem);
				if (new_lock->holder->priority < donation_first->priority)
				{
					new_lock->holder->priority = donation_first->priority; // donation의 첫번째 값과 우선순위를 비교하여 조정
				}
				curr = curr->wait_on_lock->holder; // 현재 쓰레드의 wait_on_lock의 holder가 가리키는 쓰레드로 이동 하여 연결된 쓰레드들의 priority값 조정
			}
		}
	}
	sema_down(&lock->semaphore);
	lock->holder = thread_current();
	thread_current()->wait_on_lock = NULL;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
/* lock 해제 */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock)); // 현재 쓰레드가 lock을 보유하고 있는지 확인

	struct thread *curr = lock->holder;								// 해제할 lock을 가지고 있는 쓰레드 curr
	struct list_elem *donation_elem = list_begin(&curr->donations); // 해제할 lock을 기다리고 있는 쓰레드들의 우선순위가 가장 높은 list_elem => donation_elem

	while (!list_empty(&curr->donations) && donation_elem != list_tail(&curr->donations)) // donation이 empty가 아니고, donation_elem이 tail이 아닐 때까지
	{																					  // lock을 가지고 있던 쓰레드의 donations 목록에 wait_on_lock으로 lock을 가지고 있던 쓰레도 모두 삭제
		struct thread *donation_thread = list_entry(donation_elem, struct thread, d_elem);
		if (donation_thread->wait_on_lock == lock)
		{
			donation_elem = list_remove(donation_elem); // 해당 쓰레드 삭제하고 remove로 반환된 elem값을 donation_elem으로 받아서 donation_elem 갱신
		}
		else
		{
			donation_elem = donation_elem->next; // wait_on_lock 값이 lock이 아닌 쓰레드라면 다음 쓰레드로 donation_elem 갱신
		}
	}
	// 우선 순위 기부
	// do - while 문을 사용하여 갱신이 안되는 경우가 없도록 하였다.
	if (!thread_mlfqs)
	{ // mlfq일 경우 우선순위 기부 적용 x
		do
		{
			curr->priority = curr->org_priority; // 기부 받았던 priority를 반납
			if (!list_empty(&curr->donations))	 // donations가 비어있지 않을 경우 갱신된 donations 리스트에서 첫번째 값으로 우선순위 갱신
			{
				struct thread *donation_first = list_entry(list_begin(&curr->donations), struct thread, d_elem);
				if (curr->priority < donation_first->priority)
				{
					curr->priority = donation_first->priority;
				}
			}
			// donations가 비어있을 경우
			// while문 들어가기전이므로 wait_on_lock이 있는지 확인한다.
			// 이 조건이 없으면 wait_on_lock이 null일 경우 holder를 찾지 못해 error가 나타난다.
			if (curr->wait_on_lock == NULL)
			{
				break;
			}
			curr = curr->wait_on_lock->holder; // wait_on_lock이 null이 아닐경우 holder 쓰레드로 이동한다.
		} while (curr->wait_on_lock != NULL);
	}
	//
	lock->holder = NULL; // lock 해제
	sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */

/* condition variable 데이터 구조 초기화 */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
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

/* 조건 변수의 신호를 기다린다. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock)); // 현재 쓰레드가 lock을 보유하면 true 반환

	sema_init(&waiter.semaphore, 0);
	// list_push_back (&cond->waiters, &waiter.elem);
	list_insert_ordered(&cond->waiters, &waiter.elem, new_cmp_priority, NULL);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* 조건 변수에 대기 중인 가장 높은 우선순위의 스레드에게 신호를 보낸다. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		list_sort(&cond->waiters, new_cmp_priority, NULL);
		sema_up(&list_entry(list_pop_front(&cond->waiters),
							struct semaphore_elem, elem)
					 ->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* 조건 변수에 대기 중인 모든 스레드에게 신호를 보낸다. 	*/
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}

/* add function - gdy2 */
bool new_cmp_priority(struct list_elem *a_, struct list_elem *b_,
					  void *aux UNUSED)
{
	struct semaphore_elem *a = list_entry(a_, struct semaphore_elem, elem);
	struct semaphore_elem *b = list_entry(b_, struct semaphore_elem, elem);
	struct semaphore *new_a = &a->semaphore;
	struct semaphore *new_b = &b->semaphore;

	struct list *sema_waiter_a = &new_a->waiters;
	struct list *sema_waiter_b = &new_b->waiters;
	struct list_elem *new_list_a = sema_waiter_a->head.next;
	struct list_elem *new_list_b = sema_waiter_b->head.next;

	struct thread *t_a = list_entry(new_list_a, struct thread, elem);
	struct thread *t_b = list_entry(new_list_b, struct thread, elem);

	return t_a->priority > t_b->priority;
}
/* add function - gdy2 */
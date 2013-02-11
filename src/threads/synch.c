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
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);
  
  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());
  old_level = intr_disable ();

  while (sema->value == 0) 
    {
      list_push_back(&sema->waiters, &thread_current ()->elem);
      thread_block ();
    }  
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
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

   This function may be called from an interrupt handler. 
	 Returns a pointer to the thread that wakes up.*/
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;
 
  ASSERT (sema != NULL);
  
  old_level = intr_disable ();
  
  /* Wakes up the thread with the highest priority in the waiters list */
  struct thread *next = NULL;
  if (!list_empty (&sema->waiters)) 
    {
      struct list_elem *e;
      e = list_min (&sema->waiters, thread_higher_priority, NULL);
			list_remove(e);
      next = list_entry (e, struct thread, elem);
      thread_unblock (next);
    }
    
  sema->value++;
  intr_set_level (old_level);
   /* If the running thread no longer has the highest priority, 
     it yields. */
  thread_try_yield (next);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
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
sema_test_helper (void *sema_) 
{
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
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Recursively updates a thread's donee on its priority. */
void
update_priority_donation (struct thread *t)
{
  ASSERT (!thread_mlfqs);
  
  struct thread *donee;
  donee = t->donee;
  
  if (donee != NULL) 
    {
			if (t->priority > donee->priority)
				donee->priority = t->priority;
			else
				donee->priority = get_highest_possible_priority (donee);
			
			update_priority_donation (donee);  
    }
}

/* Get the highest priority from the threads waiting for all 
	 the locks that thread t is holding. If t's base_priority
	 is higher, return the base_priority. */
int
get_highest_possible_priority (struct thread *t)
{
  ASSERT (!thread_mlfqs);
  
	int result = t->base_priority;
	if (!list_empty (&t->locks))
		{	
			struct list_elem *e;
			for (e = list_begin (&t->locks); e != list_end (&t->locks);
					e = list_next (e))
				{
					struct list *lock_waiters
						= & (&list_entry (e, struct lock, elem)->semaphore)->waiters;
      		
      		if (!list_empty (lock_waiters))
      		  {
          		struct list_elem *thread_elem
				        = list_min (lock_waiters, thread_higher_priority, NULL);
          		int highest_waiter_priority
				        = list_entry (thread_elem, struct thread, elem)->priority;

			        if (highest_waiter_priority > result)
				        result = highest_waiter_priority;
					  }
				}
		}
	return result;
}

 /* Registers the the holder as the donee of all the waiters. */
void 
lock_set_waiters_donee (struct lock *lock)
{
  ASSERT (!thread_mlfqs);
  
	struct list *waiters = & (&lock->semaphore)->waiters;
	if (!list_empty(waiters))
		{
			struct list_elem *e;
			for (e = list_begin (waiters); e != list_end (waiters);
					e = list_next (e))
				list_entry (e, struct thread, elem)->donee = lock->holder;
		}
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  if (!thread_mlfqs) 
    {
      struct thread *holder;
      holder = lock->holder;

      if (holder != NULL)
        {
					/* Registers the current thread as a potential donor of the holder. */
					thread_current ()->donee = holder;
					
					if (holder->priority < thread_get_priority ())
						{
							/* The current thread donates its priority to the holder. */
							holder->priority = thread_get_priority ();   
							/* Recursively updates the holder's donee of the new priority. */
							update_priority_donation (holder);
						}
        }
    }   
  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
	
	/* Adds the lock to the currrent thread's acquired lock list. */
	if (!thread_mlfqs)
	  list_push_back (&thread_current ()->locks, &lock->elem);
} 
      

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  lock->holder = NULL;

	if (!thread_mlfqs) 
		{		
		  /* Removes the lock from the current thread's acquired lock list. */
		  list_remove(&lock->elem);
		  
			/* Unregisters the current thread as the waiters' donee. */
			lock_set_waiters_donee (lock);
			
			thread_set_apparent_priority (
			  get_highest_possible_priority (thread_current ()));
		}

  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
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
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);  
	lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* list_less_func() for list_insert_ordered() in cond_signal().
   A semaphore with a higher prority thread comes first. */
bool 
cond_higher_priority (const struct list_elem *elem1, 
	const struct list_elem *elem2,
	void *aux UNUSED)
{
	ASSERT (elem1 != NULL);
	ASSERT (elem2 != NULL);
	/* Gets the two semaphores in the cond->waiters list. */
  struct semaphore *sema1  
		= (&list_entry (elem1, struct semaphore_elem, elem)
		  ->semaphore);
	struct semaphore *sema2 
		= (&list_entry (elem2, struct semaphore_elem, elem)
		  ->semaphore);

	/* Compares the threads' proirities in the two semaphore's
	   waiting list. */
	struct thread *t1
		= list_entry (list_front (&sema1->waiters), struct thread, elem);
	struct thread *t2
		= list_entry (list_front (&sema2->waiters), struct thread, elem);

	return t1->priority < t2->priority;
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  if (!list_empty (&cond->waiters)) 
		{
			struct list_elem *e;
      e = list_max (&cond->waiters, cond_higher_priority, NULL);
			list_remove(e);
			sema_up (&list_entry (e, struct semaphore_elem, elem)
								->semaphore);
		}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

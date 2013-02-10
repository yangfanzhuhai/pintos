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
struct thread *
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;
 
  ASSERT (sema != NULL);
  
  old_level = intr_disable ();
  
  /* Wake up the thread with the highest priority in the waiters list */
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
  
  /* If the running thread no longer has the highest priority, 
     it yields. */
  thread_try_yield (next);
    
  intr_set_level (old_level);
	return next;
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

/* Initialises the donor_elem. */
void
donor_init (struct donor_elem *donor_e,
           struct thread *d, struct lock *l)
{
  ASSERT (!thread_mlfqs);
  
  donor_e->donor = d;
  donor_e->lock  = l;
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

/* Deletes the holder's existing donor waiting for lock. */
void
lock_holder_delete_donors (struct thread *holder, 
                        struct lock *lock)
{
  ASSERT (!thread_mlfqs);
  
  struct list_elem *e;
  struct donor_elem *donor_e;
  
	for (e = list_begin (&holder->donors); e != list_end (&holder->donors);
	    	e = list_next (e)) 
		{
		  donor_e = list_entry (e, struct donor_elem, elem);
				  
		  if (donor_e->lock == lock) 
		    {
		      list_remove (&donor_e->elem);
		      /* Set the donor's donee to NULL. */
		      donor_e->donor->donee = NULL;
		    }
		}
}

/* Deletes the donor_thread from the donee_thread's donor list. 
   Returns true if successful. */
bool
thread_delete_donor (struct thread *donor_thread, 
          struct thread *donee_thread,
          struct lock *lock)
{
  ASSERT (!thread_mlfqs);
  
  struct list_elem *e;
  struct donor_elem *donor_e = NULL;
  
	for (e = list_begin (&donee_thread->donors); 
	      e != list_end (&donee_thread->donors);
	    	e = list_next (e)) 
		{
		  donor_e = list_entry (e, struct donor_elem, elem);
				  
		  if (donor_e->lock == lock && 
		      donor_e->donor == donor_thread)       
		      break;
		}
	if (donor_e != NULL)
	  list_remove (&donor_e->elem);
	  
  return donor_e != NULL;
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
      if (donee->priority < t->priority) 
        {
          donee->priority = t->priority;
          if (donee->donee != NULL)
            update_priority_donation (donee);
        }
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

      /* When lock is held by a thread with lower priority */
      if (holder != NULL && holder->priority < thread_get_priority ())
        {
		    	/* The current thread donates its priority to the holder. */
	    		holder->priority = thread_get_priority ();   
	    		
	    		struct donor_elem donor;
          
          donor_init (&donor, thread_current (), lock);
      
		    	/* Deletes the existing donor that is waiting for the same lock*/
		    	lock_holder_delete_donors (holder, lock);
			
		    	/* Inserts the current thread to the holder's donor list. */
		    	list_push_front (&holder->donors, & (&donor)->elem);
			
			    /* Records holder as the current thread's donee. */
		    	thread_current ()->donee = holder;
			
		    	/* Recursively updates the holder's donee of the new priority. */
		    	update_priority_donation (holder);
        }
    }
    
  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
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

	struct thread *woke;
  woke = sema_up (&lock->semaphore);
	
	if (!thread_mlfqs && woke != NULL) 
		{
		  struct list *donor_list;
			donor_list = &thread_current ()->donors;
			
			/* If the woke up thread is a donor, delete it from 
			  the current thread's donor list*/
			bool woke_donor	=	thread_delete_donor (woke, thread_current (), lock);
			
			if (woke_donor)
        {
          /* Set the woke up donor's donee to NULL. */
          woke->donee = NULL;
          
					if (!list_empty (donor_list))
					  {
					    struct thread *next_donor 
					      = list_entry (list_begin (donor_list),
                      struct donor_elem, elem) -> donor;
						  thread_set_apparent_priority (next_donor->priority);
            }
					else
						thread_set_apparent_priority (thread_current ()->base_priority);
				}		
		}
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
	// get the two semaphores in the cond->waiters list 
  struct semaphore *sema1  
		= (&list_entry (elem1, struct semaphore_elem, elem)
		  ->semaphore);
	struct semaphore *sema2 
		= (&list_entry (elem2, struct semaphore_elem, elem)
		  ->semaphore);

	// compare the threads' proirities in the two semaphore's
	// waiting list
	struct thread *t1
		= list_entry (list_front (&sema1->waiters), struct thread, elem);
	struct thread *t2
		= list_entry (list_front (&sema2->waiters), struct thread, elem);

	return t1->priority > t2->priority;
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
      e = list_min (&cond->waiters, cond_higher_priority, NULL);
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

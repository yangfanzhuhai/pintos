#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore 
  {
    unsigned value;             /* Current value. */
    struct list waiters;        /* List of waiting threads. */
  };

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
struct thread * sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock 
  {
    struct thread *holder;      /* Thread holding lock (for debugging). */
    struct semaphore semaphore; /* Binary semaphore controlling access. */
  };

/* Priority donation. */

/* One donor in a list. */
struct donor_elem 
  {
    struct list_elem elem;          /* List element. */
    struct thread *donor;           /* This donor thread. */
    struct lock *lock;              /* Lock that donor tries
                                        to acquire. */
  };

/* One donee in a list. */
struct donee_elem 
  {
    struct list_elem elem;          /* List element. */
    struct thread *donee;           /* This donee thread. */
    struct lock *lock;              /* Lock held by donee. */
  };
  
void donor_init(struct donor_elem *,
           struct thread *, struct lock *);          
void donee_init(struct donee_elem *,
           struct thread *, struct lock *);
void lock_holder_delete_donor(struct thread *, 
                        struct lock *);    
void delete_donee(struct thread *, struct thread *,
          struct lock *);
void delete_donee(struct thread *, struct thread *,
          struct lock *);      
       
bool delete_donor(struct thread *, struct thread *,
          struct lock *);

void update_priority_donation(struct thread *);
         
void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition 
  {
    struct list waiters;        /* List of waiting semaphore_elems. */
  };

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
bool higher_priority_sema(const struct list_elem *, 
                      const struct list_elem *, 
                       void *);
void cond_broadcast (struct condition *, struct lock *);

/* Optimization barrier.

   The compiler will not reorder operations across an
   optimization barrier.  See "Optimization Barriers" in the
   reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */

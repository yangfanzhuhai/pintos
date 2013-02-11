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
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
#include "threads/fixed-point.h"
#include <stdlib.h>

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

#define NICE_MIN (-20)
#define NICE_MAX (20)
#define LOAD_AVG_REFRESH_RATE (60)
#define MLFQS_PRIORITIES_PER_QUEUE (4)
#define MLFQS_USE_MULTILEVEL (true)

/* The system wide load average is 0 at program start */
int32_t load_avg = 0;

/* Multilevel feedback queues list and queue memory */
static struct list mlfqs_queues;
static struct bsd_queue
    queue_arr[(PRI_MAX - PRI_MIN + 1) / MLFQS_PRIORITIES_PER_QUEUE];

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;




void
initialise_mlfqs_queue (struct bsd_queue *bsdq)
{
  ASSERT(bsdq != NULL);
  list_init(&bsdq->threads);
}

void
initialise_mlfqs_queues (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  /* Assert priority range is divisible by priorities per queue */
  ASSERT((((PRI_MAX - PRI_MIN) + 1) % MLFQS_PRIORITIES_PER_QUEUE) == 0);

  list_init(&mlfqs_queues);

  int i;
  for (i = PRI_MIN; i < PRI_MAX; i += MLFQS_PRIORITIES_PER_QUEUE)
    {
      struct bsd_queue *bsdq = &queue_arr[i / MLFQS_PRIORITIES_PER_QUEUE];
      initialise_mlfqs_queue (bsdq);
      bsdq->priority_min = i;
      bsdq->priority_max = i + MLFQS_PRIORITIES_PER_QUEUE - 1;
      
      /* Must push front so highest priority queue is first */
      list_push_front (&mlfqs_queues, &bsdq->bsdelem);
    }
}

void
thread_insert_mlfqs (struct thread *t)
{
  struct list_elem *e;

  for (e = list_begin (&mlfqs_queues); e != list_end (&mlfqs_queues);
       e = list_next (e))
    {
      struct bsd_queue *bsdq = list_entry (e, struct bsd_queue, bsdelem);

      if (t->priority >= bsdq->priority_min &&
          t->priority <= bsdq->priority_max)
        {
          /* Note:
             The use of the higher_priority sorting function gives the desired 
             behaviour because it returns true only if the thread to be inserted
             has a higher priority then the next thread in the list.
             This ensures the list is sorted in descending priority order.
             If there exists a thread in the queue with the same priority as the
             thread to be inserted, the thread will be inserted after the
             existing thread - ensuring round robin access. */
          list_insert_ordered (&bsdq->threads, &t->bsdelem, higher_priority,
              NULL);
          return;
        }
    }
}

void
thread_remove_mlfqs (struct thread *t)
{
  struct list_elem *e;
  struct list_elem *e2;

  /* For each bsd queue */
  for (e = list_begin (&mlfqs_queues); e != list_end (&mlfqs_queues);
       e = list_next (e))
    {
      struct bsd_queue *bsdq = list_entry (e, struct bsd_queue, bsdelem);

      /* For each thread in the current bsd queue */
      for (e2 = list_begin (&bsdq->threads); e2 != list_end (&bsdq->threads);
           e2 = list_next (e2))
        {
          struct thread *tc = list_entry (e2, struct thread, bsdelem);

          /* Remove thread if it matches the search thread */
          if (t == tc)
            {
              list_remove (e2);
            }
        }
    }
}




/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

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

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

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
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&all_list);

  /* Initialise relevant queuing system */
  if (thread_mlfqs && MLFQS_USE_MULTILEVEL)
    {
      initialise_mlfqs_queues ();
    }
  else
    {
      list_init (&ready_list);
    }

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);

  /* Sets initial thread to have a niceness of 0 */
  if (thread_mlfqs)
    {
      /* Initial thread has a niceness of 0 and a recent_cpu of 0 */
      initial_thread->niceness = 0;
      initial_thread->recent_cpu = 0;

      /* Dynamically calculate priority 
         Note: This overwrites the value set using init_thread */
      initial_thread->priority 
          = thread_calculate_mlfqs_priority (initial_thread);
    }

  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
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
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
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
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Multilevel feedback queue setup */
  if (thread_mlfqs)
    {
      /* A created thread inherits its parent's niceness and recent_cpu */
      t->niceness = thread_current ()->niceness;
      t->recent_cpu = thread_current ()->recent_cpu;

      /* Dynamically calculate priority 
         Note: This overwrites the value set using init_thread */
      t->priority = thread_calculate_mlfqs_priority (t);
    }

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);
  
  /* Add to run queue. */
  thread_unblock (t);


  if (t->priority > thread_current ()->priority) 
    {
      thread_yield();
    }
  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
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
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));
  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);

  /* Thread is now runnable restore in the relevant run queue */
  if (thread_mlfqs && MLFQS_USE_MULTILEVEL)
    {
      thread_insert_mlfqs (t);
    }
  else
    {
      list_insert_ordered (&ready_list, &t->elem, higher_priority, NULL);
    }

  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* list_less_func() for list_insert_ordered() 
   threads with higher priority comes first */
bool 
higher_priority(const struct list_elem *elem1, 
	const struct list_elem *elem2,
	void *aux UNUSED)
{
	ASSERT(elem1 != NULL);
	ASSERT(elem2 != NULL);
	struct thread *thread1 
		= list_entry (elem1, struct thread, elem);
	struct thread *thread2 
		= list_entry (elem2, struct thread, elem);
	return thread1->priority > thread2->priority;
}


/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
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
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread)
    {
      /* Using multi level feedback queue */
      if (thread_mlfqs && MLFQS_USE_MULTILEVEL)
        {
          thread_insert_mlfqs (cur);
        }

      /* Standard queue */
      else
        {
          list_insert_ordered(&ready_list, &cur->elem, higher_priority, NULL);
        }
    }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{ 
  /* If multilevel feedback queue is in use ignore set priority as priority 
     will be dynamically determined. */
  if (!thread_mlfqs)
    {
      /* % Luke's implementation */
      struct thread *curr, *next;
      curr = thread_current();
      curr->priority = new_priority;
      next = list_entry (list_begin(&ready_list), struct thread, elem);

      if (next != NULL && curr->priority < next->priority)
      {
          thread_yield();
      }
      /* End */
      thread_current ()-> priority = new_priority;
    }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  ASSERT (nice >= NICE_MIN && nice <= NICE_MAX);
  thread_current ()->niceness = nice;
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->niceness;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return (int)round_fp_to_int (fp_int_multiplication (load_avg,100));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  int32_t ct_cpu = thread_current ()->recent_cpu;
  return (int)round_fp_to_int (fp_int_multiplication (ct_cpu,100));
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
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
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
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->magic = THREAD_MAGIC;

  /* Initialise semaphore used for thread sleeping */
  sema_init (&t->wake_up_sema, 0);

  if (thread_mlfqs && MLFQS_USE_MULTILEVEL)
    {
      t->recent_cpu = 0;
      thread_update_mlfqs_priority (t,NULL);
    }
  else
    {
      t->priority = priority;
      t->base_priority = priority;
      list_init (&t->donors);
    }

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{

  /* Multilevel feedback queue is enabled */
  if (thread_mlfqs && MLFQS_USE_MULTILEVEL)
    {
      struct list_elem *e;

      /* Return the thread with the highest priority */
      for (e = list_begin (&mlfqs_queues); e != list_end (&mlfqs_queues);
           e = list_next (e))
        {
          struct bsd_queue *bsdq = list_entry (e, struct bsd_queue, bsdelem);
          if (!list_empty (&bsdq->threads))
            {
              return list_entry (list_pop_front (&bsdq->threads), 
                  struct thread, bsdelem);
            }
        }

      /* If we reached here no threads were found */
      return idle_thread;
    }

  /* Standard priority queue */
  else
    {
      if (list_empty (&ready_list))
        return idle_thread;
      else
        return list_entry (list_pop_front (&ready_list), struct thread, elem);
    }  
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);


/* Get the number of threads that are either running or ready to run 
   (not including the idle thread). */
int
threads_ready_or_running (void)
{
  /* Notes:
       -> The idle thread is not stored on the ready list */

  int ready_threads = 0;

  if (thread_mlfqs && MLFQS_USE_MULTILEVEL)
    {
      struct list_elem *e;

      /* For each queue in the mlfqs */
      for (e = list_begin (&mlfqs_queues); e != list_end (&mlfqs_queues);
           e = list_next (e))
        {
          struct bsd_queue *bsdq = list_entry (e, struct bsd_queue, bsdelem);
          ready_threads += list_size (&bsdq->threads);
        }
    }
  else
    {
      ready_threads = list_size (&ready_list);
    }

  /* If the current thread isn't the idle thread then there is a running thread
     that should be added to the count */
  if (thread_current () != idle_thread)
    {
      ready_threads++;
    }

  return ready_threads;
}



/* Increment recent_cpu of current thread if not the idle thread */
void
thread_increment_recent_cpu (void)
{
  if (thread_current () != idle_thread)
    {
      thread_current ()->recent_cpu
          = fp_int_addition (thread_current ()->recent_cpu, 1);
    }
}


/* Update the load_avg value.

   NOTE:
    -> The load_avg value is stored as a fixed point value as defined in
       fixed-point.h */
void
update_load_avg (void)
{
  ASSERT (thread_mlfqs);
  int32_t ready_threads = (int32_t)threads_ready_or_running ();

  int32_t fp_refresh = int_to_fp ((LOAD_AVG_REFRESH_RATE));
  int32_t fp_refresh_minus_1 = int_to_fp ((LOAD_AVG_REFRESH_RATE - 1));
  int32_t fp_1 = int_to_fp (1);

  /* (LOAD_AVG_REFRESH_RATE - 1 / LOAD_AVG_REFRESH_RATE) * load_avg */
  int32_t fp_decay = fp_division (fp_refresh_minus_1,fp_refresh);
  int32_t fp_decay_component = fp_multiplication (fp_decay,load_avg);

  /* (1 / LOAD_AVG_REFRESH_RATE) * ready_threads */
  int32_t fp_recip = fp_division (fp_1,fp_refresh);
  int32_t fp_recip_component = fp_int_multiplication (fp_recip,ready_threads);

  load_avg = fp_addition (fp_decay_component,fp_recip_component);
}


/* Calculate the recent_cpu value of the given thread using the weighted moving 
   average formula:
      recent_cpu = (2*load_avg )/(2*load_avg + 1) * recent_cpu + nice */
int
thread_calculate_recent_cpu (struct thread *t)
{
  ASSERT (thread_mlfqs);
  int32_t fp_dbl_load_avg = fp_int_multiplication (load_avg,2);
  int32_t fp_dbl_load_avg_plus_1 = fp_int_addition (fp_dbl_load_avg,1);
  int32_t fp_decay = fp_division (fp_dbl_load_avg,fp_dbl_load_avg_plus_1);
  int32_t fp_decay_component = fp_multiplication (fp_decay,t->recent_cpu);
  int32_t rcpu = fp_int_addition (fp_decay_component,(int32_t)t->niceness);
  return (int)rcpu;
}

/* Update the recent_cpu value of the given thread to be the dynamically
   calculated value */
void
thread_update_recent_cpu (struct thread *t, void *aux UNUSED)
{
  ASSERT (thread_mlfqs);
  t->recent_cpu = thread_calculate_recent_cpu (t);
}

/* Update the recent_cpu value of all threads (excluding idle) */
void
threads_update_recent_cpu (void)
{
  ASSERT (thread_mlfqs);
  thread_foreach (thread_update_recent_cpu,NULL);
}

/* Calculate the new multilevel feedback queue priority of the given thread 
   using the formula:
   priority = PRI_MAX - (recent_cpu / 4) - (nice * 2) */
int
thread_calculate_mlfqs_priority (struct thread *t)
{
  ASSERT (thread_mlfqs);
  int32_t fp_recent_cpu_div_4 = fp_int_division (t->recent_cpu,4);
  int32_t int_dbl_nice = t->niceness << 1;
  int32_t int_priority = PRI_MAX - round_fp_to_int (fp_recent_cpu_div_4);
  int_priority = int_priority - int_dbl_nice;

  /* Restrict to a minimum priority of PRI_MIN */
  if ((int)int_priority < PRI_MIN)
    {
      return PRI_MIN;
    }
  else
    {
      return (int)int_priority;
    }
}

/* Update the value of the priority of the given thread according to the
   multilevel feed back queue.
   If the priority changes, remove the thread from the mlfqs and reinsert in
   the correct position */
void
thread_update_mlfqs_priority (struct thread *t, void *aux UNUSED)
{
  ASSERT (thread_mlfqs);

  /* Idle thread must always have the minimum priority */
  if (t == idle_thread)
    {
      t->priority = PRI_MIN;
      return;
    }

  int old_priority = t->priority;
  t->priority = thread_calculate_mlfqs_priority (t);


  /* If the thread is queued (status is THREAD_READY) and the thread's priority
     has changed, reinsert the thread in the queue in the correct position

     Note: it is necessary to ensure the thread is ready since every thread
           (including blocked threads) will have their priority updated */
  if (t->priority != old_priority && t->status == THREAD_READY)
    {
      /* Reinsert in the correct queue */
      if (MLFQS_USE_MULTILEVEL)
        {
          thread_remove_mlfqs (t);
          thread_insert_mlfqs (t);
        }
      else
        {
          list_remove (&t->elem);
          list_insert_ordered (&ready_list, &t->elem, higher_priority, NULL);
        }
    }
}

/* Update the mlfqs priority of all threads (excluding idle) */
void
threads_update_mlfqs_priority (void)
{
  ASSERT (thread_mlfqs);
  thread_foreach (thread_update_mlfqs_priority,NULL);
}

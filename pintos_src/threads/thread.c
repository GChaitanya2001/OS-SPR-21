#include "threads/thread.h"
#include "threads/fixed-point.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include <filesys/file.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "devices/timer.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of processes in sleep (wait) state (i.e. wait queue). */
static struct list wait_elem_list;

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
void thread_wake_up (int64_t current_tick);
static bool thread_sort_desc (const struct list_elem *left, const struct list_elem *right, void *aux UNUSED);
static bool thread_insert_desc_begin (const struct list_elem *left, const struct list_elem *right, void *aux UNUSED);
static bool thread_insert_desc_end (const struct list_elem *left, const struct list_elem *right, void *aux UNUSED);
static void thread_update_priority_for_one (struct thread *curr);
static void thread_update_recent_cpu_for_one (struct thread *curr);
static int load_avg;

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

static thread_func timer_wakeup;
static struct thread* wakeup_thread;  /*Thread which is used for waking up sleeping threads*/

void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&wait_elem_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  initial_thread->sleep_endtick = 0;

  load_avg = 0;
}


static void timer_wakeup (void *param)
{
  //printf("\nWAKEUP");
  wakeup_thread = thread_current();
  while (true)
  { 
    enum intr_level old_level;
    old_level = intr_disable();
    thread_block();
    intr_set_level(old_level);
    int64_t current_tick = timer_ticks();
    struct list_elem *e;
    //printf("\nHELLO");

	 // enumerate all threads in wait queue
	 for (e = list_begin (&wait_elem_list); e != list_end (&wait_elem_list); e = list_next (e))
	 {
	      struct thread *t = list_entry (e, struct thread, wait_elem);
	      //if wake up time(sleep_endtick) has arrived
	      if (t->sleep_endtick <= current_tick)
	       {
	          // need not sleep(wait) anymore
	          t->sleep_endtick = 0;
	          //remove this thread from the wait queue
	          list_remove (&t->wait_elem);
	          // Add to run queue(state transition from sleep to run)
	          thread_unblock (t);
	       }
	 }
   }
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

  thread_create ("wakeup", PRI_MAX, timer_wakeup, NULL);
  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (int64_t tick) 
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

  /*Unblock wakeup_thread if its time for some thread to wakeup */

  if(!list_empty(&wait_elem_list))
  {
    struct list_elem* first = list_front(&wait_elem_list);
    struct thread* cur = list_entry(first,struct thread,elem);
    if(cur->sleep_endtick <= tick && wakeup_thread->status == THREAD_BLOCKED)
    {
      // printf("\n!!");
      thread_unblock(wakeup_thread);
    }
  }

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
   scheduled.  Use a semaphore or some for_one form of
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

  /* Checking the priority values to be between PRI_MIN and PRI_MAX*/
  ASSERT (priority >= PRI_MIN && priority <= PRI_MAX);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

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

  /* mlfqs scheduling when thread_mlfqs is set to true*/
  if (thread_mlfqs)
    thread_update_priority_for_one (t);

  if (priority > thread_current ()->priority)
    thread_yield();

  #ifdef USERPROG
  sema_init (&t->wait, 0);
  t->exit_status = DEFAULT;
  list_init (&t->files);
  list_init (&t->children);
  if (thread_current () != initial_thread)
     list_push_back (&thread_current ()->children, &t->children_elem);
  t->parent = thread_current ();
  t->exited = false;
#endif

  return tid;
}

static bool 
thread_comp(const struct list_elem * e1, const struct list_elem * e2, void * aux UNUSED)
{
  struct thread * t1 = list_entry(e1, struct thread, elem);
  struct thread * t2 = list_entry(e2, struct thread, elem);
  if (t1->sleep_endtick < t2->sleep_endtick)
  {
    return true;
  }
  return false;
}

//Sleep current thread till 'ticks_end' time
void
thread_sleep_till (int64_t endticks)
{
  //Get the current thread
  struct thread *curr_thread = thread_current();

  //Set the sleep_endtick to the parameter that is passed
  curr_thread->sleep_endtick = endticks;

  // put T into the wait queue
  list_insert_ordered(&wait_elem_list, &curr_thread->wait_elem, thread_comp, NULL);

  // make the current thread block (sleeped)
  thread_block();
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
   update for_one data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;
  ASSERT (is_thread (t));
  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  /*list_push_back (&ready_list, &t->elem);*/
  list_insert_ordered (&ready_list, &t->elem, thread_insert_desc_end, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
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
  struct list_elem *l;
  struct thread *t, *cur;
  cur = thread_current ();
  for (l = list_begin (&cur->children); l != list_end (&cur->children); l = list_next (l)){
      t = list_entry (l, struct thread, children_elem);
      if (t->status == THREAD_BLOCKED && t->exited) thread_unblock (t);
      else
       {
          t->parent = NULL;
          list_remove (&t->children_elem);
      }   
  }
  process_exit ();
  ASSERT (list_size (&cur->files) == 0);
  if (cur->parent && cur->parent != initial_thread) list_remove (&cur->children_elem);
  #endif
  /* Remove thread from all threads list, set our status to dying,
     and schedule anfor_one process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();

  if(cur == wakeup_thread) return;

  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
  {
     list_insert_ordered (&ready_list, &cur->elem, thread_insert_desc_end, NULL);
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

void
thread_yield_head (struct thread *cur)
{
  enum intr_level old_level;

  if(cur == wakeup_thread) return;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread)
  {
    list_insert_ordered (&ready_list, &cur->elem, thread_insert_desc_begin, NULL);
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
  //thread_current ()->priority = new_priority;
  struct thread* curr = thread_current();
  curr->priority = new_priority;
  
  if (curr->status == THREAD_READY) /* Re-order the ready-list */
    {
      list_remove (&curr->elem);
      list_insert_ordered (&ready_list, &curr->elem, thread_insert_desc_end, NULL);
    }
  else if (curr->status == THREAD_RUNNING && list_entry (list_begin (&ready_list), struct thread, elem)->priority > new_priority)
    thread_yield_head (curr);
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
  struct thread *cur;
  cur = thread_current ();
  cur->nice = nice;
  thread_update_recent_cpu ();
  thread_update_priority ();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()-> nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
   return CONVERT_TO_NEAREST_INT (100 * load_avg);
}

void
thread_update_load_avg (void)
{
  int ready_threads;
  
  if (thread_current () != idle_thread)
  {
    ready_threads = list_size (&ready_list) + 1;
  }
  else
  {
    ready_threads = list_size (&ready_list);
  }
  load_avg = FIXED_POINT_MULT (CONVERT_TO_FP (59) / 60, load_avg) + CONVERT_TO_FP (1) / 60 * ready_threads;
}

void
thread_update_recent_cpu (void)
{
  thread_update_recent_cpu_for_one (thread_current ());
}

void
thread_update_recent_cpu_for_all (void)
{
  struct list_elem *e;
  struct thread *t;
  
  e = list_begin (&all_list);
  while (e != list_end (&all_list))
  {
      t = list_entry (e, struct thread, allelem);
      thread_update_recent_cpu_for_one (t);
      e = list_next (e);
  }
}

static void
thread_update_recent_cpu_for_one (struct thread *curr)
{
  ASSERT (is_thread (curr));
  int load;
  if (curr == idle_thread) return;
  load = 2 * load_avg;
  curr->recent_cpu = ADD_INT (FIXED_POINT_MULT (FIXED_POINT_DIV (load, ADD_INT (load, 1)), curr->recent_cpu), curr->nice);
}

void
thread_update_priority (void)
{
  thread_update_priority_for_one (thread_current ());
}

void
thread_update_priority_for_all (void)
{
  struct list_elem *e;
  struct thread *t;
  
  e = list_begin (&all_list);
  while (e != list_end (&all_list))
  {
      t = list_entry (e, struct thread, allelem);
      thread_update_priority_for_one (t);
      e = list_next (e);
  }
  sort_thread_list (&ready_list);
}

static void
thread_update_priority_for_one (struct thread *curr)
{
  ASSERT (is_thread (curr)); 
  if (curr == idle_thread || curr == wakeup_thread) return;
  curr->priority = PRI_MAX - CONVERT_TO_NEAREST_INT (curr->recent_cpu / 4) - curr->nice * 2;
  if (curr->priority > PRI_MAX)
      curr->priority = PRI_MAX;
  else if (curr->priority < PRI_MIN)
      curr->priority = PRI_MIN;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return CONVERT_TO_NEAREST_INT (100 * thread_current ()->recent_cpu);
}


/* Idle thread.  Executes when no for_one thread is ready to run.

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
         important; for_onewise, an interrupt could be handled
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
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  t->sleep_endtick = 0;
  if (thread_mlfqs)
  {
    t->nice = NICE_DEFAULT; /* NICE_DEFAULT should be zero */
    if (t == initial_thread)
      t->recent_cpu = 0;
    else
      t->recent_cpu = thread_get_recent_cpu ();
  }
  list_push_back (&all_list, &t->allelem);
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
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
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
   running to some for_one state.  This function finds anfor_one
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


static bool
thread_sort_desc (const struct list_elem *left, const struct list_elem *right, void *aux UNUSED)
{
  struct thread *a, *b;
  ASSERT (left != NULL && right != NULL);
  a = list_entry (left, struct thread, elem);
  b = list_entry (right, struct thread, elem);
  return (a->priority > b->priority);
}

/* put the threads who has outstanding priority to the head of the list */
void
sort_thread_list (struct list *l)
{
  if (list_empty (l)) return;
  list_sort (l, thread_sort_desc, NULL);
}

/* same as thread_sort_desc but use > instead of >= */
static bool
thread_insert_desc_begin (const struct list_elem *left, const struct list_elem *right, void *aux UNUSED)
{
  struct thread *a, *b;
  ASSERT (left != NULL && right != NULL);
  a = list_entry (left, struct thread, elem);
  b = list_entry (right, struct thread, elem);
  return (a->priority >= b->priority);
}

/* same as thread_sort_desc but used for Round-Robin Scheduling*/
static bool
thread_insert_desc_end (const struct list_elem *left, const struct list_elem *right, void *aux UNUSED)
{
  return thread_sort_desc (left, right, NULL);
}

struct thread *
get_thread_elem (tid_t tid)
{
  struct list_elem *temp;
  struct thread *ret = NULL;
  for (temp = list_begin (&all_list); temp != list_end (&all_list); temp = list_next (temp))
    {
      ret = list_entry (temp, struct thread, allelem);
      ASSERT (is_thread (ret));
      if (ret->tid == tid) return ret;
    }
  return NULL;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

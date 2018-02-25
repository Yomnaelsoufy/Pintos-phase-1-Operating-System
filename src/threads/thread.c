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
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"

#endif
static real load_avg;
// formate 17.14 means p.q && q = 2^14
static intg fraction = 1<<Q;


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

 int get_idle_tid(){
  return idle_thread->tid;
}
static void idle (void *aux UNUSED);
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
  //printf("strat of thread init\n");

  ASSERT (intr_get_level () == INTR_OFF);
  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  
  load_avg =integer_into_fixed_point(0);
  //printf("end of thread init\n");
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void)
{
  //printf("start in thraed start \n");
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
  //printf("end of thread start \n");
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void)
{
  //printf("start in thread ticks \n");
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

  real one;
  struct thread *cur_thread;
  if(thread_mlfqs){
    cur_thread = thread_current();
    cur_thread->recent_cpu = add_fixed_point_to_integer(cur_thread->recent_cpu ,1);
    if (timer_ticks() % TIMER_FREQ == 0) /* do this every second */
    {
       update_load_avg();
       update_all_recent_cpu();
       update_priority_of_all_threads_in_all_list();

    }


    if(timer_ticks() % 4 == 0){
      struct thread *t = thread_current();
      update_priority(t);
    }

  }
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
 // printf("end of thread tick \n");
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

  if(!thread_mlfqs){
    if(t->donated_priority > running_thread()->donated_priority){
        thread_yield();
      }
    }else{

      if(t->priority > running_thread()->priority){
        thread_yield();
      }
    }

//printf("end of thread create\n");
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
  //printf("start of thread block\n");
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
// problem is here that it doesnot return from scheduling
  schedule ();
//printf("end of thread block\n");
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
  //printf("enter in thread unblock\n");
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered(&ready_list,&t->elem,&get_max,NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
  //printf("end of thread unblock \n");
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
 // printf("start of thread current\n");
  //debug_backtrace();
  struct thread *t = running_thread ();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  //printf("end of thread current \n");
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
  //printf("start of thread exit\n");
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
 // printf("end of thread exit\n");
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void)
{
 // printf("start of thread yield\n");
  struct thread *cur = thread_current ();
  enum intr_level old_level;

  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread)
    list_insert_ordered(&ready_list,&cur->elem,&get_max,NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
 // printf("end of thread yield\n");
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  //printf("start of thread foreach\n");
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
  //  printf("end of thread_foreach\n");
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority (int new_priority)
{

//printf("start of thread_set_priority\n");
  if(thread_mlfqs){
    return;
  }
  struct thread *cur = thread_current ();
  ASSERT(cur!=NULL);
  
  if(cur->priority > new_priority){
    if(cur->donated_priority == cur->priority){ 
      cur->donated_priority = new_priority;
    }
    cur->priority = new_priority;   
    thread_yield();                 
  }else{
    cur->priority = new_priority;   
    if(cur->donated_priority < new_priority){ 
      cur->donated_priority = new_priority;
    }
  }
 // printf("end of thread_set_priority \n");
  }
/* Returns the current thread's priority. */
int thread_get_priority (void)
{
  //printf("start of thread_get_priority \n");
  if(!thread_mlfqs){
   // printf("@mlfqs end of thread_get_priority\n");
    return thread_current ()->donated_priority;
  }
 // printf("@not mlfqs end of thread\n");
  return (thread_current ()->priority);
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED)
{
  if(nice< -20 || nice >20)return;
  //printf("start in thread_set_nice\n");
  struct thread *t = thread_current();
  if(thread_mlfqs){
   t->nice_time = nice;
   }
  // printf("nice %d\n",nice);

   //printf("end of thread_set_nice\n");
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
  //printf("start in thread_get_nice\n");
  struct thread *t;
    t = running_thread();

   // printf("end in thread_get_nice\n");
    return t->nice_time;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void)
{

  //printf("start in thread_get_load_avg\n");
   real num = multiplication_of_fixed_point_with_integer(load_avg,100);
//printf("end in thread_get_load_avg\n");
  return (fixed_point_into_nearest_integer(num));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
  //printf("start in thread_get_recent_cpu\n");
  real num;
  num = 100 * thread_current()->recent_cpu;
  //printf("recent=%d\n",fixed_point_into_nearest_integer(num));
  //printf("end in thread_get_recent_cpu\n");
  return (fixed_point_into_nearest_integer(num));
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
  //printf("enter in idle\n");
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
   // printf("end of idle\n");
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux)
{
  //printf("enter in kernel_thread\n");
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
  //printf("end of kernel_thread\n");
}

/* Returns the running thread. */
 struct thread *
running_thread (void)
{
  //printf("enter in running_thread\n");
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  //printf("end of running_thread\n");
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

  //printf("start of init thread\n");
  if(thread_mlfqs && t != initial_thread){
    t->recent_cpu = integer_into_fixed_point(0);
    t->nice_time = 0;

  }else {
    t->nice_time = 0;
    t->recent_cpu = integer_into_fixed_point(0);
  } 
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->donated_priority=priority;
  list_init(&t->threads_waiting_on_me_elems);
  t->magic = THREAD_MAGIC;
  if(thread_mlfqs){   // Idle thread ???????????????????????
        update_priority(t);
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
bool get_max(const struct list_elem * a,const struct list_elem * b,void *aux){
  struct thread *first = list_entry(a,struct thread,elem);
  struct thread *second = list_entry(b,struct thread,elem);
  if(!thread_mlfqs){
  return first->donated_priority > second->donated_priority;
}else{
  return first->priority > second->priority;
}
//printf("end of init_thread\n");
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
  else{
    // for donations 
    list_sort(&ready_list,&get_max,NULL);
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
    //intr_enable();
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

  ASSERT(is_thread(cur));

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



//Convert n to fixed point: 	n * f
 real integer_into_fixed_point(intg n)
{
    real num;
    num = ((n) * fraction);
    return num;
}

//Convert x to integer (rounding to nearest):
intg fixed_point_into_nearest_integer( real x)
{

   if(x >= 0){
     return (((x) + (fraction / 2)) / fraction );
   } else{
     return (((x) - (fraction / 2)) / fraction );
   }

}

//Convert x to integer (rounding toward zero)
intg fixed_point_to_integer( real x)
{
    return ((x) / fraction);
}

 real multiplication_of_two_fixed_point( real x, real y)
//multiply two fixed point
{
   real num;
  num = (((int64_t)x) * y / fraction);
  return num;
}

// multiply of fixed point with integer
 real multiplication_of_fixed_point_with_integer( real x, intg n)
{
    real num;
    num = x * n;
    return num;
}
//division two fixed point
 real division_of_fixed_point( real x, real y)
{
   real num;
  num = ((((int64_t)(x)) * fraction) / y);
  return num;
}

// divide of fixed point with integer
 real division_of_fixed_point_with_integer( real x,intg n)
{
   real num;
  num = x / n;
  return num;
}
//Add x and y: 	x + y
 real add_two_fixed_point( real x,  real y){
  real num;
  num = x + y;
  return num;
}

// add fixed point to integer_into_fixed_point
 real add_fixed_point_to_integer( real x, intg n){
  real num;
  num = x + n * fraction;
  return num;
}

// subtract fixed point to integer_into_fixed_point
 real subtract_fixed_point_to_integer( real x, intg n){
   real num;
  num = x - n * fraction;
  return num;
}

//subtract x and y: 	x + y
 real subtract_two_fixed_point( real x,  real y){
   real num;
  num = x - y;
  return num;
}


/* update recent cpu */
void update_recent_cpu(struct thread *t){
  if(t==idle_thread){
    return;
  }
 // printf("old=%lld",fixed_point_into_nearest_integer(t->recent_cpu));
       real l1;
       real ll1;
       real l2;
       real l3;
       real l4;
      l1 = load_avg * 2;
      ll1=load_avg*2;
      l2 = add_fixed_point_to_integer(ll1 , 1);
      l3 = division_of_fixed_point(l1, l2);   
      l4 = multiplication_of_two_fixed_point(t->recent_cpu,l3);   
      t->recent_cpu = add_fixed_point_to_integer(l4,t->nice_time);
      // printf("l1=%lld\n",l1);
      // printf("l2=%lld\n",l2);
      // printf("l3=%lld\n",l3);
      // printf("l4=%lld\n",l4);
      // printf("nice_time=%lld",t->nice_time);
      // printf("new=%lld",t->recent_cpu);
}

// update recent_cpu
void update_all_recent_cpu(void){
  if(thread_mlfqs){

  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)){

    struct thread *t = list_entry(e,struct thread, allelem);


    if(t != idle_thread){
        update_recent_cpu(t);  
  }
}
}

}




/* update load average */
void update_load_avg(void){
       real load =multiplication_of_fixed_point_with_integer(load_avg, 59);
       real load1 = division_of_fixed_point_with_integer(load,60);
       real load2 = integer_into_fixed_point(length_of_ready_list());
       real load3 = division_of_fixed_point_with_integer(load2,60);
       load_avg = add_two_fixed_point(load1,load3);
     

}
/* update the priority */


void update_priority(struct thread *t){
    if(t!=idle_thread){
    t->priority = PRI_MAX - fixed_point_into_nearest_integer(division_of_fixed_point_with_integer(t->recent_cpu,4)) - t->nice_time * 2;
     }else{
      t->priority=PRI_MAX;
     }
}

// update PRIORITY

void update_priority_of_all_threads_in_all_list (void)
{
  if(thread_mlfqs){
  struct list_elem *e;
  struct thread *t;

  e = list_begin (&all_list);
  while (e != list_end (&all_list))
    {
      t = list_entry (e, struct thread, allelem);
      if(t != idle_thread){
          update_priority(t);
        
        if (t->priority > PRI_MAX)
          t->priority = PRI_MAX;
        else if (t->priority < PRI_MIN)
          t->priority = PRI_MIN;
      }
      e = list_next (e);
    }
    // i must here to resort all element
    list_sort (&ready_list, &get_max, NULL);
  }
}
/*
return length ot ready_list
*/

int length_of_ready_list(void){

  struct list_elem *e;
  size_t cnt = 0;
  struct thread *t;
  
  for (e = list_begin (&ready_list); e != list_end (&ready_list); e = list_next (e)){
    t = list_entry(e, struct thread, elem);
    if(t != idle_thread)
        cnt++;
  }
  
  if(running_thread() != idle_thread)
    cnt++;

   return cnt;
}


/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

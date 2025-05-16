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

/* struct thread의 `magic` 멤버를 위한 임의의 값.
   스택 오버플로우를 감지하는 데 사용됩니다. 자세한 내용은 thread.h의 상단 주석을 참조하세요. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210


void thread_awake(int64_t ticks);
int64_t timer_ticks(void);


/* THREAD_READY 상태의 프로세스 목록입니다. 실행 준비는 되었지만 실제로 실행되고 있지는 않은 프로세스입니다. */
static struct list ready_list;

/* sleep list 만들기 */
static struct list sleep_list;

/* Idle(유휴) 스레드입니다. */
static struct thread *idle_thread;

/* 초기 스레드로, init.c의 main()을 실행하는 스레드입니다. */
static struct thread *initial_thread;

/* allocate_tid()에서 사용하는 락입니다. */
static struct lock tid_lock;

/* 스레드 제거 요청 목록입니다. */
static struct list destruction_req;
int64_t MIN_alarm_time = INT64_MAX;

/* 통계 관련 변수입니다. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* 스케줄링 관련 매크로 및 변수입니다. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* false일 경우(기본값) 라운드 로빈 스케줄러를 사용합니다.
   true일 경우, 다단계 피드백 큐 스케줄러를 사용합니다.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* T가 유효한 스레드를 가리키는 것으로 보이면 true를 반환합니다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 현재 실행 중인 스레드를 반환합니다.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* 현재 실행 중인 코드를 스레드로 변환하여 스레딩 시스템을 초기화합니다.
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init (void) {
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
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init(&sleep_list);
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작합니다.
   idle 스레드도 생성합니다. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* 타이머 인터럽트 핸들러에 의해 각 타이머 틱마다 호출됩니다.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	thread_awake(timer_ticks());

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

/* 스레드 통계를 출력합니다. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* 이름이 NAME인 새 커널 스레드를 생성합니다. with the given initial
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
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);
	thread_preemption ();

	return tid;
}

/* 현재 스레드를 수면 상태로 전환합니다.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* 블록된 스레드 T를 실행 준비 상태로 전환합니다.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
    list_insert_ordered (&ready_list, &t->elem, compare_thread_priority, 0); 
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* 현재 실행 중인 스레드의 이름을 반환합니다. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* 현재 실행 중인 스레드를 반환합니다.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
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
thread_tid (void) {
	return thread_current ()->tid;
}

/* 현재 스레드를 스케줄에서 제거하고 파괴합니다.  Never
   returns to the caller. */
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

/*  자발적으로 CPU를 양보합니다.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ()); //인터럽트 컥텍스트에서 이 함수가 호출되지 않았는 지확인 

	old_level = intr_disable (); // 인터럽트 끄고 이전 상태에 저장 
	if (curr != idle_thread) //idle = 놀고 있는 스ㅡ레드 
		list_insert_ordered(&ready_list, &curr->elem, compare_thread_priority, NULL); 
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* 현재 스레드의 우선순위를 NEW_PRIORITY로 설정합니다. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
	thread_current()->init_priority = new_priority;
	refresh_priority();
	thread_preemption ();


}

/* 현재 스레드의 우선순위를 반환합니다. */
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

/* Idle(유휴) 스레드입니다.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
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

/* 커널 스레드의 기본이 되는 함수입니다. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* T를 이름이 지정된 블록된 스레드로 초기화합니다.
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	/*donation*/
	t ->init_priority = priority;
	t ->wait_on_lock = NULL;
	list_init(&t->donations);
}

/* 스케줄링될 다음 스레드를 선택하여 반환합니다.  Should
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

/* iretq를 사용하여 스레드를 시작합니다. */
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

/* 새 스레드의 페이지를 활성화하여 스레드를 전환합니다.
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

/* 새 프로세스를 스케줄합니다. 진입 시 인터럽트는 비활성화되어야 합니다.
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

/* 새 스레드에 사용할 tid를 반환합니다. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

static bool cmp_wakeup_tick(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct thread *ta = list_entry(a, struct thread, elem);
    struct thread *tb = list_entry(b, struct thread, elem);
    return ta->time_to_wakeup < tb->time_to_wakeup;  // 작은 값이 앞에 오도록
}

void thread_sleep(int64_t ticks) {                    // 깨어나야 할 시각(ticks)을 인자로 받음
    struct thread  *cur_thread;                        // 현재 스레드 포인터
    enum intr_level old_level;                        // 이전 인터럽트 상태 저장 변수

    old_level = intr_disable();                       // 인터럽트를 비활성화하고 상태 저장
    cur_thread = thread_current();                    // 현재 실행 중인 스레드 구함

    ASSERT(cur_thread != idle_thread);                // idle 스레드는 재우면 안 됨

    cur_thread->time_to_wakeup = ticks;               // 깨울 시각 저장
    list_insert_ordered(&sleep_list, &cur_thread->elem, cmp_wakeup_tick, NULL); // 🔁 변경
    thread_block();                                   // 현재 스레드를 BLOCKED 상태로 바꿈 (스케줄러에 의해 제거됨)

    intr_set_level(old_level);                        // 인터럽트 상태 원래대로 복원
}


void thread_awake(int64_t ticks) {
    enum intr_level old_level = intr_disable();  // 인터럽트 비활성화

    struct list_elem *e = list_begin(&sleep_list);

    // sleep_list는 wakeup_tick 오름차순으로 정렬되어 있음
    while (e != list_end(&sleep_list)) {
        struct thread *t = list_entry(e, struct thread, elem);

        if (t->time_to_wakeup <= ticks) {
            e = list_remove(e);            // 리스트에서 제거 후 다음 요소로 이동
            thread_unblock(t);             // READY 상태로 전환
        } else {
            break;  // 정렬되어 있으므로, 더 이상 볼 필요 없음
        }
    }

    intr_set_level(old_level);  // 인터럽트 복원
}


/* threads/thread.c */

bool compare_thread_priority(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED) 
{
	struct thread *a = list_entry(a_, struct thread, elem)->priority;
	struct thread *b = list_entry(b_, struct thread, elem)->priority;
	return a > b;
}

// thread.h에 꼭 함수 선언을 해줍시다.



void thread_preemption(void)
{
	if (!list_empty(&ready_list)) 
	{
		struct thread *top_priority_thread = list_begin(&ready_list);
		if (compare_thread_priority(top_priority_thread, &thread_current()->elem, NULL))
			thread_yield();
	}
}

/* threads/thread.c */

void donate_priority(void)
{
	struct thread *holder = thread_current()->wait_on_lock->holder;
	int count = 0;
	while (holder != NULL)
	{
		holder->priority = thread_current()->priority;
		count++;
		if (count > 8 || holder->wait_on_lock == NULL)
			break;
		holder = holder->wait_on_lock->holder;
	}
}


void remove_with_lock(struct lock *lock)
{
	struct list_elem *curr_donation_elem = list_begin(&thread_current()->donations);

	while (curr_donation_elem != list_tail(&thread_current()->donations))
	{
		struct thread *curr_donation_thread = list_entry(curr_donation_elem, struct thread, donations_elem);
		if (curr_donation_thread->wait_on_lock == lock)
			curr_donation_elem = list_remove(curr_donation_elem);
		else
			curr_donation_elem = list_next(curr_donation_elem);
	}
}



void refresh_priority (void)
{
  struct thread *cur = thread_current ();

  cur->priority = cur->init_priority;
  
  if (!list_empty (&cur->donations)) 
  {
    struct thread *front = list_entry(list_front (&cur->donations), struct thread, donations_elem);
    if (front->priority > cur->priority)
      cur->priority = front->priority;
  }
}

void donate_priority_thread(struct thread *t)
{
	int depth = 0;
	while (t->wait_on_lock != NULL && depth < 8)
	{
		struct thread *holder = t->wait_on_lock->holder;
		if (holder == NULL) break;
		if (holder->priority < t->priority)
			holder->priority = t->priority;

		t = holder;
		depth++;
	}
}

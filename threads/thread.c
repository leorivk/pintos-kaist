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

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
// 슬립리스트
static struct list sleep_list;


/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

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
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

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
void // 쓰레드 초기화
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/*
		gdt : 세그먼트 디스크립터를 저장하는 테이블
		세그먼트 디스크립터 : 메모리 세그먼트에 대한 정보를 저장하는 데이터 구조.
		세그먼트 기준의 주소, 한계, 접근권한, 타입등의 정보를 포함하고 있다.
		코드 세그먼트 디스크립터, 데이터 세그먼트 디스크립터, 스택 세그먼트 디스크립터 등이 있다.
	*/ 

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1, // 구조체 멤버를 초기화할때 .을 사용한다
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	// 전역 쓰레드 컨텍스트를 초기화한다.
	/* Init the globla thread context */
	lock_init (&tid_lock);			// 스레드 ID 잠금을 초기화한다.
	list_init (&ready_list);		// 준비된 스레드 목록을 초기화한다.
	list_init (&sleep_list);		// 준비된 슬립리스트를 초기화한다.
	list_init (&destruction_req);	// 스레드 파괴 요청 목록을 초기화한다.

	// 실행중인 스레드에 대한 스레드 구조체를 생성한다.
	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();					// 현재 실행중인 스레드를 가져온다.
	init_thread (initial_thread, "main", PRI_DEFAULT);	// 스레드를 초기화한다.
	initial_thread->status = THREAD_RUNNING;			// 스레드 상태를 실행중으로 설정한다.
	initial_thread->tid = allocate_tid ();				// 스레드 파괴요청 목록을 초기화한다.
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
// 이 함수를 호출하면 
void
thread_start (void) {
	/* Create the idle thread. */
	// 다음으로 작업할 쓰레드가 없으면 CPU를 유후 상태로 만들기 위해 생성하는 쓰레드
	struct semaphore idle_started;	
	sema_init (&idle_started, 0);	// idle 쓰레드를 생성하고 우선순위를 PRI_MIN으로 지정한다	
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();		// 선점형 쓰레드 스케쥴을 시작한다.

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started); // idle_started 세마포어가 신호를 보낼때까지 대기
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();		// 현재 실행중인 쓰레드를 가져온다.

	/* Update statistics. */
	if (t == idle_thread)						// 현재 실행중인 쓰레드가 뭔지 판단하고 틱을 증기시킨다.
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)			// 틱을 증가시키고 정해놓은 기준보다 더 오래 쓰레드가 실행됐으면
		intr_yield_on_return ();				// 스케쥴러에게 CPU를 양보하도록 지시
}

/* Prints thread statistics. */
void
thread_print_stats (void) {						// 현재 쓰레드의 상태를 출력
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
   // 
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;							// 새로운 쓰레드를 가르킬 포인터 선언
	tid_t tid;									// 새로운 쓰레드의 식별자를 저장할 변수 선언

	ASSERT (function != NULL);					// 함수 포인터가 유효한지 확인

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);				// 페이지 할당을 통해 스레드 구조체 할당
	if (t == NULL)								// 할당 실패시 에러 반환
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);			// 스레드 초기화
	tid = t->tid = allocate_tid ();				// 스레드 식별자를 할당하고 변수에 저장

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	// 새로운 커널 스레드가 시작되면 함수가 실행된다.
	// 새로운 스레드가 생성되면 해당 함수로 실행 흐름이 이동한다는 것을 의미함
	t->tf.rip = (uintptr_t) kernel_thread;		
	t->tf.R.rdi = (uint64_t) function;			// 새로운 스레드가 시작되면 해당 레지스터에 값을 저장함
	t->tf.R.rsi = (uint64_t) aux;				// 새로운 스레드가 시작되면 해당 레지스터에 값을 저장함
	t->tf.ds = SEL_KDSEG;						// 데이터 세그먼트 레지스터 설정
	t->tf.es = SEL_KDSEG;						// 확장 세그먼트 레지스터 설정
	t->tf.ss = SEL_KDSEG;						// 스택 세그먼트 레지스터 설정
	t->tf.cs = SEL_KCSEG;						// 코드 세그먼트 레지스터 설정
	t->tf.eflags = FLAG_IF;						// 인터럽트 플래그 설정

	/* Add to run queue. */
	thread_unblock (t);							// 준비된 큐에 스레드 추가

	return tid;									// 생성된 스레드의 식별자 반환
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
// 현재 실행중인 스레드를 슬립 상태로 전환한다.
thread_block (void) {
	ASSERT (!intr_context ());					// 현재 컨텍스트가 인터럽트 컨텍스트가 아닌지 확인한다.
	ASSERT (intr_get_level () == INTR_OFF);		// 현재 인터럽트 레벨이 OFF임을 확인한다.
	thread_current ()->status = THREAD_BLOCKED;	// 현재 쓰레드의 상태를 BLOCK 한다
	schedule ();								// 스레드 스케쥴링을 실행
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
// 인자로 받을 쓰레드의 BLOCK을 해제한다
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));						// 유효한 쓰레드인지 체크

	old_level = intr_disable ();				// 현재 인터럽트 레벨을 저장하고 인터럽트를 비활성화한다.
	ASSERT (t->status == THREAD_BLOCKED);		// 쓰레드가 블록되어 있는지 확인
	list_push_back (&ready_list, &t->elem);		// 스케쥴링이 되기를 기다리는 스레드의 자료구조에 추가	
	t->status = THREAD_READY;					// READY로 변경
	intr_set_level (old_level);					// 인터럽트 레벨을 복원한다.
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;				// 쓰레드의 이름을 반환
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();		// 현재 실행중인 쓰레드 반환

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));						// 유효한 쓰레드인지 확인
	ASSERT (t->status == THREAD_RUNNING);		// 실행중인지 확인

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;				// 현재 쓰레드의 tid 반환
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());					// 현재 컨텍스트가 인터럽트인지 확인한다

#ifdef USERPROG
	process_exit ();							// USERPROG가 구현되어있으면 프로세스를 종료
#endif											// 아니면

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();							// 인터럽트 비활성화
	do_schedule (THREAD_DYING);					// 스레드를 비활성화 시키고 스케쥴링을 실행
	NOT_REACHED ();								// 성공적으로 코드가 스케쥴링이 됐으면 이 라인은 재생되지 않음
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
// 양보
void
thread_yield (void) {
	struct thread *curr = thread_current ();			// 현재 쓰레드 가져오기
	enum intr_level old_level;							// 인터럽트의 현재 레벨 가져오기

	ASSERT (!intr_context ());							// 현재 컨텍스트가 인터럽트 컨텍스트가 아닌지 확인

	old_level = intr_disable ();						// 현재 인터럽트 레벨 저장하고 인터럽트 비활성화
	if (curr != idle_thread)							// 현재 쓰레드가 idle 쓰레드가 아닌경우
		list_push_back (&ready_list, &curr->elem);		// 준비 리스트에서 현재 쓰레드를 다시 추가
	do_schedule (THREAD_READY);							// 스레드를 준비상태로 설정하고 스케쥴로를 호출
	intr_set_level (old_level);							// 이전 인터럽트 레벨 다시 설정해주기
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;			// 현재 실행중인 스레드의 우선순의를 새로운 우선순위로 바꾼다
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;					// 현재 실행중인 쓰레드의 우선순위를 조회
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

/* Idle thread.  Executes when no other thread is ready to run.

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

	idle_thread = thread_current ();				// 현재 쓰레드를 idle 쓰레드로 설정한다.
	sema_up (idle_started);							// idle 쓰레드가 시작되었음을 알리기 위해 semaphore를 올린다

	for (;;) {										// 무한루프
		/* Let someone else run. */
		intr_disable ();							// 인터럽트를 비활성화
		thread_block ();							// 현재 쓰레드를 블록하고 CPU를 양보

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
		asm volatile ("sti; hlt" : : : "memory");	// 인터럽트를 활성화하고 CPU를 대기상태로 전환한다.
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX); // 우선순위가 유효범위 내에 있는지 확인한다
	ASSERT (name != NULL);	// name이 유효한지 확인

	memset (t, 0, sizeof *t);	// 스레드 구조체를 모두 0으로 초기화한다.
	t->status = THREAD_BLOCKED;		// 스레드의 상태를 BLOCKED로 설정
	strlcpy (t->name, name, sizeof t->name);	// 이름 복사
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);	// 스레드의 스택포인터를 설정한다
	t->priority = priority;		// 스레드의 우선순위를 설정
	t->magic = THREAD_MAGIC;	// 스레드가 올바르게 초기화되었음을 나타내는데 사용되는 MAGIC NUMBER를 설정
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;	// 실행 가능한 스레드가 없는 경우 idle 스레드 반환
	else
		// 실행 가능한 스레드 목록에서 첫번째 쓰레드를 제거하고 반환한다.
		return list_entry (list_pop_front (&ready_list), struct thread, elem);	
}

/* Use iretq to launch the thread */
// 쓰레드의 실행을 재개하는데 필요한 함수
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

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
   // 새로운 쓰레드로의 컨텍스트 스위칭
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

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
// 프로세스 스케쥴
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

// 쓰레드 스케쥴
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

/* Returns a tid to use for a new thread. */
// tid 할당
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

// thread의 상태를 block으로 만들고 schedule() 함수 실행
void thread_sleep(int64_t ticks) {
	// 현재 쓰레드 정보 가져오기
	struct thread *cur = thread_current();		// 현재 실행중인 쓰레드를 가져온다.
	// 만약 아이들이 아니면
	ASSERT(cur != idle_thread);
	// 현제 인터럽트 정보 가져올 자료형 선언
	enum intr_level old_level;
	old_level = intr_disable();					// 현재 인터럽트 레벨을 저장하고 인터럽트를 비활성화한다.
	// 쓰레드 상태 변경 (IDLE 쓰레드) -> block
	cur->status = THREAD_BLOCKED;
	schedule();
	// 인터럽트 재개
	intr_set_level (old_level);					// 인터럽트 레벨을 복원한다.
}
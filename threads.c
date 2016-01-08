#include "threads.h"
#include "string.h"
#include "time.h"

struct switch_stack_frame {
	unsigned long rflags;
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
	unsigned long rbx;
	unsigned long rbp;
	unsigned long entry;
	unsigned long thread;
	unsigned long fptr;
	unsigned long data;
} __attribute__((packed));


static struct thread bootstrap;
static struct thread *current_thread = &bootstrap;
static struct scheduler *scheduler;


void idle(void)
{ while (1) schedule(); }

static void preempt_thread(struct thread *thread)
{
	if (thread == &bootstrap)
		return;

	if (scheduler->preempt)
		scheduler->preempt(thread);
}

static void place_thread(struct thread *thread)
{
	struct thread *prev = current_thread;

	current_thread = thread;
	if (prev->state == THREAD_FINISHED)
		prev->state = THREAD_DEAD;

	if (thread == &bootstrap)
		return;

	if (scheduler->place)
		scheduler->place(thread);

	thread->time = jiffies();
}

void thread_entry(struct thread *thread, void (*fptr)(void *), void *data)
{
	place_thread(thread);

	local_preempt_enable();
	fptr(data);

	finish_thread();
}

struct thread *create_thread(void (*fptr)(void *), void *data,
			void *stack, size_t size)
{
	const size_t sz = sizeof(struct switch_stack_frame);

	if (size < sz)
		return 0;

	struct thread *thread = scheduler->alloc();

	if (!thread)
		return 0;

	thread->state = THREAD_NEW;

	void start_thread(void);

	struct switch_stack_frame *frame = (void *)((char *)stack + size - sz);

	memset(frame, 0, sizeof(*frame));
	frame->entry = (unsigned long)start_thread;
	frame->thread = (unsigned long)thread;
	frame->fptr = (unsigned long)fptr;
	frame->data = (unsigned long)data;
	thread->stack_pointer = frame;

	return thread;
}

void destroy_thread(struct thread *thread)
{
	wait_thread(thread);
	scheduler->free(thread);
}

void activate_thread(struct thread *thread)
{
	thread->state = THREAD_ACTIVE;
	scheduler->activate(thread);
}

void block_thread(void)
{
	const bool enabled = local_preempt_save();

	current_thread->state = THREAD_BLOCKED;
	schedule();
	local_preempt_restore(enabled);
}

void wait_thread(struct thread *thread)
{
	while (thread->state != THREAD_DEAD)
		schedule();
}

void finish_thread(void)
{
	local_preempt_disable();
	current_thread->state = THREAD_FINISHED;
	schedule();
}

struct thread *current(void)
{ return current_thread; }

static void switch_to(struct thread *next)
{
	struct thread *prev = current_thread;

	void switch_threads(void **prev, void *next);

	preempt_thread(prev);
	switch_threads(&prev->stack_pointer, next->stack_pointer);
	place_thread(prev);
}

static struct thread *next_thread(void)
{ return scheduler->next(); }

void schedule(void)
{
	const bool enabled = local_preempt_save();
	struct thread *thread = next_thread();

	if (thread == current_thread) {
		local_preempt_restore(enabled);
		return;
	}
	
	const bool force = (current_thread->state != THREAD_ACTIVE);

	if (!force && !thread) {
		local_preempt_restore(enabled);
		return;
	}

	switch_to(thread ? thread : &bootstrap);
	local_preempt_restore(enabled);
}

bool need_resched(void)
{ return scheduler->need_preempt(current_thread); }

void setup_threading(void)
{
	extern struct scheduler round_robin;
	void setup_round_robin(void);

	setup_round_robin();
	scheduler = &round_robin;
	bootstrap.state = THREAD_ACTIVE;
}

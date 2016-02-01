#include "thread_regs.h"
#include "threads.h"
#include "memory.h"
#include "kernel.h"
#include "string.h"
#include "paging.h"
#include "stdio.h"
#include "time.h"
#include "mm.h"

#include <stdint.h>


#ifndef CONFIG_KERNEL_STACK
#define KERNEL_STACK_ORDER 1
#else
#define KERNEL_STACK_ORDER CONFIG_KERNEL_STACK
#endif

struct switch_stack_frame {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t rbx;
	uint64_t rbp;
	uint64_t entry;
} __attribute__((packed));

struct thread_start_frame {
	struct switch_stack_frame frame;
	struct thread_regs regs;
} __attribute__((packed));


static struct thread *current_thread;
static struct thread bootstrap;
static struct scheduler *scheduler;

static void check_stack(struct thread *thread)
{
	const size_t stack_order = KERNEL_STACK_ORDER;
	const size_t stack_pages = 1ul << stack_order;
	const size_t stack_size = PAGE_SIZE * stack_pages;
	const char *begin = page_addr(thread->stack);

	if (((char *)thread->stack_pointer < begin) ||
			((char *)thread->stack_pointer >= begin + stack_size)) {
		DBG_ERR("thread %p out of stack", thread);
		DBG_ERR("\tstack_pointer %p", thread->stack_pointer);
		DBG_ERR("\tstack [%p-%p]", begin, begin + stack_size);
		while (1);
	}
}


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

	if (prev != &bootstrap)
		check_stack(prev);

	current_thread = thread;

	store_pml4(page_paddr(current_thread->mm->pt));

	if (prev->state == THREAD_FINISHED)
		prev->state = THREAD_DEAD;

	if (thread == &bootstrap)
		return;

	if (scheduler->place)
		scheduler->place(thread);

	thread->time = jiffies();
}

int kernel_thread_entry(struct thread *thread, int (*fptr)(void *),
			void *data)
{
	place_thread(thread);
	local_preempt_enable();
	return fptr(data);
}

static struct thread *__create_thread(int (*fptr)(void *), void *data,
			void *stack, size_t size)
{
	const size_t frame_size = sizeof(struct thread_start_frame);
	extern void __kernel_thread_entry(void);

	if (size < frame_size)
		return 0;

	struct thread *thread = scheduler->alloc();

	if (!thread)
		return 0;

	if (setup_thread_memory(thread)) {
		scheduler->free(thread);
		return 0;
	}

	struct thread_start_frame *frame =
				(void *)((char *)stack + size - frame_size);

	memset(frame, 0, sizeof(*frame));

	/* all kernel threads start in kernel_thread_entry */
	frame->frame.entry = (uint64_t)&__kernel_thread_entry;

	/* kernel_thread_entry arguments */
	frame->regs.rdi = (uint64_t)thread;
	frame->regs.rsi = (uint64_t)fptr;
	frame->regs.rdx = (uint64_t)data;

	/*
	 * after kernel_thread_entry finished we stil have thread_regs on
	 * the stack, initialize it to iret to finish_thread. Thread function
	 * can overwrite it to jump in userspace.
	 */
	frame->regs.ss = (uint64_t)KERNEL_DS;
	frame->regs.cs = (uint64_t)KERNEL_CS;
	frame->regs.rsp = (uint64_t)((char *)stack + size);
	frame->regs.rip = (uint64_t)&finish_thread; // finish thread on exit

	thread->stack_pointer = frame;
	thread->state = THREAD_NEW;

	return thread;
}

struct thread *create_thread(int (*fptr)(void *), void *arg)
{
	const size_t stack_order = KERNEL_STACK_ORDER;
	const size_t stack_pages = 1ul << KERNEL_STACK_ORDER;
	const size_t stack_size = PAGE_SIZE * stack_pages;

	struct page *stack = alloc_pages(stack_order);

	if (!stack)
		return 0;

	struct thread *thread = __create_thread(fptr, arg,
				page_addr(stack), stack_size);

	if (!thread)
		free_pages(stack, stack_order);
	else
		thread->stack = stack;

	return thread;
}

void destroy_thread(struct thread *thread)
{
	wait_thread(thread);
	release_thread_memory(thread);
	free_pages(thread->stack, KERNEL_STACK_ORDER);
	scheduler->free(thread);
}

void activate_thread(struct thread *thread)
{
	const bool enabled = local_preempt_save();

	DBG_ASSERT(thread->state != THREAD_ACTIVE);
	DBG_ASSERT(thread != &bootstrap);

	thread->state = THREAD_ACTIVE;
	scheduler->activate(thread);
	local_preempt_restore(enabled);
}

void wait_thread(struct thread *thread)
{
	while (thread->state != THREAD_DEAD) {
		barrier();
		schedule();
	}
}

void finish_thread(void)
{
	local_preempt_disable();
	current_thread->state = THREAD_FINISHED;
	schedule();
	DBG_ASSERT(0 && "Unreachable");
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
{
	if (current_thread == &bootstrap)
		return true;
	return scheduler->need_preempt(current_thread);
}

void setup_threading(void)
{
	extern struct scheduler round_robin;
	void setup_round_robin(void);

	setup_round_robin();
	scheduler = &round_robin;

	setup_mm();

	static struct mm mm;

	bootstrap.state = THREAD_ACTIVE;
	bootstrap.mm = &mm;
	mm.pt = pfn2page(load_pml4() >> PAGE_BITS);
	current_thread = &bootstrap;
}

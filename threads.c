#include "threads.h"
#include "kmem_cache.h"
#include "string.h"


struct switch_stack_frame {
	unsigned long rflags;
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
	unsigned long rbx;
	unsigned long rbp;
	unsigned long entry;
	unsigned long fptr;
	unsigned long data;
} __attribute__((packed));


static struct kmem_cache *thread_cache;
static struct thread bootstrap;
static struct thread *current_thread = &bootstrap;


void thread_entry(void (*fptr)(void *), void *data)
{ fptr(data); }

struct thread *create_thread(void (*fptr)(void *), void *data,
			void *stack, size_t size)
{
	const size_t sz = sizeof(struct switch_stack_frame);

	if (size < sz)
		return 0;

	struct thread *thread = kmem_cache_alloc(thread_cache);

	if (!thread)
		return 0;

	void start_thread(void);

	struct switch_stack_frame *frame = (void *)((char *)stack + size - sz);

	memset(frame, 0, sizeof(*frame));
	frame->entry = (unsigned long)start_thread;
	frame->fptr = (unsigned long)fptr;
	frame->data = (unsigned long)data;
	thread->stack_pointer = frame;
	return thread;
}

void destroy_thread(struct thread *thread)
{ kmem_cache_free(thread_cache, thread); }

struct thread *current(void)
{ return current_thread; }

#include "stdio.h"

void switch_to(struct thread *next)
{
	struct thread *prev = current_thread;

	if (prev == next)
		return;

	void switch_threads(void **prev, void *next);

	current_thread = next;
	switch_threads(&prev->stack_pointer, next->stack_pointer);
}

void setup_threading(void)
{ thread_cache = KMEM_CACHE(struct thread); }

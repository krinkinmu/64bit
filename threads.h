#ifndef __THREADS_H__
#define __THREADS_H__

#include <stdbool.h>
#include <stddef.h>

#include "interrupt.h"
#include "kernel.h"


static inline void local_preempt_disable(void)
{
	local_irq_disable();
	barrier();
}

static inline void local_preempt_enable(void)
{
	barrier();
	local_irq_enable();
}

static inline bool local_preempt_enabled(void)
{ return local_irq_enabled(); }

static inline bool local_preempt_disabled(void)
{ return local_irq_disabled(); }

static inline bool local_preempt_save(void)
{
	const bool enabled = local_preempt_enabled();

	local_preempt_disable();
	return enabled;
}

static inline void local_preempt_restore(bool enabled)
{
	if (enabled)
		local_preempt_enable();
}

enum thread_state {
	THREAD_NONE,
	THREAD_NEW,
	THREAD_ACTIVE,
	THREAD_BLOCKED,
	THREAD_FINISHED,
	THREAD_DEAD
};

struct thread {
	void *stack_pointer;
	unsigned long long time;
	enum thread_state state;
	struct page *stack;
	struct mm *mm;
};

struct scheduler {
	struct thread *(*alloc)(void);
	void (*free)(struct thread *);
	void (*activate)(struct thread *);
	bool (*need_preempt)(struct thread *);
	struct thread *(*next)(void);
	void (*preempt)(struct thread *);
	void (*place)(struct thread *);
};

struct thread *create_thread(void (*fptr)(void *), void *data);
void destroy_thread(struct thread *thread);
void activate_thread(struct thread *thread);
void block_thread(void);
void finish_thread(void);
void wait_thread(struct thread *thread);

struct thread *current(void);
void schedule(void);
bool need_resched(void);

void idle(void);
void setup_threading(void);

#endif /*__THREADS_H__*/

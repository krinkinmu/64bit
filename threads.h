#ifndef __THREADS_H__
#define __THREADS_H__

#include <stdbool.h>
#include <stddef.h>

enum thread_state {
	THREAD_NONE,
	THREAD_NEW,
	THREAD_ACTIVE,
	THREAD_BLOCKED,
	THREAD_FINISHED
};

struct thread {
	void *stack_pointer;
	unsigned long long time;
	enum thread_state state;
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

struct thread *create_thread(void (*fptr)(void *), void *data,
			void *stack, size_t size);
void destroy_thread(struct thread *thread);
void activate_thread(struct thread *thread);
void block_thread(void);
void finish_thread(void);

struct thread *current(void);
void schedule(void);
bool need_resched(void);

void idle(void);
void setup_threading(void);

#endif /*__THREADS_H__*/

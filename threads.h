#ifndef __THREADS_H__
#define __THREADS_H__

#include <stddef.h>

struct thread {
	void *stack_pointer;
};

struct thread *create_thread(void (*fptr)(void *), void *data,
			void *stack, size_t size);
void destroy_thread(struct thread *thread);

struct thread *current(void);
void switch_to(struct thread *thread);

void setup_threading(void);

#endif /*__THREADS_H__*/

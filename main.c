#include "kmem_cache.h"
#include "interrupt.h"
#include "threads.h"
#include "memory.h"
#include "paging.h"
#include "time.h"
#include "vga.h"

#include "stdio.h"

static void thread0_function(void *unused)
{
	static volatile unsigned count;

	(void) unused;

	while (1) {
		puts("thread0");
		count = 0;
		while (++count != 100000)
			(void) count;
	}
}

static void thread1_function(void *unused)
{
	static volatile unsigned count;

	(void) unused;

	while (1) {
		puts("thread1");
		count = 0;
		while (++count != 100000)
			(void) count;
	}
}

void main(void)
{
	setup_vga();
	setup_ints();
	setup_memory();
	setup_buddy();
	setup_paging();
	setup_alloc();
	setup_time();
	setup_threading();


	static char thread0_stack[4096];
	struct thread *thread0 = create_thread(thread0_function, 0,
				thread0_stack, sizeof(thread0_stack));
	activate_thread(thread0);


	static char thread1_stack[4096];
	struct thread *thread1 = create_thread(thread1_function, 0,
				thread1_stack, sizeof(thread1_stack));
	activate_thread(thread1);

	local_irq_enable();
	idle();
}

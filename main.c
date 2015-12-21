#include "kmem_cache.h"
#include "interrupt.h"
#include "threads.h"
#include "memory.h"
#include "paging.h"
#include "vga.h"

#include "stdio.h"

static void other_thread(void *tptr)
{
	struct thread *main = tptr;

	while (1) {
		puts("other thread");
		switch_to(main);
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
	setup_threading();

	static char other_stack[4096];
	struct thread *other = create_thread(other_thread, current(),
				other_stack, sizeof(other_stack));
	while (1) {
		puts("main thread");
		switch_to(other);
	}
}

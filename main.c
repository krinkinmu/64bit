#include "kmem_cache.h"
#include "interrupt.h"
#include "threads.h"
#include "memory.h"
#include "paging.h"
#include "error.h"
#include "ramfs.h"
#include "time.h"
#include "misc.h"
#include "ide.h"
#include "vga.h"
#include "vfs.h"

static void start(void *dummy)
{
	(void) dummy;

	setup_ramfs();
	setup_ide();

	while (1);
}

void main(void)
{
	setup_vga();
	setup_misc();
	setup_ints();
	setup_memory();
	setup_buddy();
	setup_paging();
	setup_alloc();
	setup_time();
	setup_threading();
	setup_vfs();

	/* start first real kernel thread */
	struct page *stack = alloc_pages(1, NT_LOW);
	void *vaddr = kernel_virt(page2pfn(stack) << PAGE_BITS);
	struct thread *start_thread = create_thread(&start, 0, vaddr,
					2 * PAGE_SIZE);
	activate_thread(start_thread);
	local_preempt_enable();
	idle();
}

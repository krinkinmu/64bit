#include "kmem_cache.h"
#include "interrupt.h"
#include "threads.h"
#include "memory.h"
#include "paging.h"
#include "error.h"
#include "ramfs.h"
#include "time.h"
#include "ide.h"
#include "vga.h"
#include "vfs.h"

static void start(void *dummy)
{
	(void) dummy;

	setup_vfs();
	setup_ramfs();
	setup_ide();
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

	struct page *stack = alloc_pages(1, NT_LOW);
	void *vaddr = kernel_virt(page2pfn(stack) << PAGE_BITS);
	struct thread *start_thread = create_thread(&start, 0, vaddr,
					2 * PAGE_SIZE);
	activate_thread(start_thread);

	idle();
}

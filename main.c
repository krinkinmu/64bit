#include "kmem_cache.h"
#include "initramfs.h"
#include "interrupt.h"
#include "threads.h"
#include "memory.h"
#include "paging.h"
#include "serial.h"
#include "ramfs.h"
#include "time.h"
#include "misc.h"
#include "ide.h"
#include "vga.h"
#include "vfs.h"

static void test_function(void *dummy)
{
	(void) dummy;
}

static void test_threading(void)
{
	DBG_INFO("start threading test");
	for (int i = 0; i != 10000; ++i) {
		struct thread *thread = create_thread(&test_function, 0);

		activate_thread(thread);
		wait_thread(thread);
		destroy_thread(thread);
	}
	DBG_INFO("finish threading test");
}

static void test_page_fault(void)
{
	const virt_t vaddr = BIT_CONST(32);
	volatile char *ptr = (char *)vaddr;

	DBG_INFO("generate page fault");
	*ptr = 13;
	DBG_ERR("no page fault");
}

static void start(void *dummy)
{
	(void) dummy;

	setup_ramfs();
	setup_initramfs();
	setup_ide(); // we aren't going to use it in near future
	test_threading();
	test_page_fault();

	while (1);
}

void main(void)
{
	setup_serial();
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
	struct thread *start_thread = create_thread(&start, 0);
	activate_thread(start_thread);
	local_preempt_enable();
	idle();
}

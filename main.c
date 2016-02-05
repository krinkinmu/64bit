#include "kmem_cache.h"
#include "initramfs.h"
#include "interrupt.h"
#include "threads.h"
#include "memory.h"
#include "paging.h"
#include "serial.h"
#include "error.h"
#include "ramfs.h"
#include "time.h"
#include "misc.h"
#include "exec.h"
#include "ide.h"
#include "vga.h"
#include "vfs.h"
#include "mm.h"

static int test_function(void *dummy)
{
	(void)dummy;
	return 0;
}

static void test_threading(void)
{
	DBG_INFO("start threading test");
	for (int i = 0; i != 10000; ++i) {
		const pid_t pid = create_kthread(&test_function, 0);

		DBG_ASSERT(pid >= 0);
		wait_thread(pid);
	}
	DBG_INFO("finish threading test");
}

static void test_exec(void)
{
	static const char test[] = "/initramfs/test";

	const int rc = exec(test);

	if (rc)
		DBG_ERR("exec failed with error %s", errstr(rc));
	else
		DBG_INFO("exec finished successfully");
}

static void test_page_fault(void)
{
	DBG_INFO("start page fault test");

	int rc = mmap(PAGE_SIZE, 2 * PAGE_SIZE, 0);
	char *ptr = (char *)PAGE_SIZE;

	if (rc)
		DBG_ERR("mmap failed with error %s", errstr(rc));

	for (int i = 0; i != PAGE_SIZE; ++i)
		DBG_ASSERT(ptr[i] == 0);
	munmap(PAGE_SIZE, 2 * PAGE_SIZE);

	rc = mmap(PAGE_SIZE, 2 * PAGE_SIZE, VMA_PERM_WRITE);

	if (rc)
		DBG_ERR("mmap failed with error %s", errstr(rc));

	for (int i = 0; i != PAGE_SIZE; ++i)
		ptr[i] = (char)i;

	for (int i = 0; i != PAGE_SIZE; ++i)
		DBG_ASSERT(ptr[i] == (char)i);
	munmap(PAGE_SIZE, 2 * PAGE_SIZE);

	DBG_INFO("finish page fault test");
}

static int start(void *dummy)
{
	(void) dummy;

	setup_ramfs();
	setup_initramfs();
	setup_ide(); // we aren't going to use it in near future
	test_threading();
	test_page_fault();
	test_exec();

	DBG_INFO("Jump to userspace!!!");
	return 0;
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
	create_kthread(&start, 0);
	local_preempt_enable();
	idle();
}

#include "kmem_cache.h"
#include "interrupt.h"
#include "threads.h"
#include "memory.h"
#include "paging.h"
#include "error.h"
#include "ramfs.h"
#include "time.h"
#include "vga.h"
#include "vfs.h"

#include "stdio.h"

static void test_deadlock(void)
{
	DEFINE_MUTEX(deadlock);	
	mutex_lock(&deadlock);
	mutex_lock(&deadlock);
	DBG_ERR("Deadlock test failed");
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

	local_irq_enable();

	setup_vfs();
	setup_ramfs();

	test_deadlock();

	idle();
}

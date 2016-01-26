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

static void start(void *dummy)
{
	(void) dummy;

	setup_ramfs();
	setup_initramfs();
	setup_ide(); // we aren't going to use it in near future

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

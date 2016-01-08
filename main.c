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
	setup_vfs();
	setup_ramfs();

	local_irq_enable();
	idle();
}

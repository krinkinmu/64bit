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

static int check_all_zero(const char *data, size_t size)
{
	for (size_t i = 0; i != size; ++i)
		if (data[i])
			return 0;
	return 1;
}

static void test_ide(void)
{
	struct page *page = alloc_pages(0, NT_LOW);
	struct bio *bio = bio_alloc();

	bio->dir = BIO_READ;
	bio->map.page = page;
	bio->map.sector = 0;
	bio->map.offset = 0;
	bio->map.length = PAGE_SIZE;

	DBG_INFO("Submit bio");
	bio_submit(bio);
	DBG_INFO("Wait bio");
	bio_wait(bio);

	if (bio->status == BIO_FINISHED) {
		DBG_INFO("read from the 0 sector finished");
		const pfn_t pfn = page2pfn(page);
		DBG_INFO("page pfn %lu", pfn);
		const char *data = kernel_virt(pfn << PAGE_BITS);
		DBG_INFO("page vaddr %lx", (unsigned long)data);

		if (check_all_zero(data, PAGE_SIZE))
			DBG_INFO("read zero-initialized data");
		else
			DBG_ERR("data isn't zero initialized");
	} else {
		DBG_ERR("read from the 0 sector failed");
	}

	bio_free(bio);
}

static void start(void *data)
{
	(void) data;

	setup_vfs();
	setup_ramfs();
	setup_ide();
	test_ide();
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

	static unsigned long stack[512];
	struct thread *st = create_thread(&start, 0, stack, sizeof(stack));
	activate_thread(st);

	local_irq_enable();

	idle();
}

#include "kmem_cache.h"
#include "interrupt.h"
#include "memory.h"
#include "paging.h"
#include "stdio.h"
#include "vga.h"

struct the_data {
	char a;
	struct the_data *self;
	struct the_data *next;
	char b;
};

void dump_buddy_state(void);

static void test_kmem_cache(void)
{
	struct kmem_cache *cache = KMEM_CACHE(struct the_data);
	struct the_data *next = 0;

	if (!cache)
		puts("Cache allocation failed");

	for (int i = 0; i != 100000; ++i) {
		struct the_data *ptr = kmem_cache_alloc(cache);

		if (!ptr) {
			printf("Cannot allocate %d-th the_data\n", i);
			break;
		}

		ptr->self = ptr;
		ptr->next = next;
		next = ptr;
	}

	dump_buddy_state();

	while (next) {
		struct the_data *ptr = next;

		next = next->next;
		kmem_cache_free(cache, ptr);
	}

	kmem_cache_destroy(cache);
}

void main(void)
{
	setup_vga();
	setup_ints();
	setup_memory();
	setup_buddy();
	setup_paging();
	setup_kmem_cache();

	dump_buddy_state();
	test_kmem_cache();

	while (1);
}

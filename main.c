#include "kmem_cache.h"
#include "interrupt.h"
#include "memory.h"
#include "paging.h"
#include "vga.h"

#include "string.h"
#include "stdio.h"

struct the_small_data {
	char a;
	struct the_small_data *self;
	struct the_small_data *next;
	char b;
};

struct the_large_data {
	struct the_large_data *next;
	char data[4096];
};

void dump_buddy_state(void);

static void test_small_kmem(void)
{
	struct kmem_cache *cache = KMEM_CACHE(struct the_small_data);
	struct the_small_data *next = 0;

	for (int i = 0; i != 1000000; ++i) {
		struct the_small_data *ptr = kmem_cache_alloc(cache);

		if (!ptr) {
			printf("Cannot allocate %d-th the_small_data\n", i);
			break;
		}

		ptr->self = ptr;
		ptr->next = next;
		next = ptr;
	}

	while (next) {
		struct the_small_data *ptr = next;

		next = next->next;
		kmem_cache_free(cache, ptr);
	}
	kmem_cache_destroy(cache);
}

static void test_large_kmem(void)
{
	struct the_large_data *next = 0;

	for (int i = 0; i != 10000; ++i) {
		struct the_large_data *ptr = kmem_alloc(sizeof(*ptr));

		if (!ptr) {
			printf("Cannot allocate %d-th the_large_data\n", i);
			break;
		}

		memset(ptr->data, 0x13, sizeof(ptr->data));

		ptr->next = next;
		next = ptr;
	}

	while (next) {
		struct the_large_data *ptr = next;

		next = next->next;
		kmem_free(ptr);
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

	dump_buddy_state();

	test_small_kmem();
	test_large_kmem();

	dump_buddy_state();

	while (1);
}

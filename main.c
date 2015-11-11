#include "balloc.h"
#include "stdio.h"
#include "vga.h"

struct mmap_entry {
	unsigned size;
	unsigned long long addr;
	unsigned long long length;
	unsigned type;
} __attribute__((__packed__));

static void print_mmap(const char *ptr)
{
	const char *mmap = ptr;

	while (1) {
		const struct mmap_entry *entry =
					(const struct mmap_entry *)mmap;

		if (!entry->size)
			break;

		mmap += entry->size + sizeof(entry->size);

		if (entry->type == 1)
			balloc_add_area(entry->addr, entry->length);
		else
			balloc_reserve_area(entry->addr, entry->length);
	}

	balloc_print_areas();
}

void main(const void *ptr, const char *cmdline)
{
	setup_vga();
	printf("cmdline: %s\n", cmdline);
	print_mmap(ptr);

	while (1);
}

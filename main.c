#include "balloc.h"
#include "string.h"
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
	}

	balloc_print_areas();
}

void main(const void *ptr, const char *cmdline)
{
	char *cmd;
	setup_vga();
	print_mmap(ptr);

	cmd = balloc_alloc(0, ~0, strlen(cmdline) + 1);
	memcpy(cmd, cmdline, strlen(cmdline) + 1);
	printf("cmdline: %s\n", cmd);
	balloc_print_areas();
	balloc_free(cmd);
	balloc_print_areas();

	while (1);
}

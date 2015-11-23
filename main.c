#include "balloc.h"
#include "memory.h"
#include "stdio.h"
#include "vga.h"

void main(const void *ptr, const char *cmdline)
{
	setup_vga();

	printf("cmdline: %s\n", cmdline);
	setup_memory(ptr);
	dump_buddy_allocator_state();

	while (1);
}

#include "interrupt.h"
#include "memory.h"
#include "stdio.h"
#include "vga.h"

void main(void)
{
	setup_vga();
	setup_memory();
	setup_int();
	setup_buddy();
	dump_buddy_allocator_state();

	while (1);
}

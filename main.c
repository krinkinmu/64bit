#include "interrupt.h"
#include "memory.h"
#include "paging.h"
#include "vga.h"

void main(void)
{
	setup_vga();
	setup_ints();
	setup_memory();
	setup_buddy();
	setup_paging();

	while (1);
}

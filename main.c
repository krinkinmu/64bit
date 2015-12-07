#include "interrupt.h"
#include "memory.h"
#include "vga.h"

void main(void)
{
	setup_vga();
	setup_ints();
	setup_memory();
	setup_buddy();

	while (1);
}

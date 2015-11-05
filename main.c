#include "stdio.h"
#include "vga.h"

void main(void *ptr, const char *cmdline)
{
	setup_vga();
	printf("memory map pointer: %p\n", ptr);
	printf("cmdline: %s\n", cmdline);

	while (1);
}

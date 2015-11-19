#include "balloc.h"
#include "stdio.h"
#include "vga.h"

void main(const void *ptr, const char *cmdline)
{
	setup_vga();

	printf("cmdline: %s\n", cmdline);
	balloc_build_mmap(ptr);

	while (1);
}

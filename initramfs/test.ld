OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(start)

PAGE_SIZE = 0x1000;

SECTIONS
{
	. = 1M + SIZEOF_HEADERS;

	.text : { *(.text) }
	.bss : { . += 2 * PAGE_SIZE; }
}

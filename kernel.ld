OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(start32)

SECTIONS
{
	. = 1M + SIZEOF_HEADERS;

	text_begin = .;
	.header : { *(.header) }
	.text : { *(.text) }
	data_begin = .;
	.rodata : { *(.rodata) }
	.data : { *(.data) }
	bss_begin = .;
	.bss : { *(.bss) }
	. = ALIGN(0x1000);
	bss_end = .;
}

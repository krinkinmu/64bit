OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(start32)

SECTIONS
{
	. = 1M + SIZEOF_HEADERS;

	text_begin = .;
	.header : { *(.header) }
	.text : { *(.text) }

	. = ALIGN(0x1000);
	data_begin = .;
	.rodata : { *(.rodata) }
	.data : { *(.data) }
	data_end = .;

	. = ALIGN(0x1000);
	bss_begin = .;
	.bss : { *(.bss) }
	bss_end = .;
}

#include "stdio.h"
#include "vga.h"

static void ultoa(unsigned long value, char *str, int base)
{
	static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

	unsigned pos = 0, i;

	do {
		str[pos++] = digits[value % base];
		value /= base;
	} while (value);

	for (i = 0; i != pos / 2; ++i) {
		const char tmp = str[i];
		str[i] = str[pos - i - 1];
		str[pos - i - 1] = tmp;
	}
	str[pos] = 0;
}

void main(void *ptr, const char *cmdline)
{
	(void) ptr;
	(void) ultoa;

	setup_vga();
	puts(cmdline);

	while (1);
}

#include "console.h"
#include "string.h"
#include "stdio.h"

int putchar(int c)
{
	const char ch = c;

	console_write(&ch, 1);
	return 0;
}

int puts(const char *str)
{
	console_write(str, strlen(str));
	putchar('\n');
	return 0;
}

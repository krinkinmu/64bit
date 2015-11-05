#include "console.h"
#include "string.h"
#include "stdio.h"
#include "vsnprintf.h"

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

int printf(const char *fmt, ...)
{
	char buffer[1024];
	va_list args;
	int cs;

	va_start(args, fmt);
	cs = vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	console_write(buffer, strlen(buffer));

	return cs;
}

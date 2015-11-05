#include "string.h"

void *memcpy(void *dst, const void *src, size_t size)
{
	char *d = dst;
	const char *s = src;

	while (size--)
		*d++ = *s++;
	return dst;
}

size_t strlen(const char *str)
{
	const char *pos = str;

	while (*pos) ++pos;
	return pos - str;
}

char *strchr(const char *str, int c)
{
	while (*str && *str != c)
		++str;
	return *str ? (char *)str : 0;
}

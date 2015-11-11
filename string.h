#ifndef __STRING_H__
#define __STRING_H__

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t size);
void *memmove(void *dst, const void *src, size_t size);
size_t strlen(const char *str);
char *strchr(const char *str, int c);

#endif /*__STRING_H__*/

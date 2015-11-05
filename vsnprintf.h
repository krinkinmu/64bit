#ifndef __VSNPRINTF_H__
#define __VSNPTRINF_H__

#include <stddef.h>
#include <stdarg.h>

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif /*__VSNPRINTF_H__*/

#ifndef __VSNPRINTF_H__
#define __VSNPRINTF_H__

#include <stddef.h>
#include <stdarg.h>

struct vsnprintf_sink {
	void (*write)(struct vsnprintf_sink *, const char *, size_t);
};

void __vsnprintf(struct vsnprintf_sink *sink, const char *fmt, va_list args);

#endif /*__VNSPRINTF_H__*/

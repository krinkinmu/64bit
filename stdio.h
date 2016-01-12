#ifndef __STDIO_H__
#define __STDIO_H__

#include <stddef.h>
#include <stdarg.h>

int putchar(int c);
int puts(const char *str);
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list args);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

/* TODO: move it somewhere else? */
#include "kernel.h"

#ifndef CONFIG_MIN_DEBUG_LEVEL
#define CONFIG_MIN_DEBUG_LEVEL 1
#endif /* CONFIG_MIN_DEBUG_LEVEL */

void dbg_printf(const char *pref, const char *file, int line,
			const char *fmt, ...);

#if CONFIG_MIN_DEBUG_LEVEL <= 0
#define DBG_INFO(...) dbg_printf("INF", __FILE__, __LINE__, __VA_ARGS__)
#else
#define DBG_INFO(...) do {} while (0)
#endif

#if CONFIG_MIN_DEBUG_LEVEL <= 1
#define DBG_WARN(...) dbg_printf("WRN", __FILE__, __LINE__, __VA_ARGS__)
#else
#define DBG_WARN(...) do {} while (0)
#endif

#if CONFIG_MIN_DEBUG_LEVEL <= 2
#define DBG_ERR(...) dbg_printf("ERR", __FILE__, __LINE__, __VA_ARGS__)
#else
#define DBG_ERR(...) do {} while (0)
#endif

#define DBG_TRACE_ENTER DBG_INFO("Enter %s", __func__)
#define DBG_TRACE_LEAVE DBG_INFO("Leave %s", __func__)
#define DBG_ASSERT(cond)					\
	do {							\
		if (!(cond)) {					\
			DBG_ERR("Condition %s failed", #cond);	\
			while (1);				\
		}						\
	} while (0)

#endif /*__STDIO_H__*/

#ifndef __KERNEL_H__
#define __KERNEL_H__

#include <stddef.h>
#include <stdint.h>

#define CONTAINER_OF(ptr, type, member) \
	(type *)( (char *)(ptr) - offsetof(type, member) )


#define ALIGNOF(type) offsetof(struct { char c; type member; }, member)

static inline uintmax_t __align_down(uintmax_t x, uintmax_t a)
{ return x - (x % a); } // TODO: should we get rid of uintmax_t division?

static inline uintmax_t __align_up(uintmax_t x, uintmax_t a)
{ return __align_down(x + a - 1, a); }

#define ALIGN(x, align)      __align_up(x, align)
#define ALIGN_DOWN(x, align) __align_down(x, align)

#endif /*__KERNEL_H__*/

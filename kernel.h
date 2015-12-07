#ifndef __KERNEL_H__
#define __KERNEL_H__

#include <stddef.h>
#include <stdint.h>

#define CONTAINER_OF(ptr, type, member) \
	(type *)( (char *)(ptr) - offsetof(type, member) )


#define ALIGNOF(type) offsetof(struct { char c; type member; }, member)

#define MIN_CONST(l, r)    ((l) < (r) ? (l) : (r))
#define MAX_CONST(l, r)    ((l) > (r) ? (l) : (r))
#define BITS_CONST(hi, lo) ((((uintmax_t)1 << ((hi) + 1)) - 1) & ((~(uintmax_t)0) << (lo)))

static inline uintmax_t __align_down(uintmax_t x, uintmax_t a)
{ return x - (x % a); } // TODO: should we get rid of uintmax_t division?

static inline uintmax_t __align_up(uintmax_t x, uintmax_t a)
{ return __align_down(x + a - 1, a); }

static inline uintmax_t __bits(int hi, int lo)
{ return BITS_CONST(hi, lo); }

static inline intmax_t __min(intmax_t l, intmax_t r)
{ return MIN_CONST(l, r); }

static inline uintmax_t __minu(uintmax_t l, uintmax_t r)
{ return MIN_CONST(l, r); }

static inline intmax_t __max(intmax_t l, intmax_t r)
{ return MAX_CONST(l, r); }

static inline uintmax_t __maxu(uintmax_t l, uintmax_t r)
{ return MAX_CONST(l, r); }

#define ALIGN(x, align)      __align_up(x, align)
#define ALIGN_DOWN(x, align) __align_down(x, align)
#define BITS(hi, lo)         __bits(hi, lo)
#define MIN(l, r)            __min(l, r)
#define MINU(l, r)           __minu(l, r)
#define MAX(l, r)            __max(l, r)
#define MAXU(l, r)           __maxu(l, r)

#endif /*__KERNEL_H__*/

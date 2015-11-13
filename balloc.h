#ifndef __BOOT_ALLOCATOR_H__
#define __BOOT_ALLOCATOR_H__

#include <stddef.h>

void balloc_add_area(unsigned long long addr, unsigned long long size);
void balloc_print_areas(void);

void *balloc_alloc_aligned(unsigned long long low, unsigned long long high,
			size_t size, size_t align);
void *balloc_alloc(unsigned long long low, unsigned long long high,
			size_t size);
void balloc_free(const void *ptr);

#endif /*__BOOT_ALLOCATOR_H__*/

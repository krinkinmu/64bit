#ifndef __BOOT_ALLOCATOR_H__
#define __BOOT_ALLOCATOR_H__

#include <stddef.h>

#define MMAP_AVAILABLE 1

struct mmap_entry {
	unsigned size;
	unsigned long long addr;
	unsigned long long length;
	unsigned type;
} __attribute__((__packed__));

void setup_memory(const struct mmap_entry *entry);
void *balloc_alloc_aligned(unsigned long long low, unsigned long long high,
			size_t size, size_t align);
void *balloc_alloc(unsigned long long low, unsigned long long high,
			size_t size);
void balloc_free(const void *ptr);
int balloc_is_free(unsigned long long addr, size_t size);

#endif /*__BOOT_ALLOCATOR_H__*/

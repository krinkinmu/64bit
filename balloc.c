#include "kernel.h"
#include "balloc.h"
#include "string.h"
#include "stdio.h"

#define MAX_AREAS_COUNT 128

struct balloc_area {
	unsigned long long addr;
	unsigned long long size;
};

struct balloc_pool {
	struct balloc_area *areas;
	int max_size;
	int size;
};

struct balloc_alloc_header {
	size_t size;
};

static struct balloc_area available_areas[MAX_AREAS_COUNT];
static struct balloc_pool avail = { available_areas, MAX_AREAS_COUNT, 0 };

static int balloc_lower_bound(const struct balloc_pool *pool,
			unsigned long long addr)
{
	int left = 0, right = pool->size;

	while (left < right) {
		const int mid = left + (right - left) / 2;
		const struct balloc_area *area = &(pool->areas[mid]);
		const unsigned long long end = area->addr + area->size;

		if (end < addr)
			left = mid + 1;
		else
			right = mid;
	}

	return left;
}

static int balloc_upper_bound(const struct balloc_pool *pool,
			unsigned long long addr)
{
	int left = 0, right = pool->size;

	while (left < right) {
		const int mid = left + (right - left) / 2;
		const struct balloc_area *area = &(pool->areas[mid]);
		const unsigned long long begin = area->addr;

		if (begin > addr)
			right = mid;
		else
			left = mid + 1;
	}

	return left;
}

static void balloc_pool_insert(struct balloc_pool *pool,
			unsigned long long addr, unsigned long long size)
{
	const int first = balloc_lower_bound(pool, addr);
	const int last = balloc_upper_bound(pool, addr + size);

	if (first != last) {
		struct balloc_area *last_area;

		if (pool->areas[first].addr < addr)
			addr = pool->areas[first].addr;

		last_area = &(pool->areas[last - 1]);
		if (size < last_area->addr + last_area->size - addr)
			size = last_area->addr + last_area->size - addr;
	}

	if (first + 1 != last) {
		const size_t sz = (pool->size - last) * sizeof(*pool->areas);

		memmove(&(pool->areas[first + 1]), &(pool->areas[last]), sz);
	}

	pool->areas[first].addr = addr;
	pool->areas[first].size = size;
	pool->size -= last - first - 1;
}

static void balloc_pool_delete(struct balloc_pool *pool,
			unsigned long long addr, unsigned long long size)
{
	const int pos = balloc_lower_bound(pool, addr);
	const size_t sz = (pool->size - pos - 1) * sizeof(*pool->areas);
	struct balloc_area *area = &(pool->areas[pos]);

	if (area->addr == addr && area->size > size) {
		area->addr += size;
		area->size -= size;
		return;
	}

	if (area->addr + area->size == addr + size && area->size > size) {
		area->size -= size;
		return;
	}

	if (area->addr == addr && area->size == size) {
		memmove(area, area + 1, sz);
		--pool->size;
		return;
	}

	memmove(area + 2, area + 1, sz);
	(area + 1)->addr = addr + size;
	(area + 1)->size = area->addr + area->size - (area + 1)->addr;
	area->size = addr - area->addr;
	++pool->size;
}

static void *balloc_alloc_aligned_from_pool(struct balloc_pool *pool,
			unsigned long long low, unsigned long long high,
			size_t size, size_t align)
{
	static const size_t sz = sizeof(struct balloc_alloc_header);

	const struct balloc_alloc_header header = { size };
	const int first = balloc_lower_bound(pool, low);
	const int last = balloc_upper_bound(pool, high);
	int i;

	for (i = first; i != last; ++i) {
		struct balloc_area *area = &(pool->areas[i]);
		unsigned long long start = area->addr;
		unsigned long long end = start + area->size;

		if (start < low) start = low;
		if (end > high) end = high;

		start = ALIGN(start + sz, align);
		if (start >= end || end - start < size)
			continue;

		memcpy((void *)(start - sz), &header, sz);
		balloc_pool_delete(pool, start - sz, size + sz);
		return (void *)start;
	}
	return 0;
}

static void balloc_free_to_pool(struct balloc_pool *pool, const void *ptr)
{
	static const size_t sz = sizeof(struct balloc_alloc_header);
	
	const char *addr = (const char *)ptr - sz;
	struct balloc_alloc_header header;
	unsigned long long size;

	memcpy(&header, addr, sz);
	size = header.size;
	balloc_pool_insert(pool, (unsigned long long)addr, size + sz);
}

void *balloc_alloc_aligned(unsigned long long low, unsigned long long high,
			size_t size, size_t align)
{
	if (low >= high || high - low < size)
		return 0;

	return balloc_alloc_aligned_from_pool(&avail, low, high, size, align);
}

void *balloc_alloc(unsigned long long low, unsigned long long high, size_t size)
{ return balloc_alloc_aligned(low, high, size, ALIGNOF(uintmax_t)); }

void balloc_free(const void *ptr)
{ balloc_free_to_pool(&avail, ptr); }

void balloc_add_area(unsigned long long addr, unsigned long long size)
{ balloc_pool_insert(&avail, addr, size); }

void balloc_print_areas(void)
{
	int i;

	for (i = 0; i != avail.size; ++i)
		printf("available memory region: %#llx-%#llx\n",
			avail.areas[i].addr,
			avail.areas[i].addr + avail.areas[i].size - 1);
}

#include "kmem_cache.h"
#include "memory.h"
#include "list.h"


struct kmem_border_tag {
	struct kmem_border_tag *next;
};

struct kmem_small_slab {
	struct list_head link;
	struct kmem_border_tag *free_list;
	struct kmem_small_cache *cache;
	struct page *pages;
	unsigned short total;
	unsigned short free;
};

struct kmem_small_cache {
	struct list_head free_list;
	struct list_head part_list;
	struct list_head full_list;
	size_t object_size;
	size_t padded_size;
	int order;
};

struct kmem_tag {
	struct kmem_tag *next;
	void *ptr;
};

struct kmem_slab {
	struct list_head link;
	struct kmem_tag *free;
	struct kmem_tag *busy;
	struct kmem_cache *cache;
	struct page *pages;
};

struct kmem_cache {
	struct list_head free_list;
	struct list_head part_list;
	struct list_head full_list;
	size_t object_size;
	int order;
};

static struct kmem_small_cache kmem_small_cache_cache;
static struct kmem_small_cache kmem_cache_cache;
static struct kmem_small_cache kmem_slab_cache;
static struct kmem_small_cache kmem_tag_cache;

static const size_t kmem_size[] = {
	16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768
};

#define KMEM_POOLS (sizeof(kmem_size)/sizeof(kmem_size[0]))
static struct kmem_cache *kmem_pool[KMEM_POOLS];


static int kmem_cache_index(size_t size)
{
	for (int i = 0; i != KMEM_POOLS; ++i) {
		if (size <= kmem_size[i])
			return i;
	}
	return -1;
}

static struct kmem_small_slab *kmem_get_small_slab(void *ptr)
{
	const pfn_t pfn = KERNEL_PHYS(ptr) >> PAGE_BITS;
	
	return pfn2page(pfn)->u.small_slab;
}

static struct kmem_slab *kmem_get_slab(void *ptr)
{
	const pfn_t pfn = KERNEL_PHYS(ptr) >> PAGE_BITS;

	return pfn2page(pfn)->u.slab;
}

static void *kmem_small_slab_alloc(struct kmem_small_cache *cache,
			struct kmem_small_slab *slab)
{
	struct kmem_border_tag *tag = slab->free_list;

	slab->free_list = tag->next;
	--slab->free;

	return ((char *)tag) - cache->object_size;
}

static void *kmem_slab_alloc(struct kmem_slab *slab)
{
	struct kmem_tag *tag = slab->free;
	void *ptr = tag->ptr;

	slab->free = tag->next;
	tag->next = slab->busy;
	slab->busy = tag;

	return ptr;
}

static void kmem_small_slab_free(struct kmem_small_cache *cache,
			struct kmem_small_slab *slab, char *ptr)
{
	struct kmem_border_tag *tag = (void *)(ptr + cache->object_size);

	tag->next = slab->free_list;
	slab->free_list = tag;
	++slab->free;
}

static void kmem_slab_free(struct kmem_slab *slab, void *ptr)
{
	struct kmem_tag *tag = slab->busy;

	slab->busy = tag->next;
	tag->next = slab->free;
	tag->ptr = ptr;
	slab->free = tag;
}

static bool kmem_small_cache_grow(struct kmem_small_cache *cache)
{
	struct page *page = alloc_pages(cache->order);

	if (!page)
		return false;

	const pfn_t pages = (pfn_t)1 << cache->order;
	const size_t size = PAGE_SIZE * pages;
	const size_t off = size - sizeof(struct kmem_small_slab);

	char *vaddr = kernel_virt(page2pfn(page) << PAGE_BITS);
	struct kmem_small_slab *slab = (struct kmem_small_slab *)(vaddr + off);

	list_init(&slab->link);
	slab->cache = cache;
	slab->pages = page;
	slab->free_list = 0;
	slab->total = 0;
	slab->free = 0;

	const size_t sz = cache->padded_size;

	for (char *ptr = vaddr; ptr + sz <= (char *)slab; ptr += sz) {
		kmem_small_slab_free(cache, slab, ptr);
		++slab->total;
	}

	for (pfn_t i = 0; i != pages; ++i)
		page[i].u.small_slab = slab;

	list_add_tail(&slab->link, &cache->free_list);

	return true;
}

static void kmem_slab_release(struct kmem_cache *cache, struct kmem_slab *slab)
{	
	while (slab->free) {
		struct kmem_tag *tag = slab->free;

		slab->free = tag->next;
		kmem_small_cache_free(&kmem_tag_cache, tag);
	}
	kmem_small_cache_free(&kmem_slab_cache, slab);
	free_pages(slab->pages, cache->order);
}

static bool kmem_cache_grow(struct kmem_cache *cache)
{
	struct kmem_slab *slab = kmem_small_cache_alloc(&kmem_slab_cache);

	if (!slab)
		return false;

	list_init(&slab->link);
	slab->free = 0;
	slab->busy = 0;
	slab->cache = cache;
	slab->pages = alloc_pages(cache->order);

	if (!slab->pages)
		goto out;

	const pfn_t pages = (pfn_t)1 << cache->order;
	const size_t count = (pages << PAGE_BITS) / cache->object_size;
	char *ptr = kernel_virt(page2pfn(slab->pages) << PAGE_BITS);

	for (unsigned i = 0; i != count; ++i, ptr += cache->object_size) {
		struct kmem_tag *tag = kmem_small_cache_alloc(&kmem_tag_cache);

		if (!tag)
			goto out;

		tag->next = slab->free;
		tag->ptr = ptr;
		slab->free = tag;
	}

	for (pfn_t i = 0; i != pages; ++i)
		slab->pages[i].u.slab = slab;

	list_add(&slab->link, &cache->free_list);

	return true;

out:
	kmem_slab_release(cache, slab);

	return false;
}

void kmem_small_cache_reap(struct kmem_small_cache *cache)
{
	struct list_head *head = &cache->free_list;

	for (struct list_head *ptr = head->next; ptr != head; ptr = ptr->next) {
		struct kmem_small_slab *slab =
			LIST_ENTRY(ptr, struct kmem_small_slab, link);

		free_pages(slab->pages, cache->order);	
	}

	list_init(&cache->free_list);
}

void kmem_cache_reap(struct kmem_cache *cache)
{
	struct list_head *head = &cache->free_list;

	for (struct list_head *ptr = head->next; ptr != head; ptr = ptr->next) {
		struct kmem_slab *slab =
			LIST_ENTRY(ptr, struct kmem_slab, link);

		kmem_slab_release(cache, slab);	
	}

	list_init(&cache->free_list);
}

void *kmem_small_cache_alloc(struct kmem_small_cache *cache)
{
	if (!list_empty(&cache->part_list)) {
		struct list_head *node = list_first(&cache->part_list);
		struct kmem_small_slab *slab =
			LIST_ENTRY(node, struct kmem_small_slab, link);

		void *ptr = kmem_small_slab_alloc(cache, slab); 

		if (!slab->free) {
			list_del(&slab->link);
			list_add(&slab->link, &cache->full_list);
		}
		return ptr;
	}

	if (list_empty(&cache->free_list) && !kmem_small_cache_grow(cache))
		return 0;

	struct list_head *node = list_first(&cache->free_list);
	struct kmem_small_slab *slab =
		LIST_ENTRY(node, struct kmem_small_slab, link);

	void *ptr = kmem_small_slab_alloc(cache, slab); 

	list_del(&slab->link);
	list_add(&slab->link, &cache->part_list);

	return ptr;
}

void *kmem_cache_alloc(struct kmem_cache *cache)
{
	if (!list_empty(&cache->part_list)) {
		struct list_head *node = list_first(&cache->part_list);
		struct kmem_slab *slab =
			LIST_ENTRY(node, struct kmem_slab, link);

		void *ptr = kmem_slab_alloc(slab); 

		if (!slab->free) {
			list_del(&slab->link);
			list_add(&slab->link, &cache->full_list);
		}
		return ptr;
	}

	if (list_empty(&cache->free_list) && !kmem_cache_grow(cache))
		return 0;

	struct list_head *node = list_first(&cache->free_list);
	struct kmem_slab *slab = LIST_ENTRY(node, struct kmem_slab, link);

	void *ptr = kmem_slab_alloc(slab); 

	list_del(&slab->link);
	list_add(&slab->link, &cache->part_list);

	return ptr;
}

void kmem_small_cache_free(struct kmem_small_cache *cache, void *ptr)
{
	struct kmem_small_slab *slab = kmem_get_small_slab(ptr);

	kmem_small_slab_free(cache, slab, ptr);

	if (slab->free == slab->total) {
		list_del(&slab->link);
		list_add(&slab->link, &cache->free_list);
		return;
	}

	if (slab->free == 1) {
		list_del(&slab->link);
		list_add(&slab->link, &cache->part_list);
	}
}

void kmem_cache_free(struct kmem_cache *cache, void *ptr)
{
	struct kmem_slab *slab = kmem_get_slab(ptr);
	const bool full = (slab->free == 0);

	kmem_slab_free(slab, ptr);

	if (!slab->busy) {
		list_del(&slab->link);
		list_add(&slab->link, &cache->free_list);
		return;
	}

	if (full) {
		list_del(&slab->link);
		list_add(&slab->link, &cache->part_list);
	}
}

static void kmem_small_cache_init(struct kmem_small_cache *cache,
			size_t size, size_t align)
{
	list_init(&cache->free_list);
	list_init(&cache->part_list);
	list_init(&cache->full_list);

	const size_t sz = sizeof(struct kmem_border_tag);
	const size_t al = ALIGN_OF(struct kmem_border_tag);

	size = ALIGN(size, al);
	align = MAXU(align, al);

	const size_t object_size = size;
	const size_t padded_size = ALIGN(size + sz, align);

	int order = 0;
	for (; order != BUDDY_ORDERS; ++order) {
		const size_t bytes = (PAGE_SIZE << order) - sizeof(*cache);

		if (bytes / padded_size >= 8)
			break;
	}
	cache->object_size = object_size;
	cache->padded_size = padded_size;
	cache->order = order;
}

static void kmem_cache_init(struct kmem_cache *cache, size_t size, size_t align)
{
	list_init(&cache->free_list);
	list_init(&cache->part_list);
	list_init(&cache->full_list);

	size = ALIGN(size, MAXU(sizeof(void *), align));

	int order = 0;

	for (; order != BUDDY_ORDERS; ++order) {
		const size_t bytes = (PAGE_SIZE << order);

		if (bytes / size >= 8)
			break;
	}
	cache->object_size = size;
	cache->order = order;
}


struct kmem_small_cache *kmem_small_cache_create(size_t size, size_t align)
{
	struct kmem_small_cache *cache =
		kmem_small_cache_alloc(&kmem_small_cache_cache);

	if (!cache)
		return 0;

	kmem_small_cache_init(cache, size, align);

	return cache;
}

struct kmem_cache *kmem_cache_create(size_t size, size_t align)
{
	struct kmem_cache *cache = kmem_small_cache_alloc(&kmem_cache_cache);

	if (!cache)
		return 0;

	kmem_cache_init(cache, size, align);

	return cache;
}

void kmem_small_cache_destroy(struct kmem_small_cache *cache)
{
	kmem_small_cache_reap(cache);

	if (!list_empty(&cache->full_list))
		return;

	if (!list_empty(&cache->part_list))
		return;

	kmem_small_cache_free(&kmem_small_cache_cache, cache);
}

void kmem_cache_destroy(struct kmem_cache *cache)
{
	kmem_cache_reap(cache);

	if (!list_empty(&cache->full_list))
		return;

	if (!list_empty(&cache->part_list))
		return;

	kmem_small_cache_free(&kmem_cache_cache, cache);
}

void *kmem_alloc(size_t size)
{
	const int i = kmem_cache_index(size);

	if (i == -1)
		return 0;

	return kmem_cache_alloc(kmem_pool[i]);
}

void kmem_free(void *ptr)
{
	if (!ptr)
		return;

	struct kmem_slab *slab = kmem_get_slab(ptr);

	if (!slab)
		return;

	kmem_cache_free(slab->cache, ptr);
}

void setup_alloc(void)
{
	kmem_small_cache_init(&kmem_small_cache_cache,
		sizeof(struct kmem_small_cache),
		ALIGN_OF(struct kmem_small_cache));

	kmem_small_cache_init(&kmem_cache_cache, sizeof(struct kmem_cache),
		ALIGN_OF(struct kmem_cache));

	kmem_small_cache_init(&kmem_slab_cache, sizeof(struct kmem_slab),
		ALIGN_OF(struct kmem_slab));

	kmem_small_cache_init(&kmem_tag_cache, sizeof(struct kmem_tag),
		ALIGN_OF(struct kmem_tag));

	for (int i = 0; i != KMEM_POOLS; ++i)
		kmem_pool[i] = kmem_cache_create(kmem_size[i], sizeof(void *));
}

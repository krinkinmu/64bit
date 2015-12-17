#include "kmem_cache.h"
#include "memory.h"
#include "list.h"


struct kmem_slab_tag {
	struct kmem_slab_tag *next;
};

struct kmem_slab {
	struct list_head link;
	struct kmem_slab_tag *free_list;
	struct kmem_cache *cache;
	struct page *pages;
	short free_objs;
	short total_objs;
};

struct kmem_cache {
	struct list_head free_list;
	struct list_head part_list;
	struct list_head full_list;
	int order;
	int size;
	int padding;
};


static struct kmem_cache cache_cache;

static const unsigned kmem_size[] = {
	8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};
static const int kmem_pools = sizeof(kmem_size)/sizeof(kmem_size[0]);
static struct kmem_cache *kmem_pool[sizeof(kmem_size)/sizeof(kmem_size[0])];


static int kmem_cache_index(unsigned size)
{
	for (int i = 0; i != kmem_pools; ++i) {
		if (size <= kmem_size[i])
			return i;
	}
	return -1;
}

static struct kmem_slab *kmem_get_slab(void *ptr)
{
	const pfn_t pfn = KERNEL_PHYS(ptr) >> PAGE_BITS;
	
	return pfn2page(pfn)->u.slab;
}

static void *kmem_slab_alloc(struct kmem_cache *cache, struct kmem_slab *slab)
{
	struct kmem_slab_tag *tag = slab->free_list;

	slab->free_list = tag->next;
	--slab->free_objs;

	return (char *)tag - cache->size;
}

static void kmem_slab_free(struct kmem_cache *cache, struct kmem_slab *slab,
			char *ptr)
{
	struct kmem_slab_tag *tag = (struct kmem_slab_tag *)(ptr + cache->size);

	tag->next = slab->free_list;
	slab->free_list = tag;
	++slab->free_objs;
}

static bool kmem_cache_grow(struct kmem_cache *cache)
{
	struct page *page = alloc_pages(cache->order);

	if (!page)
		return false;

	const pfn_t pages = (pfn_t)1 << cache->order;
	const unsigned long size = PAGE_SIZE * pages;
	const unsigned long offset = size - sizeof(struct kmem_slab);

	char *vaddr = kernel_virt(page2pfn(page) << PAGE_BITS);
	struct kmem_slab *slab = (struct kmem_slab *)(vaddr + offset);

	for (pfn_t i = 0; i != pages; ++i)
		page[i].u.slab = slab;

	list_init(&slab->link);
	slab->cache = cache;
	slab->pages = page;
	slab->free_list = 0;
	slab->free_objs = 0;
	slab->total_objs = 0;

	const int sz = cache->size + cache->padding;

	for (char *ptr = vaddr; ptr + sz <= (char *)slab; ptr += sz) {
		kmem_slab_free(cache, slab, ptr);
		++slab->total_objs;
	}

	list_add_tail(&slab->link, &cache->free_list);

	return true;
}

void kmem_cache_reap(struct kmem_cache *cache)
{
	struct list_head *head = &cache->free_list;

	for (struct list_head *ptr = head->next; ptr != head; ptr = ptr->next) {
		struct kmem_slab *slab =
			LIST_ENTRY(ptr, struct kmem_slab, link);

		free_pages(slab->pages, cache->order);	
	}

	list_init(&cache->free_list);
}

void *kmem_cache_alloc(struct kmem_cache *cache)
{
	if (!list_empty(&cache->part_list)) {
		struct list_head *node = list_first(&cache->part_list);
		struct kmem_slab *slab =
			LIST_ENTRY(node, struct kmem_slab, link);

		void *ptr = kmem_slab_alloc(cache, slab); 

		if (!slab->free_objs) {
			list_del(&slab->link);
			list_add(&slab->link, &cache->full_list);
		}
		return ptr;
	}

	if (list_empty(&cache->free_list) && !kmem_cache_grow(cache))
		return 0;

	struct list_head *node = list_first(&cache->free_list);
	struct kmem_slab *slab = LIST_ENTRY(node, struct kmem_slab, link);

	void *ptr = kmem_slab_alloc(cache, slab); 

	list_del(&slab->link);
	list_add(&slab->link, &cache->part_list);
	return ptr;
}

void kmem_cache_free(struct kmem_cache *cache, void *ptr)
{
	struct kmem_slab *slab = kmem_get_slab(ptr);

	kmem_slab_free(cache, slab, ptr);

	if (slab->free_objs == slab->total_objs) {
		list_del(&slab->link);
		list_add(&slab->link, &cache->free_list);
		return;
	}

	if (slab->free_objs == 1) {
		list_del(&slab->link);
		list_add(&slab->link, &cache->part_list);
	}
}

static void kmem_cache_init(struct kmem_cache *cache, unsigned size,
			unsigned align)
{
	list_init(&cache->free_list);
	list_init(&cache->part_list);
	list_init(&cache->full_list);

	const unsigned sz = sizeof(struct kmem_slab_tag);
	const unsigned al = ALIGN_OF(struct kmem_slab_tag);

	size = ALIGN(size, al);
	align = MAXU(align, al);

	const unsigned entry_size = size + sz;
	const unsigned object_size = ALIGN(entry_size, align);

	cache->size = size;
	cache->padding = object_size - entry_size;

	for (int order = 0; order != BUDDY_ORDERS; ++order) {
		const unsigned long bs = (PAGE_SIZE << order) - sizeof(*cache);

		cache->order = order;
		if (bs / object_size >= 8)
			break;
	}
}

struct kmem_cache *kmem_cache_create(unsigned size, unsigned align)
{
	struct kmem_cache *cache = kmem_cache_alloc(&cache_cache);

	if (!cache)
		return 0;

	kmem_cache_init(cache, size, align);

	return cache;
}

void kmem_cache_destroy(struct kmem_cache *cache)
{
	kmem_cache_reap(cache);

	if (!list_empty(&cache->full_list))
		return;

	if (!list_empty(&cache->part_list))
		return;

	kmem_cache_free(&cache_cache, cache);
}

void *kmem_alloc(unsigned size)
{
	const int i = kmem_cache_index(size);

	if (-1 == i)
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
	kmem_cache_init(&cache_cache, sizeof(struct kmem_cache),
		ALIGN_OF(struct kmem_cache));

	for (int i = 0; i != kmem_pools; ++i)
		kmem_pool[i] = kmem_cache_create(kmem_size[i], sizeof(void *));
}

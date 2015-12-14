#include "kmem_cache.h"
#include "memory.h"
#include "list.h"

struct kmem_slab_tag {
	struct kmem_slab_tag *next;
};

struct kmem_slab {
	struct list_head link;
	struct kmem_slab_tag *free_list;
	short free_objs;
	short total_objs;
};

#define KMEM_SLAB_OFFSET (PAGE_SIZE - sizeof(struct kmem_slab))

struct kmem_cache {
	struct list_head free_list;
	struct list_head part_list;
	struct list_head full_list;
	int size;
	int padding;
};

struct kmem_cache cache_cache;


static struct kmem_slab *kmem_get_slab(void *ptr)
{
	char *page = (char *)((uintptr_t)ptr & ~PAGE_MASK);

	return (struct kmem_slab *)(page + KMEM_SLAB_OFFSET);
}

static void *kmem_get_page(struct kmem_slab *slab)
{ return (char *)slab - KMEM_SLAB_OFFSET; }

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
	struct page *page = alloc_pages(0);

	if (!page)
		return false;

	char *vaddr = kernel_virt(page2pfn(page) << PAGE_BITS);
	struct kmem_slab *slab = kmem_get_slab(vaddr);

	list_init(&slab->link);
	slab->free_list = 0;
	slab->free_objs = 0;
	slab->total_objs = 0;

	const int object_size = cache->size;
	const int size = object_size + cache->padding;

	for (char *ptr = vaddr; ptr + size <= (char *)slab; ptr += size) {
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
		const phys_t paddr = kernel_phys(kmem_get_page(slab));
		struct page *page = pfn2page(paddr >> PAGE_BITS);

		free_pages(page, 0);	
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

	cache->size = size;
	cache->padding = sz + (align - ((size + sz) & (align - 1)));
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

void setup_kmem_cache(void)
{
	kmem_cache_init(&cache_cache, sizeof(struct kmem_cache),
		ALIGN_OF(struct kmem_cache));
}

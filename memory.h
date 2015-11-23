#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "list.h"

#define PAGE_BITS 12
#define PAGE_SIZE (1 << PAGE_BITS)

#define PAGE_NODE_BITS   8ul
#define PAGE_NODE_MASK   ((1ul << PAGE_NODE_BITS) - 1)

#define BUDDY_ORDERS 12

typedef unsigned long pfn_t;

struct memory_node;

struct page {
	unsigned long flags;
	struct list_head link;
	int order;
};

static inline unsigned long page_node_id(const struct page *page)
{ return page->flags & PAGE_NODE_MASK; }

static inline int page_get_order(const struct page *page)
{ return page->order; }

static inline void page_set_order(struct page *page, int order)
{ page->order = order; }

struct memory_node {
	struct page *mmap;
	pfn_t begin_pfn;
	pfn_t end_pfn;
	unsigned long id;

	struct list_head free_list[BUDDY_ORDERS];
};

void memory_node_add_at(unsigned long long addr, unsigned long long size);
struct memory_node *memory_node_get(unsigned long id);

static inline struct memory_node *page_node(const struct page * const page)
{ return memory_node_get(page_node_id(page)); }

struct page *pfn2page(pfn_t pfn);
pfn_t page2pfn(const struct page * const page);

struct page *alloc_pages_node(int order, struct memory_node *node);
void free_pages_node(struct page *pages, int order, struct memory_node *node);

void dump_buddy_allocator_state(void);

#endif /*__MEMORY_H__*/

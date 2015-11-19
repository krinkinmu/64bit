#ifndef __MEMORY_H__
#define __MEMORY_H__

#define PAGE_BITS 12
#define PAGE_SIZE (1 << PAGE_BITS)

#define PAGE_NODE_BITS 8
#define PAGE_NODE_MASK ((1 << PAGE_NODE_BITS) - 1)

typedef unsigned long pfn_t;

struct memory_node;

struct page {
	unsigned long flags;
};

static inline unsigned long page_node_id(const struct page * const page)
{ return page->flags & PAGE_NODE_MASK; }


struct memory_node {
	struct page *mmap;
	pfn_t begin_pfn;
	pfn_t end_pfn;
	unsigned long id;
};

void memory_node_add_at(unsigned long long addr, unsigned long long size);
struct memory_node *memory_node_get(unsigned long id);

static inline struct memory_node *page_node(const struct page * const page)
{ return memory_node_get(page_node_id(page)); }

struct page *pfn2page(pfn_t pfn);
pfn_t page2pfn(const struct page * const page);

#endif /*__MEMORY_H__*/

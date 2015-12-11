#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "balloc.h"
#include "list.h"

#define PAGE_NODE_BITS  8ul
#define PAGE_NODE_MASK  ((1ul << PAGE_NODE_BITS) - 1)

#define BUDDY_ORDERS    12

#define PAGE_BITS       12
#define PAGE_SIZE       (1 << PAGE_BITS)
#define PAGE_MASK       (PAGE_SIZE - 1)

#define VIRTUAL_BASE    0xffffffff80000000ul
#define PHYSICAL_BASE   0x0000000000000000ul
#define KERNEL_SIZE     (1ul << 31) // 2GB - kernel memory model
#define KERNEL_PAGES    (KERNEL_SIZE / PAGE_SIZE)

#define KERNEL_CS       0x18
#define KERNEL_DS       0x20


typedef unsigned long pfn_t;
typedef unsigned long phys_t;


#define KERNEL_PHYS(x)  ((phys_t)(x) - VIRTUAL_BASE)
#define KERNEL_VIRT(x)  ((void *)((phys_t)(x) + VIRTUAL_BASE))


static inline phys_t kernel_phys(const void *vaddr)
{ return KERNEL_PHYS(vaddr); }

static inline void *kernel_virt(phys_t paddr)
{ return KERNEL_VIRT(paddr); }


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

struct memory_node *memory_node_get(unsigned long id);

static inline struct memory_node *page_node(const struct page * const page)
{ return memory_node_get(page_node_id(page)); }

struct page *pfn2page(pfn_t pfn);
pfn_t page2pfn(const struct page * const page);

struct page *alloc_pages_node(int order, struct memory_node *node);
void free_pages_node(struct page *pages, int order, struct memory_node *node);
struct page *alloc_pages(int order);
void free_pages(struct page *pages, int order);

void setup_memory(void);
void setup_buddy(void);

#endif /*__MEMORY_H__*/

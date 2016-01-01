#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "balloc.h"
#include "list.h"

#define BUDDY_ORDERS      12
#define BUDDY_ORDER_BITS  8ul

#define PAGE_NODE_BITS    8ul
#define PAGE_NODE_MASK    (BIT_CONST(PAGE_NODE_BITS) - 1)

#define PAGE_BUSY_BIT     PAGE_NODE_BITS
#define PAGE_BUSY_MASK    BIT_CONST(PAGE_BUSY_BIT)

#define PAGE_BITS         12
#define PAGE_SIZE         BIT_CONST(PAGE_BITS)
#define PAGE_MASK         (PAGE_SIZE - 1)

#define PADDR_BITS        48

#define VIRTUAL_BASE      0xffffffff80000000ul
#define PHYSICAL_BASE     0x0000000000000000ul
#define MAX_PHYS_SIZE     BIT_CONST(36)       // max 0.5GB of page structs
#define KERNEL_SIZE       (3 * BIT_CONST(29)) // 1.5GB - kernel memory model
#define KERNEL_PAGES      (KERNEL_SIZE / PAGE_SIZE)

#define KMAP_SIZE         BIT_CONST(29)       // 0.5GB - temporary map area
#define KMAP_PAGES        (KMAP_SIZE / PAGE_SIZE)

#define KERNEL_CS         0x18
#define KERNEL_DS         0x20


typedef uintptr_t pfn_t;
typedef uintptr_t phys_t;

#define KERNEL_PHYS(x)  ((phys_t)(x) - VIRTUAL_BASE)
#define KERNEL_VIRT(x)  ((void *)((phys_t)(x) + VIRTUAL_BASE))


static inline phys_t kernel_phys(const void *vaddr)
{ return KERNEL_PHYS(vaddr); }

static inline void *kernel_virt(phys_t paddr)
{ return KERNEL_VIRT(paddr); }


struct memory_node;
struct kmem_slab;

struct page {
	unsigned long flags;
	struct list_head link;

	union {
		struct kmem_slab *slab;
		int order;
	} u;
};


static inline int page_node_id(const struct page *page)
{ return page->flags & PAGE_NODE_MASK; }

static inline bool page_busy(const struct page *page)
{ return (page->flags & PAGE_BUSY_MASK) != 0; }

static inline bool page_free(const struct page *page)
{ return !page_busy(page); }

static inline void page_set_busy(struct page *page)
{ page->flags |= PAGE_BUSY_MASK; }

static inline void page_set_free(struct page *page)
{ page->flags &= ~PAGE_BUSY_MASK; }

static inline int page_get_order(const struct page *page)
{ return page->u.order; }

static inline void page_set_order(struct page *page, int order)
{ page->u.order = order; }


enum node_type {
	NT_LOW,
	NT_HIGH,
	NT_COUNT
};

struct memory_node {
	struct list_head link;
	struct page *mmap;
	pfn_t begin_pfn;
	pfn_t end_pfn;
	int id;
	enum node_type type;

	struct list_head free_list[BUDDY_ORDERS];
};


struct memory_node *memory_node_get(int id);
struct page *pfn2page(pfn_t pfn);
pfn_t page2pfn(const struct page * const page);
struct page *alloc_pages_node(int order, struct memory_node *node);
void free_pages_node(struct page *pages, int order, struct memory_node *node);
struct page *alloc_pages(int order, int type);
void free_pages(struct page *pages, int order);

static inline struct memory_node *page_node(const struct page * const page)
{ return memory_node_get(page_node_id(page)); }


void setup_memory(void);
void setup_buddy(void);

#endif /*__MEMORY_H__*/

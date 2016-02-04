#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "locking.h"
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

#define KERNEL_BASE       0xffffffff80000000ul
#define HIGH_BASE         0xffff800000000000ul
#define PHYSICAL_BASE     0x0000000000000000ul
#define MAX_PHYS_SIZE     BIT_CONST(36)       // max 0.5GB of page structs

#ifdef CONFIG_KERNEL_SIZE
#define KERNEL_SIZE CONFIG_KERNEL_SIZE
#else
#define KERNEL_SIZE 3ul * 512ul * 1024ul * 1024ul
#endif

#ifdef CONFIG_KMAP_SIZE
#define KMAP_SIZE CONFIG_KMAP_SIZE
#else
#define KMAP_SIZE (512ul * 1024ul * 1024ul - PAGE_SIZE)
#endif

#define LOWMEM_SIZE       KERNEL_SIZE
#define KERNEL_PAGES      (KERNEL_SIZE / PAGE_SIZE)
#define KMAP_PAGES        (KMAP_SIZE / PAGE_SIZE)

#define KERNEL_CS         0x18
#define KERNEL_DS         0x20
#define USER_CS           0x2b
#define USER_DS           0x33


typedef uintptr_t pfn_t;
typedef uintptr_t phys_t;
typedef uintptr_t virt_t;

#define KERNEL_PHYS(x)  ((phys_t)(x) - KERNEL_BASE)
#define KERNEL_VIRT(x)  ((void *)((phys_t)(x) + KERNEL_BASE))
#define PA(x)           ((phys_t)(x) - HIGH_BASE)
#define VA(x)           ((void *)((phys_t)(x) + HIGH_BASE))


static inline phys_t kernel_phys(const void *vaddr)
{ return KERNEL_PHYS(vaddr); }

static inline void *kernel_virt(phys_t paddr)
{ return KERNEL_VIRT(paddr); }

static inline phys_t pa(const void *vaddr)
{ return PA(vaddr); }

static inline void *va(phys_t paddr)
{ return VA(paddr); }


struct memory_node;
struct kmem_slab;

struct page {
	unsigned long flags;
	struct list_head link;

	union {
		struct kmem_slab *slab;
		int refcount;
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
	struct spinlock lock;
	pfn_t begin_pfn;
	pfn_t end_pfn;
	int id;
	enum node_type type;

	struct list_head free_list[BUDDY_ORDERS];
};

void memory_free_region(unsigned long long addr, unsigned long long size);

struct memory_node *memory_node_get(int id);
pfn_t max_pfns(void);
struct page *pfn2page(pfn_t pfn);
pfn_t page2pfn(const struct page * const page);
struct page *alloc_pages_node(int order, struct memory_node *node);
void free_pages_node(struct page *pages, int order, struct memory_node *node);
struct page *__alloc_pages(int order, int type);
struct page *alloc_pages(int order);
void free_pages(struct page *pages, int order);

static inline struct memory_node *page_node(const struct page * const page)
{ return memory_node_get(page_node_id(page)); }

static inline phys_t page_paddr(struct page *page)
{ return (phys_t)page2pfn(page) << PAGE_BITS; }

static inline void *page_addr(struct page *page)
{ return va(page_paddr(page)); }

struct gdt_ptr {
	uint16_t size;
	uint64_t addr;
} __attribute__((packed));

static inline void *get_gdt_ptr(void)
{
	struct gdt_ptr ptr;

	__asm__("sgdt %0" : "=m"(ptr));
	return (void *)ptr.addr;
}

void setup_memory(void);
void setup_buddy(void);

#endif /*__MEMORY_H__*/

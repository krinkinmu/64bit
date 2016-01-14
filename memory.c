#include <stdint.h>

#include "locking.h"
#include "kernel.h"
#include "memory.h"
#include "balloc.h"
#include "stdio.h"

#define MAX_MEMORY_NODES (1 << PAGE_NODE_BITS)

static struct memory_node nodes[MAX_MEMORY_NODES];
static int memory_nodes;
static LIST_HEAD(node_order);
static struct list_head *node_type[NT_COUNT];


struct memory_node *memory_node_get(int id)
{ return &nodes[id]; }

static pfn_t node_pfn(const struct memory_node *node, const struct page *page)
{ return page - node->mmap; }

static struct page *node_page(const struct memory_node *node, pfn_t pfn)
{ return &node->mmap[pfn]; }

static int pfn_max_order(pfn_t pfn)
{
	for (int i = 0; i != BUDDY_ORDERS - 1; ++i)
		if (pfn & ((pfn_t)1 << i))
			return i;

	return BUDDY_ORDERS - 1;
}

static struct memory_node *pfn_node(pfn_t pfn)
{
	for (int i = 0; i != memory_nodes; ++i) {
		if (nodes[i].end_pfn <= pfn)
			continue;

		return &nodes[i];
	}

	return 0;
}

static void __memory_node_add(enum node_type type, unsigned long begin,
			unsigned long end)
{
	struct memory_node *node = &nodes[memory_nodes];
	const pfn_t pages = (end - begin) >> PAGE_BITS;
	const pfn_t pfn = begin >> PAGE_BITS;

	list_init(&node->link);
	spinlock_init(&node->lock);
	node->begin_pfn = pfn;
	node->end_pfn = pfn + pages;
	node->id = memory_nodes++;
	node->type = type;
	for (int i = 0; i != BUDDY_ORDERS; ++i)
		list_init(&node->free_list[i]);

	const long long mmap = balloc_alloc_aligned(PAGE_SIZE, KERNEL_SIZE,
				sizeof(struct page) * pages, PAGE_SIZE);

	node->mmap = kernel_virt(mmap);
	for (pfn_t i = 0; i != pages; ++i) {
		struct page *page = &node->mmap[i];

		page->flags = node->id;
		page_set_busy(page);
		list_init(&page->link);
	}

	printf("memory node %ld (%s): pfns %ld-%ld\n",
		node->id, type == NT_LOW ? "low" : "high",
		node->begin_pfn, node->end_pfn - 1);
}

static void memory_node_add(unsigned long long addr, unsigned long long size)
{
	const unsigned long long begin = ALIGN(addr, PAGE_SIZE);
	const unsigned long long end = MINU(ALIGN_DOWN(addr + size, PAGE_SIZE),
				MAX_PHYS_SIZE);

	if (begin >= end)
		return;

	if (begin < KERNEL_SIZE && end > KERNEL_SIZE) {
		__memory_node_add(NT_LOW, begin, KERNEL_SIZE);
		__memory_node_add(NT_HIGH, KERNEL_SIZE, end);
	} else {
		const enum node_type type = (end <= KERNEL_SIZE)
					? NT_LOW : NT_HIGH;

		__memory_node_add(type, begin, end);
	}
}

static void __memory_free_region(unsigned long long begin,
			unsigned long long end)
{
	const pfn_t b = begin >> PAGE_BITS;
	const pfn_t e = end >> PAGE_BITS;
	const pfn_t pages = e - b;

	struct memory_node *node = pfn_node(b);

	for (pfn_t pfn = b - node->begin_pfn; pfn != pages;) {
		struct page *page = node_page(node, pfn);
		int order = pfn_max_order(pfn);

		/**
		 * Actually we need not check that order is non negative
		 * because order originally non negative and goes less
		 * while (1 << order) > pages and pages is positive, so
		 * order never get less than zero.
		 * But clang static checker complains about it, so i've
		 * added order check in loop condition
		 */
		while (order && pfn + ((pfn_t)1 << order) > pages)
			--order;

		free_pages_node(page, order, node);
		pfn += (pfn_t)1 << order;
	}
}

static void memory_free_region(unsigned long long addr, unsigned long long size)
{
	const unsigned long long begin = ALIGN(addr, PAGE_SIZE);
	const unsigned long long end = MINU(ALIGN_DOWN(addr + size, PAGE_SIZE),
				MAX_PHYS_SIZE);

	if (begin >= end)
		return;

	if (begin < KERNEL_SIZE && end > KERNEL_SIZE) {
		__memory_free_region(begin, KERNEL_SIZE);
		__memory_free_region(KERNEL_SIZE, end);
	} else {
		__memory_free_region(begin, end);
	}
}


static const uint32_t MMAP_AVAILABLE = 1;

struct mmap_entry {
	uint32_t size;
	uint64_t addr;
	uint64_t length;
	uint32_t type;
} __attribute__((packed));


void setup_memory(void)
{
	extern const phys_t mmap;
	extern const unsigned long mmap_len;

	phys_t mmap_paddr = *((unsigned long *)kernel_virt((phys_t)&mmap));
	unsigned long len = *((unsigned long *)kernel_virt((phys_t)&mmap_len));

	const char *begin = kernel_virt(mmap_paddr);
	const char *end = begin + len;

	while (begin < end) {
		const struct mmap_entry *ptr =
					(const struct mmap_entry *)begin;

		begin += ptr->size + sizeof(ptr->size);
		balloc_add_region(ptr->addr, ptr->length);
		printf("memory range: %#llx-%#llx type %u\n",
			(unsigned long long)ptr->addr,
			(unsigned long long)ptr->addr + ptr->length - 1,
			(unsigned)ptr->type);
	}

	begin = kernel_virt(mmap_paddr);
	while (begin < end) {
		const struct mmap_entry *ptr =
					(const struct mmap_entry *)begin;

		begin += ptr->size + sizeof(ptr->size);
		if (ptr->type != MMAP_AVAILABLE)
			balloc_reserve_region(ptr->addr, ptr->length);
	}

	extern char text_virt_begin[];
	extern char bss_virt_end[];

	const phys_t kernel_begin = (phys_t)kernel_phys(text_virt_begin);
	const phys_t kernel_end = (phys_t)kernel_phys(bss_virt_end);
	printf("reserve memory range: %#llx-%#llx for kernel\n",
			(unsigned long long) kernel_begin,
			(unsigned long long) kernel_end - 1);
	balloc_reserve_region(kernel_begin, kernel_end - kernel_begin);
}

void setup_buddy(void)
{
	balloc_for_each_region(&memory_node_add);
	balloc_for_each_free_region(&memory_free_region);

	struct list_head type_nodes[NT_COUNT];

	for (int i = 0; i != NT_COUNT; ++i)
		list_init(&type_nodes[i]);

	for (int i = 0; i != memory_nodes; ++i) {
		struct memory_node *node = memory_node_get(i);

		list_add_tail(&node->link, &type_nodes[node->type]);
	}

	for (int i = 0; i != NT_COUNT; ++i) {
		list_splice(type_nodes + i, &node_order);
		node_type[i] = node_order.next;
	}
}

struct page *pfn2page(pfn_t pfn)
{
	struct memory_node *node = pfn_node(pfn);

	if (!node)
		return 0;

	return node_page(node, pfn - node->begin_pfn);
}

pfn_t page2pfn(const struct page * const page)
{
	const struct memory_node * const node = page_node(page);

	return node->begin_pfn + node_pfn(node, page);
}

static pfn_t buddy_pfn(pfn_t pfn, int order)
{ return pfn ^ ((pfn_t)1 << order); }

static struct page *__alloc_pages_node(int order, struct memory_node *node)
{
	int coorder = order;

	while (coorder < BUDDY_ORDERS) {
		if (!list_empty(&node->free_list[coorder]))
			break;
		++coorder;
	}

	if (coorder >= BUDDY_ORDERS)
		return 0;

	struct page *page = LIST_ENTRY(list_first(&node->free_list[coorder]),
				struct page, link);

	list_del(&page->link);
	page_set_busy(page);

	while (coorder > order) {
		const pfn_t pfn = node_pfn(node, page);
		const pfn_t bpfn = buddy_pfn(pfn, --coorder);
		struct page *buddy = node_page(node, bpfn);

		page_set_order(buddy, coorder);
		page_set_free(buddy);

		list_add(&buddy->link, &node->free_list[coorder]);
	}

	return page;
}

struct page *alloc_pages_node(int order, struct memory_node *node)
{
	const bool enabled = spin_lock_irqsave(&node->lock);
	struct page * pages = __alloc_pages_node(order, node);

	spin_unlock_irqrestore(&node->lock, enabled);

	return pages;
}

static void dump_buddy_node_state(struct memory_node *node)
{
	for (int i = 0; i != BUDDY_ORDERS; ++i) {
		const int sz = list_size(&node->free_list[i]);

		if (sz)
			printf("\torder %d: %d\n", i, sz);
	}
}

void dump_buddy_state(void)
{
	for (int i = 0; i != memory_nodes; ++i) {
		struct memory_node *node = memory_node_get(i);

		printf("node %d\n", i);
		dump_buddy_node_state(node);
	}
}

static void __free_pages_node(struct page *pages, int order,
			struct memory_node *node)
{
	const pfn_t node_pfns = node->end_pfn - node->begin_pfn;
	pfn_t pfn = node_pfn(node, pages);

	while (order < BUDDY_ORDERS - 1) {
		const pfn_t bpfn = buddy_pfn(pfn, order);

		if (bpfn >= node_pfns)
			break;

		if (bpfn + ((pfn_t)1 << order) > node_pfns)
			break;

		struct page *buddy = node_page(node, bpfn);

		if (page_busy(buddy))
			break;

		if (order != page_get_order(buddy))
			break;

		list_del(&buddy->link);
		++order;

		if (bpfn < pfn) {
			pfn = bpfn;
			pages = buddy;
		}
	}

	page_set_order(pages, order);
	page_set_free(pages);

	list_add(&pages->link, &node->free_list[order]);
}

void free_pages_node(struct page *pages, int order, struct memory_node *node)
{
	if (!pages)
		return;

	const bool enabled = spin_lock_irqsave(&node->lock);

	__free_pages_node(pages, order, node);
	spin_unlock_irqrestore(&node->lock, enabled);	
}

struct page *alloc_pages(int order, int type)
{
	const struct list_head *head = &node_order;
	struct list_head *ptr = node_type[type];

	for (; ptr != head; ptr = ptr->next) {
		struct memory_node *node = LIST_ENTRY(ptr, struct memory_node,
					link);
		struct page *pages = alloc_pages_node(order, node);

		if (pages)
			return pages;
	}

	return 0;
}

void free_pages(struct page *pages, int order)
{
	if (!pages)
		return;

	struct memory_node *node = page_node(pages);

	free_pages_node(pages, order, node);
}

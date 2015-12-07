#include "kernel.h"
#include "memory.h"
#include "balloc.h"
#include "stdio.h"

#define MAX_MEMORY_NODES 16

static struct memory_node nodes[MAX_MEMORY_NODES];
static int memory_nodes;

struct memory_node *memory_node_get(unsigned long id)
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

static void memory_node_add(unsigned long long addr, unsigned long long size)
{
	const unsigned long long begin = ALIGN(addr, PAGE_SIZE);
	const unsigned long long end = ALIGN_DOWN(addr + size, PAGE_SIZE);

	if (begin >= end)
		return;

	struct memory_node *node = &nodes[memory_nodes];
	const pfn_t pages = (end - begin) >> PAGE_BITS;
	const pfn_t pfn = begin >> PAGE_BITS;

	node->begin_pfn = pfn;
	node->end_pfn = pfn + pages;
	node->id = memory_nodes++;
	for (int i = 0; i != BUDDY_ORDERS; ++i)
		list_init(&node->free_list[i]);

	node->mmap = balloc_alloc_aligned(PAGE_SIZE, ~0ull,
				sizeof(struct page) * pages, PAGE_SIZE);
	for (pfn_t i = 0; i != pages; ++i) {
		struct page *page = &node->mmap[i];

		page->flags = node->id;
		page_set_order(page, BUDDY_ORDERS);
	}

	printf("memory node %ld: pfns %ld-%ld\n",
		node->id, node->begin_pfn, node->end_pfn - 1);
}

static void memory_free_region(unsigned long long addr, unsigned long long size)
{
	const unsigned long long begin = ALIGN(addr, PAGE_SIZE);
	const unsigned long long end = ALIGN_DOWN(addr + size, PAGE_SIZE);

	if (begin >= end)
		return;

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

void setup_buddy(void)
{
	balloc_for_each_region(&memory_node_add);
	balloc_for_each_free_region(&memory_free_region);
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

struct page *alloc_pages_node(int order, struct memory_node *node)
{
	int coorder = order;

	while (coorder < BUDDY_ORDERS) {
		if (!list_empty(&node->free_list[coorder]))
			break;
		++coorder;
	}

	if (order >= BUDDY_ORDERS)
		return 0;

	struct page *page = CONTAINER_OF(list_first(&node->free_list[coorder]),
		struct page, link);

	list_del(&page->link);
	while (coorder > order) {
		const pfn_t pfn = node_pfn(node, page);
		const pfn_t bpfn = buddy_pfn(pfn, --coorder);
		struct page *buddy = node_page(node, bpfn);

		page_set_order(buddy, coorder);
		list_add(&buddy->link, &node->free_list[coorder]);
	}
	page_set_order(page, BUDDY_ORDERS);
	return page;
}

void free_pages_node(struct page *pages, int order, struct memory_node *node)
{
	if (order >= BUDDY_ORDERS) {
		puts("shit");
		while (1);
	}

	pfn_t pfn = node_pfn(node, pages);

	while (order < BUDDY_ORDERS - 1) {
		const pfn_t bpfn = buddy_pfn(pfn, order);

		if (bpfn >= node->end_pfn - node->begin_pfn)
			break;

		struct page *buddy = node_page(node, bpfn);

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
	list_add(&pages->link, &node->free_list[order]);
}

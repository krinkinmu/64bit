#include "kernel.h"
#include "memory.h"
#include "balloc.h"
#include "stdio.h"

#define MAX_MEMORY_NODES 16

static struct memory_node nodes[MAX_MEMORY_NODES];
static int memory_nodes;

void memory_node_add_at(unsigned long long addr, unsigned long long size)
{
	const unsigned long long begin = ALIGN(addr, PAGE_SIZE);
	const unsigned long long end = ALIGN_DOWN(addr + size, PAGE_SIZE);

	if (begin >= end)
		return;

	struct memory_node *node = &nodes[memory_nodes];
	pfn_t pages = (end - begin) >> PAGE_BITS;
	pfn_t pfn = begin >> PAGE_BITS;

	node->begin_pfn = pfn;
	node->end_pfn = pfn + pages;
	node->id = memory_nodes++;
	for (int i = 0; i != BUDDY_ORDERS; ++i)
		list_init(&node->free_list[i]);

	node->mmap = balloc_alloc_aligned(PAGE_SIZE, ~0ull,
				sizeof(*(node->mmap)) * pages, PAGE_SIZE);
	for (pfn_t i = 0; i != pages; ++i) {
		struct page *page = &node->mmap[i];

		page->flags = node->id;
		page_set_order(page, BUDDY_ORDERS);
	}

	for (pfn_t i = 0; i != pages; ++i) {
		struct page *page = &node->mmap[i];

		if (balloc_is_free(page2pfn(page) << PAGE_BITS, PAGE_SIZE))
			free_pages_node(page, 0, node);
	}

	printf("memory node %ld: pfns %ld-%ld\n",
		node->id, node->begin_pfn, node->end_pfn - 1);
}

struct memory_node *memory_node_get(unsigned long id)
{ return &nodes[id]; }

static pfn_t node_pfn(const struct memory_node *node, const struct page *page)
{ return page - node->mmap; }

static struct page *node_page(const struct memory_node *node, pfn_t pfn)
{ return &node->mmap[pfn]; }

struct page *pfn2page(pfn_t pfn)
{
	for (int i = 0; i != memory_nodes; ++i) {
		if (nodes[i].begin_pfn > pfn)
			continue;

		return node_page(&nodes[i], pfn - nodes[i].begin_pfn);
	}

	return 0;
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

static void dump_memory_node_state(const struct memory_node *node)
{
	printf("Memory Node %lu [%lu-%lu]\n", node->id, node->begin_pfn,
		node->end_pfn - 1);
	for (int order = 0; order != BUDDY_ORDERS; ++order) {
		const size_t size = list_size(&node->free_list[order]);

		if (size)
			printf("\torder %d: %zu blocks\n", order, size);
	}
}

void dump_buddy_allocator_state(void)
{
	struct page *page = alloc_pages_node(3, &nodes[1]);

	free_pages_node(page, 3, &nodes[1]);
	dump_memory_node_state(&nodes[1]);
}

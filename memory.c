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

	node->mmap = balloc_alloc_aligned(PAGE_SIZE, ~0ull,
				sizeof(*(node->mmap)) * pages, PAGE_SIZE);
	for (pfn_t i = 0; i != pages; ++i)
		node->mmap[i].flags = node->id;
	printf("memory node %ld: pfns %ld-%ld\n",
		node->id, node->begin_pfn, node->end_pfn - 1);
}

struct memory_node *memory_node_get(unsigned long id)
{ return &nodes[id]; }

struct page *pfn2page(pfn_t pfn)
{
	for (int i = 0; i != memory_nodes; ++i) {
		if (nodes[i].begin_pfn > pfn)
			continue;

		return &nodes[i].mmap[pfn - nodes[i].begin_pfn];
	}

	return 0;
}

pfn_t page2pfn(const struct page * const page)
{
	const struct memory_node * const node = page_node(page);

	return node->begin_pfn + (page - node->mmap);
}

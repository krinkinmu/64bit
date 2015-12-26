#include "paging.h"
#include "memory.h"
#include "string.h"
#include "list.h"

#define PTE_KERNEL (PTE_PRESENT | PTE_READ | PTE_WRITE | PTE_SUPERUSER)
#define PTE_FLAGS  BITS_CONST(11, 0)

static unsigned long page_common_flags(unsigned long old, unsigned long new)
{ return (old | new) & PTE_FLAGS; }

static phys_t alloc_page_table(void)
{
	const struct page *pt = alloc_pages(0, NT_LOW);
	const phys_t paddr = page2pfn(pt) << PAGE_BITS;

	memset(kernel_virt(paddr), 0, PAGE_SIZE);

	return paddr;
}

static void pml1_map(pte_t *pml1, virt_t virt, phys_t phys, pfn_t pages,
			unsigned long flags)
{
	const int entries = MINU(pages, PT_SIZE);

	for (int i = pml1_index(virt); i != entries; ++i) {
		pml1[i] = (phys + i * PAGE_SIZE) | flags;
		flush_tlb_page(virt + i * PAGE_SIZE);
	}
}

static void pml1_unmap(pte_t *pml1, virt_t virt, pfn_t pages)
{
	const int entries = MINU(pages, PT_SIZE);

	for (int i = pml1_index(virt); i != entries; ++i) {
		pml1[i] = 0;
		flush_tlb_page(virt + i * PAGE_SIZE);
	}
}

static void pml2_map(pte_t *pml2, virt_t virt, phys_t phys, pfn_t pages,
			unsigned long flags)
{
	for (int i = pml2_index(virt); pages && i != PT_SIZE; ++i) {
		phys_t paddr = pml2[i] & BITS(PADDR_BITS - 1, 12);

		if ((pml2[i] & PTE_PRESENT) == 0)
			paddr = alloc_page_table();

		pte_t *pt = kernel_virt(paddr);
		const pfn_t to_map = MINU(pages, PML1_PAGES);
		const phys_t bytes = to_map << PAGE_BITS;

		pml1_map(pt, virt, phys, to_map, flags);

		virt += bytes;
		phys += bytes;
		pages -= to_map;

		pml2[i] = paddr | page_common_flags(pml2[i], flags);
	}
}

static void pml2_unmap(pte_t *pml2, virt_t virt, pfn_t pages)
{
	for (int i = pml2_index(virt); pages && i != PT_SIZE; ++i) {
		const phys_t paddr = pml2[i] & BITS(PADDR_BITS - 1, 12);
		pte_t *pt = kernel_virt(paddr);
		const pfn_t to_unmap = MINU(pages, PML1_PAGES);
		const phys_t bytes = to_unmap << PAGE_BITS;

		pml1_unmap(pt, virt, to_unmap);

		virt += bytes;
		pages -= to_unmap;
	}
}

static void pml3_map(pte_t *pml3, virt_t virt, phys_t phys, pfn_t pages,
			unsigned long flags)
{
	for (int i = pml3_index(virt); pages && i != PT_SIZE; ++i) {
		phys_t paddr = pml3[i] & BITS(PADDR_BITS - 1, 12);

		if ((pml3[i] & PTE_PRESENT) == 0)
			paddr = alloc_page_table();

		pte_t *pt = kernel_virt(paddr);
		const pfn_t to_map = MINU(pages, PML2_PAGES);
		const phys_t bytes = to_map << PAGE_BITS;

		pml2_map(pt, virt, phys, to_map, flags);

		virt += bytes;
		phys += bytes;
		pages -= to_map;

		pml3[i] = paddr | page_common_flags(pml3[i], flags);
	}
}

static void pml3_unmap(pte_t *pml3, virt_t virt, pfn_t pages)
{
	for (int i = pml3_index(virt); pages && i != PT_SIZE; ++i) {
		const phys_t paddr = pml3[i] & BITS(PADDR_BITS - 1, 12);
		pte_t *pt = kernel_virt(paddr);
		const pfn_t to_unmap = MINU(pages, PML2_PAGES);
		const phys_t bytes = to_unmap << PAGE_BITS;

		pml2_unmap(pt, virt, to_unmap);

		virt += bytes;
		pages -= to_unmap;
	}
}

static void pml4_map(pte_t *pml4, virt_t virt, phys_t phys, pfn_t pages,
			unsigned long flags)
{
	for (int i = pml4_index(virt); pages && i != PT_SIZE; ++i) {
		phys_t paddr = pml4[i] & BITS(PADDR_BITS - 1, 12);

		if ((pml4[i] & PTE_PRESENT) == 0)
			paddr = alloc_page_table();

		pte_t *pt = kernel_virt(paddr);
		const pfn_t to_map = MINU(pages, PML3_PAGES);
		const phys_t bytes = to_map << PAGE_BITS;

		pml3_map(pt, virt, phys, to_map, flags);

		virt += bytes;
		phys += bytes;
		pages -= to_map;

		pml4[i] = paddr | page_common_flags(pml4[i], flags);
	}
}

static void pml4_unmap(pte_t *pml4, virt_t virt, pfn_t pages)
{
	for (int i = pml4_index(virt); pages && i != PT_SIZE; ++i) {
		const phys_t paddr = pml4[i] & BITS(PADDR_BITS - 1, 12);
		pte_t *pt = kernel_virt(paddr);
		const pfn_t to_unmap = MINU(pages, PML3_PAGES);
		const phys_t bytes = to_unmap << PAGE_BITS;

		pml3_unmap(pt, virt, to_unmap);

		virt += bytes;
		pages -= to_unmap;
	}
}


#define TMAP_ORDERS 20

struct tmap_range {
	struct list_head link;
	pfn_t pages;
};

static struct tmap_range all_tmap_ranges[TMAP_PAGES];
static struct list_head free_tmap_ranges[TMAP_ORDERS];


static struct tmap_range *virt2tmap(virt_t vaddr)
{
	const pfn_t range = (vaddr - VIRTUAL_BASE - KERNEL_SIZE) >> PAGE_BITS;

	return &all_tmap_ranges[range];
}

static virt_t tmap2virt(struct tmap_range *range)
{
	const pfn_t page = range - all_tmap_ranges;

	return VIRTUAL_BASE + KERNEL_SIZE + (page << PAGE_BITS);
}

static struct tmap_range *tmap_find_free_range(int order, pfn_t pages)
{
	struct list_head *head = &free_tmap_ranges[order];
	struct list_head *ptr = head->next;

	for (; ptr != head; ptr = ptr->next) {
		struct tmap_range *range = LIST_ENTRY(ptr, struct tmap_range,
					link);

		if (range->pages >= pages)
			return range;
	}
	return 0;
}

static void tmap_free_range(struct tmap_range *range, pfn_t pages)
{
	if (range > all_tmap_ranges) {
		struct tmap_range *prev = range - (range - 1)->pages;

		if (!list_empty(&prev->link)) {
			list_del(&prev->link);
			pages += prev->pages;
			range = prev;
		}
	}

	if (range + pages < all_tmap_ranges + TMAP_PAGES) {
		struct tmap_range *next = range + pages;

		if (!list_empty(&next->link)) {
			list_del(&next->link);
			pages += next->pages;
		}
	}

	(range + pages - 1)->pages = range->pages = pages;
	list_add(&range->link, free_tmap_ranges + ilog2(pages));
}

static struct tmap_range *tmap_alloc_range(pfn_t pages)
{
	for (int order = ilog2(pages); order < TMAP_ORDERS; ++order) {
		struct tmap_range *range = tmap_find_free_range(order, pages);

		if (!range)
			continue;

		list_del(&range->link);
		list_init(&range->link);

		if (range->pages > pages)
			tmap_free_range(range + pages, range->pages - pages);

		(range + pages - 1)->pages = range->pages = pages;
		return range;
	}
	return 0;
}

void *tmap(struct page *pages, pfn_t count)
{
	struct tmap_range *range = tmap_alloc_range(count);

	if (!range)
		return 0;

	const virt_t vaddr = tmap2virt(range);

	pte_t *pt = kernel_virt(load_pml4());
	pml4_map(pt, tmap2virt(range), page2pfn(pages) << PAGE_BITS,
				count, PTE_KERNEL);
	return (void *)vaddr;
}

void tunmap(void *vaddr)
{
	struct tmap_range *range = virt2tmap((virt_t)vaddr);
	const pfn_t count = range->pages;

	pte_t *pt = kernel_virt(load_pml4());
	pml4_unmap(pt, (virt_t)vaddr, count);
	tmap_free_range(range, range->pages);
}

/*
static void setup_tmap(void)
{
	for (int i = 0; i != TMAP_ORDERS; ++i)
		list_init(&free_tmap_ranges[i]);

	tmap_free_range(all_tmap_ranges, TMAP_PAGES);	
}
*/

void setup_paging(void)
{
	const phys_t paddr = alloc_page_table();
	pte_t *pt = kernel_virt(paddr);

	pml4_map(pt, VIRTUAL_BASE, PHYSICAL_BASE,
				KERNEL_PAGES + TMAP_PAGES, PTE_KERNEL);
	store_pml4(paddr);

	//setup_tmap();
}

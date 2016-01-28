#include "paging.h"
#include "memory.h"
#include "string.h"
#include "error.h"
#include "list.h"


#define PTE_KERNEL (PTE_READ | PTE_WRITE | PTE_SUPERUSER)
#define PTE_FLAGS  BITS_CONST(11, 0)


static unsigned long page_common_flags(unsigned long old, unsigned long new)
{ return (old | new) & PTE_FLAGS; }

static struct page *alloc_page(void)
{
	struct page *pt = alloc_pages(0, NT_LOW);

	if (pt) {
		pt->u.refcount = 0;
		memset(page_addr(pt), 0, PAGE_SIZE);
	}

	return pt;
}

static int pml1_map(struct page *parent, pte_t *pml1,
			virt_t virt, phys_t phys, pfn_t pages,
			unsigned long flags)
{
	const int index = pml1_index(virt);
	const int entries = MINU(index + pages, PT_SIZE);

	for (int i = index; i != entries; ++i) {
		const pte_t pte = pml1[i];

		pml1[i] = phys | flags;
		flush_tlb_page(virt);

		if ((pte & PTE_PRESENT) == 0)
			get_page(parent);

		phys += PAGE_SIZE;
		virt += PAGE_SIZE;
	}

	return 0;
}

static void pml1_unmap(struct page *parent, pte_t *pml1,
			virt_t virt, pfn_t pages)
{
	const int index = pml1_index(virt);
	const int entries = MINU(index + pages, PT_SIZE);

	for (int i = index; i != entries; ++i) {
		if ((pml1[i] & PTE_PRESENT) != 0) {
			pml1[i] = 0;
			put_page(parent);
			flush_tlb_page(virt);
		}
		virt += PAGE_SIZE;
	}
}

static int pml2_map(struct page *parent, pte_t *pml2,
			virt_t virt, phys_t phys, pfn_t pages,
			unsigned long flags)
{
	for (int i = pml2_index(virt); pages && i != PT_SIZE; ++i) {
		const pte_t pte = pml2[i];
		struct page *page = ((pte & PTE_PRESENT) == 0)
				? get_page(alloc_page())
				: get_page(pfn2page((pfn_t)pte >> PAGE_BITS));

		if (!page)
			return -ENOMEM;

		phys_t paddr = page_paddr(page);
		pte_t *pt = kernel_virt(paddr);
		const pfn_t to_map = MINU(pages, PML1_PAGES);
		const phys_t bytes = to_map << PAGE_BITS;
		const int rc = pml1_map(page, pt, virt, phys, to_map, flags);

		if (page->u.refcount > 1) {
			if ((pte & PTE_PRESENT) == 0)
				get_page(parent);
			pml2[i] = paddr | page_common_flags(pte, flags);
		}

		put_page(page);

		if (rc)
			return rc;

		virt += bytes;
		phys += bytes;
		pages -= to_map;
	}

	return 0;
}

static void pml2_unmap(struct page *parent, pte_t *pml2,
			virt_t virt, pfn_t pages)
{
	for (int i = pml2_index(virt); pages && i != PT_SIZE; ++i) {
		const pte_t pte = pml2[i];
		const pfn_t to_unmap = MINU(pages, PML1_PAGES);
		const phys_t bytes = to_unmap << PAGE_BITS;

		if ((pte & PTE_PRESENT) == 0) {
			virt += bytes;
			pages -= to_unmap;
			continue;
		}

		struct page *page = get_page(pfn2page((pfn_t)pte >> PAGE_BITS));
		pte_t *pt = page_addr(page);

		pml1_unmap(page, pt, virt, to_unmap);

		if (page->u.refcount == 1) {
			pml2[i] = 0;
			put_page(parent);
		}

		put_page(page);
		virt += bytes;
		pages -= to_unmap;
	}
}

static int pml3_map(struct page *parent, pte_t *pml3,
			virt_t virt, phys_t phys, pfn_t pages,
			unsigned long flags)
{
	for (int i = pml3_index(virt); pages && i != PT_SIZE; ++i) {
		const pte_t pte = pml3[i];
		struct page *page = ((pte & PTE_PRESENT) == 0)
				? get_page(alloc_page())
				: get_page(pfn2page((pfn_t)pte >> PAGE_BITS));

		if (!page)
			return -ENOMEM;

		phys_t paddr = page_paddr(page);
		pte_t *pt = kernel_virt(paddr);
		const pfn_t to_map = MINU(pages, PML2_PAGES);
		const phys_t bytes = to_map << PAGE_BITS;
		const int rc = pml2_map(page, pt, virt, phys, to_map, flags);

		if (page->u.refcount > 1) {
			/* if we created new entry then get_page */
			if ((pte & PTE_PRESENT) == 0)
				get_page(parent);
			pml3[i] = paddr | page_common_flags(pte, flags);
		}

		put_page(page);

		if (rc)
			return rc;

		virt += bytes;
		phys += bytes;
		pages -= to_map;
	}

	return 0;
}

static void pml3_unmap(struct page *parent, pte_t *pml3,
			virt_t virt, pfn_t pages)
{
	for (int i = pml3_index(virt); pages && i != PT_SIZE; ++i) {
		const pte_t pte = pml3[i];
		const pfn_t to_unmap = MINU(pages, PML2_PAGES);
		const phys_t bytes = to_unmap << PAGE_BITS;

		if ((pte & PTE_PRESENT) == 0) {
			virt += bytes;
			pages -= to_unmap;
			continue;
		}

		struct page *page = get_page(pfn2page((pfn_t)pte >> PAGE_BITS));
		pte_t *pt = page_addr(page);

		pml2_unmap(page, pt, virt, to_unmap);

		if (page->u.refcount == 1) {
			pml3[i] = 0;
			put_page(parent);
		}

		put_page(page);
		virt += bytes;
		pages -= to_unmap;
	}
}

static int pml4_map(pte_t *pml4, virt_t virt, phys_t phys, pfn_t pages,
			unsigned long flags)
{
	for (int i = pml4_index(virt); pages && i != PT_SIZE; ++i) {
		const pte_t pte = pml4[i];
		struct page *page = ((pte & PTE_PRESENT) == 0)
				? get_page(alloc_page())
				: get_page(pfn2page((pfn_t)pte >> PAGE_BITS));

		if (!page)
			return -ENOMEM;

		phys_t paddr = page_paddr(page);
		pte_t *pt = kernel_virt(paddr);
		const pfn_t to_map = MINU(pages, PML3_PAGES);
		const phys_t bytes = to_map << PAGE_BITS;
		const int rc = pml3_map(page, pt, virt, phys, to_map, flags);

		if (page->u.refcount > 1)
			pml4[i] = paddr | page_common_flags(pte, flags);

		put_page(page);

		if (rc)
			return rc;

		virt += bytes;
		phys += bytes;
		pages -= to_map;
	}

	return 0;
}

static void pml4_unmap(pte_t *pml4, virt_t virt, pfn_t pages)
{
	for (int i = pml4_index(virt); pages && i != PT_SIZE; ++i) {
		const pte_t pte = pml4[i];
		const pfn_t to_unmap = MINU(pages, PML3_PAGES);
		const phys_t bytes = to_unmap << PAGE_BITS;

		if ((pte & PTE_PRESENT) == 0) {
			virt += bytes;
			pages -= to_unmap;
			continue;
		}

		struct page *page = get_page(pfn2page((pfn_t)pte >> PAGE_BITS));
		pte_t *pt = page_addr(page);

		pml3_unmap(page, pt, virt, to_unmap);

		if (page->u.refcount == 1)
			pml4[i] = 0;

		put_page(page);
		virt += bytes;
		pages -= to_unmap;
	}
}

int map_range(pte_t *pml4, virt_t virt, phys_t phys, pfn_t pages,
			unsigned long flags)
{
	if ((virt & (PAGE_SIZE - 1)) != 0)
		return -EINVAL;

	if ((phys & (PAGE_SIZE - 1)) != 0)
		return -EINVAL;

	if (((virt_t)pml4 & (PAGE_SIZE - 1)) != 0)
		return -EINVAL;

	if (!pml4)
		return -EINVAL;

	if (!pages)
		return 0;

	const int rc = pml4_map(pml4, virt, phys, pages, flags | PTE_PRESENT);

	if (rc)
		pml4_unmap(pml4, virt, pages);

	return rc;
}

int unmap_range(pte_t *pml4, virt_t virt, pfn_t pages)
{
	if ((virt & (PAGE_SIZE - 1)) != 0)
		return -EINVAL;

	if (((virt_t)pml4 & (PAGE_SIZE - 1)) != 0)
		return -EINVAL;

	if (!pml4)
		return -EINVAL;

	if (!pages)
		return 0;

	pml4_unmap(pml4, virt, pages);

	return 0;
}

#define KMAP_ORDERS 16

struct kmap_range {
	struct list_head link;
	pfn_t pages;
};

static struct kmap_range all_kmap_ranges[KMAP_PAGES];
static struct list_head free_kmap_ranges[KMAP_ORDERS];


static int kmap_order(pfn_t pages)
{ return MIN(ilog2(pages), KMAP_ORDERS - 1); }

static struct kmap_range *virt2kmap(virt_t vaddr)
{
	const pfn_t range = (vaddr - VIRTUAL_BASE - KERNEL_SIZE) >> PAGE_BITS;

	return &all_kmap_ranges[range];
}

static virt_t kmap2virt(struct kmap_range *range)
{
	const pfn_t page = range - all_kmap_ranges;

	return VIRTUAL_BASE + KERNEL_SIZE + (page << PAGE_BITS);
}

static struct kmap_range *kmap_find_free_range(int order, pfn_t pages)
{
	struct list_head *head = &free_kmap_ranges[order];
	struct list_head *ptr = head->next;

	for (; ptr != head; ptr = ptr->next) {
		struct kmap_range *range = LIST_ENTRY(ptr, struct kmap_range,
					link);

		if (range->pages >= pages)
			return range;
	}
	return 0;
}

static void kmap_free_range(struct kmap_range *range, pfn_t pages)
{
	if (range > all_kmap_ranges) {
		struct kmap_range *prev = range - (range - 1)->pages;

		if (!list_empty(&prev->link)) {
			list_del(&prev->link);
			pages += prev->pages;
			range = prev;
		}
	}

	if (range + pages < all_kmap_ranges + KMAP_PAGES) {
		struct kmap_range *next = range + pages;

		if (!list_empty(&next->link)) {
			list_del(&next->link);
			pages += next->pages;
		}
	}

	(range + pages - 1)->pages = range->pages = pages;
	list_add(&range->link, free_kmap_ranges + kmap_order(pages));
}

static struct kmap_range *kmap_alloc_range(pfn_t pages)
{
	for (int order = kmap_order(pages); order < KMAP_ORDERS; ++order) {
		struct kmap_range *range = kmap_find_free_range(order, pages);

		if (!range)
			continue;

		const pfn_t range_pages = range->pages;

		list_del(&range->link);
		list_init(&range->link);
		(range + pages - 1)->pages = range->pages = pages;

		if (range_pages > pages)
			kmap_free_range(range + pages, range_pages - pages);

		return range;
	}
	return 0;
}

void *kmap(struct page *pages, pfn_t count)
{
	struct kmap_range *range = kmap_alloc_range(count);

	if (!range)
		return 0;

	const virt_t vaddr = kmap2virt(range);
	pte_t *pt = kernel_virt(load_pml4());

	map_range(pt, vaddr, page2pfn(pages) << PAGE_BITS,
				count, PTE_KERNEL);
	return (void *)vaddr;
}

void kunmap(void *vaddr)
{
	struct kmap_range *range = virt2kmap((virt_t)vaddr);
	const pfn_t count = range->pages;

	pte_t *pt = kernel_virt(load_pml4());
	unmap_range(pt, (virt_t)vaddr, count);
	kmap_free_range(range, range->pages);
}

static void setup_kmap(void)
{
	for (int i = 0; i != KMAP_ORDERS; ++i)
		list_init(&free_kmap_ranges[i]);

	kmap_free_range(all_kmap_ranges, KMAP_PAGES);
}

static void pml2_pin(pte_t *pml2, virt_t virt, pfn_t pages)
{
	for (int i = pml2_index(virt); pages && i != PT_SIZE; ++i) {
		const pte_t pte = pml2[i];
		const pfn_t to_pin = MINU(pages, PML1_PAGES);
		const phys_t bytes = to_pin << PAGE_BITS;

		if ((pte & PTE_PRESENT) == 0) {
			virt += bytes;
			pages -= to_pin;
			continue;
		}

		get_page(pfn2page((pfn_t)pte >> PAGE_BITS));
		virt += bytes;
		pages -= to_pin;
	}
}

static void pml3_pin(pte_t *pml3, virt_t virt, pfn_t pages)
{
	for (int i = pml3_index(virt); pages && i != PT_SIZE; ++i) {
		const pte_t pte = pml3[i];
		const pfn_t to_pin = MINU(pages, PML2_PAGES);
		const phys_t bytes = to_pin << PAGE_BITS;

		if ((pte & PTE_PRESENT) == 0) {
			virt += bytes;
			pages -= to_pin;
			continue;
		}

		struct page *page = get_page(pfn2page((pfn_t)pte >> PAGE_BITS));
		pte_t *pt = page_addr(page);

		pml2_pin(pt, virt, to_pin);
		virt += bytes;
		pages -= to_pin;
	}
}

static void pml4_pin(pte_t *pml4, virt_t virt, pfn_t pages)
{
	for (int i = pml4_index(virt); pages && i != PT_SIZE; ++i) {
		const pte_t pte = pml4[i];
		const pfn_t to_pin = MINU(pages, PML3_PAGES);
		const phys_t bytes = to_pin << PAGE_BITS;

		if ((pte & PTE_PRESENT) == 0) {
			virt += bytes;
			pages -= to_pin;
			continue;
		}

		struct page *page = get_page(pfn2page((pfn_t)pte >> PAGE_BITS));
		pte_t *pt = page_addr(page);

		pml3_pin(pt, virt, to_pin);
		virt += bytes;
		pages -= to_pin;
	}
}

void setup_paging(void)
{
	const phys_t opaddr = load_pml4();
	pte_t *opt = kernel_virt(opaddr);

	struct page *page = alloc_page();
	const phys_t paddr = page_paddr(page);
	pte_t *pt = kernel_virt(paddr);

	pt[0] = opt[0]; // preserve lower 4GB identity mapping for initramfs
	DBG_ASSERT(map_range(pt, VIRTUAL_BASE, PHYSICAL_BASE,
				KERNEL_PAGES + KMAP_PAGES, PTE_KERNEL) == 0);
	pml4_pin(pt, VIRTUAL_BASE, KERNEL_PAGES + KMAP_PAGES);
	store_pml4(paddr);
	setup_kmap();
}

#include "paging.h"
#include "string.h"
#include "error.h"
#include "stdio.h"


#define PT_FLAGS          (PTE_PRESENT | PTE_WRITE | PTE_USER)

static int pt_index(virt_t vaddr, int level)
{
	switch (level) {
	case 4:
		return pml4_i(vaddr);
	case 3:
		return pml3_i(vaddr);
	case 2:
		return pml2_i(vaddr);
	case 1:
		return pml1_i(vaddr);
	}

	DBG_ASSERT("Unreachable");
	return 0;
}

static virt_t pt_iter_addr(const struct pt_iter *iter)
{
	const virt_t addr = (virt_t)iter->idx[3] << 39 |
			(virt_t)iter->idx[2] << 30 |
			(virt_t)iter->idx[1] << 21 |
			(virt_t)iter->idx[0] << 12;

	return canonical(addr);
}

struct pt_iter *pt_iter_set(struct pt_iter *iter, pte_t *pml4, virt_t addr)
{
	memset(iter, 0, sizeof(*iter));

	if (!pml4)
		return iter;

	int level = PT_MAX_LEVEL;
	int idx = pml4_i(addr);
	pte_t pte = pml4[idx];

	iter->pt[level] = pml4;
	iter->idx[level] = idx;

	while (pte_present(pte) && level != 0 && !pte_large(pte)) {
		const phys_t paddr = pte_phys(pte);
		pte_t *pt = va(paddr);

		idx = pt_index(addr, level--);
		pte = pt[idx];

		iter->idx[level] = idx;
		iter->pt[level] = pt;
	}

	iter->level = level;
	iter->addr = addr;

	return iter;
}

struct pt_iter *pt_iter_next(struct pt_iter *iter)
{
	int level = iter->level;
	int idx = iter->idx[level];

	while (idx == PT_SIZE - 1 && level != PT_MAX_LEVEL)
		idx = iter->idx[++level];

	if (idx == PT_SIZE - 1)
		return 0;

	pte_t pte = iter->pt[level][++idx];

	iter->idx[level] = idx;

	while (pte_present(pte) && level != 0 && !pte_large(pte)) {
		const phys_t paddr = pte_phys(pte);
		pte_t *pt = va(paddr);

		iter->idx[--level] = 0;
		iter->pt[level] = pt;
		pte = pt[0];
	}
	iter->level = level;

	while (level != 0) {
		iter->pt[--level] = 0;
		iter->idx[level] = 0;
	}
	iter->addr = pt_iter_addr(iter);

	return iter;
}

bool pt_iter_present(const struct pt_iter *iter)
{
	const int level = iter->level;
	const int index = iter->idx[level];

	return pte_present(iter->pt[level][index]);
}

bool pt_iter_large(const struct pt_iter *iter)
{
	const int level = iter->level;
	const int index = iter->idx[level];

	return pte_large(iter->pt[level][index]);
}

static struct page *alloc_page_table(void)
{
	struct page *page = __alloc_pages(0, NT_LOW);

	if (page) {
		memset(va(page_paddr(page)), 0, PAGE_SIZE);
		page->u.refcount = 1;
	}
	return page;
}

static void pt_release_pml2(pte_t *pml2, virt_t from, virt_t to)
{
	virt_t vaddr = from;

	for (int i = pml2_i(from); vaddr != to; ++i) {
		const pte_t pte = pml2[i];

		if (pte_present(pte) && !pte_large(pte)) {
			const pfn_t pfn = pte_phys(pte) >> PAGE_BITS;
			struct page *pt = pfn2page(pfn);

			if (pt->u.refcount == 1)
				pml2[i] = 0;
			put_page(pt);
		}

		vaddr += MINU(PML1_SIZE - (vaddr & PML1_MASK), to - vaddr);	
	}
}

static int pt_populate_pml2(pte_t *pml2, virt_t from, virt_t to, bool large)
{
	virt_t vaddr = from;

	for (int i = pml2_i(from); vaddr != to; ++i) {
		struct page *pt = 0;

		if (!pte_present(pml2[i]) && !large) {
			pt = alloc_page_table();

			if (!pt) {
				pt_release_pml2(pml2, from, vaddr);
				return -ENOMEM;
			}

			pml2[i] = page_paddr(pt) | PT_FLAGS;
		} else {
			const pte_t pte = pml2[i];

			if (!pte_large(pte)) {
				const pfn_t pfn = pte_phys(pte) >> PAGE_BITS;

				pt = pfn2page(pfn);
				get_page(pt);
			}
		}

		vaddr += MINU(PML1_SIZE - (vaddr & PML1_MASK), to - vaddr);
	}

	return 0;
}

static void pt_release_pml3(pte_t *pml3, virt_t from, virt_t to)
{
	virt_t vaddr = from;

	for (int i = pml3_i(from); vaddr != to; ++i) {
		const pte_t pte = pml3[i];
		const virt_t bytes = MINU(PML2_SIZE - (vaddr & PML2_MASK),
					to - vaddr);

		if (pte_present(pte)) {
			const phys_t paddr = pte_phys(pte);
			const pfn_t pfn = paddr >> PAGE_BITS;
			struct page *pt = pfn2page(pfn);

			pt_release_pml2(va(paddr), vaddr, vaddr + bytes);

			if (pt->u.refcount == 1)
				pml3[i] = 0;
			put_page(pt);
		}
		vaddr += bytes;	
	}
}

static int pt_populate_pml3(pte_t *pml3, virt_t from, virt_t to, bool large)
{
	virt_t vaddr = from;

	for (int i = pml3_i(from); vaddr != to; ++i) {
		struct page *pt = 0;
		phys_t paddr = 0;

		if (!pte_present(pml3[i])) {
			pt = alloc_page_table();

			if (!pt) {
				pt_release_pml3(pml3, from, vaddr);
				return -ENOMEM;
			}

			paddr = page_paddr(pt);
			pml3[i] = paddr | PT_FLAGS;
		} else {
			const pte_t pte = pml3[i];

			paddr = pte_phys(pte);
			pt = pfn2page(paddr >> PAGE_BITS);
			get_page(pt);
		}

		const virt_t bytes = MINU(PML2_SIZE - (vaddr & PML2_MASK),
					to - vaddr);
		const int rc = pt_populate_pml2(va(paddr), vaddr, vaddr + bytes,
					large);

		if (rc) {
			put_page(pt);
			pt_release_pml3(pml3, from, vaddr);
			return rc;
		}

		vaddr += bytes;
	}

	return 0;
}

static void pt_release_pml4(pte_t *pml4, virt_t from, virt_t to)
{
	virt_t vaddr = from;

	for (int i = pml4_i(from); vaddr != to; ++i) {
		const pte_t pte = pml4[i];
		const virt_t bytes = MINU(PML3_SIZE - (vaddr & PML3_MASK),
					to - vaddr);

		if (pte_present(pte)) {
			const phys_t paddr = pte_phys(pte);
			const pfn_t pfn = paddr >> PAGE_BITS;
			struct page *pt = pfn2page(pfn);

			pt_release_pml3(va(paddr), vaddr, vaddr + bytes);

			if (pt->u.refcount == 1)
				pml4[i] = 0;
			put_page(pt);
		}
		vaddr += bytes;	
	}
}

static int pt_populate_pml4(pte_t *pml4, virt_t from, virt_t to, bool large)
{
	virt_t vaddr = from;

	for (int i = pml4_i(from); vaddr < to; ++i) {
		struct page *pt = 0;
		phys_t paddr = 0;

		if (!pte_present(pml4[i])) {
			pt = alloc_page_table();

			if (!pt) {
				pt_release_pml4(pml4, from, vaddr);
				return -ENOMEM;
			}

			paddr = page_paddr(pt);
			pml4[i] = paddr | PT_FLAGS;
		} else {
			const pte_t pte = pml4[i];

			paddr = pte_phys(pte);
			pt = pfn2page(paddr >> PAGE_BITS);
			get_page(pt);
		}

		const virt_t bytes = MINU(PML3_SIZE - (vaddr & PML3_MASK),
					to - vaddr);
		const int rc = pt_populate_pml3(va(paddr), vaddr, vaddr + bytes,
					large);

		if (rc) {
			put_page(pt);
			pt_release_pml4(pml4, from, vaddr);
			return rc;
		}

		vaddr += bytes;
	}

	return 0;
}

static int __pt_populate_range(pte_t *pml4, virt_t from, virt_t to, bool large)
{
	DBG_ASSERT(linear(from) < linear(to));
	DBG_ASSERT(pml4 != 0);

	from = ALIGN_DOWN(linear(from), PAGE_SIZE);
	to = ALIGN(linear(to), PAGE_SIZE);

	return pt_populate_pml4(pml4, from, to, large);	
}

static void __pt_release_range(pte_t *pml4, virt_t from, virt_t to)
{
	DBG_ASSERT(linear(from) < linear(to));
	DBG_ASSERT(pml4 != 0);

	from = ALIGN_DOWN(linear(from), PAGE_SIZE);
	to = ALIGN(linear(to), PAGE_SIZE);

	pt_release_pml4(pml4, from, to);
}

int pt_populate_range(pte_t *pt, virt_t from, virt_t to)
{
	return __pt_populate_range(pt, from, to, false);
}

int pt_populate_range_large(pte_t *pt, virt_t from, virt_t to)
{
	return __pt_populate_range(pt, from, to, true);
}

void pt_release_range(pte_t *pt, virt_t from, virt_t to)
{
	__pt_release_range(pt, from, to);
}

static int map_range_large(pte_t *pml4, virt_t from, virt_t to, phys_t phys)
{
	const int rc = pt_populate_range_large(pml4, from, to);

	if (rc)
		return rc;

	struct pt_iter iter;

	for_each_slot_in_range(pml4, from, to, iter) {
		const int level = iter.level;
		const int index = iter.idx[level];
		pte_t *pt = iter.pt[level];

		DBG_ASSERT(level == 1 || level == 0);
		DBG_ASSERT(!pte_present(pt[index]));

		if (level == 1) {
			pt[index] = phys | PTE_WRITE | PTE_LARGE | PTE_PRESENT;
			phys += PML1_SIZE;
		} else {
			pt[index] = phys | PTE_WRITE | PTE_PRESENT;
			phys += PAGE_SIZE;
		}
	}

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
	const pfn_t range = (vaddr - KERNEL_BASE - KERNEL_SIZE) >> PAGE_BITS;

	return &all_kmap_ranges[range];
}

static virt_t kmap2virt(struct kmap_range *range)
{
	const pfn_t page = range - all_kmap_ranges;

	return KERNEL_BASE + KERNEL_SIZE + (page << PAGE_BITS);
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

void *kmap(struct page **pages, size_t count)
{
	struct kmap_range *range = kmap_alloc_range(count);

	if (!range)
		return 0;

	const virt_t vaddr = kmap2virt(range);
	pte_t *pt = va(load_pml4());

	for (size_t i = 0; i != count; ++i) {
		struct page *page = pages[i];

		map_range(pt, vaddr, page_paddr(page), 1, PTE_WRITE);
	}

	return (void *)vaddr;
}

void kunmap(void *vaddr)
{
	struct kmap_range *range = virt2kmap((virt_t)vaddr);
	const pfn_t count = range->pages;

	pte_t *pt = va(load_pml4());
	unmap_range(pt, (virt_t)vaddr, count);
	kmap_free_range(range, range->pages);
}

static int setup_kmap_mapping(pte_t *pml4)
{
	for (int i = 0; i != KMAP_ORDERS; ++i)
		list_init(&free_kmap_ranges[i]);

	kmap_free_range(all_kmap_ranges, KMAP_PAGES);

	return pt_populate_range(pml4, KERNEL_BASE + KERNEL_SIZE,
				KERNEL_BASE + KERNEL_SIZE + KMAP_SIZE);
}

static int setup_fixed_mapping(pte_t *pml4)
{
	const virt_t bytes = max_pfns() << PAGE_BITS;

	return map_range_large(pml4, HIGH_BASE, HIGH_BASE + bytes, 0);
}

static int setup_kernel_mapping(pte_t *pml4)
{
	const virt_t bytes = KERNEL_SIZE;

	return map_range_large(pml4, KERNEL_BASE, KERNEL_BASE + bytes, 0);
}

size_t gather_pages(pte_t *pml4, virt_t virt, pfn_t pages, struct pages *set)
{
	const virt_t from = virt;
	const virt_t to = from + ((virt_t)pages << PAGE_BITS);

	struct pt_iter iter;

	list_init(&set->head);
	set->count = 0;

	for_each_slot_in_range(pml4, from, to, iter) {
		if (!pt_iter_present(&iter))
			continue;

		DBG_ASSERT(iter.level == 0);

		const int level = iter.level;
		const int idx = iter.idx[level];
		const phys_t phys = pte_phys(iter.pt[level][idx]);
		struct page *page = pfn2page(phys >> PAGE_BITS);

		list_add_tail(&page->link, &set->head);
		++set->count;
	}

	return set->count;
}

int map_range(pte_t *pml4, virt_t virt, phys_t phys, pfn_t pages,
				unsigned long flags)
{
	const virt_t from = virt;
	const virt_t to = from + ((virt_t)pages << PAGE_BITS);
	const int rc = pt_populate_range(pml4, from, to);

	if (rc)
		return rc;

	struct pt_iter iter;

	for_each_slot_in_range(pml4, from, to, iter) {
		DBG_ASSERT(iter.level == 0);

		const int level = iter.level;
		const int index = iter.idx[level];
		pte_t *pt = iter.pt[level];

		DBG_ASSERT(pt != 0);
		DBG_ASSERT(!pte_present(pt[index]));

		pt[index] = phys | flags | PTE_PRESENT;
		flush_tlb_addr(virt);
		phys += PAGE_SIZE;
		virt += PAGE_SIZE;
	}

	return 0;
}

int unmap_range(pte_t *pml4, virt_t virt, pfn_t pages)
{
	const virt_t from = virt;
	const virt_t to = from + ((virt_t)pages << PAGE_BITS);

	struct pt_iter iter;

	for_each_slot_in_range(pml4, from, to, iter) {
		DBG_ASSERT(iter.level == 0);

		const int level = iter.level;
		const int index = iter.idx[level];
		pte_t *pt = iter.pt[level];

		DBG_ASSERT(pt != 0);
		DBG_ASSERT(!pte_present(pt[index]));

		pt[index] = 0;
		flush_tlb_addr(virt);
		virt += PAGE_SIZE;
	}

	pt_release_range(pml4, from, to);
	return 0;
}

void setup_paging(void)
{
	struct page *page = alloc_page_table();

	DBG_ASSERT(page != 0);

	const phys_t paddr = page_paddr(page);
	pte_t *pt = va(paddr);

	DBG_ASSERT(setup_fixed_mapping(pt) == 0);
	DBG_ASSERT(setup_kernel_mapping(pt) == 0);
	DBG_ASSERT(setup_kmap_mapping(pt) == 0);
	store_pml4(paddr);
}

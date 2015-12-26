#include "paging.h"
#include "memory.h"
#include "string.h"

#define PTE_KERNEL (PTE_PRESENT | PTE_READ | PTE_WRITE | PTE_SUPERUSER)

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
	for (int i = pml1_index(virt); pages && i != PT_SIZE; ++i) {
		pml1[i] = phys | flags;
		phys += PAGE_SIZE;
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

		pml2[i] = paddr | flags;
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

		pml3[i] = paddr | flags;
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

		pml4[i] = paddr | flags;
	}
}

void setup_paging(void)
{
	const phys_t paddr = alloc_page_table();
	pte_t *pt = kernel_virt(paddr);

	pml4_map(pt, VIRTUAL_BASE, PHYSICAL_BASE, KERNEL_PAGES, PTE_KERNEL);
	store_pml4(paddr);
}

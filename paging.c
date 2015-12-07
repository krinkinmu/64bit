#include "paging.h"
#include "memory.h"

#define PTE_KERNEL (PTE_PRESENT | PTE_READ | PTE_WRITE | PTE_SUPERUSER)

static void pml1_map(pte_t *pml1, virt_t virt, phys_t phys, pfn_t pages)
{
	for (int i = pml1_index(virt); pages && i != PT_SIZE; ++i) {
		pml1[i] = phys | PTE_KERNEL;
		phys += PAGE_SIZE;
	}
}

static void pml2_map(pte_t *pml2, virt_t virt, phys_t phys, pfn_t pages)
{
	for (int i = pml2_index(virt); pages && i != PT_SIZE; ++i) {
		const struct page *pml1p = alloc_pages(0);
		const phys_t pml1 = page2pfn(pml1p) << PAGE_BITS;
		pte_t *pml1v = kernel_virt(pml1);
		const pfn_t to_map = MINU(pages, PML1_PAGES);
		const phys_t bytes = to_map << PAGE_BITS;

		pml1_map(pml1v, virt, phys, to_map);

		virt += bytes;
		phys += bytes;
		pages -= to_map;
		pml2[i] = pml1 | PTE_KERNEL;
	}
}

static void pml3_map(pte_t *pml3, virt_t virt, phys_t phys, pfn_t pages)
{
	for (int i = pml3_index(virt); pages && i != PT_SIZE; ++i) {
		const struct page *pml2p = alloc_pages(0);
		const phys_t pml2 = page2pfn(pml2p) << PAGE_BITS;
		pte_t *pml2v = kernel_virt(pml2);
		const pfn_t to_map = MINU(pages, PML2_PAGES);
		const phys_t bytes = to_map << PAGE_BITS;

		pml2_map(pml2v, virt, phys, to_map);

		virt += bytes;
		phys += bytes;
		pages -= to_map;
		pml3[i] = pml2 | PTE_KERNEL;
	}
}

static void pml4_map(pte_t *pml4, virt_t virt, phys_t phys, pfn_t pages)
{
	for (int i = pml4_index(virt); pages && i != PT_SIZE; ++i) {
		const struct page *pml3p = alloc_pages(0);
		const phys_t pml3 = page2pfn(pml3p) << PAGE_BITS;
		pte_t *pml3v = kernel_virt(pml3);
		const pfn_t to_map = MINU(pages, PML3_PAGES);
		const phys_t bytes = to_map << PAGE_BITS;

		pml3_map(pml3v, virt, phys, to_map);

		virt += bytes;
		phys += bytes;
		pages -= to_map;
		pml4[i] = pml3 | PTE_KERNEL;
	}
}

void setup_paging(void)
{
	const struct page *pml4p = alloc_pages(0);
	const phys_t pml4 = page2pfn(pml4p) << PAGE_BITS;
	pte_t *pml4v = kernel_virt(pml4);

	pml4_map(pml4v, VIRTUAL_BASE, PHYSICAL_BASE, KERNEL_PAGES);
	store_pml4(pml4);
}

#ifndef __PAGING_H__
#define __PAGING_H__

#include <stdbool.h>
#include <stdint.h>

#include "kernel.h"
#include "memory.h"
#include "list.h"


#define PTE_PRESENT   (1ul << 0)
#define PTE_WRITE     (1ul << 1)
#define PTE_USER      (1ul << 2)
#define PTE_LARGE     (1ul << 7)


typedef uint64_t  pte_t;
typedef uintptr_t virt_t;

static inline bool pte_present(pte_t pte)
{ return (pte & PTE_PRESENT) != 0; }

static inline bool pte_write(pte_t pte)
{ return (pte & PTE_WRITE) != 0; }

static inline bool pte_user(pte_t pte)
{ return (pte & PTE_USER) != 0; }

static inline phys_t pte_paddr(pte_t pte)
{ return (phys_t)(pte & BITS_CONST(47, 12)); }


#define PT_SIZE       (PAGE_SIZE / sizeof(pte_t))
#define PML1_PAGES    ((pfn_t)PT_SIZE)
#define PML2_PAGES    (PT_SIZE * PML1_PAGES)
#define PML3_PAGES    (PT_SIZE * PML2_PAGES)
#define PML4_PAGES    (PT_SIZE * PML3_PAGES)


static inline int pml4_index(virt_t vaddr)
{ return (vaddr & BITS_CONST(47, 39)) >> 39; }

static inline int pml3_index(virt_t vaddr)
{ return (vaddr & BITS_CONST(38, 30)) >> 30; }

static inline int pml2_index(virt_t vaddr)
{ return (vaddr & BITS_CONST(29, 21)) >> 21; }

static inline int pml1_index(virt_t vaddr)
{ return (vaddr & BITS_CONST(20, 12)) >> 12; }

static inline int page_offset(virt_t vaddr)
{ return vaddr & BITS_CONST(11, 0); }

static inline phys_t pte_phys(pte_t pte)
{ return pte & BITS_CONST(PADDR_BITS - 1, 12); }

static inline void flush_tlb_page(virt_t vaddr)
{ __asm__ volatile ("invlpg (%0)" : : "r"(vaddr) : "memory"); }

static inline void store_pml4(phys_t pml4)
{ __asm__ volatile ("movq %0, %%cr3" : : "a"(pml4) : "memory"); }

static inline phys_t load_pml4(void)
{
	phys_t pml4;

	__asm__ volatile ("movq %%cr3, %0" : "=a"(pml4));
	return pml4;
}

static inline struct page *get_page(struct page *page)
{
	if (page)
		++page->u.refcount;
	return page;
}

static inline void put_page(struct page *page)
{
	if (page && --page->u.refcount == 0)
		free_pages(page, 0);
}

struct pages {
	struct list_head head;
	size_t count;
};

size_t gather_pages(pte_t *pml4, virt_t virt, pfn_t pages, struct pages *set);
int map_range(pte_t *pml4, virt_t virt, phys_t phys, pfn_t pages,
			unsigned long flags);
int unmap_range(pte_t *pml4, virt_t virt, pfn_t pages);

void *kmap(struct page **pages, size_t count);
void kunmap(void *vaddr);
void setup_paging(void);

#endif /*__PAGEING_H__*/

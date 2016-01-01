#ifndef __PAGING_H__
#define __PAGING_H__

#include <stdint.h>

#include "kernel.h"
#include "memory.h"


#define PTE_PRESENT   (1ul << 0)
#define PTE_READ      0ul
#define PTE_WRITE     (1ul << 1)
#define PTE_SUPERUSER 0ul
#define PTE_USER      (1ul << 2)


typedef uint64_t  pte_t;
typedef uintptr_t virt_t;


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

void *kmap(struct page *pages, pfn_t count);
void kunmap(void *vaddr);
void setup_paging(void);

#endif /*__PAGEING_H__*/

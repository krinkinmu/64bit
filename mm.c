#include "kmem_cache.h"
#include "threads.h"
#include "memory.h"
#include "string.h"
#include "error.h"
#include "mm.h"

#include <stdbool.h>


static struct kmem_cache *mm_cachep;
static struct kmem_cache *vma_cachep;
static struct page *zero_page;

static struct page *alloc_page_table(void)
{
	struct page *page = alloc_pages(0);

	if (page)
		memset(page_addr(page), 0, PAGE_SIZE);
	return page;
}

static void free_page_table(struct page *pt)
{
	free_pages(pt, 0);
}

static struct vma *alloc_vma(void)
{
	struct vma *vma = kmem_cache_alloc(vma_cachep);

	if (vma)
		memset(vma, 0, sizeof(*vma));
	return vma;
}

static void free_vma(struct vma *vma)
{
	kmem_cache_free(vma_cachep, vma);
}

static struct mm *alloc_mm(void)
{
	struct mm *mm = kmem_cache_alloc(mm_cachep);

	if (mm)
		memset(mm, 0, sizeof(*mm));
	return mm;
}

static void free_mm(struct mm *mm)
{
	kmem_cache_free(mm_cachep, mm);
}

static struct page *copy_page(struct page *page)
{
	if (page->u.refcount == 1)
		return page;

	struct page *new = alloc_pages(0);

	if (!new)
		return 0;

	void *dst = page_addr(new);
	void *src = page_addr(page);

	memcpy(dst, src, PAGE_SIZE);
	return new;
}

static struct page *mapped_page(struct mm *mm, virt_t addr)
{
	const virt_t from = addr;
	const virt_t to = addr + PAGE_SIZE;
	struct pt_iter iter;

	for_each_slot_in_range(page_addr(mm->pt), from, to, iter) {
		DBG_ASSERT(iter.level == 0);
		DBG_ASSERT(iter.pt[iter.level] != 0);

		if (iter.level != 0)
			return 0;

		const int level = iter.level;
		const int index = iter.idx[level];
		pte_t *pt = iter.pt[level];
		const pte_t pte = pt[index];

		if (pte_present(pte)) {
			const pfn_t pfn = pte_phys(pte) >> PAGE_BITS;
			struct page *page = pfn2page(pfn);

			return page;
		}
	}

	return 0;
}

static int anon_page_fault(struct mm *mm, struct vma *vma,
				virt_t vaddr, int access)
{
	if (access == VMA_ACCESS_WRITE && (vma->perm & VMA_PERM_WRITE) == 0)
		return -EINVAL;

	if (access == VMA_ACCESS_READ) {
		__mmap_pages(mm, vaddr, &zero_page, 1, PTE_USER);
		flush_tlb_addr(vaddr);

		return 0;
	}

	struct page *old = mapped_page(mm, vaddr);

	if (old) {
		struct page *new = copy_page(old);

		if (!new)
			return -ENOMEM;

		__mmap_pages(mm, vaddr, &new, 1, PTE_USER | PTE_WRITE);
		flush_tlb_addr(vaddr);
		put_page(old);

		return 0;
	}

	struct page *page = alloc_pages(0);

	if (!page)
		return -ENOMEM;

	memset(page_addr(page), 0, PAGE_SIZE);
	page->u.refcount = 0;
	__mmap_pages(mm, vaddr, &page, 1, PTE_USER | PTE_WRITE);
	flush_tlb_addr(vaddr);

	return 0;
}

struct vma_iter {
	struct rb_node **plink;
	struct rb_node *parent;
	struct vma *vma;
};

static int __lookup_vma(struct mm *mm, virt_t begin, virt_t end,
			struct vma_iter *iter)
{
	struct rb_node **plink = &mm->vma.root;
	struct rb_node *parent = 0;

	while (*plink) {
		struct vma *vma = TREE_ENTRY(*plink, struct vma, link);

		if (begin >= vma->end) {
			parent = *plink;
			plink = &parent->right;
		} else if (end <= vma->begin) {
			parent = *plink;
			plink = &parent->left;
		} else {
			iter->plink = plink;
			iter->parent = parent;
			iter->vma = vma;
			return 1;
		}
	}

	iter->plink = plink;
	iter->parent = parent;
	iter->vma = 0;
	return 0;
}

struct vma *lookup_vma(struct mm *mm, virt_t addr)
{
	struct vma_iter iter;

	addr &= ~((virt_t)PAGE_MASK);
	__lookup_vma(mm, addr, addr + 1, &iter);
	return iter.vma;
}

int mm_page_fault(struct thread *thread, virt_t vaddr, int access)
{
	struct mm *mm = thread->mm;

	if (!mm)
		return -EINVAL;

	vaddr &= ~((virt_t)PAGE_MASK);

	struct vma_iter iter;

	/*
	 * __lookup_vma won't work with empty regions, so +1.
	 * Seems like a dirty hack.
	 */
	if (!__lookup_vma(mm, vaddr, vaddr + 1, &iter))
		return -EINVAL;

	return iter.vma->fault(mm, iter.vma, vaddr, access);
}

static void insert_vma(struct mm *mm, struct vma *vma)
{
	struct vma_iter iter;

	__lookup_vma(mm, vma->begin, vma->end, &iter);
	rb_link(&vma->link, iter.parent, iter.plink);
	rb_insert(&vma->link, &mm->vma);	
}

int __mmap(struct mm *mm, virt_t begin, virt_t end, int perm)
{
	struct vma_iter iter;

	begin = ALIGN_DOWN_CONST(begin, PAGE_SIZE);
	end = ALIGN_CONST(end, PAGE_SIZE);

	if (__lookup_vma(mm, begin, end, &iter))
		return -EBUSY;

	int rc = pt_populate_range(page_addr(mm->pt), begin, end);

	if (rc)
		return rc;

	struct vma *vma = alloc_vma();

	if (!vma) {
		pt_release_range(page_addr(mm->pt), begin, end);
		return -ENOMEM;
	}

	vma->begin = begin;
	vma->end = end;
	vma->perm = perm;
	vma->mm = mm;
	vma->fault = &anon_page_fault;

	rb_link(&vma->link, iter.parent, iter.plink);
	rb_insert(&vma->link, &mm->vma);

	return 0;
}

int mmap(virt_t begin, virt_t end, int perm)
{
	return __mmap(current()->mm, begin, end, perm);
}

void __munmap(struct mm *mm, virt_t begin, virt_t end)
{
	struct vma_iter iter;

	__munmap_pages(mm, begin, (end - begin) >> PAGE_BITS);
	pt_release_range(page_addr(mm->pt), begin, end);

	while (__lookup_vma(mm, begin, end, &iter)) {
		struct vma *vma = iter.vma;

		if (vma->begin >= begin) {
			if (vma->end <= end) {
				rb_erase(&vma->link, &mm->vma);
				free_vma(vma);
			} else {
				vma->begin = end;
			}
		} else {
			if (vma->end <= end) {
				vma->end = begin;
			} else {
				struct vma *high = 0;

				/* we must not fail */
				while (!high)
					high = alloc_vma();

				*high = *vma;
				high->begin = end;
				vma->end = begin;
				insert_vma(mm, high);
			}
		}
	}
}

void munmap(virt_t begin, virt_t end)
{
	__munmap(current()->mm, begin, end);
}

struct mm *create_mm(void)
{
	struct mm *mm = alloc_mm();

	if (!mm)
		return 0;

	struct page *pt = alloc_page_table();

	if (!pt) {
		free_mm(mm);
		return 0;
	}

	/*
	 * kernel part is shared between the all threads, so we need to
	 * copy at least kernel part of page table, to make it usable.
	 */
	const size_t offset = pml4_i(HIGH_BASE) * sizeof(pte_t);

	memcpy((char *)page_addr(pt) + offset,
		(char *)va(load_pml4()) + offset, PAGE_SIZE - offset);
	mm->pt = pt;

	return mm;
}

static int copy_vma(struct mm *dst, struct vma *vma)
{
	if (__mmap(dst, vma->begin, vma->end, vma->perm))
		return -ENOMEM;

	struct mm *old_mm = vma->mm;
	pte_t *pt = page_addr(old_mm->pt);
	struct pt_iter iter;

	for_each_slot_in_range(pt, vma->begin, vma->end, iter) {
		DBG_ASSERT(iter.level == 0);
		DBG_ASSERT(iter.pt[iter.level] != 0);

		const int level = iter.level;
		const int index = iter.idx[level];
		const pte_t pte = iter.pt[level][index];

		if (!pte_present(pte))
			continue;

		const phys_t phys = pte_phys(pte);
		struct page *page = pfn2page(phys >> PAGE_BITS);

		if ((pte & PTE_WRITE) != 0) {
			iter.pt[level][index] &= ~((pte_t)PTE_WRITE);
			flush_tlb_addr(iter.addr);
		}
		__mmap_pages(dst, iter.addr, &page, 1, PTE_USER);
	}

	return 0;
}

int copy_mm(struct mm *dst, struct mm *src)
{
	struct rb_node *ptr = rb_leftmost(src->vma.root);

	while (ptr) {
		struct vma *vma = TREE_ENTRY(ptr, struct vma, link);
		const int rc = copy_vma(dst, vma);

		if (rc)
			return rc;
		ptr = rb_next(ptr);
	}

	return 0;
}

static void unmap_all_vma(struct mm *mm)
{
	struct rb_node *ptr = rb_leftmost(mm->vma.root);

	while (ptr) {
		struct vma *vma = TREE_ENTRY(ptr, struct vma, link);

		ptr = rb_next(ptr);
		__munmap(mm, vma->begin, vma->end);
	}
}

void __mmap_pages(struct mm *mm, virt_t addr, struct page **pages, pfn_t count,
			unsigned long flags)
{
	DBG_ASSERT((addr & PAGE_MASK) == 0);

	const virt_t from = addr;
	const virt_t to = from + ((virt_t)count << PAGE_BITS);

	struct pt_iter iter;
	pfn_t i = 0;

	for_each_slot_in_range(page_addr(mm->pt), from, to, iter) {
		DBG_ASSERT(iter.level == 0);
		DBG_ASSERT(iter.pt[iter.level] != 0);

		struct page *page = pages[i++];
		const phys_t paddr = page_paddr(page);
		const int index = iter.idx[iter.level];
		pte_t *pt = iter.pt[iter.level];

		get_page(page);
		pt[index] = paddr | flags | PTE_PRESENT;
	}
}

void __munmap_pages(struct mm *mm, virt_t addr, pfn_t count)
{
	DBG_ASSERT((addr & PAGE_MASK) == 0);

	const virt_t from = addr;
	const virt_t to = from + ((virt_t)count << PAGE_BITS);

	struct pt_iter iter;

	for_each_slot_in_range(page_addr(mm->pt), from, to, iter) {
		DBG_ASSERT(iter.level == 0);
		DBG_ASSERT(iter.pt[iter.level] != 0);

		const int index = iter.idx[iter.level];
		pte_t *pt = iter.pt[iter.level];
		const pte_t pte = pt[index];

		pt[index] = 0;
		if (pte_present(pte)) {
			const phys_t phys = pte_phys(pte);
			const pfn_t pfn = phys >> PAGE_BITS;
			struct page *page = pfn2page(pfn);

			put_page(page);
		}
	}
}

void release_mm(struct mm *mm)
{
	unmap_all_vma(mm);
	free_page_table(mm->pt);
	free_mm(mm);
}

void setup_mm(void)
{
	DBG_ASSERT((mm_cachep = KMEM_CACHE(struct mm)) != 0);
	DBG_ASSERT((vma_cachep = KMEM_CACHE(struct vma)) != 0);
	DBG_ASSERT((zero_page = alloc_pages(0)) != 0);
	memset(page_addr(zero_page), 0, PAGE_SIZE);
	zero_page->u.refcount = 1;
}

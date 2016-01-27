#include "kmem_cache.h"
#include "threads.h"
#include "memory.h"
#include "string.h"
#include "error.h"
#include "mm.h"

#include <stdbool.h>


static struct kmem_cache *mm_cachep;
static struct kmem_cache *vma_cachep;

static bool put_page(struct page *page)
{
	if (--page->u.refcount == 0) {
		free_pages(page, 0);
		return true;
	}
	return false;
}

/*
static struct page *get_page(struct page *page)
{
	if (page)
		++page->u.refcount;
	return page;
}

static struct page *alloc_page(void)
{
	return get_page(alloc_pages(0, NT_HIGH));
}
*/

static struct page *alloc_page_table(void)
{
	return alloc_pages(0, NT_LOW);
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

static int anon_page_fault(struct thread *thread, struct vma *vma,
			virt_t vaddr, int access)
{
	(void) thread;
	(void) vma;
	(void) vaddr;
	(void) access;

	return -1;
}

struct vma_iter {
	struct rb_node **plink;
	struct rb_node *parent;
	struct vma *vma;
};

static int lookup_vma(struct mm *mm, virt_t begin, virt_t end,
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

static void insert_vma(struct mm *mm, struct vma *vma)
{
	struct vma_iter iter;

	lookup_vma(mm, vma->begin, vma->end, &iter);
	rb_link(&vma->link, iter.parent, iter.plink);
	rb_insert(&vma->link, &mm->vma);	
}

static int __mmap(struct thread *thread, virt_t begin, virt_t end, int perm)
{
	struct mm *mm = thread->mm;
	struct vma_iter iter;

	if (lookup_vma(mm, begin, end, &iter))
		return -EBUSY;

	struct vma *vma = alloc_vma();

	if (!vma)
		return -ENOMEM;

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
	return __mmap(current(), begin, end, perm);
}

static bool unmap_pml1_range(struct page *page, virt_t begin, virt_t end)
{
	pte_t *pml1 = page_addr(page);
	bool put = false;

	for (int i = pml1_index(begin); i != pml1_index(end); ++i) {
		const pte_t pte = pml1[i];
		const pfn_t pfn = pte_phys(pte) >> PAGE_BITS;

		if ((pte & PTE_PRESENT)) {
			struct page *user = pfn2page(pfn);

			pml1[i] = 0;
			put_page(user);
			put |= put_page(page);
			flush_tlb_page(begin);
		}
		begin += PAGE_SIZE;
	}
	return put;
}

static bool unmap_pml2_range(struct page *page,
			virt_t begin, virt_t end)
{
	pte_t *pml2 = page_addr(page);
	bool put = false;

	for (int i = pml2_index(begin); i != pml2_index(end); ++i) {
		const pfn_t remain = (end - begin) >> PAGE_BITS;
		const pfn_t to_unmap = MINU(remain, PML1_PAGES);
		const virt_t bytes = (virt_t)PAGE_SIZE * to_unmap;
		const pte_t pte = pml2[i];
		const pfn_t pfn = pte_phys(pte) >> PAGE_BITS;

		if ((pte & PTE_PRESENT)) {
			struct page *pml1 = pfn2page(pfn);

			if (unmap_pml1_range(pml1, begin, begin + bytes)) {
				pml2[i] = 0;
				put |= put_page(page);
			}
		}
		begin += bytes;
	}
	return put;
}

static bool unmap_pml3_range(struct page *page, virt_t begin, virt_t end)
{
	pte_t *pml3 = page_addr(page);
	bool put = false;

	for (int i = pml3_index(begin); i != pml3_index(end); ++i) {
		const pfn_t remain = (end - begin) >> PAGE_BITS;
		const pfn_t to_unmap = MINU(remain, PML2_PAGES);
		const virt_t bytes = (virt_t)PAGE_SIZE * to_unmap;
		const pte_t pte = pml3[i];
		const pfn_t pfn = pte_phys(pte) >> PAGE_BITS;

		if ((pte & PTE_PRESENT)) {
			struct page *pml2 = pfn2page(pfn);

			if (unmap_pml2_range(pml2, begin, begin + bytes)) {
				pml3[i] = 0;
				put |= put_page(page);
			}
		}
		begin += bytes;
	}
	return put;
}

static void unmap_vma_range(struct mm *mm, virt_t begin, virt_t end)
{
	pte_t *pml4 = page_addr(mm->pt);

	for (int i = pml4_index(begin); i != pml4_index(end); ++i) {
		const pfn_t remain = (end - begin) >> PAGE_BITS;
		const pfn_t to_unmap = MINU(remain, PML3_PAGES);
		const virt_t bytes = (virt_t)PAGE_SIZE * to_unmap;
		const pte_t pte = pml4[i];
		const pfn_t pfn = pte_phys(pte) >> PAGE_BITS;

		if ((pte & PTE_PRESENT)) {
			struct page *pml3 = pfn2page(pfn);

			if (unmap_pml3_range(pml3, begin, begin + bytes))
				pml4[i] = 0;
		}
		begin += bytes;
	}
}

static void __munmap(struct thread *thread, virt_t begin, virt_t end)
{
	struct mm *mm = thread->mm;
	struct vma_iter iter;

	unmap_vma_range(mm, begin, end);
	while (lookup_vma(mm, begin, end, &iter)) {
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
	__munmap(current(), begin, end);
}

int setup_thread_memory(struct thread *thread)
{
	struct mm *mm = alloc_mm();

	if (!mm)
		return -ENOMEM;

	struct page *pt = alloc_page_table();

	if (!pt) {
		free_mm(mm);
		return -ENOMEM;
	}

	/*
	 * kernel part is shared between the all threads, so we need to
	 * copy at least kernel part of page table.
	 */
	memcpy(page_addr(pt), kernel_virt(load_pml4()), PAGE_SIZE);
	mm->pt = pt;
	thread->mm = mm;

	return 0;
}

static void unmap_all_vma(struct thread *thread, struct mm *mm)
{
	struct rb_node *ptr = rb_leftmost(mm->vma.root);

	while (ptr) {
		struct vma *vma = TREE_ENTRY(ptr, struct vma, link);

		ptr = rb_next(ptr);
		__munmap(thread, vma->begin, vma->end);
	}
}

void release_thread_memory(struct thread *thread)
{
	struct mm *mm = thread->mm;

	unmap_all_vma(thread, mm);
	thread->mm = 0;
	free_page_table(mm->pt);
	free_mm(mm);
}

void setup_mm(void)
{
	mm_cachep = KMEM_CACHE(struct mm);
	vma_cachep = KMEM_CACHE(struct vma);
}

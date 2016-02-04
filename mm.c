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

static unsigned long get_page_flags(int vma_flags)
{
	return ((vma_flags & VMA_PERM_WRITE) != 0)
				? (PTE_USER | PTE_WRITE)
				: PTE_USER;
}

static struct page *copy_page(struct page *page)
{
	if (page->u.refcount == 1) {
		get_page(page);
		return page;
	}

	struct page *new = alloc_pages(0);

	if (!new)
		return 0;

	void *dst = page_addr(new);
	void *src = page_addr(page);

	memcpy(dst, src, PAGE_SIZE);
	get_page(new);
	return new;
}

static int anon_page_fault(struct vma *vma, virt_t vaddr, int access)
{
	if (access == VMA_ACCESS_WRITE && (vma->perm & VMA_PERM_WRITE) == 0)
		return -EINVAL;

	if (access == VMA_ACCESS_READ) {
		get_page(zero_page);

		const int rc = map_range(page_addr(vma->mm->pt),
					vaddr,
					page_paddr(zero_page),
					1, 0);

		if (rc)
			put_page(zero_page);

		return rc;
	}

	struct pages set;

	if (gather_pages(page_addr(vma->mm->pt), vaddr, 1, &set) != 0) {
		struct page *old = LIST_ENTRY(list_first(&set.head),
					struct page, link);
		struct page *new = copy_page(old);

		if (!new)
			return -ENOMEM;

		const int rc = map_range(page_addr(vma->mm->pt),
					vaddr, page_paddr(new), 1,
					get_page_flags(vma->perm));

		if (rc)
			put_page(new);
		else
			put_page(old);

		return rc;
	}

	struct page *page = alloc_pages(0);

	if (!page)
		return -ENOMEM;

	get_page(page);	
	memset(page_addr(page), 0, PAGE_SIZE);

	const int rc = map_range(page_addr(vma->mm->pt),
				vaddr, page_paddr(page), 1,
				get_page_flags(vma->perm));

	if (rc)
		put_page(page);

	return rc;
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

int mm_page_fault(struct thread *thread, virt_t vaddr, int access)
{
	struct mm *mm = thread->mm;

	if (!mm)
		return -EINVAL;

	vaddr &= ~((virt_t)(PAGE_SIZE - 1));

	struct vma_iter iter;

	/*
	 * lookup_vma won't work with empty regions, so +1.
	 * Seems like a dirty hack.
	 */
	if (!lookup_vma(mm, vaddr, vaddr + 1, &iter))
		return -EINVAL;

	return iter.vma->fault(iter.vma, vaddr, access);
}

static void insert_vma(struct mm *mm, struct vma *vma)
{
	struct vma_iter iter;

	lookup_vma(mm, vma->begin, vma->end, &iter);
	rb_link(&vma->link, iter.parent, iter.plink);
	rb_insert(&vma->link, &mm->vma);	
}

int __mmap(struct mm *mm, virt_t begin, virt_t end, int perm)
{
	struct vma_iter iter;

	begin = ALIGN_DOWN_CONST(begin, PAGE_SIZE);
	end = ALIGN_CONST(end, PAGE_SIZE);

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
	return __mmap(current()->mm, begin, end, perm);
}

static void unmap_vma_range(struct mm *mm, virt_t begin, virt_t end)
{
	struct pages set;

	gather_pages(page_addr(mm->pt), begin, (end - begin) >> PAGE_BITS,
			&set);
	unmap_range(page_addr(mm->pt), begin, (end - begin) >> PAGE_BITS);

	struct list_head *head = &set.head;
	struct list_head *ptr = head->next;

	while (ptr != head) {
		struct page *page = LIST_ENTRY(ptr, struct page, link);

		ptr = ptr->next;
		put_page(page);
	}
}

void __munmap(struct mm *mm, virt_t begin, virt_t end)
{
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

static void unmap_all_vma(struct mm *mm)
{
	struct rb_node *ptr = rb_leftmost(mm->vma.root);

	while (ptr) {
		struct vma *vma = TREE_ENTRY(ptr, struct vma, link);

		ptr = rb_next(ptr);
		__munmap(mm, vma->begin, vma->end);
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

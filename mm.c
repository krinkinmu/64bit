#include "kmem_cache.h"
#include "threads.h"
#include "memory.h"
#include "string.h"
#include "error.h"
#include "mm.h"

#include <stdbool.h>


static struct kmem_cache *mm_cachep;
static struct kmem_cache *vma_cachep;

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

static void unmap_vma_range(struct mm *mm, virt_t begin, virt_t end)
{
	unmap_range(page_addr(mm->pt), begin, (end - begin) >> PAGE_BITS);
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

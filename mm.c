#include "kmem_cache.h"
#include "threads.h"
#include "memory.h"
#include "string.h"
#include "error.h"
#include "mm.h"


static struct kmem_cache *mm_cachep;
static struct kmem_cache *vma_cachep;

/*
static void put_page(struct page *page)
{
	if (--page->u.refcount == 0)
		free_pages(page, 0);
}

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

/*
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
*/

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

void release_thread_memory(struct thread *thread)
{
	struct mm *mm = thread->mm;

	thread->mm = 0;
	free_page_table(mm->pt);
	free_mm(mm);
}

int mmap(uintptr_t begin, uintptr_t end, int perm)
{
	(void) begin;
	(void) end;
	(void) perm;
	return 0;
}

int munmap(uintptr_t begin, uintptr_t end)
{
	(void) begin;
	(void) end;
	return 0;
}

void setup_mm(void)
{
	mm_cachep = KMEM_CACHE(struct mm);
	vma_cachep = KMEM_CACHE(struct vma);
}

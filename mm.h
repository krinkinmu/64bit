#ifndef __MM_H__
#define __MM_H__

#include "rbtree.h"
#include "memory.h"
#include "paging.h"


enum vma_perm {
	VMA_PERM_WRITE = 1 << 0,
};

enum vma_access {
	VMA_ACCESS_READ,
	VMA_ACCESS_WRITE
};

struct mm;

struct vma {
	struct rb_node link;
	virt_t begin;
	virt_t end;
	int perm;
	struct mm *mm;
	int (*fault)(struct mm *, struct vma *, virt_t, int);
};

struct mm {
	struct rb_tree vma;
	struct page *pt;
};


struct mm *create_mm(void);
int copy_mm(struct mm * dst, struct mm *src);
void release_mm(struct mm *mm);

struct thread;

int mm_page_fault(struct thread *thread, virt_t vaddr, int access);

/* work with current thread mm */
int __mmap(struct mm *mm, virt_t begin, virt_t end, int perm);
void __munmap(struct mm *mm, virt_t begin, virt_t end);
void __mmap_pages(struct mm *mm, virt_t addr, struct page **pages, pfn_t count,
			unsigned long flags);
void __munmap_pages(struct mm *mm, virt_t addr, pfn_t count);
int mmap(virt_t begin, virt_t end, int perm);
void munmap(virt_t begin, virt_t end);

void setup_mm(void);

#endif /*__MM_H__*/

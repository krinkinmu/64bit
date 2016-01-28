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
	uintptr_t begin;
	uintptr_t end;
	int perm;
	struct mm *mm;
	int (*fault)(struct vma *, virt_t, int);
};

struct mm {
	struct rb_tree vma;
	struct page *pt;
};


struct thread;

int setup_thread_memory(struct thread *thread);
void release_thread_memory(struct thread *thread);

int mm_page_fault(struct thread *thread, virt_t vaddr, int access);

/* work with current thread mm */
int mmap(virt_t begin, virt_t end, int perm);
void munmap(virt_t begin, virt_t end);

void setup_mm(void);

#endif /*__MM_H__*/

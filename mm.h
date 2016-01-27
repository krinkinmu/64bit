#ifndef __MM_H__
#define __MM_H__

#include "rbtree.h"
#include "memory.h"
#include "paging.h"


enum vma_perm {
	VMA_PERM_WRITE = 1 << 0,
	VMA_PERM_EXECUTE = 1 << 1
};

enum vma_access {
	VMA_ACCESS_READ,
	VMA_ACCESS_WRITE,
	VMA_ACCESS_EXECUTE
};

struct mm;

struct vma {
	struct rb_node link;
	uintptr_t begin;
	uintptr_t end;
	int perm;
	struct mm *mm;
	int (*fault)(virt_t vaddr, int access);
};

struct mm {
	struct rb_tree vma;
	struct page *pt;
};


struct thread;

int setup_thread_memory(struct thread *thread);
void release_thread_memory(struct thread *thread);

/* work with current thread mm */
int mmap(virt_t begin, virt_t end, int perm);
int munmap(virt_t begin, virt_t end);

void setup_mm(void);

#endif /*__MM_H__*/

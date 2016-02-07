#include "thread_regs.h"
#include "threads.h"
#include "kernel.h"
#include "paging.h"
#include "memory.h"
#include "string.h"
#include "stdio.h"
#include "error.h"
#include "exec.h"
#include "vfs.h"
#include "mm.h"

#include <stdint.h>
#include <stddef.h>


#ifndef CONFIG_USER_STACK_SIZE
#define USER_STACK_SIZE CONFIG_USER_STACK_SIZE
#else
#define USER_STACK_SIZE (2ul * 1024ul * 1024ul)
#endif

#define ELF_NIDENT      16
#define ELF_CLASS       4
#define ELF_DATA        5
#define ELF_CLASS64     2
#define ELF_DATA2LSB    1
#define ELF_EXEC        2

struct elf_hdr {
	uint8_t e_ident[ELF_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
} __attribute__((packed));


#define PT_NULL 0
#define PT_LOAD 1
#define PF_X    0x1
#define PF_W    0x2
#define PF_R    0x4

struct elf_phdr {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
} __attribute__((packed));


static int read_buf(struct fs_file *file, void *data, size_t size)
{
	char *buf = data;
	size_t rd = 0;

	while (rd < size) {
		const int rc = vfs_read(file, buf + rd, size - rd);

		if (rc < 0)
			return rc;

		if (rc == 0)
			return -EIO;

		rd += rc;
	}

	return 0;
}

static int read_elf_hdr(struct fs_file *file, struct elf_hdr *hdr)
{
	const int rc = vfs_seek(file, 0, FSS_SET);

	if (rc < 0)
		return rc;

	return read_buf(file, hdr, sizeof(*hdr));
}

static int read_elf_phdr(struct fs_file *file, struct elf_phdr *phdr)
{
	return read_buf(file, phdr, sizeof(*phdr));
}

static int check_elf_hdr(const struct elf_hdr *hdr)
{
	if (memcmp(hdr->e_ident, "\x7f""ELF", 4)) // heh...
		return -ENOEXEC;

	if (hdr->e_ident[ELF_CLASS] != ELF_CLASS64)
		return -ENOEXEC;

	if (hdr->e_ident[ELF_DATA] != ELF_DATA2LSB)
		return -ENOEXEC;

	if (hdr->e_type != ELF_EXEC)
		return -ENOEXEC;

	return 0;
}

static int map_elf_phdr(struct mm *mm, const struct elf_phdr *phdr,
			struct fs_file *file)
{
	if (phdr->p_type != PT_LOAD)
		return 0;

	const virt_t begin = phdr->p_vaddr;
	const virt_t end = begin + phdr->p_memsz;
	const int perm = ((phdr->p_flags & PF_W) != 0) ? VMA_PERM_WRITE : 0;
	const unsigned long flags = (perm == VMA_PERM_WRITE) ? PTE_WRITE : 0;

	int rc = __mmap(mm, begin, end, perm);

	if (rc)
		return rc;

	if (!phdr->p_filesz)
		return 0;

	rc = vfs_seek(file, (int)phdr->p_offset, FSS_SET);
	if (rc < 0)
		return rc;

	struct page *page = 0;
	virt_t addr = ALIGN_DOWN(begin, PAGE_SIZE);
	size_t offset = begin & PAGE_MASK;
	size_t remain = phdr->p_filesz;

	while (remain) {
		page = alloc_pages(0);

		if (!page) {
			__munmap(mm, begin, end);
			return -ENOMEM;
		}

		const size_t size = MINU(remain, PAGE_SIZE - offset);
		char *buffer = page_addr(page);

		memset(buffer, 0, PAGE_SIZE);

		const int rc = read_buf(file, buffer + offset, size);

		if (rc) {
			free_pages(page, 0);
			__munmap(mm, begin, end);
			return rc;
		}

		__mmap_pages(mm, addr, &page, 1, flags | PTE_USER);

		addr += PAGE_SIZE;
		remain -= size;
		offset = 0;
	}

	return 0;
}

static int load_binary(struct mm *mm, const struct elf_hdr *hdr,
			struct fs_file *file)
{
	int offset = hdr->e_phoff;

	for (int i = 0; i != (int)hdr->e_phnum; ++i) {
		int rc = vfs_seek(file, offset, FSS_SET);

		if (rc < 0)
			return rc;

		struct elf_phdr phdr;

		rc = read_elf_phdr(file, &phdr);
		if (rc < 0)
			return rc;

		rc = map_elf_phdr(mm, &phdr, file);
		if (rc < 0)
			return rc;

		offset += hdr->e_phentsize;
	}

	return 0;
}

static int setup_stack(struct mm *mm, size_t size)
{
	const virt_t stack_size = ALIGN(size, PAGE_SIZE);
	const virt_t stack_begin = TASK_SIZE - stack_size;
	const virt_t stack_end = TASK_SIZE;

	const int rc = __mmap(mm, stack_begin, stack_end, VMA_PERM_WRITE);

	if (rc)
		return rc;

	mm->stack = lookup_vma(mm, stack_begin);
	DBG_ASSERT(mm->stack != 0);
	return 0;
}

int exec(const char *name)
{
	struct fs_file file;

	int rc = vfs_open(name, &file);

	if (rc)
		return -EIO;

	struct elf_hdr hdr;

	rc = read_elf_hdr(&file, &hdr);
	if (rc) {
		vfs_release(&file);
		return rc;
	}

	rc = check_elf_hdr(&hdr);
	if (rc) {
		vfs_release(&file);
		return rc;
	}

	struct mm *new_mm = create_mm();

	if (!new_mm) {
		vfs_release(&file);
		return -ENOMEM;
	}

	rc = setup_stack(new_mm, USER_STACK_SIZE);
	if (rc) {
		release_mm(new_mm);
		vfs_release(&file);
		return rc;
	}

	rc = load_binary(new_mm, &hdr, &file);
	if (rc) {
		release_mm(new_mm);
		vfs_release(&file);
		return rc;
	}

	struct thread *thread = current();
	struct mm *old_mm = thread->mm;

	thread->mm = new_mm;
	store_pml4(page_paddr(new_mm->pt));
	release_mm(old_mm);

	struct thread_regs *regs = thread_regs(thread);

	memset(regs, 0, sizeof(*regs));
	regs->rip = hdr.e_entry;
	regs->rsp = new_mm->stack->end;
	regs->cs = USER_CS;
	regs->ss = USER_DS;
	regs->rflags = RFLAGS_IF;
	vfs_release(&file);

	return rc;
}

#include "string.h"
#include "stdio.h"
#include "error.h"
#include "exec.h"
#include "vfs.h"

#include <stdint.h>
#include <stddef.h>


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

		if (rc <= 0)
			return -EIO;
		rd += rc;
	}

	return 0;
}

static int read_elf_hdr(struct fs_file *file, struct elf_hdr *hdr)
{
	if (vfs_seek(file, 0, FSS_SET) < 0)
		return -EIO;

	return read_buf(file, hdr, sizeof(*hdr));
}

static int read_elf_phdr(struct fs_file *file, struct elf_phdr *phdr)
{
	return read_buf(file, phdr, sizeof(*phdr));
}

static void dump_elf_phdr(const struct elf_phdr *phdr)
{
	DBG_INFO("type: %x, file offset: %#llx, file size: %#llx",
		phdr->p_type, phdr->p_offset, phdr->p_filesz);
	DBG_INFO("load address: %p, memory size: %#llx",
		phdr->p_vaddr, phdr->p_memsz);
}

static int dump_elf_phdrs(struct fs_file *file, const struct elf_hdr *hdr)
{
	size_t offset = hdr->e_phoff;

	for (int i = 0; i != (int)hdr->e_phnum; ++i) {
		if (vfs_seek(file, offset, FSS_SET) < 0)
			return -EIO;

		struct elf_phdr phdr;

		if (read_elf_phdr(file, &phdr))
			return -EIO;

		dump_elf_phdr(&phdr);
		offset += hdr->e_phentsize;
	}
	return 0;
}

static int dump_elf_file(struct fs_file *file)
{
	struct elf_hdr hdr;

	if (read_elf_hdr(file, &hdr))
		return -EIO;

	if (memcmp(hdr.e_ident, "\x7f""ELF", 4)) { // heh...
		DBG_ERR("Not an elf file");
		return -EINVAL;
	}

	if (hdr.e_ident[ELF_CLASS] != ELF_CLASS64) {
		DBG_ERR("Not a 64bit elf");
		return -EINVAL;
	}

	if (hdr.e_ident[ELF_DATA] != ELF_DATA2LSB) {
		DBG_ERR("Not a little-endian binary");
		return -EINVAL;
	}

	if (hdr.e_type != ELF_EXEC) {
		DBG_ERR("Not an executable file");
		return -EINVAL;
	}

	DBG_INFO("ident: %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x",
		hdr.e_ident[0],  hdr.e_ident[1],  hdr.e_ident[2],
		hdr.e_ident[3],  hdr.e_ident[4],  hdr.e_ident[5],
		hdr.e_ident[6],  hdr.e_ident[7],  hdr.e_ident[8],
		hdr.e_ident[9],  hdr.e_ident[10], hdr.e_ident[11],
		hdr.e_ident[12], hdr.e_ident[13], hdr.e_ident[14],
		hdr.e_ident[15]);

	DBG_INFO("type: %x", hdr.e_type);	
	DBG_INFO("machine: %x", hdr.e_machine);	
	DBG_INFO("version: %x", hdr.e_version);	
	DBG_INFO("entry: %p", hdr.e_entry);	
	DBG_INFO("program header offset: %llx", hdr.e_phoff);	
	DBG_INFO("flags: %x", hdr.e_flags);
	DBG_INFO("program header entry size: %x", hdr.e_phentsize);	
	DBG_INFO("program header entry count: %x", hdr.e_phnum);

	return dump_elf_phdrs(file, &hdr);
}

int exec(const char *name)
{
	struct fs_file file;

	if (vfs_open(name, &file))
		return -EIO;

	const int rc = dump_elf_file(&file);

	vfs_release(&file);

	return rc;
}

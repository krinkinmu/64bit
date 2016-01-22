#include "multiboot.h"
#include "string.h"
#include "stdio.h"
#include "misc.h"

#include <stdint.h>


#define MMAP_MAX_COUNT  20
#define MAX_CMDLINE_LEN 256

const char *cmdline;
struct mmap_entry mmap[MMAP_MAX_COUNT];
int mmap_count;

unsigned long initrd_begin;
unsigned long initrd_end;

struct mboot_mmap_entry {
	uint32_t size;
	uint64_t addr;
	uint64_t length;
	uint32_t type;
} __attribute__((packed));

static void setup_mmap(struct multiboot_info *info)
{
	if ((info->flags & (1ul << 6)) == 0) {
		DBG_ERR("mmap info isn't available");
		while (1);
	}

	const char *begin = (const char *)((uintptr_t)info->mmap_addr);
	const char *end = begin + info->mmap_length;

	while (begin < end) {
		const struct mboot_mmap_entry *entry =
					(const struct mboot_mmap_entry *)begin;

		mmap[mmap_count].addr = entry->addr;
		mmap[mmap_count].length = entry->length;
		mmap[mmap_count].type = entry->type;

		begin += entry->size + sizeof(entry->size);
		++mmap_count;
	}
}

static void setup_cmdline(const struct multiboot_info *info)
{
	static char cmdline_buf[MAX_CMDLINE_LEN];

	if (info->flags & (1ul << 2)) {
		strcpy(cmdline_buf, (const char *)((uintptr_t)info->cmdline));
		cmdline = cmdline_buf;
	}
}

struct mboot_mod {
	uint32_t mod_start;
	uint32_t mod_end;
	uint32_t string;
	uint32_t reserved;
} __attribute__((packed));

static void setup_initrd(const struct multiboot_info *info)
{
	if ((info->flags & (1ul << 3)) == 0 || info->mods_count == 0) {
		DBG_ERR("No initrd provided!\n");
		while (1);
	}

	const struct mboot_mod *mods =
			(const struct mboot_mod *)((uintptr_t)info->mods_addr);

	for (int i = 0; i != (int)info->mods_count; ++i) {
		const struct mboot_mod *mod = mods + i;

		if (mod->mod_end - mod->mod_start < 6)
			continue;

		const void *ptr = (const void *)((uintptr_t)mod->mod_start);

		if (memcmp(ptr, "070701", 6))
			continue;

		initrd_begin = mod->mod_start;
		initrd_end = mod->mod_end;
		return;
	}

	DBG_ERR("No initrd found!\n");
	while (1);
}

void setup_misc(void)
{
	extern const uint32_t mboot_info;

	struct multiboot_info *pmboot_info = (void *)((uintptr_t)mboot_info);

	setup_mmap(pmboot_info);
	setup_cmdline(pmboot_info);
	setup_initrd(pmboot_info);
}

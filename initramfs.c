#include "initramfs.h"
#include "stdlib.h"
#include "string.h"
#include "error.h"
#include "misc.h"
#include "vfs.h"

#define S_IFMT  0xF000
#define S_IFDIR 0x4000
#define S_IFREG 0x8000

#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)

struct cpio_header {
	char magic[6];
	char inode[8];
	char mode[8];
	char uid[8];
	char gid[8];
	char nlink[8];
	char mtime[8];
	char filesize[8];
	char major[8];
	char minor[8];
	char rmajor[8];
	char rminor[8];
	char namesize[8];
	char chksum[8];
} __attribute__((packed));

static unsigned long parse_hex(const char *data)
{
	static char buffer[9];

	memset(buffer, 0, sizeof(buffer));
	memcpy(buffer, data, sizeof(buffer) - 1);
	return strtoul(buffer, 0, 16);
}

static void parse_cpio(const char *data, unsigned long size)
{
	unsigned long pos = 0;

	while (pos < size && size - pos >= sizeof(struct cpio_header)) {
		const struct cpio_header *hdr = (const void *)(data + pos);
		const unsigned long mode = parse_hex(hdr->mode);
		const unsigned long namesize = parse_hex(hdr->namesize);
		const unsigned long filesize = parse_hex(hdr->filesize);

		pos += sizeof(struct cpio_header);
		const char *filename = data + pos;
		pos = ALIGN(pos + namesize, 4);

		if (pos > size)
			break;

		if (strcmp(filename, "TRAILER!!!") == 0)
			break;

		const char *filedata = data + pos;
		pos = ALIGN(pos + filesize, 4);

		(void) filedata;

		if (S_ISDIR(mode)) {
			DBG_INFO("initramfs dir %s", filename);
		} else if (S_ISREG(mode)) {
			DBG_INFO("initramfs file %s", filename);
		}
	}
}

void setup_initramfs(void)
{
	const int rc = vfs_mount("ramfs", "initramfs", 0, 0);

	if (rc) {
		DBG_ERR("initramfs mount failed with error %s", errstr(rc));
		while (1);
	}

	parse_cpio((const char *)((uintptr_t)initrd_begin),
				initrd_end - initrd_begin);
}

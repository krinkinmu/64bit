#include <sys/types.h>
#include <unistd.h>

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "disk-io.h"

static int read_at(int fd, size_t off, void *data, size_t size)
{
	char *buf = data;
	size_t rd = 0;

	if (lseek(fd, off, SEEK_SET) == (off_t)-1) {
		perror("Seek failed");
		return -1;
	}

	while (rd != size) {
		const ssize_t rc = read(fd, buf + rd, size - rd);

		if (rc == 0) {
			fprintf(stderr, "Unexpected end of file\n");
			return -1;
		}

		if (rc < 0) {
			perror("Read failed");
			return -1;
		}

		rd += rc;
	}
	return 0;
}

static int write_at(int fd, size_t off, const void *data, size_t size)
{
	const char *buf = data;
	size_t wr = 0;

	if (lseek(fd, off, SEEK_SET) == (off_t)-1) {
		perror("Seek failed");
		return -1;
	}

	while (wr < size) {
		const ssize_t rc = write(fd, buf + wr, size - wr);

		if (rc < 0) {
			perror("Write failed");
			return -1;
		}

		wr += rc;
	}
	return 0;
}

struct hwfs_io_extent *hwfs_create_io_extent(uint64_t size)
{
	struct hwfs_io_extent *ext = malloc(sizeof(*ext) + size);

	if (ext) {
		memset(ext, 0, sizeof(*ext));
		ext->state = HWFS_EXTENT_NEW;
		ext->size = size;
		ext->data = ext + 1;
		ext->links = 1;
	}
	return ext;
}

void hwfs_destroy_io_extent(struct hwfs_io_extent *ext)
{
	free(ext);
}

struct hwfs_io_extent *hwfs_get_io_extent(struct hwfs_io_extent *extent)
{
	++extent->links;
	return extent;
}

void hwfs_put_io_extent(struct hwfs_io_extent *extent)
{
	if (--extent->links == 0)
		hwfs_destroy_io_extent(extent);
}

int hwfs_sync_io_extent(int fd, struct hwfs_io_extent *extent)
{
	if (extent->state == HWFS_EXTENT_UPTODATE)
		return 0;

	const size_t offset = extent->offset;
	const size_t size = extent->size;

	if (extent->state == HWFS_EXTENT_DIRTY) {
		if (write_at(fd, offset, extent->data, size))
			return -1;
	} else {
		if (read_at(fd, offset, extent->data, size))
			return -1;
	}

	extent->state = HWFS_EXTENT_UPTODATE;
	return 0;
}

int hwfs_write_io_extent(struct hwfs_io_extent *extent, size_t offset,
			const void *data, size_t size)
{
	if (offset + size > extent->size)
		return -1;

	memcpy((char *)extent->data + offset, data, size);
	extent->state = HWFS_EXTENT_DIRTY;
	return 0;
}

int hwfs_read_io_extent(struct hwfs_io_extent *extent, size_t offset,
			void *data, size_t size)
{
	if (offset + size > extent->size)
		return -1;

	memcpy(data, (const char *)extent->data + offset, size);
	return 0;
}

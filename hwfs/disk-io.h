#ifndef __HWFS_DISK_IO_H__
#define __HWFS_DISK_IO_H__

#include <stddef.h>

#include "rbtree.h"

enum hwfs_io_extent_state {
	HWFS_EXTENT_NEW,
	HWFS_EXTENT_DIRTY,
	HWFS_EXTENT_UPTODATE
};

struct hwfs_io_extent {
	struct rb_node link;
	uint64_t offset;
	uint64_t size;
	void *data;
	enum hwfs_io_extent_state state;
	int links;
};

struct hwfs_io_extent *hwfs_create_io_extent(uint64_t size);
void hwfs_destroy_io_extent(struct hwfs_io_extent *extent);
int hwfs_sync_io_extent(int fd, struct hwfs_io_extent *extent);
struct hwfs_io_extent *hwfs_get_io_extent(struct hwfs_io_extent *extent);
void hwfs_put_io_extent(struct hwfs_io_extent *extent);
int hwfs_write_io_extent(struct hwfs_io_extent *extent, size_t offset,
			const void *data, size_t size);
int hwfs_read_io_extent(struct hwfs_io_extent *extent, size_t offset,
			void *data, size_t size);

#endif /*__HWFS_DISK_IO_H__*/

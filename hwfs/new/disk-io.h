#ifndef __HWFS_DISK_IO_H__
#define __HWFS_DISK_IO_H__

#include <stddef.h>

#include "rbtree.h"
#include "list.h"

struct disk_io {
	struct rb_tree blocks;
	struct list_head lru;
	size_t block_count;
	size_t block_size;

	int fd;
};

void setup_disk_io(struct disk_io *dio, size_t block_size, int fd);
void release_disk_io(struct disk_io *dio);

struct disk_block {
	struct list_head link;
	struct rb_node node;
	size_t blocknr;
	void *data;
	int links;
};

struct disk_block *disk_get_block(struct disk_io *dio, size_t blocknr);
void disk_put_block(struct disk_io *dio, struct disk_block *block);

#endif /*__HWFS_DISK_IO_H__*/

#ifndef __HWFS_CACHE_H__
#define __HWFS_CACHE_H__

#include "rbtree.h"

struct hwfs_block_cache;

struct hwfs_block {
	struct hwfs_block_cache *cache;
	struct rb_node link;
	long blocknr;
	int links;
	void *data;
};

struct hwfs_block_cache {
	struct rb_tree blocks;
	int block_size;
	long disk_size;
	int fd;
};

void hwfs_block_cache_setup(struct hwfs_block_cache *cache, int fd,
			int block_size, long disk_size);
void hwfs_block_cache_release(struct hwfs_block_cache *cache);
struct hwfs_block *hwfs_get_block(struct hwfs_block_cache *cache, long block);
void hwfs_put_block(struct hwfs_block *block);

#endif /*__HWFS_CACHE_H__*/

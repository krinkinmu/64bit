#ifndef __HWFS_TRANSACTION_H__
#define __HWFS_TRANSACTION_H__

#include <stddef.h>

#include "disk-io.h"
#include "rbtree.h"

struct hwfs_free_space_info {
	struct rb_node link;
	uint64_t blocknr;
	uint64_t size;
	uint64_t free;
	uint64_t next;
};

struct hwfs_tree {
	struct hwfs_io_extent *root;
};

struct hwfs_trans {
	struct rb_tree free_space;
	struct rb_tree io_cache;
	struct rb_tree released;
	struct rb_tree dirty;
	struct hwfs_io_extent *super_block;
	struct hwfs_tree fs_tree;
	struct hwfs_tree extent_tree;
	size_t node_size;
	int fd;
};

int hwfs_trans_setup(struct hwfs_trans *trans, int fd);
void hwfs_trans_release(struct hwfs_trans *trans);
int hwfs_trans_commit(struct hwfs_trans *trans);
int hwfs_trans_reserve(struct hwfs_trans *trans, size_t data_size);

#endif /*__HWFS_TRANSACTION_H__*/

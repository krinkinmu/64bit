#ifndef __HWFS_TRANSACTION_H__
#define __HWFS_TRANSACTION_H__

#include <stddef.h>

#include "disk-io.h"
#include "rbtree.h"
#include "btree.h"

struct hwfs_trans {
	struct rb_tree io_cache;
	struct hwfs_io_extent *super_block;
	struct hwfs_tree fs_tree;
	struct hwfs_tree extent_tree;
	size_t node_size;
	int fd;
};

int hwfs_trans_setup(struct hwfs_trans *trans, int fd);
void hwfs_trans_release(struct hwfs_trans *trans);
int hwfs_trans_commit(struct hwfs_trans *trans);

int64_t hwfs_trans_alloc(struct hwfs_trans *trans, size_t blocks);
void hwfs_trans_free(struct hwfs_trans *trans, uint64_t blocknr, size_t blocks);

struct hwfs_io_extent *hwfs_trans_get_extent(struct hwfs_trans *trans,
			uint64_t blocknr);
struct hwfs_io_extent *hwfs_trans_get_new_extent(struct hwfs_trans *trans,
			uint64_t blocknr);
void hwfs_trans_put_extent(struct hwfs_trans *trans,
			struct hwfs_io_extent *ext);

#endif /*__HWFS_TRANSACTION_H__*/

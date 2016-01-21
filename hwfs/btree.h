#ifndef __BTREE_H__
#define __BTREE_H__

#include <stddef.h>

#include "hwfs.h"

struct disk_block;
struct disk_io;

struct free_space {
	uint64_t count;
	uint64_t next;
};

struct btree {
	struct disk_block *root;
};

int setup_btree(struct disk_io *dio, struct btree *btree, size_t blocknr);
void release_btree(struct disk_io *dio, struct btree *btree);

int btree_insert(struct disk_io *dio, struct free_space *fs,
			struct btree *tree,
			const struct hwfs_key *key,
			const void *data, size_t size);

#endif /*__BTREE_H__*/

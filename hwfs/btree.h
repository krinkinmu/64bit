#ifndef __B_PLUS_TREE_H__
#define __B_PLUS_TREE_H__

struct hwfs_block;
struct hwfs_key;

int hwfs_leaf_insert(struct hwfs_block *block, struct hwfs_key *key,
			const void *item, int size);

#endif /*__B_PLUS_TREE_H__*/

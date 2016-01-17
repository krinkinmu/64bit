#ifndef __B_PLUS_TREE_H__
#define __B_PLUS_TREE_H__

#include <stdint.h>


#define HWFS_MAX_HEIGHT 10

struct hwfs_block_cache;
struct hwfs_block;
struct hwfs_key;

struct hwfs_stripe {
	uint64_t blocknr;
	uint64_t count;
	uint64_t next;
};

struct hwfs_trans {
	struct hwfs_block_cache *cache;
	struct hwfs_block *super_block;
	struct hwfs_block *fs_tree_root;
	struct hwfs_block *extent_tree_root;
	struct hwfs_stripe free_space;
};

struct hwfs_iter {
	struct hwfs_block *node[HWFS_MAX_HEIGHT];
	int pos[HWFS_MAX_HEIGHT];
	int root_level;
};


static inline intmax_t MAX(intmax_t a, intmax_t b)
{ return a > b ? a : b; }

static inline intmax_t MIN(intmax_t a, intmax_t b)
{ return a < b ? a : b; }

void hwfs_trans_setup(struct hwfs_trans *trans, struct hwfs_block_cache *cache);
void hwfs_trans_release(struct hwfs_trans *trans);
int hwfs_leaf_insert(struct hwfs_block *block, struct hwfs_key *key,
			const void *item, int size);
int hwfs_get_key(struct hwfs_iter *iter, struct hwfs_key *key);
int hwfs_get_data_size(struct hwfs_iter *iter);
int hwfs_get_data(struct hwfs_iter *iter, void *data, int off, int sz);
int hwfs_next(struct hwfs_block_cache *cache, struct hwfs_iter *iter);
int hwfs_prev(struct hwfs_block_cache *cache, struct hwfs_iter *iter);
void hwfs_release_iter(struct hwfs_iter *iter);
int hwfs_lookup(struct hwfs_block_cache *cache, struct hwfs_block *root,
			struct hwfs_key *key, struct hwfs_iter *iter);

#endif /*__B_PLUS_TREE_H__*/

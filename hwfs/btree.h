#ifndef __B_PLUS_TREE_H__
#define __B_PLUS_TREE_H__

#include "rbtree.h"
#include "hwfs.h"

#define HWFS_MAX_HEIGHT 16
#define HWFS_MAX_BLOCKS 16

struct hwfs_io_extent;
struct hwfs_trans;

enum hwfs_node_state {
	HWFS_NODE_NEW,
	HWFS_NODE_UPTODATE,
	HWFS_NODE_DIRTY
};

struct hwfs_node {
	struct rb_node link;
	struct hwfs_tree_header header;
	uint64_t blocknr;
	int size;
	enum hwfs_node_state state;
	struct hwfs_io_extent *block[HWFS_MAX_BLOCKS];
	void *data;
};

struct hwfs_tree {
	struct rb_tree nodes;
	struct hwfs_node *root;
};

struct hwfs_tree_iter {
	struct hwfs_node *node[HWFS_MAX_HEIGHT];
	int slot[HWFS_MAX_HEIGHT];
	int height;
};

struct hwfs_node *hwfs_create_node(size_t size);
void hwfs_destroy_node(struct hwfs_node *node);
void hwfs_put_node(struct hwfs_trans *trans, struct hwfs_node *node);
int hwfs_read_node(struct hwfs_trans *trans, struct hwfs_node *node,
			uint64_t blocknr);
int hwfs_write_node(struct hwfs_trans *trans, struct hwfs_node *node);

int hwfs_setup_tree(struct hwfs_trans *trans,
			struct hwfs_tree *tree,
			uint64_t root);
void hwfs_release_tree(struct hwfs_trans *trans,
			struct hwfs_tree *tree);
struct hwfs_node *hwfs_new_node(struct hwfs_trans *trans,
			struct hwfs_tree *tree,
			size_t blocks);
struct hwfs_node *hwfs_get_node(struct hwfs_trans *trans,
			struct hwfs_tree *tree,
			uint64_t blocknr);
struct hwfs_node *hwfs_cow_node(struct hwfs_trans *trans,
			struct hwfs_tree *tree,
			struct hwfs_node *node);

static inline int hwfs_node_items(struct hwfs_node *node)
{
	return node->header.count;
}

void hwfs_leaf_get_value(struct hwfs_node *node, int pos,
			struct hwfs_value *value);
void hwfs_leaf_set_value(struct hwfs_node *node, int pos,
			const struct hwfs_value *value);

void hwfs_node_get_item(struct hwfs_node *node, int pos,
			struct hwfs_item *item);
void hwfs_node_set_item(struct hwfs_node *node, int pos,
			const struct hwfs_item *item);

int hwfs_leaf_pos(struct hwfs_node *node, const struct hwfs_key *key);
int hwfs_node_pos(struct hwfs_node *node, const struct hwfs_key *key);
int hwfs_leaf_room(struct hwfs_node *node);
void hwfs_leaf_insert(struct hwfs_node *node, const struct hwfs_key *key,
				const void *data, int size);

#endif /*__B_PLUS_TREE_H__*/

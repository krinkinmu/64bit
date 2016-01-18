#include <stdlib.h>

#include "transaction.h"
#include "btree.h"

struct hwfs_node *hwfs_create_node(void)
{
	struct hwfs_node *node = malloc(sizeof(*node));

	if (node) {
		memset(node, 0, sizeof(*node));
		node->state = HWFS_NODE_NEW;
	}
	return node;
}

void hwfs_destroy_node(struct hwfs_node *node)
{
	free(node);
}

static void hwfs_release_node(struct hwfs_trans *trans, struct hwfs_node *node)
{
	for (unsigned i = 0; i != HWFS_MAX_BLOCKS; ++i) {
		if (!node->block[i])
			continue;
		hwfs_trans_put_extent(trans, node->block[i]);
	}
	memset(node, 0, sizeof(*node));
}

int hwfs_read_node(struct hwfs_trans *trans, struct hwfs_node *node,
			uint64_t blocknr)
{
	node->block[0] = hwfs_trans_get_extent(trans, blocknr);

	if (!node->block[0])
		return -1;

	struct hwfs_disk_tree_header header;

	hwfs_read_io_extent(node->block[0], 0, &header, sizeof(header));
	hwfs_tree_to_host(&node->header, &header);

	for (unsigned i = 1; i < node->header.blocks; ++i) {
		node->block[i] = hwfs_trans_get_extent(trans, blocknr + i);

		if (!node->block[i]) {
			hwfs_release_node(trans, node);
			return -1;
		}
	}

	node->state = HWFS_NODE_UPTODATE;
	node->blocknr = blocknr;
	return 0;
}

void hwfs_put_node(struct hwfs_trans *trans, struct hwfs_node *node)
{
	hwfs_release_node(trans, node);
	hwfs_destroy_node(node);
}

struct hwfs_node_cache_iter {
	struct rb_node **plink;
	struct rb_node *parent;
	struct hwfs_node *node;
};

static int hwfs_node_cache_lookup(struct hwfs_tree *tree, uint64_t blocknr,
			struct hwfs_node_cache_iter *iter)
{
	struct rb_node **plink = &tree->nodes.root;
	struct rb_node *parent = 0;

	while (*plink) {
		struct hwfs_node *node = TREE_ENTRY(*plink, struct hwfs_node,
					link);
		const uint64_t x = node->blocknr;

		if (x == blocknr) {
			iter->plink = plink;
			iter->parent = parent;
			iter->node = node;
			return 1;
		}

		parent = *plink;
		if (x < blocknr)
			plink = &parent->right;
		else
			plink = &parent->left;
	}

	iter->plink = plink;
	iter->parent = parent;
	iter->node = 0;
	return 0;
}

int hwfs_setup_tree(struct hwfs_trans *trans,
			struct hwfs_tree *tree,
			uint64_t root)
{
	memset(tree, 0, sizeof(*tree));
	tree->root = hwfs_get_node(trans, tree, root);

	if (!tree->root)
		return -1;
	return 0;
}

static void __hwfs_put_nodes(struct hwfs_trans *trans, struct rb_node *node)
{
	while (!node) {
		struct hwfs_node *x = TREE_ENTRY(node, struct hwfs_node, link);

		__hwfs_put_nodes(trans, node->right);
		node = node->left;
		hwfs_put_node(trans, x);
	}
}

void hwfs_release_tree(struct hwfs_trans *trans,
			struct hwfs_tree *tree)
{
	__hwfs_put_nodes(trans, tree->nodes.root);
	memset(tree, 0, sizeof(*tree));
}

struct hwfs_node *hwfs_new_node(struct hwfs_trans *trans,
			struct hwfs_tree *tree,
			size_t blocks)
{
	struct hwfs_node *new = hwfs_create_node();

	if (!new)
		return 0;

	const int64_t blocknr = hwfs_trans_alloc(trans, blocks);

	if (blocknr < 0) {
		hwfs_destroy_node(new);
		return 0;
	}

	for (size_t i = 0; i != blocks; ++i) {
		new->block[i] = hwfs_trans_get_new_extent(trans, blocknr + i);

		if (!new->block[i]) {
			hwfs_trans_free(trans, blocknr, blocks);
			hwfs_put_node(trans, new);
			return 0;
		}
	}

	struct hwfs_disk_tree_header hdr;

	new->header.level = 0;
	new->header.count = 0;
	new->header.blocks = blocks;

	hwfs_tree_to_disk(&hdr, &new->header);
	hwfs_write_io_extent(new->block[0]->data, 0, &hdr, sizeof(hdr));

	new->state = HWFS_NODE_DIRTY;
	new->blocknr = blocknr;

	struct hwfs_node_cache_iter iter;

	hwfs_node_cache_lookup(tree, blocknr, &iter);
	rb_link(&new->link, iter.parent, iter.plink);
	rb_insert(&new->link, &tree->nodes);

	return new;
}

struct hwfs_node *hwfs_get_node(struct hwfs_trans *trans,
			struct hwfs_tree *tree, uint64_t blocknr)
{
	struct hwfs_node_cache_iter iter;

	if (hwfs_node_cache_lookup(tree, blocknr, &iter))
		return iter.node;

	struct hwfs_node *node = hwfs_create_node();

	if (!node)
		return 0;

	if (hwfs_read_node(trans, node, blocknr)) {
		hwfs_put_node(trans, node);
		return 0;
	}

	rb_link(&node->link, iter.parent, iter.plink);
	rb_insert(&node->link, &tree->nodes);
	return node;
}

struct hwfs_node *hwfs_cow_node(struct hwfs_trans *trans,
			struct hwfs_tree *tree,
			struct hwfs_node *node)
{
	if (node->state != HWFS_NODE_UPTODATE)
		return node;

	const size_t blocks = node->header.blocks;
	struct hwfs_node *copy = hwfs_new_node(trans, tree, blocks);

	if (!copy)
		return 0;

	for (size_t i = 0; i != blocks; ++i)
		hwfs_write_io_extent(copy->block[i], 0, node->block[i]->data,
					trans->node_size);
	copy->header = node->header;
	if (node == tree->root)
		tree->root = copy;
	hwfs_trans_free(trans, node->blocknr, node->header.blocks);

	return copy;
}

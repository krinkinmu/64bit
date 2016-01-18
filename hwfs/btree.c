#include <stdlib.h>

#include "transaction.h"
#include "disk-io.h"
#include "btree.h"

struct hwfs_node *hwfs_create_node(size_t size)
{
	struct hwfs_node *node = malloc(sizeof(*node) + size);

	if (node) {
		memset(node, 0, sizeof(*node));
		node->state = HWFS_NODE_NEW;
		node->data = node + 1;
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

	char *data = node->data;

	hwfs_read_io_extent(node->block[0], 0, data, trans->node_size);
	hwfs_tree_to_host(&node->header, (struct hwfs_disk_tree_header *)data);
	data += trans->node_size;

	for (unsigned i = 1; i < node->header.blocks; ++i) {
		node->block[i] = hwfs_trans_get_extent(trans, blocknr + i);

		if (!node->block[i]) {
			hwfs_release_node(trans, node);
			return -1;
		}

		hwfs_read_io_extent(node->block[i], 0, data, trans->node_size);
		data += trans->node_size;
	}

	node->state = HWFS_NODE_UPTODATE;
	node->blocknr = blocknr;
	node->size = node->header.blocks * trans->node_size;
	return 0;
}

static void hwfs_sync_node(struct hwfs_node *node)
{
	const char *data = node->data;

	hwfs_tree_to_disk(node->data, &node->header);

	for (unsigned i = 0; i < node->header.blocks; ++i) {
		const int size = node->block[i]->size;

		hwfs_write_io_extent(node->block[i], 0, data, size);
		data += size;
	}
}

int hwfs_write_node(struct hwfs_trans *trans, struct hwfs_node *node)
{
	for (unsigned i = 0; i < node->header.blocks; ++i) {
		if (hwfs_sync_io_extent(trans->fd, node->block[i]))
			return -1;
	}
	node->state = HWFS_NODE_UPTODATE;
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
	while (node) {
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
	struct hwfs_node *new =
			hwfs_create_node(trans->node_size * HWFS_MAX_BLOCKS);

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

	new->header.level = 0;
	new->header.count = 0;
	new->header.blocks = blocks;
	new->state = HWFS_NODE_DIRTY;
	new->blocknr = blocknr;
	new->size = blocks * trans->node_size;

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

	struct hwfs_node *node =
			hwfs_create_node(trans->node_size * HWFS_MAX_BLOCKS);

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

	char *data = copy->data;

	for (size_t i = 0; i != blocks; ++i) {
		hwfs_write_io_extent(copy->block[i], 0, node->block[i]->data,
					trans->node_size);
		hwfs_read_io_extent(copy->block[i], 0, data, trans->node_size);
		data += trans->node_size;
	}
	copy->header = node->header;
	if (node == tree->root)
		tree->root = copy;
	hwfs_trans_free(trans, node->blocknr, node->header.blocks);

	return copy;
}

void hwfs_leaf_get_value(struct hwfs_node *node, int pos,
			struct hwfs_value *value)
{
	struct hwfs_disk_leaf_header *hdr = node->data;
	struct hwfs_disk_value dvalue = hdr->value[pos];

	hwfs_value_to_host(value, &dvalue);
}

void hwfs_leaf_set_value(struct hwfs_node *node, int pos,
			const struct hwfs_value *value)
{
	struct hwfs_disk_leaf_header *hdr = node->data;

	hwfs_value_to_disk(hdr->value + pos, value);
}

void hwfs_node_get_item(struct hwfs_node *node, int pos,
			struct hwfs_item *item)
{
	struct hwfs_disk_node_header *hdr = node->data;
	struct hwfs_disk_item ditem = hdr->item[pos];

	hwfs_item_to_host(item, &ditem);
}

void hwfs_node_set_item(struct hwfs_node *node, int pos,
			const struct hwfs_item *item)
{
	struct hwfs_disk_node_header *hdr = node->data;

	hwfs_item_to_disk(hdr->item + pos, item);
}

static int hwfs_cmp_keys(const struct hwfs_key *l, const struct hwfs_key *r)
{
	if (l->id < r->id)
		return -1;
	if (l->id > r->id)
		return 1;
	if (l->type < r->type)
		return -1;
	if (l->type > r->type)
		return 1;
	if (l->offset < r->offset)
		return -1;
	if (l->offset > r->offset)
		return 1;
	return 0;
}

int hwfs_leaf_pos(struct hwfs_node *node, const struct hwfs_key *key)
{
	const int count = hwfs_node_items(node);
	int i = 0;

	for (; i != count; ++i) {
		struct hwfs_value value;

		hwfs_leaf_get_value(node, i, &value);

		const int cmp = hwfs_cmp_keys(&value.key, key);

		if (cmp >= 0)
			break;
	}
	return i;
}

int hwfs_node_pos(struct hwfs_node *node, const struct hwfs_key *key)
{
	const int count = hwfs_node_items(node);
	int i = 0;

	for (; i != count; ++i) {
		struct hwfs_item item;

		hwfs_node_get_item(node, i, &item);

		const int cmp = hwfs_cmp_keys(&item.key, key);

		if (cmp >= 0)
			break;
	}
	return i;
}

static int hwfs_leaf_room_end(struct hwfs_node *node)
{
	const int count = hwfs_node_items(node);

	if (count == 0)
		return node->size;

	struct hwfs_value value;

	hwfs_leaf_get_value(node, count - 1, &value);
	return value.offset;
}

static int hwfs_leaf_value_offset(int pos)
{
	const int header_size = sizeof(struct hwfs_disk_leaf_header);
	const int value_size = sizeof(struct hwfs_disk_value);

	return header_size + value_size * pos;
}

static int hwfs_leaf_room_begin(struct hwfs_node *node)
{
	return hwfs_leaf_value_offset(hwfs_node_items(node));
}

int hwfs_leaf_room(struct hwfs_node *node)
{
	return hwfs_leaf_room_end(node) - hwfs_leaf_room_begin(node);
}

static int hwfs_leaf_reserve(struct hwfs_node *node, int pos, int size)
{
	const int count = hwfs_node_items(node);

	struct hwfs_value value;

	hwfs_leaf_get_value(node, pos, &value);

	const int data_end = value.offset + value.size;
	const int data_begin = hwfs_leaf_room_end(node);
	const int dst = data_begin - size;

	if (size != 0) {
		char *buf = node->data;

		memmove(buf + dst, buf + data_begin, data_end - data_begin);
		for (int i = pos; i != count; ++i) {
			hwfs_leaf_get_value(node, i, &value);
			value.offset -= size;
			hwfs_leaf_set_value(node, i, &value);
		}
	}

	return data_end - size;
}

static void hwfs_leaf_move_keys(struct hwfs_node *node, int pos, int shift)
{
	const int to = hwfs_leaf_value_offset(pos + shift);
	const int begin = hwfs_leaf_value_offset(pos);
	const int end = hwfs_leaf_value_offset(hwfs_node_items(node));
	char *buf = node->data;

	memmove(buf + to, buf + begin, end - begin);
}

void hwfs_leaf_insert(struct hwfs_node *node, const struct hwfs_key *key,
			const void *data, int size)
{
	const int count = hwfs_node_items(node);
	const int pos = hwfs_leaf_pos(node, key);
	struct hwfs_value value;

	value.key = *key;
	value.size = size;

	if (pos == count) {
		value.offset = hwfs_leaf_room_end(node) - size;
	} else {
		value.offset = hwfs_leaf_reserve(node, pos, size);
		hwfs_leaf_move_keys(node, pos, 1);
	}

	if (size != 0)
		memcpy((char *)node->data + value.offset, data, size);
	hwfs_leaf_set_value(node, pos, &value);
	++node->header.count;
	hwfs_sync_node(node);
	node->state = HWFS_NODE_DIRTY;
}

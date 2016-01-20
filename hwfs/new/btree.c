#include "disk-io.h"
#include "btree.h"
#include "hwfs.h"

int setup_btree(struct disk_io *dio, struct btree *btree, size_t blocknr)
{
	btree->root = disk_get_block(dio, blocknr);

	return btree->root == 0;
}

void release_btree(struct disk_io *dio, struct btree *btree)
{
	disk_put_block(dio, btree->root);
	btree->root = 0;
}

static int __btree_lower_bound(const void *data, int item_size, int items,
		const void *key, int (*keycmp)(const void *, const void *))
{
	int pos = 0, len = items;

	while (len) {
		const int half = len / 2;
		const int m = pos + half;
		const void *pitem = (const char *)data + item_size * m;
		const int cmp = keycmp(key, pitem);

		if (cmp > 0) {
			pos = m + 1;
			len = len - half - 1;
		} else {
			len = half;
		}
	}
	return pos;
}

static int __btree_upper_bound(const void *data, int item_size, int items,
		const void *key, int (*keycmp)(const void *, const void *))
{
	int pos = 0, len = items;

	while (len) {
		const int half = len / 2;
		const int m = pos + half;
		const void *pitem = (const char *)data + item_size * m;
		const int cmp = keycmp(key, pitem);

		if (cmp >= 0) {
			pos = m + 1;
			len = len - half - 1;
		} else {
			len = half;
		}
	}
	return pos;
}

static int hwfs_host_key_cmp(const struct hwfs_key *l, const struct hwfs_key *r)
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

static int hwfs_key_cmp(const void *l, const void *r)
{
	const struct hwfs_key *lkey = l;
	struct hwfs_key rkey;

	hwfs_key_to_host(&rkey, r);
	return hwfs_host_key_cmp(lkey, &rkey);
}

static int btree_lower_bound(const struct disk_block *node,
			const struct hwfs_key *key)
{
	const struct hwfs_disk_tree_header *hdr = node->data;
	const struct hwfs_disk_leaf_header *lhdr = node->data;
	const struct hwfs_disk_node_header *nhdr = node->data;
	const int count = le16toh(hdr->count);

	if (le16toh(hdr->level) == 0)
		return __btree_lower_bound(lhdr->value,
				sizeof(struct hwfs_disk_value), count,
				key, &hwfs_key_cmp);
	return __btree_lower_bound(nhdr->item,
				sizeof(struct hwfs_disk_item), count,
				key, &hwfs_key_cmp);
}

static int btree_upper_bound(const struct disk_block *node,
			const struct hwfs_key *key)
{
	const struct hwfs_disk_tree_header *hdr = node->data;
	const struct hwfs_disk_leaf_header *lhdr = node->data;
	const struct hwfs_disk_node_header *nhdr = node->data;
	const int count = le16toh(hdr->count);

	if (le16toh(hdr->level) == 0)
		return __btree_upper_bound(lhdr->value,
				sizeof(struct hwfs_disk_value), count,
				key, &hwfs_key_cmp);
	return __btree_upper_bound(nhdr->item,
				sizeof(struct hwfs_disk_item), count,
				key, &hwfs_key_cmp);
}

static int btree_leaf_room(struct disk_io *dio, struct disk_block *block)
{
	struct hwfs_disk_leaf_header *hdr = block->data;
	struct hwfs_disk_value *value = hdr->value;
	const int count = le16toh(hdr->hdr.count);

	if (count == 0)
		return dio->block_size - sizeof(*hdr);

	const int room_begin = (char *)(value + count) - (char *)block->data;
	const int room_end = le16toh(value[count - 1].offset);
	return room_end - room_begin;
}

static struct disk_block *btree_alloc_node(struct disk_io *dio,
			struct free_space *fs)
{
	if (fs->next == fs->count)
		return 0;

	struct disk_block *block = disk_get_block(dio, ++fs->next);

	if (!block)
		return 0;

	struct hwfs_disk_tree_header *hdr = block->data;

	memset(block->data, 0, dio->block_size);
	hdr->blocks = htole16(1);
	return block;
}

static int btree_enough_room(struct disk_io *dio, struct disk_block *block,
			int data_size)
{
	static const int value_size = sizeof(struct hwfs_disk_value);

	struct hwfs_disk_tree_header *hdr = block->data;
	const int level = le16toh(hdr->level);

	if (level == 0)
		return btree_leaf_room(dio, block) >= data_size + value_size;
	return le16toh(hdr->count) < HWFS_NODE_FANOUT(dio->block_size) - 1;
}

static int btree_grow(struct disk_io *dio, struct free_space *fs,
			struct btree *tree)
{
	struct disk_block *new_root = btree_alloc_node(dio, fs);

	if (!new_root)
		return -1;

	struct hwfs_disk_tree_header *ohdr = tree->root->data;
	struct hwfs_disk_node_header *nhdr = new_root->data;
	const int level = le16toh(ohdr->level);

	nhdr->hdr.level = htole16(level + 1);
	nhdr->hdr.count = htole16(1);
	nhdr->hdr.blocks = 0;

	if (level == 0) {
		struct hwfs_disk_leaf_header *hdr = tree->root->data;

		nhdr->item[0].key = hdr->value[0].key;
	} else {
		struct hwfs_disk_node_header *hdr = tree->root->data;

		nhdr->item[0].key = hdr->item[0].key;
	}

	nhdr->item[0].blocknr = htole64(new_root->blocknr);
	disk_put_block(dio, tree->root);
	tree->root = new_root;
	++fs->next;
	return 0;
}

static int btree_split_node(struct disk_io *dio, struct free_space *fs,
			struct disk_block *parent, int slot,
			struct disk_block *block)
{
	struct hwfs_disk_node_header *phdr = parent->data;
	struct hwfs_disk_node_header *hdr = block->data;
	const int pcount = le16toh(phdr->hdr.count);
	const int count = le16toh(hdr->hdr.count);
	const int split = count / 2;

	struct disk_block *next = btree_alloc_node(dio, fs);

	if (!next)
		return -1;

	struct hwfs_disk_node_header *nhdr = next->data;

	memcpy(nhdr->item, hdr->item + split,
				sizeof(*hdr->item) * (count - split));
	memmove(phdr->item + slot + 2, phdr->item + slot + 1,
				sizeof(*phdr->item) * (pcount - slot - 1));
	phdr->item[slot + 1].key = nhdr->item[0].key;
	phdr->item[slot + 1].blocknr = htole64(next->blocknr);
	phdr->hdr.count = htole16(pcount + 1);
	nhdr->hdr.count = htole16(count - split);
	hdr->hdr.count = htole16(split);
	++fs->next;

	return 0;
}

static int btree_split_leaf(struct disk_io *dio, struct free_space *fs,
			struct disk_block *parent, int slot,
			struct disk_block *block)
{
	(void) dio;
	(void) fs;
	(void) parent;
	(void) slot;
	(void) block;

	return -1;
}

static int btree_leaf_insert(struct disk_io *dio, struct free_space *fs,
			struct disk_block *parent, int slot,
			struct disk_block *block, const struct hwfs_key *key,
			const void *data, size_t size)
{
	if (!btree_enough_room(dio, block, size)) {
		struct hwfs_disk_node_header *hdr = parent->data;
		struct hwfs_disk_item *item = hdr->item;

		if (btree_split_leaf(dio, fs, parent, slot, block)) {
			disk_put_block(dio, parent);
			disk_put_block(dio, block);
			return -1;
		}
		
		if (hwfs_key_cmp(key, item + slot + 1) > 0) {
			const uint64_t blocknr = le64toh(item[++slot].blocknr);

			disk_put_block(dio, block);
			block = disk_get_block(dio, blocknr);

			if (!block) {
				disk_put_block(dio, parent);
				return -1;
			}
		}
	}

	struct hwfs_disk_leaf_header *hdr = block->data;
	struct hwfs_disk_value *value = hdr->value;
	const int count = le16toh(hdr->hdr.count);

	slot = btree_lower_bound(block, key);
	hdr->hdr.count = htole16(count + 1);

	if (slot != count) {
		const int data_begin = le16toh(value[count - 1].offset);
		const int data_end = le16toh(value[slot].offset)
					+ le16toh(value[slot].size);

		memmove((char *)block->data + data_begin - size,
			(char *)block->data + data_begin, data_end - data_begin);
		memmove(value + slot + 1, value + slot,
				(count - slot) * sizeof(*value));

		for (int i = slot + 1; i != count + 1; ++i) {
			const int offset = le16toh(value[i].offset);
			value[i].offset = htole16(offset - size);
		}

		value[slot].offset = htole16(data_end - size);
	} else {
		const int offset = count == 0 ? dio->block_size - size
				: le16toh(value[count - 1].offset) - size;

		value[slot].offset = htole16(offset);
	}

	hwfs_key_to_disk(&value[slot].key, key);
	value[slot].size = htole16(size);

	if (size)
		memcpy((char *)block->data + le16toh(value[slot].offset),
			data, size);

	disk_put_block(dio, parent);
	disk_put_block(dio, block);

	return 0;
}

int btree_insert(struct disk_io *dio, struct free_space *fs,
			struct btree *tree,
			const struct hwfs_key *key,
			const void *data, size_t size)
{
	if (!btree_enough_room(dio, tree->root, size)
			&& btree_grow(dio, fs, tree))
		return -1;

	struct disk_block *block = disk_ref_block(tree->root);
	struct disk_block *parent = 0;
	int slot = 0;

	struct hwfs_disk_tree_header *hdr = block->data;
	int level = le16toh(hdr->level);
	uint64_t blocknr;

	while (level) {
		if (!btree_enough_room(dio, block, size)) {
			struct hwfs_disk_node_header *hdr = parent->data;
			struct hwfs_disk_item *item = hdr->item;

			if (btree_split_node(dio, fs, parent, slot, block)) {
				disk_put_block(dio, parent);
				disk_put_block(dio, block);
				return -1;
			}

			if (hwfs_key_cmp(key, item + slot + 1) > 0) {
				blocknr = le64toh(item[++slot].blocknr);
				disk_put_block(dio, block);
				block = disk_get_block(dio, blocknr);

				if (!block) {
					disk_put_block(dio, parent);
					return -1;
				}
			}
		}

		struct hwfs_disk_node_header *nhdr = block->data;

		disk_put_block(dio, parent);
		parent = block;
		slot = btree_upper_bound(block, key) - 1;

		if (slot < 0) {
			hwfs_key_to_disk(&nhdr->item[0].key, key);
			slot = 0;
		}

		blocknr = le64toh(nhdr->item[slot].blocknr);
		block = disk_get_block(dio, blocknr);
		if (!block) {
			disk_put_block(dio, parent);
			return -1;
		}
		hdr = block->data;
		level = le16toh(hdr->level);
	}

	return btree_leaf_insert(dio, fs, parent, slot, block,
				key, data, size);
}

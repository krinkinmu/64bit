#include <string.h>
#include <assert.h>

#include "btree.h"
#include "cache.h"
#include "hwfs.h"

static int hwfs_node_count(struct hwfs_block *block)
{
	struct hwfs_tree_header *hdr = block->data;

	return le16toh(hdr->count);
}

static int hwfs_node_level(struct hwfs_block *block)
{
	struct hwfs_tree_header *hdr = block->data;

	return le16toh(hdr->level);
}

/* compare keys in cpu format */
static int hwfs_cmp_keys(const struct hwfs_key *l, const struct hwfs_key *r)
{
	return memcmp(l, r, sizeof(*l));
}

static void hwfs_key_to_cpu(struct hwfs_key *key)
{
	key->id = le64toh(key->id);
	key->offset = le64toh(key->offset);
}

static void hwfs_key_to_disk(struct hwfs_key *key)
{
	key->id = htole64(key->id);
	key->offset = htole64(key->offset);
}

static int hwfs_leaf_pos(struct hwfs_block *block, struct hwfs_key *key)
{
	struct hwfs_leaf_header *hdr = block->data;
	struct hwfs_value *value = hdr->value;
	const int count = hwfs_node_count(block);
	int i = 0;

	for (; i < count; ++i) {
		struct hwfs_key tmp = value[i].key;
		
		hwfs_key_to_cpu(&tmp);

		if (hwfs_cmp_keys(&tmp, key) >= 0)
			break;
	}
	return i;
}

static int hwfs_node_pos(struct hwfs_block *block, struct hwfs_key *key)
{
	struct hwfs_node_header *hdr = block->data;
	struct hwfs_item *item = hdr->item;
	const int count = hwfs_node_count(block);
	int i = 0;

	for (; i < count; ++i) {
		struct hwfs_key tmp = item[i].key;
		
		hwfs_key_to_cpu(&tmp);
		if (hwfs_cmp_keys(&tmp, key) >= 0)
			break;
	}
	return i;
}

static int hwfs_tree_pos(struct hwfs_block *block, struct hwfs_key *key)
{
	if (hwfs_node_level(block) != 0)
		return hwfs_node_pos(block, key);
	return hwfs_leaf_pos(block, key);
}

static int hwfs_reserve_data(struct hwfs_block *block, int pos, int size)
{
	struct hwfs_block_cache *cache = block->cache;
	struct hwfs_leaf_header *hdr = block->data;
	struct hwfs_value *value = hdr->value;
	const int count = hwfs_node_count(block);

	if (count == 0)
		return cache->block_size - size;

	if (pos >= count)
		return le16toh(value[count - 1].offset) - size;

	const int end = le16toh(value[pos].offset) + le16toh(value[pos].size);
	const int begin = le16toh(value[count - 1].offset);
	char *data = block->data;

	memmove(data + begin - size, data + begin, end - begin);
	for (int i = pos; i != count; ++i)
		value[i].offset = htole16(le16toh(value[i].offset) - size);

	return end - size;
}

static void hwfs_reserve_values(struct hwfs_block *block, int pos, int places)
{
	struct hwfs_leaf_header *hdr = block->data;
	struct hwfs_value *value = hdr->value;
	const int count = hwfs_node_count(block);

	if (pos >= count || places == 0)
		return;

	memmove(value + pos + places, value + pos,
				(count - pos) * sizeof(*value));
}

static unsigned hwfs_leaf_room(struct hwfs_block *block)
{
	struct hwfs_block_cache *cache = block->cache;
	struct hwfs_leaf_header *hdr = block->data;
	struct hwfs_value *value = hdr->value;
	const int count = hwfs_node_count(block);

	if (count == 0)
		return cache->block_size - sizeof(*hdr);

	const int data_begin = le16toh(value[count - 1].offset);
	const int value_end = (char *)(value + count) - (char *)block->data;

	assert(value_end <= data_begin);

	return data_begin - value_end;
}

int hwfs_leaf_insert(struct hwfs_block *block, struct hwfs_key *key,
			const void *item, int size)
{
	struct hwfs_leaf_header *hdr = block->data;
	struct hwfs_value *value = hdr->value;

	if (hwfs_leaf_room(block) < size + sizeof(struct hwfs_value))
		return -1;

	const int pos = hwfs_leaf_pos(block, key);
	const int offset = hwfs_reserve_data(block, pos, size);
	hwfs_reserve_values(block, pos, 1);

	hdr->hdr.count = htole16(le16toh(hdr->hdr.count) + 1);
	value[pos].key = *key;
	value[pos].size = htole16(size);
	value[pos].offset = htole16(offset);
	hwfs_key_to_disk(&value[pos].key);

	if (size != 0)
		memcpy((char *)block->data + offset, item, size);
	return 0;
}

void hwfs_trans_setup(struct hwfs_trans *trans, struct hwfs_block_cache *cache)
{
	memset(trans, 0, sizeof(*trans));
	trans->cache = cache;

	trans->super_block = hwfs_get_block(cache, 0);

	struct hwfs_super_block *sb = trans->super_block->data;

	trans->fs_tree_root = hwfs_get_block(cache, le64toh(sb->fs_tree_root));
	trans->extent_tree_root = hwfs_get_block(cache,
				le64toh(sb->extent_tree_root));
}

void hwfs_trans_release(struct hwfs_trans *trans)
{
	hwfs_put_block(trans->extent_tree_root);
	hwfs_put_block(trans->fs_tree_root);
	hwfs_put_block(trans->super_block);
}

static int hwfs_get_value(struct hwfs_iter *iter, struct hwfs_value *value)
{
	struct hwfs_block *block = iter->node[0];

	if (!block)
		return -1;

	struct hwfs_leaf_header *hdr = block->data;
	*value = hdr->value[iter->pos[0]];
	return 0;
}

int hwfs_get_key(struct hwfs_iter *iter, struct hwfs_key *key)
{
	struct hwfs_value value;

	if (hwfs_get_value(iter, &value) != 0)
		return -1;

	*key = value.key;
	hwfs_key_to_cpu(key);
	return 0;
}

int hwfs_get_data_size(struct hwfs_iter *iter)
{
	struct hwfs_value value;

	if (hwfs_get_value(iter, &value) != 0)
		return -1;

	return le16toh(value.size);
}

int hwfs_get_data(struct hwfs_iter *iter, void *data, int off, int sz)
{
	struct hwfs_value value;

	if (hwfs_get_value(iter, &value) != 0)
		return -1;

	struct hwfs_block *block = iter->node[0];
	const int offset = le16toh(value.offset);
	const int size = le16toh(value.size);
	const int copy = MAX(0, MIN(size - off, sz));

	if (copy)
		memcpy(data, (char *)block->data + offset + off, copy);
	return copy;
}

int hwfs_prev(struct hwfs_block_cache *cache, struct hwfs_iter *iter)
{
	int level = 0;

	if (iter->node[level] == 0)
		return -1;

	while (level <= iter->root_level) {
		struct hwfs_block *block = iter->node[level];

		if (iter->pos[level] > 0) {
			--iter->pos[level];
			break;
		}

		hwfs_put_block(block);
		iter->node[level] = 0;
		++level;
	}

	if (level > iter->root_level)
		return -1;

	while (level > 0) {
		struct hwfs_block *block = iter->node[level];
		struct hwfs_node_header *node_hdr = block->data;
		struct hwfs_item *item = node_hdr->item;
		const int pos = iter->pos[level];
		const uint64_t blocknr = le64toh(item[pos].blocknr);

		struct hwfs_block *child = hwfs_get_block(cache, blocknr);
		const int child_level = hwfs_node_level(child);

		assert(child_level == level - 1);

		iter->node[child_level] = child;
		iter->pos[child_level] = hwfs_node_count(child) - 1;
		level = child_level;
	}

	return 0;
}

int hwfs_next(struct hwfs_block_cache *cache, struct hwfs_iter *iter)
{
	int level = 0;

	if (iter->node[level] == 0)
		return -1;

	while (level <= iter->root_level) {
		struct hwfs_block *block = iter->node[level];
		const int count = hwfs_node_count(block);

		if (iter->pos[level] < count - 1) {
			++iter->pos[level];
			break;
		}

		hwfs_put_block(block);
		iter->node[level] = 0;
		iter->pos[level] = 0;
		++level;
	}

	if (level > iter->root_level)
		return -1;

	while (level > 0) {
		struct hwfs_block *block = iter->node[level];
		struct hwfs_node_header *node_hdr = block->data;
		struct hwfs_item *item = node_hdr->item;
		const int pos = iter->pos[level];
		const uint64_t blocknr = le64toh(item[pos].blocknr);

		struct hwfs_block *child = hwfs_get_block(cache, blocknr);
		const int child_level = hwfs_node_level(child);

		assert(child_level == level - 1);

		iter->node[child_level] = child;
		level = child_level;
	}

	return 0;
}

void hwfs_release_iter(struct hwfs_iter *iter)
{
	for (int level = 0; iter->node[level]; ++level) {
		hwfs_put_block(iter->node[level]);
		iter->node[level] = 0;
		iter->pos[level] = 0;
	}
	iter->root_level = 0;
}

int hwfs_lookup(struct hwfs_block_cache *cache, struct hwfs_block *root,
			struct hwfs_key *key, struct hwfs_iter *iter)
{
	struct hwfs_block *block = root;
	int level = hwfs_node_level(block);

	memset(iter, 0, sizeof(*iter));
	iter->root_level = level;
	iter->pos[level] = hwfs_tree_pos(block, key);
	iter->node[level] = block;
	++block->links;

	while (level != 0) {
		struct hwfs_node_header *node_hdr = block->data;
		struct hwfs_item *item = node_hdr->item;
		const int pos = iter->pos[level];
		const uint64_t blocknr = le64toh(item[pos].blocknr);

		block = hwfs_get_block(cache, blocknr);
		level = hwfs_node_level(block);
		iter->node[level] = block;
		iter->pos[level] = hwfs_tree_pos(block, key);
	}

	struct hwfs_key tmp;

	hwfs_get_key(iter, &tmp);
	return hwfs_cmp_keys(&tmp, key) == 0;
}

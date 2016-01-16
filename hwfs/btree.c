#include <string.h>
#include <assert.h>

#include "btree.h"
#include "cache.h"
#include "hwfs.h"

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
	const int count = le16toh(hdr->values);
	int i = 0;

	for (; i < count; ++i) {
		struct hwfs_key tmp = value[i].key;
		
		hwfs_key_to_cpu(&tmp);
		if (hwfs_cmp_keys(&tmp, key) >= 0)
			break;
	}
	return i;
}

static int hwfs_reserve_data(struct hwfs_block *block, int pos, int size)
{
	struct hwfs_block_cache *cache = block->cache;
	struct hwfs_leaf_header *hdr = block->data;
	struct hwfs_value *value = hdr->value;
	const int count = le16toh(hdr->values);

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
	const int count = le16toh(hdr->values);

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
	const int count = le16toh(hdr->values);

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

	hdr->values = htole16(le16toh(hdr->values) + 1);
	value[pos].key = *key;
	value[pos].size = htole16(size);
	value[pos].offset = htole16(offset);
	hwfs_key_to_disk(&value[pos].key);

	if (size != 0)
		memcpy((char *)block->data + offset, item, size);
	return 0;
}

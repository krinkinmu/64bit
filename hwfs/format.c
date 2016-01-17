#include <stdio.h>

#include <endian.h>

#include "format.h"
#include "cache.h"
#include "btree.h"


static const long hwfs_bootstrap_size = 3;
static const long hwfs_super_block = 0;
static const long hwfs_fs_tree_root = 1;
static const long hwfs_extent_tree_root = 2;


static void hwfs_bootstrap(struct hwfs_block_cache *cache)
{
	struct hwfs_block *sb_b = hwfs_get_block(cache, hwfs_super_block);
	struct hwfs_block *fsr_b = hwfs_get_block(cache, hwfs_fs_tree_root);
	struct hwfs_block *ext_b = hwfs_get_block(cache, hwfs_extent_tree_root);

	struct hwfs_super_block *sb = sb_b->data;

	sb->magic = htole64(HWFS_MAGIC);
	sb->fs_tree_root = htole64(fsr_b->blocknr);
	sb->extent_tree_root = htole64(ext_b->blocknr);
	sb->root_node_id = htole64(0);
	sb->next_node_id = htole64(1);
	sb->node_size = htole16(cache->block_size);

	/*
	 * Two reduce number of corner cases we insert sentinel values in
	 * both trees - sentinel values have lowest and highest possible keys.
	 */
	struct hwfs_leaf_header *fsr = fsr_b->data;
	struct hwfs_key key = { 0, 0, 0 };

	fsr->hdr.level = htole16(0);
	fsr->hdr.count = htole16(0);

	hwfs_leaf_insert(fsr_b, &key, 0, 0);

	key.id = ~(uint64_t)0;
	key.type = ~(uint8_t)0;
	key.offset = ~(uint64_t)0;

	hwfs_leaf_insert(fsr_b, &key, 0, 0);

	/*
	 * We never release super block so it serves as lower sentinel value.
	 * But because we don't store disk size anywhere we use high sentinel
	 * node as a disk end marker.
	 */
	struct hwfs_leaf_header *ext = ext_b->data;

	ext->hdr.level = htole16(0);
	ext->hdr.count = htole16(0);

	struct hwfs_extent extent = { htole64(hwfs_bootstrap_size), 0 };

	key.id = 0;
	key.type = HWFS_EXTENT;
	key.offset = 0;

	hwfs_leaf_insert(ext_b, &key, &extent, sizeof(extent));

	extent.size = htole64(1);
	extent.free = 0;

	key.offset = htole64(cache->disk_size / cache->block_size);

	hwfs_leaf_insert(ext_b, &key, &extent, sizeof(extent));

	hwfs_put_block(ext_b);
	hwfs_put_block(fsr_b);
	hwfs_put_block(sb_b);
}

static void print_keys(struct hwfs_block_cache *cache, uint64_t root)
{
	struct hwfs_block *block = hwfs_get_block(cache, root);
	struct hwfs_iter iter;
	struct hwfs_key key;

	key.id = 0;
	key.type = 0;
	key.offset = 0;

	hwfs_lookup(cache, block, &key, &iter);

	while (1) {
		if (hwfs_get_key(&iter, &key))
			break;

		fprintf(stderr, "id: %llu, type: %u, offset: %llu\n",
					(unsigned long long) key.id,
					(unsigned) key.type,
					(unsigned long long) key.offset);
		hwfs_next(cache, &iter);
	}
	hwfs_put_block(block);
	hwfs_release_iter(&iter);
}

int hwfs_format(int fd, int block_size, long size)
{
	struct hwfs_block_cache cache;

	if (size / block_size <= hwfs_bootstrap_size) {
		fprintf(stderr, "Disk isn't large enough\n");
		return -1;
	}

	hwfs_block_cache_setup(&cache, fd, block_size, size);
	hwfs_bootstrap(&cache);
	hwfs_block_cache_release(&cache);

	hwfs_block_cache_setup(&cache, fd, block_size, size);
	fprintf(stderr, "Filesystem tree keys:\n");
	print_keys(&cache, 1);
	fprintf(stderr, "Extent tree keys:\n");
	print_keys(&cache, 2);
	hwfs_block_cache_release(&cache);

	return 0;
}

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

	struct hwfs_leaf_header *fsr = fsr_b->data;

	fsr->level = htole16(0);
	fsr->values = htole16(0);

	struct hwfs_leaf_header *ext = ext_b->data;

	ext->level = htole16(0);
	ext->values = htole16(0);

	struct hwfs_extent extent = { htole64(hwfs_bootstrap_size), 0 };
	struct hwfs_key key = { 0, HWFS_EXTENT, 0 };

	hwfs_leaf_insert(ext_b, &key, &extent, sizeof(extent));

	hwfs_put_block(ext_b);
	hwfs_put_block(fsr_b);
	hwfs_put_block(sb_b);
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

	return 0;
}

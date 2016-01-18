#include <stdio.h>

#include "disk-io.h"
#include "format.h"
#include "btree.h"
#include "hwfs.h"


int hwfs_bootstrap(int fd, size_t node_size, size_t node_count)
{
	static const size_t hwfs_fs_tree_root = 1;
	static const size_t hwfs_extent_tree_root = 2;

	if (node_count < 3) {
		fprintf(stderr, "Device is too small\n");
		return -1;
	}

	struct hwfs_io_extent *ext = hwfs_create_io_extent(node_size);
	struct hwfs_node *node = hwfs_create_node(node_size);

	if (!ext || !node) {
		fprintf(stderr, "Failed to allocate memory\n");
		hwfs_put_io_extent(ext);
		hwfs_destroy_node(node);
		return -1;
	}

	node->size = node_size;
	node->block[0] = ext;


	struct hwfs_disk_super_block dsuper;
	struct hwfs_super_block super;

	super.magic = HWFS_MAGIC;
	super.fs_tree_root = hwfs_fs_tree_root;
	super.extent_tree_root = hwfs_extent_tree_root;
	super.root_node_id = 0;
	super.next_node_id = 1;
	super.node_size = node_size;

	ext->offset = 0;
	hwfs_super_to_disk(&dsuper, &super);
	hwfs_memset_io_extent(ext, 0, 0, node_size);
	hwfs_write_io_extent(ext, 0, &dsuper, sizeof(dsuper));
	if (hwfs_sync_io_extent(fd, ext) != 0) {
		fprintf(stderr, "Failed to initialize super block\n");
		hwfs_put_io_extent(ext);
		return -1;
	}

	node->header.level = 0;
	node->header.count = 0;
	node->header.blocks = 1;

	struct hwfs_key key;

	key.id = 0;
	key.type = HWFS_LOW_SENTINEL;
	key.offset = 0;

	hwfs_leaf_insert(node, &key, 0, 0);

	key.id = ~(uint64_t)0;
	key.type = HWFS_HIGH_SENTINEL;
	key.offset = ~(uint64_t)0;

	hwfs_leaf_insert(node, &key, 0, 0);

	ext->offset = hwfs_fs_tree_root * node_size;
	if (hwfs_sync_io_extent(fd, ext) != 0) {
		fprintf(stderr, "Failed to initialize fs tree\n");
		hwfs_put_io_extent(ext);
		hwfs_destroy_node(node);
		return -1;
	}

	node->header.level = 0;
	node->header.count = 0;
	node->header.blocks = 1;

	key.id = 0;
	key.type = HWFS_LOW_SENTINEL;
	key.offset = 0;
	hwfs_leaf_insert(node, &key, 0, 0);

	key.id = ~(uint64_t)0;
	key.offset = ~(uint64_t)0;
	hwfs_leaf_insert(node, &key, 0, 0);

	struct hwfs_disk_extent dextent;
	struct hwfs_extent extent;

	key.id = 0;
	key.type = HWFS_EXTENT;
	key.offset = 0;
	extent.size = 1;
	extent.free = 0;
	hwfs_extent_to_disk(&dextent, &extent);
	hwfs_leaf_insert(node, &key, &dextent, sizeof(dextent));

	key.offset = 1;
	extent.size = 2;
	extent.free = 0;
	hwfs_extent_to_disk(&dextent, &extent);
	hwfs_leaf_insert(node, &key, &dextent, sizeof(dextent));

	ext->offset = hwfs_extent_tree_root * node_size;
	if (hwfs_sync_io_extent(fd, ext) != 0) {
		fprintf(stderr, "Failed to initialize extent tree\n");
		hwfs_put_io_extent(ext);
		hwfs_destroy_node(node);
		return -1;
	}

	hwfs_put_io_extent(ext);
	hwfs_destroy_node(node);

	return 0;
}

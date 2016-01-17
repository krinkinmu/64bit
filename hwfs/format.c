#include <stdio.h>

#include "disk-io.h"
#include "format.h"
#include "hwfs.h"

int hwfs_bootstrap(int fd, size_t node_size, size_t node_count)
{
	static const size_t hwfs_fs_tree_root = 1;
	static const size_t hwfs_extent_tree_root = 2;

	if (node_count < 3) {
		fprintf(stderr, "Device is too small\n");
		return -1;
	}

	struct hwfs_disk_super_block dsuper;
	struct hwfs_super_block super;

	super.magic = HWFS_MAGIC;
	super.fs_tree_root = hwfs_fs_tree_root;
	super.extent_tree_root = hwfs_extent_tree_root;
	super.root_node_id = 0;
	super.next_node_id = 1;
	super.node_size = node_size;

	struct hwfs_io_extent *ext = hwfs_create_io_extent(node_size);

	if (!ext) {
		fprintf(stderr, "Failed to allocate memory\n");
		return -1;
	}

	ext->offset = 0;
	hwfs_super_to_disk(&dsuper, &super);
	hwfs_memset_io_extent(ext, 0, 0, node_size);
	hwfs_write_io_extent(ext, 0, &dsuper, sizeof(dsuper));
	if (hwfs_sync_io_extent(fd, ext) != 0) {
		fprintf(stderr, "Failed to initialize super block\n");
		hwfs_put_io_extent(ext);
		return -1;
	}

	struct hwfs_tree_header hdr;
	struct hwfs_disk_tree_header dhdr;

	hdr.level = 0;
	hdr.count = 0;
	hdr.blocks = 1;

	hwfs_tree_to_disk(&dhdr, &hdr);

	ext->offset = hwfs_fs_tree_root * node_size;
	hwfs_memset_io_extent(ext, 0, 0, node_size);
	hwfs_write_io_extent(ext, 0, &dhdr, sizeof(dhdr));
	if (hwfs_sync_io_extent(fd, ext) != 0) {
		fprintf(stderr, "Failed to initialize fs tree\n");
		hwfs_put_io_extent(ext);
		return -1;
	}

	ext->offset = hwfs_extent_tree_root * node_size;
	hwfs_memset_io_extent(ext, 0, 0, node_size);
	hwfs_write_io_extent(ext, 0, &dhdr, sizeof(dhdr));
	if (hwfs_sync_io_extent(fd, ext) != 0) {
		fprintf(stderr, "Failed to initialize extent tree\n");
		hwfs_put_io_extent(ext);
		return -1;
	}

	hwfs_put_io_extent(ext);

	return 0;
}

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "disk-io.h"
#include "btree.h"
#include "hwfs.h"

static const size_t block_size = 4096;
static const uint64_t root_inode = 1;
static const uint64_t free_block = 3;
static const uint64_t extent_root = 2;
static const uint64_t fs_root = 1;


static long device_blocks(int fd)
{
	const off_t offset = lseek(fd, 0, SEEK_END);

	if (offset == (off_t)-1) {
		perror("Cannot get disk size");
		return -1;
	}
	return offset / block_size;
}

static int bootstrap(struct disk_io *dio, long disk_size)
{
	struct free_space fs = { disk_size, free_block };
	struct hwfs_disk_tree_header *hdr;
	struct hwfs_super_block sb;
	struct hwfs_disk_extent extent;
	struct hwfs_disk_inode root;
	struct btree tree;
	struct hwfs_key key;
	struct disk_block *block;


	/* super block and two roots initialization */
	sb.magic = HWFS_MAGIC;
	sb.fs_tree_root = fs_root;
	sb.extent_tree_root = extent_root;
	sb.root_node_id = root_inode;
	sb.next_node_id = root_inode + 1;
	sb.block_size = block_size;

	block = disk_get_block(dio, 0);
	if (!block) {
		fprintf(stderr, "Failed to get super block\n");
		return -1;
	}

	hwfs_super_to_disk(block->data, &sb);
	disk_put_block(dio, block);

	block = disk_get_block(dio, fs_root);
	if (!block) {
		fprintf(stderr, "Failed to setup fs tree root\n");
		return -1;
	}
	memset(block->data, 0, dio->block_size);
	hdr = block->data;
	hdr->blocks = htole16(1);
	disk_put_block(dio, block);

	block = disk_get_block(dio, extent_root);
	if (!block) {
		fprintf(stderr, "Failed to setup extent tree root\n");
		return -1;
	}
	memset(block->data, 0, dio->block_size);
	hdr = block->data;
	hdr->blocks = htole16(1);
	disk_put_block(dio, block);


	/*
	 * fs tree: low/high sentinel, empty root dir inode.
	 */	
	if (setup_btree(dio, &tree, fs_root)) {
		fprintf(stderr, "Cannot read fs tree root\n");
		return -1;
	}

	key.id = 0;
	key.type = HWFS_LOW_SENTINEL;
	key.offset = 0;

	if (btree_insert(dio, &fs, &tree, &key, 0, 0)) {
		fprintf(stderr, "Failed to insert low sentinel\n");
		release_btree(dio, &tree);
		return -1;	
	}

	key.id = ~(uint64_t)0;
	key.type = HWFS_HIGH_SENTINEL;
	key.offset = ~(uint64_t)0;

	if (btree_insert(dio, &fs, &tree, &key, 0, 0)) {
		fprintf(stderr, "Failed to insert high sentinel\n");
		release_btree(dio, &tree);
		return -1;
	}

	key.id = root_inode;
	key.type = HWFS_INODE;
	key.offset = 0;

	root.size = 0;
	root.links = htole32(1);
	root.type = HWFS_DIR;

	if (btree_insert(dio, &fs, &tree, &key, &root, sizeof(root))) {
		fprintf(stderr, "Failed to create root directory\n");
		release_btree(dio, &tree);
		return -1;
	}

	release_btree(dio, &tree);


	/*
	 * extent tree: low/high sentinel, superblock 1 block extent,
	 * tree nodes extent, fake out-of-disk extent
	 */
	if (setup_btree(dio, &tree, extent_root)) {
		fprintf(stderr, "Cannot read fs tree root\n");
		return -1;
	}

	key.id = 0;
	key.type = HWFS_LOW_SENTINEL;
	key.offset = 0;

	if (btree_insert(dio, &fs, &tree, &key, 0, 0)) {
		fprintf(stderr, "Failed to insert low sentinel\n");
		release_btree(dio, &tree);
		return -1;
	}

	key.id = ~(uint64_t)0;
	key.type = HWFS_HIGH_SENTINEL;
	key.offset = ~(uint64_t)0;

	if (btree_insert(dio, &fs, &tree, &key, 0, 0)) {
		fprintf(stderr, "Failed to insert high sentinel\n");
		release_btree(dio, &tree);
		return -1;
	}

	extent.free = 0;

	key.id = 0;
	key.type = HWFS_EXTENT;
	key.offset = 1;

	if (btree_insert(dio, &fs, &tree, &key, &extent, sizeof(extent))) {
		fprintf(stderr, "Failed to insert superblock extent\n");
		release_btree(dio, &tree);
		return -1;
	}

	key.id = 1;
	key.type = HWFS_EXTENT;
	key.offset = fs.next - 1;

	if (btree_insert(dio, &fs, &tree, &key, &extent, sizeof(extent))) {
		fprintf(stderr, "Failed to insert data extent\n");
		release_btree(dio, &tree);
		return -1;
	}

	key.id = disk_size;
	key.type = HWFS_EXTENT;
	key.offset = 1;

	if (btree_insert(dio, &fs, &tree, &key, &extent, sizeof(extent))) {
		fprintf(stderr, "Failed to write disk size limit\n");
		release_btree(dio, &tree);
		return -1;
	}

	release_btree(dio, &tree);

	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Disk file name expected\n");
		return 1;
	}

	const int fd = open(argv[1], O_RDWR);

	if (fd < 0) {
		perror("Cannot open disk file");
		return 1;
	}

	const long blocks = device_blocks(fd);

	if (blocks < 0) {
		close(fd);
		return 1;
	}

	struct disk_io dio;
	int rc = 0;

	setup_disk_io(&dio, block_size, fd);

	if (bootstrap(&dio, blocks)) {
		fprintf(stderr, "mkfs failed\n");
		rc = 1;
	}

	release_disk_io(&dio);
	close(fd);

	return rc;
}

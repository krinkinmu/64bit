#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "disk-io.h"
#include "hwfs.h"

static const size_t block_size = 4096;

/*
static long device_blocks(int fd)
{
	const off_t offset = lseek(fd, 0, SEEK_END);

	if (offset == (off_t)-1) {
		perror("Cannot get disk size");
		return -1;
	}
	return offset / node_size;
}
*/

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

	struct disk_io dio;

	setup_disk_io(&dio, block_size, fd);

	struct hwfs_super_block sb;

	sb.magic = HWFS_MAGIC;
	sb.fs_tree_root = 1;
	sb.extent_tree_root = 2;
	sb.root_node_id = 0;
	sb.next_node_id = 1;
	sb.block_size = block_size;

	struct disk_block *block = disk_get_block(&dio, 0);

	if (block)
		hwfs_super_to_disk(block->data, &sb);
	disk_put_block(&dio, block);

	release_disk_io(&dio);

	close(fd);

	return 0;
}

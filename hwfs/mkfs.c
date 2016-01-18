#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "transaction.h"
#include "format.h"
#include "hwfs.h"


static const size_t node_size = 4096;

static long device_nodes(int fd)
{
	const off_t offset = lseek(fd, 0, SEEK_END);

	if (offset == (off_t)-1) {
		perror("Cannot get disk size");
		return -1;
	}
	return offset / node_size;
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

	const long nodes = device_nodes(fd);

	if (nodes < 0) {
		close(fd);
		return 1;
	}

	hwfs_bootstrap(fd, node_size, nodes);

	struct hwfs_tree_iter iter;
	struct hwfs_trans trans;
	struct hwfs_key key;

	hwfs_trans_setup(&trans, fd);

	key.id = 0;
	key.type = 0;
	key.offset = 0;

	if (hwfs_lookup(&trans, &trans.fs_tree, &key, &iter) == 0) {
		fprintf(stderr, "Filesystem tree keys:\n");

		while (iter.node[0]) {
			hwfs_get_key(&iter, &key);
			fprintf(stderr, "%llu %u %llu\n",
				(unsigned long long) key.id,
				(unsigned) key.type,
				(unsigned long long) key.offset);
			hwfs_next(&trans, &trans.fs_tree, &iter);
		}
	}

	key.id = 0;
	key.type = 0;
	key.offset = 0;

	if (hwfs_lookup(&trans, &trans.extent_tree, &key, &iter) == 0) {
		fprintf(stderr, "Filesystem tree keys:\n");

		while (iter.node[0]) {
			hwfs_get_key(&iter, &key);
			fprintf(stderr, "%llu %u %llu\n",
				(unsigned long long) key.id,
				(unsigned) key.type,
				(unsigned long long) key.offset);
			hwfs_next(&trans, &trans.extent_tree, &iter);
		}
	}

	hwfs_trans_release(&trans);

	close(fd);

	return 0;
}

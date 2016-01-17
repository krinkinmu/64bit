#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

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

	close(fd);

	return 0;
}

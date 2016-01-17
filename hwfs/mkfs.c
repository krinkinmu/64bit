#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "disk-io.h"


static void test_disk_io(int fd)
{
	const size_t block_size = 4096;

	for (int i = 1; 1; ++i) {
		struct hwfs_io_extent *extent =
					hwfs_create_io_extent(i * block_size);

		if (!extent) {
			fprintf(stderr, "Cannot allocate io extent\n");
			return;
		}

		if (hwfs_sync_io_extent(fd, extent)) {
			fprintf(stderr, "Cannot sync %zu bytes extent\n",
						i * block_size);
			hwfs_put_io_extent(extent);
			return;
		}
	}
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

	test_disk_io(fd);

	close(fd);

	return 0;
}

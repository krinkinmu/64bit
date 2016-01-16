#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "format.h"


static const int block_size = 4096;


static long device_size(int fd)
{
	const off_t size = lseek(fd, 0, SEEK_END);

	if (size == (off_t)-1)
		return -1;
	return (long)size;
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

	const long size = device_size(fd);

	if (size < 0) {
		perror("Cannot detect device size");
		close(fd);
		return 1;
	}

	if (hwfs_format(fd, block_size, size) < 0)
		fprintf(stderr, "Format failed\n");

	close(fd);

	return 0;
}

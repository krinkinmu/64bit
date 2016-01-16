#include "format.h"
#include "cache.h"

int hwfs_format(int fd, int block_size, long size)
{
	struct hwfs_block_cache cache;

	hwfs_block_cache_setup(&cache, fd, block_size, size);

	struct hwfs_block *block0 = hwfs_get_block(&cache, 0);
	struct hwfs_block *block1 = hwfs_get_block(&cache, 1);
	struct hwfs_block *block2 = hwfs_get_block(&cache, 2);
	struct hwfs_block *block3 = hwfs_get_block(&cache, 3);

	hwfs_put_block(block3);
	hwfs_put_block(block2);
	hwfs_put_block(block1);
	hwfs_put_block(block0);

	hwfs_block_cache_release(&cache);

	return 0;
}

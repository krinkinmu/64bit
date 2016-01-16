#ifndef __HWFS_FORMAT_H__
#define __HWFS_FORMAT_H__

#include "hwfs.h"

struct hwfs_disk_stripe {
	long blocknr;
	long blocks;
	long free;
	long next;
};

struct hwfs_block_cache;

struct hwfs_trans {
	struct hwfs_block_cache *cache;
	struct hwfs_disk_stripe stripe;
};

int hwfs_format(int fd, int block_size, long size);

#endif /*__HWFS_FORMAT_H__*/

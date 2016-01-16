#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>

#include "cache.h"


struct hwfs_cache_iter {
	struct rb_node *parent;
	struct rb_node **plink;
	struct hwfs_block *block;
};

static int hwfs_find_block(struct hwfs_block_cache *cache,
			long blocknr, struct hwfs_cache_iter *iter)
{
	struct rb_node **plink = &cache->blocks.root;
	struct rb_node *parent = 0;
	struct hwfs_block *ptr = 0;

	while (*plink) {
		struct hwfs_block *x = TREE_ENTRY(*plink, struct hwfs_block,
					link);

		if (x->blocknr == blocknr) {
			ptr = x;
			break;
		}

		parent = *plink;
		if (x->blocknr < blocknr)
			plink = &parent->right;
		else
			plink = &parent->left;
	}

	iter->parent = parent;
	iter->plink = plink;
	iter->block = ptr;

	return ptr != 0;
}

static int hwfs_read_block(struct hwfs_block *block)
{
	struct hwfs_block_cache *cache = block->cache;
	const int block_size = cache->block_size;
	char *buf = block->data;
	int rd = 0;

	if (lseek(cache->fd, block_size * block->blocknr, SEEK_SET) < 0) {
		perror("Failed to set disk position");
		return -1;
	}

	while (rd < block_size) {
		const int rc = read(cache->fd, buf + rd, block_size - rd);

		if (rc < 0) {
			perror("Failed to read block");
			return -1;
		}

		if (rc == 0) {
			fprintf(stderr, "Unexpected end of disk");
			return -1;
		}

		rd += rc;
	}

	return 0;
}

static int hwfs_write_block(struct hwfs_block *block)
{
	struct hwfs_block_cache *cache = block->cache;
	const int block_size = cache->block_size;
	const char *buf = block->data;
	int wr = 0;

	if (lseek(cache->fd, block_size * block->blocknr, SEEK_SET) < 0) {
		perror("Failed to set disk position");
		return -1;
	}

	while (wr < block_size) {
		const int rc = write(cache->fd, buf + wr, block_size - wr);

		if (rc < 0) {
			perror("Failed to write block");
			return -1;
		}

		wr += rc;
	}
	return 0;
}

struct hwfs_block *hwfs_get_block(struct hwfs_block_cache *cache, long blocknr)
{
	struct hwfs_cache_iter iter;

	if (hwfs_find_block(cache, blocknr, &iter)) {
		++iter.block->links;
		return iter.block;
	}

	struct hwfs_block *block = malloc(sizeof(*block) + cache->block_size);

	if (!block)
		return 0;

	memset(block, 0, sizeof(*block));
	block->cache = cache;
	block->blocknr = blocknr;
	block->links = 1;
	block->data = block + 1;

	if (hwfs_read_block(block) < 0) {
		free(block);
		return 0;
	}
	
	rb_link(&block->link, iter.parent, iter.plink);
	rb_insert(&block->link, &cache->blocks);

	return block;
}

void hwfs_put_block(struct hwfs_block *block)
{
	struct hwfs_block_cache *cache = block->cache;

	if (--block->links > 0)
		return;

	if (!hwfs_write_block(block)) {
		rb_erase(&block->link, &cache->blocks);
		free(block);
	}
}

void hwfs_block_cache_setup(struct hwfs_block_cache *cache, int fd,
			int block_size, long disk_size)
{
	memset(cache, 0, sizeof(*cache));
	cache->block_size = block_size;
	cache->disk_size = disk_size;
	cache->fd = fd;
}

static void hwfs_release_blocks(struct rb_node *node)
{
	while (node) {
		struct hwfs_block *block = TREE_ENTRY(node, struct hwfs_block,
					link);

		hwfs_release_blocks(node->right);
		node = node->left;
		hwfs_write_block(block);
		free(block);
	}
}

void hwfs_block_cache_release(struct hwfs_block_cache *cache)
{ hwfs_release_blocks(cache->blocks.root); }

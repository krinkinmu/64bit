#include <sys/types.h>
#include <unistd.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "disk-io.h"


static const size_t cache_threshold = 1000;


static int read_at(int fd, size_t off, void *data, size_t size)
{
	char *buf = data;
	size_t rd = 0;

	if (lseek(fd, off, SEEK_SET) == (off_t)-1) {
		perror("Seek failed");
		return -1;
	}

	while (rd != size) {
		const ssize_t rc = read(fd, buf + rd, size - rd);

		if (rc == 0) {
			fprintf(stderr, "Unexpected end of file\n");
			return -1;
		}

		if (rc < 0) {
			perror("Read failed");
			return -1;
		}

		rd += rc;
	}
	return 0;
}

static int write_at(int fd, size_t off, const void *data, size_t size)
{
	const char *buf = data;
	size_t wr = 0;

	if (lseek(fd, off, SEEK_SET) == (off_t)-1) {
		perror("Seek failed");
		return -1;
	}

	while (wr < size) {
		const ssize_t rc = write(fd, buf + wr, size - wr);

		if (rc < 0) {
			perror("Write failed");
			return -1;
		}

		wr += rc;
	}
	return 0;
}

static int disk_read_block(struct disk_io *dio, struct disk_block *block)
{
	return read_at(dio->fd, dio->block_size * block->blocknr,
				block->data, dio->block_size);
}

static int disk_write_block(struct disk_io *dio, struct disk_block *block)
{
	return write_at(dio->fd, dio->block_size * block->blocknr,
				block->data, dio->block_size);
}

void setup_disk_io(struct disk_io *dio, size_t block_size, int fd)
{
	memset(dio, 0, sizeof(*dio));
	list_init(&dio->lru);
	dio->block_size = block_size;
	dio->fd = fd;
}

static struct disk_block *disk_alloc_block(struct disk_io *dio)
{
	if (dio->block_count >= cache_threshold && !list_empty(&dio->lru)) {
		struct list_head *ptr = list_first(&dio->lru);
		struct disk_block *block = LIST_ENTRY(ptr, struct disk_block,
					link);

		if (!disk_write_block(dio, block)) {
			rb_erase(&block->node, &dio->blocks);
			list_del(ptr);
			return block;
		}
	}

	struct disk_block *block = malloc(sizeof(*block) + dio->block_size);

	if (block) {
		block->data = block + 1;
		++dio->block_count;
	}
	return block;
}

static void disk_drop_block(struct disk_io *dio, struct disk_block *block)
{
	free(block);
	--dio->block_count;
}

static void __release_blocks(struct disk_io *dio, struct rb_node *node)
{
	while (node) {
		struct disk_block *block = TREE_ENTRY(node, struct disk_block,
					node);

		__release_blocks(dio, node->left);
		node = node->right;

		if (block->links)
			fprintf(stderr, "Someone stil holds reference to %zu\n",
						block->blocknr);

		disk_write_block(dio, block);
		disk_drop_block(dio, block);
	}
}

void release_disk_io(struct disk_io *dio)
{
	__release_blocks(dio, dio->blocks.root);
}

struct disk_block *disk_get_block(struct disk_io *dio, size_t blocknr)
{
	struct rb_node **plink = &dio->blocks.root;
	struct rb_node *parent = 0;

	while (*plink) {
		struct disk_block *block = TREE_ENTRY(*plink, struct disk_block,
					node);

		if (block->blocknr == blocknr) {
			if (++block->links == 1)
				list_del(&block->link);
			return block;
		}

		parent = *plink;
		if (block->blocknr < blocknr)
			plink = &parent->right;
		else
			plink = &parent->left;
	}

	struct disk_block *block = disk_alloc_block(dio);

	if (!block)
		return 0;

	block->blocknr = blocknr;
	block->data = block + 1;
	block->links = 1;

	if (disk_read_block(dio, block)) {
		disk_drop_block(dio, block);
		return 0;
	}

	rb_link(&block->node, parent, plink);
	rb_insert(&block->node, &dio->blocks);
	return block;
}

void disk_put_block(struct disk_io *dio, struct disk_block *block)
{
	if (!block)
		return;

	if (--block->links == 0) {
		if (dio->block_count > cache_threshold &&
				!disk_write_block(dio, block)) {
			rb_erase(&block->node, &dio->blocks);
			disk_drop_block(dio, block);
		} else {
			list_add_tail(&block->link, &dio->lru);
		}
	}
}

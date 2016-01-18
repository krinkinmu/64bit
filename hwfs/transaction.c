#include <string.h>
#include <stdio.h>

#include "transaction.h"
#include "hwfs.h"

int64_t hwfs_trans_alloc(struct hwfs_trans *trans, size_t blocks)
{
	(void) trans;
	(void) blocks;

	return 0;
}

void hwfs_trans_free(struct hwfs_trans *trans, uint64_t blocknr, size_t blocks)
{
	(void) trans;
	(void) blocknr;
	(void) blocks;
}

static struct hwfs_io_extent *__hwfs_trans_get_extent(struct hwfs_trans *trans,
				uint64_t blocknr, int sync)
{
	const uint64_t offset = trans->node_size * blocknr;
	struct rb_node **plink = &trans->io_cache.root;
	struct rb_node *parent = 0;
	struct hwfs_io_extent *ext = 0;

	while (*plink) {
		ext = TREE_ENTRY(*plink, struct hwfs_io_extent, link);

		parent = *plink;

		if (ext->offset == offset)
			return hwfs_get_io_extent(ext);

		if (ext->offset < offset)
			plink = &parent->right;
		else
			plink = &parent->left;
	}

	ext = hwfs_create_io_extent(trans->node_size);
	if (!ext)
		return 0;

	if (sync && hwfs_sync_io_extent(trans->fd, ext) != 0) {
		hwfs_put_io_extent(ext);
		return 0;
	}

	rb_link(&ext->link, parent, plink);
	rb_insert(&ext->link, &trans->io_cache);
	return ext;
}

struct hwfs_io_extent *hwfs_trans_get_extent(struct hwfs_trans *trans,
				uint64_t blocknr)
{
	return __hwfs_trans_get_extent(trans, blocknr, 1);
}

struct hwfs_io_extent *hwfs_trans_get_new_extent(struct hwfs_trans *trans,
				uint64_t blocknr)
{
	return __hwfs_trans_get_extent(trans, blocknr, 0);
}

void hwfs_trans_put_extent(struct hwfs_trans *trans, struct hwfs_io_extent *ext)
{
	if (ext->links == 1)
		rb_erase(&ext->link, &trans->io_cache);
	hwfs_put_io_extent(ext);
}

int hwfs_trans_setup(struct hwfs_trans *trans, int fd)
{
	memset(trans, 0, sizeof(*trans));
	trans->node_size = 512;
	trans->fd = fd;
	trans->super_block = hwfs_trans_get_extent(trans, 0);
	if (!trans->super_block)
		return -1;

	struct hwfs_super_block super;
	struct hwfs_disk_super_block *sb = trans->super_block->data;

	hwfs_super_to_host(&super, sb);

	trans->node_size = super.node_size;

	if (hwfs_setup_tree(trans, &trans->fs_tree, super.fs_tree_root)) {
		hwfs_trans_put_extent(trans, trans->super_block);
		return -1;
	}

	if (hwfs_setup_tree(trans, &trans->extent_tree, super.extent_tree_root)) {
		hwfs_release_tree(trans, &trans->fs_tree);
		hwfs_trans_put_extent(trans, trans->super_block);
		return -1;
	}

	return 0;
}

static void __hwfs_trans_release_cache(struct hwfs_trans *trans,
			struct rb_node *node)
{
	while (node) {
		struct hwfs_io_extent *ext = TREE_ENTRY(node,
					struct hwfs_io_extent, link);

		__hwfs_trans_release_cache(trans, node->left);
		node = node->right;
		hwfs_put_io_extent(ext);
	}
}

static void hwfs_trans_release_cache(struct hwfs_trans *trans)
{
	if (trans->io_cache.root) {
		fprintf(stderr, "IO cache still contains extents\n");
		__hwfs_trans_release_cache(trans, trans->io_cache.root);
	}
}

void hwfs_trans_release(struct hwfs_trans *trans)
{
	hwfs_release_tree(trans, &trans->extent_tree);
	hwfs_release_tree(trans, &trans->fs_tree);
	hwfs_trans_release_cache(trans);
}

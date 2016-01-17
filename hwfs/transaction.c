#include <string.h>
#include <stdio.h>

#include "transaction.h"
#include "hwfs.h"

static struct hwfs_io_extent *hwfs_trans_get_extent(struct hwfs_trans *trans,
				uint64_t blocknr)
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

	if (hwfs_sync_io_extent(trans->fd, ext) != 0) {
		hwfs_put_io_extent(ext);
		return 0;
	}

	rb_link(&ext->link, parent, plink);
	rb_insert(&ext->link, &trans->io_cache);
	return ext;
}

static void hwfs_trans_put_extent(struct hwfs_trans *trans,
			struct hwfs_io_extent *ext)
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
	trans->fs_tree.root = hwfs_trans_get_extent(trans, super.fs_tree_root);
	if (!trans->fs_tree.root) {
		hwfs_trans_put_extent(trans, trans->super_block);
		return -1;
	}

	trans->extent_tree.root =
			hwfs_trans_get_extent(trans, super.extent_tree_root);
	if (!trans->extent_tree.root) {
		hwfs_trans_put_extent(trans, trans->fs_tree.root);
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
	hwfs_trans_put_extent(trans, trans->extent_tree.root);
	hwfs_trans_put_extent(trans, trans->fs_tree.root);
	hwfs_trans_put_extent(trans, trans->super_block);
	hwfs_trans_release_cache(trans);
}

#include <stddef.h>

#include "kmem_cache.h"
#include "memory.h"
#include "string.h"
#include "error.h"
#include "ramfs.h"


static struct kmem_cache *ramfs_node_cache;
static struct kmem_cache *ramfs_entry_cache;

static struct fs_node_ops ramfs_file_node_ops;
static struct fs_node_ops ramfs_dir_node_ops;

struct ramfs_dir_iterator {
	struct ramfs_entry *entry;
	struct rb_node *parent;
	struct rb_node **plink;
};


static struct fs_node *VFS_NODE(struct ramfs_node *node)
{ return &node->vfs_node; }

static struct ramfs_node *RAMFS_NODE(struct fs_node *node)
{ return (struct ramfs_node *)node; }

static struct ramfs_node *ramfs_node_create(struct fs_node_ops *ops)
{
	struct ramfs_node *node = kmem_cache_alloc(ramfs_node_cache);

	if (node) {
		memset(node, 0, sizeof(*node));
		vfs_node_get(VFS_NODE(node));
		VFS_NODE(node)->ops = ops;
	}
	return node;
}

static struct ramfs_entry *ramfs_entry_create(const char *name,
			struct ramfs_node *node)
{
	struct ramfs_entry *entry = kmem_cache_alloc(ramfs_entry_cache);

	if (entry) {
		memset(entry, 0, sizeof(*entry));
		strcpy(entry->name, name);
		entry->node = node;
	}
	return entry;
}

static void ramfs_entry_destroy(struct ramfs_entry *entry)
{
	vfs_node_put(VFS_NODE(entry->node));
	kmem_cache_free(ramfs_entry_cache, entry);
}

static bool ramfs_entry_lookup(struct ramfs_node *dir, const char *name,
			struct ramfs_dir_iterator *iter)
{
	struct rb_node **plink = &dir->children.root;
	struct rb_node *parent = 0;
	struct ramfs_entry *entry = 0;

	while (*plink) {
		entry = TREE_ENTRY(*plink, struct ramfs_entry, link);

		const int cmp = strcmp(entry->name, name);

		if (!cmp)
			break;

		if (cmp < 0)
			plink = &(*plink)->right;
		else
			plink = &(*plink)->left;
		parent = *plink;
	}

	iter->entry = entry;
	iter->parent = parent;
	iter->plink = plink;

	return entry != 0;
}

static void ramfs_entry_link(struct ramfs_node *dir, struct ramfs_entry *entry,
			struct ramfs_dir_iterator *iter)
{
	vfs_node_get(VFS_NODE(dir));
	rb_link(&entry->link, iter->parent, iter->plink);
	rb_insert(&entry->link, &dir->children);
}

static int ramfs_entry_unlink(struct ramfs_node *dir, struct fs_entry *entry)
{
	struct ramfs_dir_iterator iter;

	if (!ramfs_entry_lookup(dir, entry->name, &iter))
		return -ENOENT;

	rb_erase(&iter.entry->link, &dir->children);
	ramfs_entry_destroy(iter.entry);
	vfs_node_put(VFS_NODE(dir));
	vfs_node_put(entry->node);
	entry->node = 0;
	return 0;
}

static int ramfs_create_ops(struct fs_node *dir, struct fs_entry *file,
			struct fs_node_ops *ops)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);
	struct ramfs_dir_iterator iter;

	if (ramfs_entry_lookup(parent, file->name, &iter))
		return -EEXIST;

	struct ramfs_node *node = ramfs_node_create(ops);

	if (!node)
		return -ENOMEM;

	struct ramfs_entry *entry = ramfs_entry_create(file->name, node);

	if (!entry) {
		vfs_node_put(VFS_NODE(node));
		return -ENOMEM;
	}

	ramfs_entry_link(parent, entry, &iter);
	file->node = vfs_node_get(VFS_NODE(node));
	return 0;
}

static int ramfs_link(struct fs_entry *src, struct fs_node *dir,
			struct fs_entry *dst)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);
	struct ramfs_dir_iterator iter;

	if (src->node->ops != &ramfs_file_node_ops)
		return -ENOTSUP;

	if (ramfs_entry_lookup(parent, dst->name, &iter))
		return -EEXIST;

	struct ramfs_node *node = RAMFS_NODE(vfs_node_get(src->node));
	struct ramfs_entry *entry = ramfs_entry_create(dst->name, node);

	if (!entry) {
		vfs_node_put(VFS_NODE(node));
		return -ENOMEM;
	}

	ramfs_entry_link(parent, entry, &iter);
	return 0;	
}

static int ramfs_create(struct fs_node *dir, struct fs_entry *entry)
{ return ramfs_create_ops(dir, entry, &ramfs_file_node_ops); }

static int ramfs_mkdir(struct fs_node *dir, struct fs_entry *entry)
{ return ramfs_create_ops(dir, entry, &ramfs_dir_node_ops); }

static int ramfs_unlink(struct fs_node *dir, struct fs_entry *entry)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);

	if (entry->node->ops != &ramfs_file_node_ops)
		return -ENOTSUP;

	return ramfs_entry_unlink(parent, entry);
}

static int ramfs_rmdir(struct fs_node *dir, struct fs_entry *entry)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);

	if (entry->node->ops != &ramfs_dir_node_ops)
		return -ENOTSUP;

	return ramfs_entry_unlink(parent, entry);
}

static int ramfs_lookup(struct fs_node *dir, struct fs_entry *entry)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);
	struct ramfs_dir_iterator iter;

	if (entry->node->ops != &ramfs_dir_node_ops)
		return -ENOTSUP;

	if (!ramfs_entry_lookup(parent, entry->name, &iter))
		return -ENOENT;

	entry->node = vfs_node_get(VFS_NODE(iter.entry->node));
	return 0;	
}

static void ramfs_release_file_node(struct fs_node *node)
{
	free_pages(RAMFS_NODE(node)->page, 0);
	kmem_cache_free(ramfs_node_cache, node);
}

static void ramfs_dir_release(struct rb_node *node)
{
	while (node) {
		struct ramfs_entry *entry = TREE_ENTRY(node, struct ramfs_entry,
					link);

		ramfs_entry_destroy(entry);
		ramfs_dir_release(node->right);
		node = node->left;
	}
}

static void ramfs_release_dir_node(struct fs_node *node)
{
	struct ramfs_node *dir = RAMFS_NODE(node);

	ramfs_dir_release(dir->children.root);
	kmem_cache_free(ramfs_node_cache, dir);
}

static struct fs_node_ops ramfs_file_node_ops = {
	.release = ramfs_release_file_node
};

static struct fs_node_ops ramfs_dir_node_ops = {
	.create = ramfs_create,
	.link = ramfs_link,
	.unlink = ramfs_unlink,
	.mkdir = ramfs_mkdir,
	.rmdir = ramfs_rmdir,
	.lookup = ramfs_lookup,
	.release = ramfs_release_dir_node
};

static int ramfs_mount(struct fs_mount *mnt, const void *data, size_t size)
{
	(void) data;
	(void) size;

	struct ramfs_node *root = ramfs_node_create(&ramfs_dir_node_ops);

	if (!root)
		return -ENOMEM;

	mnt->root = VFS_NODE(root);
	return 0;
}

static void ramfs_umount(struct fs_mount *mnt)
{ vfs_node_put(mnt->root); }

static struct fs_type_ops ramfs_type_ops = {
	.mount = ramfs_mount,
	.umount = ramfs_umount
};

static struct fs_type ramfs_type = {
	.name = "ramfs",
	.ops = &ramfs_type_ops
};

void setup_ramfs(void)
{
	ramfs_node_cache = KMEM_CACHE(struct ramfs_node);
	ramfs_entry_cache = KMEM_CACHE(struct ramfs_entry);
	register_filesystem(&ramfs_type);
}

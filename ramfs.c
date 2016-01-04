#include <stddef.h>
#include "ramfs.h"


static struct kmem_cache *ramfs_node_cache;
static struct kmem_cache *ramfs_entry_cache;

extern static struct fs_node_ops ramfs_file_node_ops;
extern static struct fs_node_ops ramfs_dir_node_ops;

struct ramfs_dir_iterator {
	struct ramfs_entry *entry;
	struct rb_node *parent;
	struct rb_node **plink;
};


static struct fs_node *VFS_NODE(struct ramfs_node *node)
{ return &node->vfs_node; }

static struct ramfs_node *RAMFS_NODE(struct fs_node *node)
{ return (struct ramfs_node *)node; }

static struct ramfs_node *ramfs_node_create(struct vfs_node_ops *ops)
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
	if (entry->node)
		vfs_node_put(VFS_NODE(entry->node));
	kmem_cache_free(ramfs_entry_cache, entry);
}

static bool ramfs_entry_lookup(struct fs_node *dir, const char *name,
			struct ramfs_dir_iterator *iter)
{
	struct rb_node **plink = &dir->chidren.root;
	struct rb_node *parent = 0;
	struct ramfs_entry *entry;

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

static void ramfs_entry_unlink(struct ramfs_node *dir,
			struct ramfs_entry *entry)
{
	rb_erase(&entry->link, &dir->children);
	vfs_node_put(VFS_NODE(dir));
}

static int ramfs_create_ops(struct fs_node *dir, struct fs_entry *file,
			struct fs_node_ops *ops)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);
	struct ramfs_entry_iterator iter;

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
	struct ramfs_entry_iterator iter;

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

static int ramfs_unlink(struct fs_node *dir, struct fs_entry *entry)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);
	struct ramfs_dir_iterator iter;

	if (src->node->ops != &ramfs_file_node_ops)
		return -ENOTSUP;

	if (ramfs_entry_lookup(parent, entry->name, &iter))
		return -EEXIST;

	ramfs_entry_unlink(parent, iter->entry);
	return 0;
}

static int ramfs_create(struct fs_node *dir, struct fs_entry *entry)
{ return ramfs_create_ops(dir, entry, &ramfs_file_node_ops); }

static int ramfs_mkdir(struct fs_node *dir, struct fs_entry *entry)
{ return ramfs_create_ops(dir, entry, &ramfs_dir_node_ops); }

static void __ramfs_dir_release(struct rb_node *node)
{
	while (node) {
		struct ramfs_entry *entry = LIST_ENTRY(node,
					struct ramfs_entry, link);

		__ramfs_dir_release(node->rb_right);
		node = node->rb_left;
		ramfs_entry_destroy(entry);
	}
}

static void ramfs_dir_release(struct ramfs_node *node)
{
	__ramfs_dir_release(node->children.root);
	node->children = { 0 };
}

static int ramfs_rmdir(struct fs_node *dir, struct fs_entry *entry)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);
	struct ramfs_node *node = RAMFS_NODE(entry->node);
	struct ramfs_dir_iterator iter;

	if (src->node->ops != &ramfs_dir_node_ops)
		return -ENOTSUP;

	if (ramfs_entry_lookup(parent, entry->name, &iter))
		return -EEXIST;

	ramfs_entry_unlink(parent, iter->entry);
	vfs_node_put(entry->node);
	entry->node = 0;
	return 0;
}

static int ramfs_mount(struct fs_mount *mnt, const void *data, size_t size)
{
	
}

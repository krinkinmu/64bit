#include <stdbool.h>
#include <stddef.h>

#include "kmem_cache.h"
#include "kernel.h"
#include "paging.h"
#include "memory.h"
#include "string.h"
#include "error.h"
#include "ramfs.h"


static struct kmem_cache *ramfs_node_cache;
static struct kmem_cache *ramfs_entry_cache;

static struct fs_node_ops ramfs_file_node_ops;
static struct fs_file_ops ramfs_file_ops;

static struct fs_node_ops ramfs_dir_node_ops;
static struct fs_file_ops ramfs_dir_ops;

struct ramfs_dir_iterator {
	struct ramfs_entry *entry;
	struct rb_node *parent;
	struct rb_node **plink;
};


static struct fs_node *VFS_NODE(struct ramfs_node *node)
{ return &node->vfs_node; }

static struct ramfs_node *RAMFS_NODE(struct fs_node *node)
{ return (struct ramfs_node *)node; }

static struct ramfs_node *ramfs_node_create(struct fs_node_ops *ops,
			struct fs_file_ops *fops)
{
	struct ramfs_node *node = kmem_cache_alloc(ramfs_node_cache);

	if (node) {
		memset(node, 0, sizeof(*node));
		vfs_node_init(VFS_NODE(node));
		list_init(&node->pages);
		vfs_node_get(VFS_NODE(node));
		VFS_NODE(node)->ops = ops;
		VFS_NODE(node)->fops = fops;
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
	struct ramfs_entry *entry;

	iter->entry = 0;

	while (*plink) {
		entry = TREE_ENTRY(*plink, struct ramfs_entry, link);

		const int cmp = strcmp(entry->name, name);

		if (!cmp) {
			iter->entry = entry;
			break;
		}

		parent = *plink;
		if (cmp < 0)
			plink = &parent->right;
		else
			plink = &parent->left;
	}

	iter->parent = parent;
	iter->plink = plink;

	return iter->entry != 0;
}

static void ramfs_entry_link(struct ramfs_node *dir, struct ramfs_entry *entry,
			struct ramfs_dir_iterator *iter)
{
	const bool enabled = spin_lock_irqsave(&VFS_NODE(dir)->lock);

	++VFS_NODE(dir)->refcount;
	++VFS_NODE(dir)->size;
	spin_unlock_irqrestore(&VFS_NODE(dir)->lock, enabled);

	rb_link(&entry->link, iter->parent, iter->plink);
	rb_insert(&entry->link, &dir->children);
}

static int ramfs_entry_unlink(struct ramfs_node *dir, struct fs_entry *entry)
{
	struct ramfs_dir_iterator iter;

	if (!ramfs_entry_lookup(dir, entry->name, &iter))
		return -ENOENT;

	rb_erase(&iter.entry->link, &dir->children);

	const bool enabled = spin_lock_irqsave(&VFS_NODE(dir)->lock);
	--VFS_NODE(dir)->size;
	spin_unlock_irqrestore(&VFS_NODE(dir)->lock, enabled);

	ramfs_entry_destroy(iter.entry);
	vfs_entry_detach(entry);
	return 0;
}

static int ramfs_create_ops(struct fs_node *dir, struct fs_entry *file,
			struct fs_node_ops *ops, struct fs_file_ops *fops)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);
	struct ramfs_dir_iterator iter;

	if (ramfs_entry_lookup(parent, file->name, &iter))
		return -EEXIST;

	struct ramfs_node *node = ramfs_node_create(ops, fops);

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

	/* Just a sanity check */
	if (dir->ops != &ramfs_dir_node_ops)
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
	dst->node = vfs_node_get(VFS_NODE(node));
	return 0;	
}

static int ramfs_create(struct fs_node *dir, struct fs_entry *entry)
{ return ramfs_create_ops(dir, entry, &ramfs_file_node_ops, &ramfs_file_ops); }

static int ramfs_mkdir(struct fs_node *dir, struct fs_entry *entry)
{ return ramfs_create_ops(dir, entry, &ramfs_dir_node_ops, &ramfs_dir_ops); }

static int ramfs_unlink(struct fs_node *dir, struct fs_entry *entry)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);

	if (dir->ops != &ramfs_dir_node_ops)
		return -ENOTSUP;

	if (!entry->node)
		return -EINVAL;

	if (entry->node->ops != &ramfs_file_node_ops)
		return -ENOTSUP;

	return ramfs_entry_unlink(parent, entry);
}

static int ramfs_rmdir(struct fs_node *dir, struct fs_entry *entry)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);

	if (dir->ops != &ramfs_dir_node_ops)
		return -ENOTSUP;

	return ramfs_entry_unlink(parent, entry);
}

static int ramfs_lookup(struct fs_node *dir, struct fs_entry *entry)
{
	struct ramfs_node *parent = RAMFS_NODE(dir);
	struct ramfs_dir_iterator iter;

	if (dir->ops != &ramfs_dir_node_ops)
		return -ENOTSUP;

	if (!ramfs_entry_lookup(parent, entry->name, &iter))
		return -ENOENT;

	entry->node = vfs_node_get(VFS_NODE(iter.entry->node));
	return 0;	
}

static void ramfs_release_file_node(struct fs_node *node)
{
	struct list_head *head = &RAMFS_NODE(node)->pages;
	struct list_head *ptr = head->next;

	while (ptr != head) {
		struct page *page = LIST_ENTRY(ptr, struct page, link);

		ptr = ptr->next;
		free_pages(page, 0);
	}
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

static int ramfs_write(struct fs_file *file, const char *data, size_t size)
{
	struct fs_node *fs_node = file->node;
	struct ramfs_node *node = RAMFS_NODE(fs_node);
	struct list_head *head = &node->pages;
	struct list_head *ptr = head;

	const bool enabled = spin_lock_irqsave(&fs_node->lock);

	const size_t idx = file->offset >> PAGE_BITS;
	const size_t off = file->offset & PAGE_MASK;
	const size_t sz = MINU(PAGE_SIZE - off, size);

	for (size_t i = 0; i <= idx; ++i) {
		if (ptr->next == head) {
			struct page *page = alloc_pages(0, NT_HIGH);

			if (!page) {
				spin_unlock_irqrestore(&fs_node->lock, enabled);
				return -ENOMEM;
			}

			list_add(&page->link, ptr);
		}
		ptr = ptr->next;
	}

	struct page *page = LIST_ENTRY(ptr, struct page, link);
	char *vaddr = kmap(page, 1);

	if (!vaddr) {
		spin_unlock_irqrestore(&fs_node->lock, enabled);
		return -ENOMEM;
	}

	memcpy(vaddr + off, data, sz);
	kunmap(vaddr);
	file->offset += sz;
	file->node->size = MAX(file->offset, file->node->size);
	spin_unlock_irqrestore(&fs_node->lock, enabled);

	return (int)sz;
}

static int ramfs_read(struct fs_file *file, char *data, size_t size)
{
	struct fs_node *fs_node = file->node;
	struct ramfs_node *node = RAMFS_NODE(fs_node);
	struct list_head *head = &node->pages;
	struct list_head *ptr = head;

	const bool enabled = spin_lock_irqsave(&fs_node->lock);

	if (file->offset >= file->node->size) {
		spin_unlock_irqrestore(&fs_node->lock, enabled);
		return 0;
	}

	const size_t rem = file->node->size - file->offset;
	const size_t idx = file->offset >> PAGE_BITS;
	const size_t off = file->offset & PAGE_MASK;
	const size_t sz = MINU(MINU(PAGE_SIZE - off, size), rem);

	for (size_t i = 0; i <= idx; ++i) {
		if (ptr->next == head) {
			spin_unlock_irqrestore(&fs_node->lock, enabled);
			return 0;
		}
		ptr = ptr->next;
	}

	struct page *page = LIST_ENTRY(ptr, struct page, link);
	char *vaddr = kmap(page, 1);

	if (!vaddr) {
		spin_unlock_irqrestore(&fs_node->lock, enabled);
		return -ENOMEM;
	}

	memcpy(data, vaddr + off, sz);
	kunmap(vaddr);
	file->offset += sz;
	spin_unlock_irqrestore(&fs_node->lock, enabled);

	return (int)sz;	
}

static int ramfs_iterate(struct fs_file *dir, struct dir_iter_ctx *ctx)
{
	struct ramfs_node *node = RAMFS_NODE(dir->node);
	struct rb_node *ptr = rb_leftmost(node->children.root);

	int offset = ctx->offset;

	while (ptr && offset--)
		ptr = rb_next(ptr);

	while (ptr) {
		struct ramfs_entry *entry = TREE_ENTRY(ptr,
			struct ramfs_entry, link);

		ptr = rb_next(ptr);
		if (!ctx->emit(ctx, entry->name, strlen(entry->name)))
			break;
		++ctx->offset;
	}

	return 0;
}

static struct fs_file_ops ramfs_dir_ops = {
	.iterate = ramfs_iterate,
	.seek = vfs_seek_default
};

static struct fs_file_ops ramfs_file_ops = {
	.read = ramfs_read,
	.write = ramfs_write,
	.seek = vfs_seek_default
};

static int ramfs_mount(struct fs_mount *mnt, const void *data, size_t size)
{
	(void) data;
	(void) size;

	struct ramfs_node *root = ramfs_node_create(&ramfs_dir_node_ops,
		&ramfs_dir_ops);

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

#ifdef CONFIG_RAMFS_TEST
	void ramfs_test(void);

	ramfs_test();
#endif /* CONFIG_RAMFS_TEST */
}

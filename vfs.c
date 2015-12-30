#include "kmem_cache.h"
#include "kernel.h"
#include "string.h"
#include "vfs.h"


static struct kmem_cache *fs_entry_cache;
static struct fs_entry fs_root_entry;
static struct fs_node fs_root_node;

static LIST_HEAD(fs_mounts);
static LIST_HEAD(fs_types);


static void vfs_entry_destroy(struct fs_entry *entry);


static struct fs_entry *vfs_entry_get(struct fs_entry *entry)
{
	++entry->refcount;
	return entry;
}

static void vfs_entry_put(struct fs_entry *entry)
{
	if (--entry->refcount == 0)
		vfs_entry_destroy(entry);
}

static struct fs_entry *vfs_entry_lookup(struct fs_entry *dir, const char *name)
{
	struct rb_node **plink = &dir->children.root;
	struct rb_node *parent = 0;
	struct fs_entry *entry;
	int res;

	while (*plink) {
		parent = *plink;
		entry = TREE_ENTRY(parent, struct fs_entry, link);
		res = strcmp(entry->name, name);

		if (!res)
			return entry;

		if (res < 0)
			plink = &parent->right;
		else
			plink = &parent->left;
	}

	entry = kmem_cache_alloc(fs_entry_cache);
	if (!entry)
		return 0;

	memset(entry, 0, sizeof(*entry));

	struct fs_node *node = dir->node;

	if (node->ops->lookup(node, entry)) {
		kmem_cache_free(fs_entry_cache, entry);
		return 0;
	}

	entry->parent = vfs_entry_get(dir);
	rb_link(&entry->link, parent, plink);
	rb_insert(&entry->link, &dir->children);

	return 0;
}

static void vfs_entry_destroy(struct fs_entry *entry)
{
	if (entry->parent) {
		rb_erase(&entry->link, &entry->parent->children);
		vfs_entry_put(entry->parent);
	}
	kmem_cache_free(fs_entry_cache, entry);
}


static int vfs_root_lookup(struct fs_node *root, struct fs_entry *entry)
{
	struct list_head *head = &fs_mounts;
	struct list_head *ptr = head->next;

	(void) root;

	for (; ptr != head; ptr = ptr->next) {
		struct fs_mount *mount = LIST_ENTRY(ptr, struct fs_mount, link);
		const char *name = mount->name;

		if (!strcmp(name, entry->name)) {
			entry->node = mount->root;
			return 0;
		}
	}
	return -1;
}

static int vfs_root_iterate(struct fs_file *root, struct dir_iter_ctx *ctx)
{
	struct list_head *head = &fs_mounts;
	struct list_head *ptr = head->next;

	(void) root;

	for (long i = 0; i != ctx->offset && ptr != head; ptr = ptr->next);

	while (ptr != head) {
		struct fs_mount *mount = LIST_ENTRY(ptr, struct fs_mount, link);
		const char *name = mount->name;

		if (ctx->emit(ctx, name, strlen(name))) {
			++ctx->offset;
			break;
		}
	}
	return 0;
}

static struct fs_node_ops fs_root_node_ops = {
	.lookup = &vfs_root_lookup
};

static struct fs_file_ops fs_root_file_ops = {
	.iterate = &vfs_root_iterate
};


static const char *vfs_next_entry_name(char *next, const char *full)
{
	while (*full != '/' && *full != '\0')
		*next++ = *full++;
	*next = '\0';
	return *full == '/' ? full + 1 : full;
}

static struct fs_entry *vfs_resolve_name(const char *full)
{
	struct fs_entry *entry = &fs_root_entry;
	char next[MAX_PATH_LEN];

	++full;
	while (*full) {
		full = vfs_next_entry_name(next, full);
		entry = vfs_entry_lookup(entry, next);

		if (!entry)
			return 0;
	}
	return vfs_entry_get(entry);
}

int vfs_open(const char *name, struct fs_file *file)
{
	struct fs_entry *entry = vfs_resolve_name(name);

	if (!entry)
		return -1;

	file->entry = entry;
	file->node = entry->node;
	file->ops = entry->node->fops;
	file->offset = 0;

	if (file->ops->open) {
		const int ret = file->ops->open(file);

		if (ret) {
			vfs_entry_put(entry);
			return ret;
		}
	}

	return 0;
}

int vfs_release(struct fs_file *file)
{
	if (file->ops->release) {
		const int ret = file->ops->release(file);

		if (ret)
			return ret;
	}

	vfs_entry_put(file->entry);
	return 0;
}

int vfs_read(struct fs_file *file, char *buffer, size_t size)
{
	if (file->ops->read)
		return file->ops->read(file, buffer, size);
	return -1;
}

int vfs_write(struct fs_file *file, const char *buffer, size_t size)
{
	if (file->ops->write)
		return file->ops->write(file, buffer, size);
	return -1;
}


struct readdir_ctx {
	struct dir_iter_ctx ctx;
	struct dirent *entries;
	size_t count;
	size_t pos;
};

static int readdir_next_entry(struct dir_iter_ctx *ctx, const char *name,
			size_t len)
{
	struct readdir_ctx *rdctx = (struct readdir_ctx *)ctx;
	struct dirent *entries = rdctx->entries;
	size_t count = rdctx->count;
	size_t pos = rdctx->pos;

	if (pos != count) {
		memset(entries[pos].name, 0, MAX_PATH_LEN);
		strncpy(entries[pos].name, name, MINU(MAX_PATH_LEN - 1, len));
		++pos;
	}
	rdctx->pos = pos;
	return pos != count;
}

int vfs_readdir(struct fs_file *file, struct dirent *entries, size_t count)
{
	if (file->ops->iterate) {
		struct readdir_ctx ctx;

		ctx.ctx.emit = &readdir_next_entry;
		ctx.ctx.offset = file->offset;
		ctx.entries = entries;
		ctx.count = count;
		ctx.pos = 0;

		const int rc = file->ops->iterate(file, &ctx.ctx);

		if (file->offset != ctx.ctx.offset) {
			const int ret = ctx.ctx.offset - file->offset;

			file->offset = ctx.ctx.offset;
			return ret;
		}
		return rc;
	}

	return -1;
}


void setup_vfs(void)
{
	fs_entry_cache = KMEM_CACHE(struct fs_entry);

	fs_root_entry.node = &fs_root_node;
	fs_root_entry.refcount = 1;
	fs_root_node.ops = &fs_root_node_ops;
	fs_root_node.fops = &fs_root_file_ops;
}

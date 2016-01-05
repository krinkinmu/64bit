#include "kmem_cache.h"
#include "kernel.h"
#include "string.h"
#include "error.h"
#include "vfs.h"


static struct kmem_cache *fs_entry_cache;
static struct fs_entry fs_root_entry;
static struct fs_node fs_root_node;

static LIST_HEAD(fs_mounts);
static LIST_HEAD(fs_types);


struct fs_entry *vfs_entry_create(const char *name)
{
	struct fs_entry *entry = kmem_cache_alloc(fs_entry_cache);

	if (!entry)
		return 0;

	memset(entry, 0, sizeof(*entry));
	strcpy(entry->name, name);
	entry->refcount = 1;
	return entry;
}

static int vfs_lookup_child(struct fs_node *dir, struct fs_entry *child)
{
	if (!dir->ops->lookup)
		return -ENOTSUP;
	return dir->ops->lookup(dir, child);
}

static struct fs_entry * vfs_entry_lookup(struct fs_entry *dir,
			const char *name, bool create, int *rc)
{
	struct rb_node **plink = &dir->children.root;
	struct rb_node *parent = 0;
	struct fs_entry *entry;

	while (*plink) {
		parent = *plink;
		entry = TREE_ENTRY(parent, struct fs_entry, link);

		const int cmp = strcmp(entry->name, name);

		if (!cmp)
			return vfs_entry_get(entry);

		if (cmp < 0)
			plink = &parent->right;
		else
			plink = &parent->left;
	}

	entry = vfs_entry_create(name);
	if (!entry) {
		*rc = -ENOMEM;
		return 0;
	}

	*rc = vfs_lookup_child(dir->node, entry);
	if (*rc && !create) {
		vfs_entry_put(entry);
		return 0;
	}

	entry->parent = vfs_entry_get(dir);
	rb_link(&entry->link, parent, plink);
	rb_insert(&entry->link, &dir->children);
	return entry;
}

void vfs_entry_evict(struct fs_entry *entry)
{
	if (entry->parent) {
		rb_erase(&entry->link, &entry->parent->children);
		vfs_entry_put(entry->parent);
		entry->parent = 0;
	}
}

void vfs_entry_detach(struct fs_entry *entry)
{
	if (entry->node) {
		vfs_node_put(entry->node);
		entry->node = 0;
	}
}

void vfs_entry_destroy(struct fs_entry *entry)
{
	vfs_entry_evict(entry);
	vfs_entry_detach(entry);
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
			entry->node = vfs_node_get(mount->root);
			return 0;
		}
	}
	return -ENOENT;
}

static int vfs_root_iterate(struct fs_file *root, struct dir_iter_ctx *ctx)
{
	struct list_head *head = &fs_mounts;
	struct list_head *ptr = head->next;

	(void) root;

	for (int i = 0; i != ctx->offset && ptr != head; ptr = ptr->next);

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

static struct fs_entry *vfs_resolve_name(const char *path, bool create, int *rc)
{
	struct fs_entry *entry = vfs_entry_get(&fs_root_entry);

	++path;
	while (*path) {
		char name[MAX_PATH_LEN];
		struct fs_entry *next;

		path = vfs_next_entry_name(name, path);
		next = vfs_entry_lookup(entry, name, create && *path == 0, rc);
		vfs_entry_put(entry);
		entry = next;

		if (!entry)
			return 0;
	}

	return entry;
}

static void vfs_file_cleanup(struct fs_file *file)
{
	if (file->entry)
		vfs_entry_put(file->entry);

	file->entry = 0;
	file->node = 0;
	file->ops = 0;
	file->offset = 0;	
}

static int vfs_file_open(const char *name, struct fs_file *file, bool create)
{
	int rc = 0;
	struct fs_entry *entry = vfs_resolve_name(name, create, &rc);

	if (!entry)
		return rc;

	if (create) {
		struct fs_entry *parent = entry->parent;
		struct fs_node *node = parent->node;

		if (entry->node) {
			vfs_entry_put(entry);
			return -EEXIST;
		}

		if (!node->ops->create) {
			vfs_entry_put(entry);
			return -ENOTSUP;
		}

		const int ret = node->ops->create(node, entry);

		if (ret) {
			vfs_entry_put(entry);
			return ret;
		}
	}

	file->entry = entry;
	file->node = entry->node;
	file->ops = entry->node->fops;
	file->offset = 0;

	if (file->ops->open) {
		rc = file->ops->open(file);
		if (rc)
			vfs_file_cleanup(file);
	}

	return rc;
}

int vfs_create(const char *name, struct fs_file *file)
{ return vfs_file_open(name, file, true); }

int vfs_open(const char *name, struct fs_file *file)
{ return vfs_file_open(name, file, false); }

int vfs_release(struct fs_file *file)
{
	if (file->ops->release) {
		const int ret = file->ops->release(file);

		if (ret)
			return ret;
	}

	vfs_file_cleanup(file);
	return 0;
}

int vfs_read(struct fs_file *file, char *buffer, size_t size)
{
	if (file->ops->read)
		return file->ops->read(file, buffer, size);
	return -ENOTSUP;
}

int vfs_write(struct fs_file *file, const char *buffer, size_t size)
{
	if (file->ops->write)
		return file->ops->write(file, buffer, size);
	return -ENOTSUP;
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

	return -ENOTSUP;
}

int vfs_link(const char *oldname, const char *newname)
{
	int rc = 0;
	struct fs_entry *oldentry = vfs_resolve_name(oldname, false, &rc);

	if (!oldentry)
		return rc;

	struct fs_entry *parent = oldentry->parent;
	struct fs_node *dir = parent->node;

	if (!dir->ops->link) {
		vfs_entry_put(oldentry);
		return -ENOTSUP;
	}

	struct fs_entry *newentry = vfs_resolve_name(newname, true, &rc);

	if (!newentry) {
		vfs_entry_put(oldentry);
		return rc;
	}

	if (newentry->node) {
		vfs_entry_put(oldentry);
		vfs_entry_put(newentry);
		return -EEXIST;
	}

	const int ret = dir->ops->link(oldentry, dir, newentry);

	vfs_entry_put(oldentry);
	vfs_entry_put(newentry);
	return ret;
}

int vfs_unlink(const char *name)
{
	int rc = 0;
	struct fs_entry *entry = vfs_resolve_name(name, false, &rc);

	if (!entry)
		return rc;

	struct fs_entry *parent = entry->parent;
	struct fs_node *node = parent->node;

	if (!node->ops->unlink) {
		vfs_entry_put(entry);
		return -ENOTSUP;
	}

	const int ret = node->ops->unlink(node, entry);

	vfs_entry_put(entry);
	return ret;
}

int register_filesystem(struct fs_type *type)
{
	list_add_tail(&type->link, &fs_types);
	return 0;
}

int unregister_filesystem(struct fs_type *type)
{
	if (type->refcount)
		return -EBUSY;
	list_del(&type->link);
	return 0;
}


static struct fs_type *vfs_lookup_filesystem(const char *name)
{
	struct list_head *head = &fs_types;
	struct list_head *ptr = head->next;

	for (; ptr != head; ptr = ptr->next) {
		struct fs_type *fs = LIST_ENTRY(ptr, struct fs_type, link);

		if (!strcmp(fs->name, name))
			return fs;
	}
	return 0;
}

static struct fs_mount *vfs_lookup_mount(const char *name)
{
	struct list_head *head = &fs_mounts;
	struct list_head *ptr = head->next;

	for (; ptr != head; ptr = ptr->next) {
		struct fs_mount *mnt = LIST_ENTRY(ptr, struct fs_mount, link);

		if (!strcmp(mnt->name, name))
			return mnt;
	}
	return 0;
}

static struct fs_mount *vfs_mount_create(struct fs_type *fs, const char *name)
{
	struct fs_mount *mnt;

	if (fs->ops->alloc)
		mnt = fs->ops->alloc();
	else
		mnt = kmem_alloc(sizeof(*mnt));

	if (mnt) {
		memset(mnt, 0, sizeof(*mnt));
		strncpy(mnt->name, name, MAX_PATH_LEN - 1);
		mnt->fs = fs;
	}

	return mnt;
}

static void vfs_mount_destroy(struct fs_type *fs, struct fs_mount *mnt)
{
	if (fs->ops->free)
		fs->ops->free(mnt);
	else
		kmem_free(mnt);
}

int vfs_mount(const char *fs_name, const char *mount, const void *data,
			size_t size)
{
	struct fs_type *fs = vfs_lookup_filesystem(fs_name);

	if (!fs)
		return -ENOENT;

	if (vfs_lookup_mount(mount))
		return -EEXIST;

	struct fs_mount *mnt = vfs_mount_create(fs, mount);

	if (!mnt)
		return -ENOMEM;

	const int ret = fs->ops->mount(mnt, data, size);

	if (ret) {
		vfs_mount_destroy(mnt->fs, mnt);
		return ret;
	}

	list_add_tail(&mnt->link, &fs_mounts);
	++fs->refcount;
	return ret;
}

int vfs_umount(const char *mount)
{
	struct fs_mount *mnt = vfs_lookup_mount(mount);

	if (!mnt)
		return -ENOENT;

	list_del(&mnt->link);
	mnt->fs->ops->umount(mnt);
	--mnt->fs->refcount;
	vfs_mount_destroy(mnt->fs, mnt);
	return 0;
}

void setup_vfs(void)
{
	fs_entry_cache = KMEM_CACHE(struct fs_entry);

	fs_root_entry.node = &fs_root_node;
	fs_root_entry.refcount = 1;
	fs_root_node.ops = &fs_root_node_ops;
	fs_root_node.fops = &fs_root_file_ops;
}

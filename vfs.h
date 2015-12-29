#ifndef __VFS_H__
#define __VFS_H__

#include <stddef.h>

#include "rbtree.h"
#include "list.h"

struct fs_mount;
struct fs_entry;
struct fs_node;
struct fs_file;
struct dir_iter_ctx;

/**
 * alloc - allocates filesystem specific fs_mount [optional]
 * free - frees filesystem specific fs_mount [optional]
 * mount - takes fs_mount and mount arguments, and mount
 *         filesystem (or failes), e. g. reads superblock
 *         from disk, makes sanity checks, creates caches
 *         and so on.
 * umount - unmount filesystem. e. g. releases memory, flushes
 *          caches and so on.
 */
struct fs_type_ops {
	struct fs_mount *(*alloc)(void);
	void (*free)(struct fs_mount *);
	int (*mount)(struct fs_mount *, const void *, size_t);
	void (*umount)(struct fs_mount *);
	struct fs_node *(*root)(struct fs_mount *);
};

/**
 * create - creates a file
 * link - create hard link to the file specified by first
 *        fs_entry in the directory specified by fs_node
 *        with name specified by last fs_entry
 * unlink - deletes hard link to the file specified by
 *          fs_entry in the directory specified by fs_node.
 * remove - removes a file (we don't support hard links, so
 *          just remove)
 * mkdir - creates a directory
 * rmdir - removes a directory
 * lookup - lookups entry with a name specified by fs_entry
 *          in a directory specified by fs_node, and binds
 *          fs_node to fs_entry if successful.
 */
struct fs_node_ops {
	int (*create)(struct fs_node *, struct fs_entry *);
	int (*link)(struct fs_entry *, struct fs_node *, struct fs_entry *);
	int (*unlink)(struct fs_node *, struct fs_entry *);
	int (*mkdir)(struct fs_node *, struct fs_entry *);
	int (*rmdir)(struct fs_node *, struct fs_entry *);
	int (*lookup)(struct fs_node *, struct fs_entry *);
};

/**
 * open - called when file description created
 * release - called when last reference to fs_file released
 * read - reads data from the file to the specified buffer
 * writes - writed data to the file from the specified buffer
 * iterate - readdir but with little bit tricky interface.
 */
struct fs_file_ops {
	int (*open)(struct fs_file *);
	void (*release)(struct fs_file *);
	long (*read)(struct fs_file *, char *, size_t);
	long (*write)(struct fs_file *, const char *, size_t);
	int (*iterate)(struct fs_file *, struct dir_iter_ctx *);
};

/**
 * Filesystem driver provides struct fs_type to the kernel
 */
struct fs_type {
	struct list_head link;
	const char *name;
	struct fs_type_ops *ops;
};

/**
 * struct fs_mount represents mount point
 */
struct fs_mount {
	struct list_head link;
	struct fs_type *fs;
	struct fs_entry *root;
};

/**
 * struct fs_node is analogue of disk filesystem inode, for every
 * filesystem entity (file, directory, etc..) there is an fs_node
 */
struct fs_node {
	struct fs_mount *mount;
	struct fs_node_ops *ops;
};

/**
 * struct fs_entry represents chunk of file path, for example for
 * a path /usr/bin/python there might be three fs_entry structures:
 * usr, bin and python. So fs_entry structures reperesents a tree
 * of dirs (somehow... i don't know how to track children so far).
 */
struct fs_entry {
	struct rb_node link;
	struct rb_tree children;
	struct fs_entry *parent;
	struct fs_node *node;
	const char *name;
};

/**
 * struct fs_file is so called file description
 */
struct fs_file {
	struct fs_entry *entry;
	struct fs_node *node;
	struct fs_file_ops *ops;
	long offset;
};

/**
 * Just an utility structure for directory iteration.
 */
struct dir_iter_ctx {
	int (*emit)(struct dir_iter_ctx *, const char *, size_t);
	long offset;
};

#endif /*__VFS_H__*/

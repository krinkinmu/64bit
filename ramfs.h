#ifndef __RAMFS_H__
#define __RAMFS_H__

#include "rbtree.h"
#include "list.h"
#include "vfs.h"


struct ramfs_node {
	struct fs_node vfs_node;
	struct rb_tree children;
	struct list_head pages;
};

struct ramfs_entry {
	struct rb_node link;
	struct ramfs_node *node;
	char name[MAX_PATH_LEN];
};

void setup_ramfs(void);

#endif /*__RAMFS_H__*/

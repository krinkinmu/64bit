#ifndef __RAMFS_H__
#define __RAMFS_H__

#include "vfs.h"
#include "rbtree.h"


struct ramfs_node {
	struct fs_node vfs_node;
	struct rb_tree children;
	struct page *page;
};

struct ramfs_entry {
	struct rb_node link;
	struct ramfs_node *node;
	char name[MAX_PATH_LEN];
};

void ramfs_setup(void);

#endif /*__RAMFS_H__*/

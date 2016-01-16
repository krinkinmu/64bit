#ifndef __HWFS_H__
#define __HWFS_H__

#include <stdint.h>

#define HWFS_MAGIC    0x12345678
#define HWFS_MAX_PATH 16

/* directory entry type */
#define HWFS_FILE     1
#define HWFS_DIR      2

/* b+ tree value type */
#define HWFS_INODE    3
#define HWFS_ENTRY    4
#define HWFS_DATA     5
#define HWFS_EXTENT   6 /* extents stored in a separate tree */

/* pretty much btrfs key */
struct hwfs_key {
	uint64_t id;     // 0 for HWFS_EXTENT
	uint8_t type;
	uint64_t offset; // or hash for HWFS_ENTRY
} __attribute__((packed));

/* b+ internal node is array of hwfs_items and hwfs_node_header */
struct hwfs_item {
	struct hwfs_key key;
	uint64_t offset;
} __attribute__((packed));

/* b+ leaf is array of hwfs_values and hwfs_leaf_header */
struct hwfs_value {
	struct hwfs_key key;
	uint16_t offset;     // data offset in block
} __attribute__((packed));

struct hwfs_inode {
	uint64_t size;
	uint32_t links;
	uint8_t type;
} __attribute__((packed));

struct hwfs_entry {
	uint64_t nodeid;          // child, not parent
	uint8_t type;             // child type
	char name[HWFS_MAX_PATH];
} __attribute__((packed));

struct hwfs_data {
	uint64_t offset; // disk offset of file data extent
	uint64_t size;   // size of file data extent
} __attribute__((packed));

struct hwfs_extent {
	uint64_t size;
	uint64_t free;
} __attribute__((packed));

struct hwfs_super_block {
	uint64_t magic;
	uint64_t fs_tree_root;
	uint64_t extent_tree_root;
	uint64_t root_node_id;
	uint64_t next_node_id;
	uint16_t node_size;        // fanout
} __attribute__((packed));

struct hwfs_node_header {
	uint16_t level;
	uint16_t items;
	struct hwfs_item item[];
} __attribute__((packed));

struct hwfs_leaf_header {
	uint16_t level;
	uint16_t values;
	struct hwfs_value value[];
} __attribute__((packed));

#endif /*__HWFS_H__*/

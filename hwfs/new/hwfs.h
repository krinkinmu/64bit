#ifndef __HWFS_H__
#define __HWFS_H__

#include <stdint.h>
#include <string.h>

#include <endian.h>

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

#define HWFS_LOW_SENTINEL  0
#define HWFS_HIGH_SENTINEL 255

/* pretty much btrfs key */
struct hwfs_disk_key {
	uint64_t id;     // 0 for HWFS_EXTENT
	uint8_t type;
	uint64_t offset; // or hash for HWFS_ENTRY
} __attribute__((packed));

struct hwfs_key {
	uint64_t id;
	uint8_t type;
	uint64_t offset;
};

static inline void hwfs_key_to_host(struct hwfs_key *hkey,
			const struct hwfs_disk_key *dkey)
{
	hkey->id = le64toh(dkey->id);
	hkey->type = dkey->type;
	hkey->offset = le64toh(dkey->offset);
}

static inline void hwfs_key_to_disk(struct hwfs_disk_key *dkey,
			const struct hwfs_key *hkey)
{
	dkey->id = htole64(hkey->id);
	dkey->type = hkey->type;
	dkey->offset = htole64(hkey->offset);
}

/* b+ internal node is array of hwfs_items and hwfs_node_header */
struct hwfs_disk_item {
	struct hwfs_disk_key key;
	uint64_t blocknr;
} __attribute__((packed));

struct hwfs_item {
	struct hwfs_key key;
	uint64_t blocknr;
};

static inline void hwfs_item_to_host(struct hwfs_item *hitem,
			const struct hwfs_disk_item *ditem)
{
	hwfs_key_to_host(&hitem->key, &ditem->key);
	hitem->blocknr = le64toh(ditem->blocknr);
}

static inline void hwfs_item_to_disk(struct hwfs_disk_item *ditem,
			const struct hwfs_item *hitem)
{
	hwfs_key_to_disk(&ditem->key, &hitem->key);
	ditem->blocknr = htole64(hitem->blocknr);
}

/* b+ leaf is array of hwfs_values and hwfs_leaf_header */
struct hwfs_disk_value {
	struct hwfs_disk_key key;
	uint16_t offset;     // data offset in block
	uint16_t size;
} __attribute__((packed));

struct hwfs_value {
	struct hwfs_key key;
	uint16_t offset;
	uint16_t size;
};

static inline void hwfs_value_to_host(struct hwfs_value *hvalue,
			const struct hwfs_disk_value *dvalue)
{
	hwfs_key_to_host(&hvalue->key, &dvalue->key);
	hvalue->offset = le16toh(dvalue->offset);
	hvalue->size = le16toh(dvalue->size);
}

static inline void hwfs_value_to_disk(struct hwfs_disk_value *dvalue,
			const struct hwfs_value *hvalue)
{
	hwfs_key_to_disk(&dvalue->key, &hvalue->key);
	dvalue->offset = htole16(hvalue->offset);
	dvalue->size = htole16(hvalue->size);
}

struct hwfs_disk_inode {
	uint64_t size;
	uint32_t links;
	uint8_t type;
} __attribute__((packed));

struct hwfs_inode {
	uint64_t size;
	uint32_t links;
	uint8_t type;
};

static inline void hwfs_inode_to_host(struct hwfs_inode *hnode,
			const struct hwfs_disk_inode *dnode)
{
	hnode->size = le64toh(dnode->size);
	hnode->links = le32toh(dnode->links);
	hnode->type = dnode->type;
}

static inline void hwfs_inode_to_disk(struct hwfs_disk_inode *dnode,
			const struct hwfs_inode *hnode)
{
	dnode->size = htole64(hnode->size);
	dnode->links = htole32(hnode->links);
	dnode->type = hnode->type;
}

struct hwfs_disk_entry {
	uint64_t nodeid;          // child, not parent
	uint8_t type;             // child type
	char name[HWFS_MAX_PATH];
} __attribute__((packed));

struct hwfs_entry {
	uint64_t nodeid;
	uint8_t type;
	char name[HWFS_MAX_PATH];
};

static inline void hwfs_entry_to_host(struct hwfs_entry *hentry,
			const struct hwfs_disk_entry *dentry)
{
	hentry->nodeid = le64toh(dentry->nodeid);
	hentry->type = dentry->type;
	memcpy(hentry->name, dentry->name, sizeof(hentry->name));
}

static inline void hwfs_entry_to_disk(struct hwfs_disk_entry *dentry,
			const struct hwfs_entry *hentry)
{
	dentry->nodeid = htole64(hentry->nodeid);
	dentry->type = hentry->type;
	memcpy(dentry->name, hentry->name, sizeof(dentry->name));
}

struct hwfs_disk_data {
	uint64_t offset; // disk offset of file data extent
	uint64_t size;   // size of file data extent
} __attribute__((packed));

struct hwfs_data {
	uint64_t offset;
	uint64_t size;
};

static inline void hwfs_data_to_host(struct hwfs_data *hdata,
			const struct hwfs_disk_data *ddata)
{
	hdata->offset = le64toh(ddata->offset);
	hdata->size = le64toh(ddata->size);
}

static inline void hwfs_data_to_disk(struct hwfs_disk_data *ddata,
			const struct hwfs_data *hdata)
{
	ddata->offset = htole64(hdata->offset);
	ddata->size = htole64(hdata->size);
}

struct hwfs_disk_extent {
	uint64_t size;
	uint64_t free;
} __attribute__((packed));

struct hwfs_extent {
	uint64_t size;
	uint64_t free;
};

static inline void hwfs_extent_to_host(struct hwfs_extent *hext,
			const struct hwfs_disk_extent *dext)
{
	hext->size = le64toh(dext->size);
	hext->free = le64toh(dext->free);
}

static inline void hwfs_extent_to_disk(struct hwfs_disk_extent *dext,
			const struct hwfs_extent *hext)
{
	dext->size = htole64(hext->size);
	dext->free = htole64(hext->free);
}

struct hwfs_disk_super_block {
	uint64_t magic;
	uint64_t fs_tree_root;
	uint64_t extent_tree_root;
	uint64_t root_node_id;
	uint64_t next_node_id;
	uint16_t block_size;        // fanout
} __attribute__((packed));

struct hwfs_super_block {
	uint64_t magic;
	uint64_t fs_tree_root;
	uint64_t extent_tree_root;
	uint64_t root_node_id;
	uint64_t next_node_id;
	uint16_t block_size;
};

static inline void hwfs_super_to_host(struct hwfs_super_block *hsuper,
			const struct hwfs_disk_super_block *dsuper)
{
	hsuper->magic = le64toh(dsuper->magic);
	hsuper->fs_tree_root = le64toh(dsuper->fs_tree_root);
	hsuper->extent_tree_root = le64toh(dsuper->extent_tree_root);
	hsuper->root_node_id = le64toh(dsuper->root_node_id);
	hsuper->next_node_id = le64toh(dsuper->next_node_id);
	hsuper->block_size = le16toh(dsuper->block_size);
}

static inline void hwfs_super_to_disk(struct hwfs_disk_super_block *dsuper,
			const struct hwfs_super_block *hsuper)
{
	dsuper->magic = htole64(hsuper->magic);
	dsuper->fs_tree_root = htole64(hsuper->fs_tree_root);
	dsuper->extent_tree_root = htole64(hsuper->extent_tree_root);
	dsuper->root_node_id = htole64(hsuper->root_node_id);
	dsuper->next_node_id = htole64(hsuper->next_node_id);
	dsuper->block_size = htole16(hsuper->block_size);
}

struct hwfs_disk_tree_header {
	uint16_t level;
	uint16_t count;
	uint16_t blocks;
} __attribute__((packed));

struct hwfs_tree_header {
	uint16_t level;
	uint16_t count;
	uint16_t blocks;
};

static inline void hwfs_tree_to_host(struct hwfs_tree_header *hhdr,
			const struct hwfs_disk_tree_header *dhdr)
{
	hhdr->level = le16toh(dhdr->level);
	hhdr->count = le16toh(dhdr->count);
	hhdr->blocks = le16toh(dhdr->blocks);
}

static inline void hwfs_tree_to_disk(struct hwfs_disk_tree_header *dhdr,
			const struct hwfs_tree_header *hhdr)
{
	dhdr->level = htole16(hhdr->level);
	dhdr->count = htole16(hhdr->count);
	dhdr->blocks = htole16(hhdr->blocks);
}

struct hwfs_disk_node_header {
	struct hwfs_disk_tree_header hdr;
	struct hwfs_disk_item item[];
} __attribute__((packed));

struct hwfs_disk_leaf_header {
	struct hwfs_disk_tree_header hdr;
	struct hwfs_disk_value value[];
} __attribute__((packed));

#endif /*__HWFS_H__*/

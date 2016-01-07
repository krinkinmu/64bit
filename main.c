#include "kmem_cache.h"
#include "interrupt.h"
#include "threads.h"
#include "memory.h"
#include "paging.h"
#include "error.h"
#include "ramfs.h"
#include "time.h"
#include "vga.h"
#include "vfs.h"

#include "stdio.h"


#define RAMFS_ROOT "root"
#define RAMFS_FILE "file"
#define RAMFS_DIRN 10

static void test_readdir(struct fs_file *dir)
{
	struct dirent entries[RAMFS_DIRN];
	int rc = vfs_readdir(dir, entries, RAMFS_DIRN);

	if (rc >= 0) {
		vfs_debug("vfs_readdir returned %d entries", rc);
		for (int i = 0; i != rc; ++i)
			vfs_debug("entry: %s", entries[i].name);
	} else {
		vfs_debug("vfs_readdir failed with error: %s", errstr(rc));
	}
}

static void test_create(void)
{
	struct fs_file file;
	const char *file_path = "/" RAMFS_ROOT "/" RAMFS_FILE;
	int rc = vfs_create(file_path, &file);

	if (!rc) {
		vfs_debug("vfs_create(%s) succeeded", file_path);
		rc = vfs_release(&file);
		if (!rc)
			vfs_debug("vfs_release succeeded");
		else
			vfs_debug("vfs_release failed with error: %s",
				errstr(rc));
	} else {
		vfs_debug("vfs_create(%s) failed with error: %s",
			file_path, errstr(rc));
	}
}

static void test_open(void)
{
	struct fs_file file;
	const char *file_path = "/" RAMFS_ROOT "/" RAMFS_FILE;
	int rc = vfs_open(file_path, &file);

	if (!rc) {
		vfs_debug("vfs_open(%s) succeeded", file_path);
		rc = vfs_release(&file);
		if (!rc)
			vfs_debug("vfs_release succeeded");
		else
			vfs_debug("vfs_release failed with error: %s",
				errstr(rc));
	} else {
		vfs_debug("vfs_open(%s) failed with error: %s",
			file_path, errstr(rc));
	}
}

static void test_unlink(void)
{
	const char *file_path = "/" RAMFS_ROOT "/" RAMFS_FILE;
	int rc = vfs_unlink(file_path);

	if (!rc)
		vfs_debug("vfs_unlink(%s) succeeded", file_path);
	else
		vfs_debug("vfs_unlink(%s) failed with error: %s",
			file_path, errstr(rc));
}

static void test_root(void)
{
	struct fs_file root_dir;
	const char *root_path = "/" RAMFS_ROOT;
	int rc = vfs_open(root_path, &root_dir);

	if (!rc) {
		vfs_debug("vfs_open(%s) succeeded", root_path);
		test_readdir(&root_dir);
		rc = vfs_release(&root_dir);
		if (rc)
			vfs_debug("vfs_release failed with error: %s",
				errstr(rc));
		else
			vfs_debug("vfs_release succeeded");
	} else {
		vfs_debug("vfs_open(%s) failed with error: %s",
			root_path, errstr(rc));
	}
}

static void test_vfs_and_ramfs(void)
{
	test_create();
	test_open();
	test_root();
	test_unlink();
	test_root();
}

void main(void)
{
	setup_vga();
	setup_ints();
	setup_memory();
	setup_buddy();
	setup_paging();
	setup_alloc();
	setup_time();
	setup_threading();
	setup_vfs();
	setup_ramfs();

	int rc = vfs_mount("ramfs", RAMFS_ROOT, 0, 0);

	if (!rc) {
		vfs_debug("successfully mounted");
		test_vfs_and_ramfs();
		rc = vfs_umount(RAMFS_ROOT);
		if (rc)
			vfs_debug("vfs_umount failed with %d", rc);
		else
			vfs_debug("successfully unmounted");
	} else {
		vfs_debug("vfs_mount failed with %d", rc);
	}

	local_irq_enable();
	idle();
}

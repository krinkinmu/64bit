#include "stdio.h"
#include "error.h"
#include "vfs.h"

#define RAMFS_ROOT "ramfs_smoke_test_root"
#define RAMFS_FILE "file"
#define RAMFS_DIRN 10

#include <stdarg.h>

static void ramfs_dbg(const char *file, int line, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	printf("%s:%d ", file, line);
	vprintf(fmt, args);
	putchar('\n');
	va_end(args);
}

#define RAMFS_DBG(...) ramfs_dbg(__FILE__, __LINE__, __VA_ARGS__)

static void test_readdir(struct fs_file *dir)
{
	struct dirent entries[RAMFS_DIRN];
	int rc = vfs_readdir(dir, entries, RAMFS_DIRN);

	if (rc >= 0) {
		RAMFS_DBG("vfs_readdir returned %d entries", rc);
		for (int i = 0; i != rc; ++i)
			vfs_debug("entry: %s", entries[i].name);
	} else {
		RAMFS_DBG("vfs_readdir failed with error: %s", errstr(rc));
	}
}

static void test_create(void)
{
	struct fs_file file;
	const char *file_path = "/" RAMFS_ROOT "/" RAMFS_FILE;
	int rc = vfs_create(file_path, &file);

	if (!rc) {
		RAMFS_DBG("vfs_create(%s) succeeded", file_path);
		rc = vfs_release(&file);
		if (!rc)
			RAMFS_DBG("vfs_release succeeded");
		else
			RAMFS_DBG("vfs_release failed with error: %s",
				errstr(rc));
	} else {
		RAMFS_DBG("vfs_create(%s) failed with error: %s",
			file_path, errstr(rc));
	}
}

static void test_open(void)
{
	struct fs_file file;
	const char *file_path = "/" RAMFS_ROOT "/" RAMFS_FILE;
	int rc = vfs_open(file_path, &file);

	if (!rc) {
		RAMFS_DBG("vfs_open(%s) succeeded", file_path);
		rc = vfs_release(&file);
		if (!rc)
			RAMFS_DBG("vfs_release succeeded");
		else
			RAMFS_DBG("vfs_release failed with error: %s",
				errstr(rc));
	} else {
		RAMFS_DBG("vfs_open(%s) failed with error: %s",
			file_path, errstr(rc));
	}
}

static void test_unlink(void)
{
	const char *file_path = "/" RAMFS_ROOT "/" RAMFS_FILE;
	int rc = vfs_unlink(file_path);

	if (!rc)
		RAMFS_DBG("vfs_unlink(%s) succeeded", file_path);
	else
		RAMFS_DBG("vfs_unlink(%s) failed with error: %s",
			file_path, errstr(rc));
}

static void test_root(void)
{
	struct fs_file root_dir;
	const char *root_path = "/" RAMFS_ROOT;
	int rc = vfs_open(root_path, &root_dir);

	if (!rc) {
		RAMFS_DBG("vfs_open(%s) succeeded", root_path);
		test_readdir(&root_dir);
		rc = vfs_release(&root_dir);
		if (rc)
			RAMFS_DBG("vfs_release failed with error: %s",
				errstr(rc));
		else
			RAMFS_DBG("vfs_release succeeded");
	} else {
		RAMFS_DBG("vfs_open(%s) failed with error: %s",
			root_path, errstr(rc));
	}
}

static void ramfs_do_test(void)
{
	test_create();
	test_open();
	test_root();
	test_unlink();
	test_root();
}

void ramfs_test(void)
{
	int rc = vfs_mount("ramfs", RAMFS_ROOT, 0, 0);

	if (!rc) {
		RAMFS_DBG("successfully mounted");
		ramfs_do_test();
		rc = vfs_umount(RAMFS_ROOT);
		if (rc)
			RAMFS_DBG("vfs_umount failed with %d", rc);
		else
			RAMFS_DBG("successfully unmounted");
	} else {
		RAMFS_DBG("vfs_mount failed with %d", rc);
	}
}

#ifndef __KERNEL_CONFIG_H__
#define __KERNEL_CONFIG_H__

//#define CONFIG_QEMU_GDB_HANG      /* infinite loop after long mode enabled */
//#define CONFIG_VFS_DEBUG_PRINT    /* vfs_debug */

/**
 * This option regulates debug output verbosity:
 *   - 0 - info
 *   - 1 - warn
 *   - 2 - err
 * Default value is 1.
 */
#define CONFIG_MIN_DEBUG_LEVEL  0
#define CONFIG_RAMFS_TEST         /* run ramfs smoke test */

#endif /*__KERNEL_CONFIG_H__*/

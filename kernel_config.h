#ifndef __KERNEL_CONFIG_H__
#define __KERNEL_CONFIG_H__

//#define CONFIG_QEMU_GDB_HANG      /* infinite loop after long mode enabled */

/**
 * This option regulates debug output verbosity:
 *   - 0 - info
 *   - 1 - warn
 *   - 2 - err
 * Default value is 1.
 */
#define CONFIG_MIN_DEBUG_LEVEL  0
//#define CONFIG_RAMFS_TEST       /* run ramfs smoke test */
//#define CONFIG_IDE_TEST         /* run ide smoke test */
#define CONFIG_KERNEL_SIZE      3 * 512 * 1024 * 1024
#define CONFIG_KMAP_SIZE        512 * 1024 * 1024

#endif /*__KERNEL_CONFIG_H__*/

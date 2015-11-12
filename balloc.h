#ifndef __BOOT_ALLOCATOR_H__
#define __BOOT_ALLOCATOR_H__

void balloc_add_area(unsigned long long addr, unsigned long long size);
void balloc_print_areas(void);

#endif /*__BOOT_ALLOCATOR_H__*/

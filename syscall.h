#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#define MAX_SYSCALL_NR 10 // 10 is enough so far

#ifndef __ASM_FILE__

typedef int (*syscall_t)();

extern syscall_t syscall_table[];

#endif

#endif /*__SYSCALL_H__*/

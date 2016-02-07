#include "syscall.h"
#include "threads.h"
#include "stdio.h"
#include "exec.h"

#include <stddef.h>

static int write(const char *data, size_t size)
{
	while (size--)
		putchar(*data++);
	return 0;
}

syscall_t syscall_table[MAX_SYSCALL_NR] = {
	(syscall_t)write,
	(syscall_t)getpid,
	(syscall_t)exit,
	(syscall_t)wait,
	(syscall_t)fork,
	(syscall_t)exec
};

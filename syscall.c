#include "syscall.h"
#include "stdio.h"

static int first_syscall(void)
{
	DBG_INFO("It's my first syscall!!!");
	return 0;
}

syscall_t syscall_table[MAX_SYSCALL_NR] = {
	(syscall_t)first_syscall
};

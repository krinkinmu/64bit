#include <stddef.h>
#include <stdarg.h>

#include "vsnprintf.h"
#include "string.h"
#include "libsys.h"


static void write(const char *data, size_t size)
{ syscall(0, data, size); }

static long getpid(void)
{ return syscall(1); }

static void exit(void)
{ syscall(2); }

static long wait(long pid)
{ return syscall(3, pid); }

static long fork(void)
{ return syscall(4); }

static long exec(const char *cmd)
{ return syscall(5, cmd); }

static void printf_write(struct vsnprintf_sink *sink,
			const char *data, size_t size)
{
	(void) sink;

	write(data, size);
}

static void vprintf(const char *fmt, va_list args)
{
	struct vsnprintf_sink sink = { &printf_write };

	__vsnprintf(&sink, fmt, args);
}

static void printf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}


void main(void)
{
	printf("Userspace process pid %ld\n", getpid());

	const long pid = fork();

	if (pid == 0)
		printf("Child process %ld started\n", getpid());
	else if (pid > 0)
		printf("Forked child process %ld\n", pid);
	else
		printf("Fork failed with error %ld\n", pid);

	while (1);
}

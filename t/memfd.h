#ifndef T_MEMFILE_H
#define T_MEMFILE_H
/* LCOV_EXCL_START */

#include <unistd.h>
#include <sys/syscall.h>

static inline int memfd(const char *test)
{
	int fd = syscall(SYS_memfd_create, test, 0);
	if (fd < 0)
		BAIL_OUT("memfd_create system call failed.  oops.");
	return fd;
}

/* LCOV_EXCL_STOP */
#endif

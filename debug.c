#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *stddbg = NULL;

int
debugto(int fd)
{
	FILE *f;

	assert(fd >= 0);

	f = fdopen(fd, "w");
	if (f == NULL)
		return -1;

	if (stddbg)
		fclose(stddbg);

	stddbg = f;
	return 0;
}

void
debugf(const char *fmt, ...)
{
	va_list ap;

	if (!stddbg)
		return;

	assert(fmt != NULL);

	va_start(ap, fmt);
	vfprintf(stddbg, fmt, ap);
	va_end(ap);
}

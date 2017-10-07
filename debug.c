#include "bolo.h"
#include <stdarg.h>

static FILE *stddbg = NULL;

int
debugto(int fd)
{
	FILE *f;

	BUG(fd >= 0, "debugto() given an invalid file descriptor to send debug output to");

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

	BUG(fmt != NULL, "debugf() given a NULL format string to print");

	va_start(ap, fmt);
	vfprintf(stddbg, fmt, ap);
	va_end(ap);
}

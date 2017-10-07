#include "bolo.h"
#include <stdarg.h>
#include <time.h>

static FILE *OUT   = NULL;
static char *PRE   = NULL;
static int   LEVEL = LOG_ERRORS;

void startlog(const char *bin, pid_t pid, int level)
{
	ssize_t n;

	BUG(bin, "startlog() given a NULL program name");
	BUG(level >= LOG_ERRORS || level <= LOG_INFO, "startlog() given an out-of-range log level");

	if (!OUT)
		OUT = stdout;

	free(PRE);
	if (pid > 0) n = asprintf(&PRE, "%s[%d]", bin, pid);
	else         n = asprintf(&PRE, "%s",     bin);
	if (n < (ssize_t)strlen(bin))
		bail("failed to allocate memory in startlog()");

	LEVEL = level;
}

void logto(int fd)
{
	FILE *out;

	BUG(fd >= 0, "logto() given an invalid file descriptor to log to");

	out = fdopen(fd, "w");
	if (!out)
		bail("failed to reopen log output fd");

	OUT = out;
}

static void
_vlogf(const char *facility, const char *fmt, va_list args)
{
	struct tm tm;
	time_t t;
	char date[256];

	if (!time(&t)) {
		strcpy(date, "(date/time unknown -- time() failed)");
		goto print;
	}

	if (!gmtime_r(&t, &tm)) {
		strcpy(date, "(date/time unknown -- gmtime_r() failed)");
		goto print;
	}

	/* date format is rfc1123 / rfc822 compliant
	     [Fri, 06 Aug 2010 16:45:17-0400] */
	if (strftime(date, 256, "[%a, %d %b %Y %H:%M:%S%z]", &tm) == 0) {
		strcpy(date, "(date/time unknown -- strftime failed");
		goto print;
	}

print:
	fprintf(OUT, "%s %s: %s ", date, PRE, facility);
	vfprintf(OUT, fmt, args);
	fprintf(OUT, "\n");
	fflush(OUT);
}

void errorf(const char *fmt, ...)
{
	va_list args;

	BUG(fmt != NULL, "errorf() given a NULL format string to print");
	BUG(OUT != NULL, "errorf() has nowhere to print output");

	va_start(args, fmt);
	_vlogf("ERROR", fmt, args);
	va_end(args);
}

void warningf(const char *fmt, ...)
{
	va_list args;

	BUG(fmt != NULL, "warningf() given a NULL format string to print");
	BUG(OUT != NULL, "warningf() has nowhere to print output");

	if (LEVEL < LOG_WARNINGS)
		return;

	va_start(args, fmt);
	_vlogf("WARN ", fmt, args);
	va_end(args);
}

void infof(const char *fmt, ...)
{
	va_list args;

	BUG(fmt != NULL, "infof() given a NULL format string to print");
	BUG(OUT != NULL, "infof() has nowhere to print output");

	if (LEVEL < LOG_INFO)
		return;

	va_start(args, fmt);
	_vlogf("INFO ", fmt, args);
	va_end(args);
}

#ifdef TEST
TESTS {
	subtest {
		int fd;
		char buf[8192], *line, *c;

		fd = memfd("log");
		logto(fd);
		startlog("test-driver", 12345, LOG_WARNINGS);
		infof("an informational message (number %d)", 42);
		warningf("oh noes, a %s (%x)!", "warning", 0xdecafbad);
		errorf("error %d (%s)", ENOENT, error(ENOENT));

		lseek(fd, 0, SEEK_SET);
		ok(read(fd, buf, 8192) > 0,
			"logging subsystem should have written some octets");

		line = buf;
		c = strchr(line, '\n');
		ok(c && *c, "should have found a newline in the output");
		*c++ = '\0';

		memset(line+1, 'X', 30);
		is_string(line,
			"[XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX] test-driver[12345]: WARN  oh noes, a warning (decafbad)!",
			"warnf() should have written a log message");

		line = c;
		c = strchr(line, '\n');
		ok(c && *c, "should have found a second newline in the output");
		*c++ = '\0';

		memset(line+1, 'X', 30);
		is_string(line,
			"[XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX] test-driver[12345]: ERROR error 2 (No such file or directory)",
			"errorf() should have written a log message");
	}
}
#endif

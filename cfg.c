#include "bolo.h"
#include <ctype.h>

int
configure(struct config *cfg, int fd)
{
	char buf[8192];
	char *k, *v, *eol, *ws;
	ssize_t n, used;

	used = 0;
	memset(buf, 0, 8192);
	for (;;) {
		for (;;) {
			eol = strchr(buf, '\n');
			if (!eol || !*eol) {
				if (used == 8192) {
					errorf("failed to read configuration: line too long (>8k)");
					return -1;
				}

				n = read(fd, buf+used, 8192-used);
				if (n == 0) break;
				if (n <  0) {
					errorf("failed to read configuration: %s (error %d)",
							strerror(errno), errno);
					return -1;
				}

				used += n;
				continue;
			}
			break;
		}

		if (used == 0)
			return 0;

		/* find start of the key */
		k = &buf[0];
		while (k != eol && isspace(*k))
			k++;

		/* find the end of the key */
		v = k;
		while (v != eol && !isspace(*v) && *v != '=')
			v++;

		if (*v == '=') { /* handle 'key=value' specifically */
			*v++ = '\0';
		} else {
			*v++ = '\0';
			/* find the '=' */
			while (v != eol && *v != '=')
				v++;
			if (*v != '=') {
				errorf("failed to read configuration: missing '=' for '%s' directive", k);
				return -1;
			}
			v++;
		}

		/* find the start of the value */
		while (v != eol && isspace(*v))
			v++;

		/* trim trailing line whitespace */
		for (ws = eol; isspace(*ws) && ws != v; *ws-- = '\0')
			;
		eol++;

		/* check the key against known keys */
		if (streq(k, "log_level")) {
			     if (streq(v, "error"))   cfg->log_level = LOG_ERRORS;
			else if (streq(v, "warning")) cfg->log_level = LOG_WARNINGS;
			else if (streq(v, "info"))    cfg->log_level = LOG_INFO;
			else {
				errorf("failed to read configuration: invalid `log_level' value '%s'", v);
				return -1;
			}

		} else {
			errorf("failed to read configuration fie: unrecognized configuration directive '%s'", k);
			return -1;
		}

		/* slide the buffer */
		n = (eol - buf);
		memmove(buf, buf+n, used-n);
		used -= n;
		buf[used] = 0;
	}
}

#ifdef TEST
#define put(fd,s) write((fd), (s), strlen(s))
#define test1(raw) do { \
	int fd; \
	struct config cfg; \
	memset(&cfg, 0, sizeof(cfg)); \
\
	fd = memfd("cfg"); \
	put(fd, raw "\n"); \
\
	startlog("test:cfg", 0, LOG_ERRORS); \
\
	lseek(fd, 0, SEEK_SET); \
	ok(configure(&cfg, fd) == 0, \
		"configure() should succeed for '%s'", (raw)); \
\
	is_unsigned(cfg.log_level, LOG_WARNINGS, \
		"properly parsed '%s\\n'", raw); \
\
	close(fd); \
} while (0)

TESTS {
	subtest { test1("log_level = warning");  }
	subtest { test1(" log_level = warning"); }
	subtest { test1("log_level=warning");    }
	subtest { test1("log_level =warning");   }
	subtest { test1("log_level= warning");   }
	subtest { test1("log_level = warning "); }
	subtest { test1(" log_level = warning "); }
}
#endif

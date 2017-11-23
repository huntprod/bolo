#include "bolo.h"
#include <ctype.h>

int
configure(struct config *cfg, int fd)
{
	char buf[8192], *p;
	char *k, *v, *next, *eol;
	ssize_t n, used;

	used = 0;
	memset(buf, 0, 8192);
	for (;;) {
		for (;;) {
			eol = strchr(buf, '\n');
			if (eol && *eol) break;

			if (used == 8191) {
				errorf("failed to read configuration: line too long (>8k)");
				return -1;
			}

			n = read(fd, buf+used, 8191-used);
			if (n == 0) {
				if (!eol) {
					buf[used] = '\n';
					eol = buf + used;
				}
				break;
			}
			if (n <  0) {
				errorf("failed to read configuration: %s (error %d)",
						error(errno), errno);
				return -1;
			}

			used += n;
		}

		if (used == 0)
			return 0;

		next = eol;
		BUG(next != NULL,  "configure() ended up with a NULL eol marker somehow (this is curious)");
		BUG(*next == '\n', "configure() ended up with a non-newline eol marker somehow (this is curious)");

		/* treat comments as ending the line */
		eol = strchr(buf, '#');
		if (eol && *eol) *eol = '\n';
		else              eol = next;

		BUG(eol != NULL,  "configure() ended up with a NULL eol marker somehow (this is curious)");
		BUG(*eol == '\n', "configure() ended up with a non-newline eol marker somehow (this is curious)");

		/* find start of the key */
		k = &buf[0];
		while (k != eol && isspace(*k))
			k++;

		if (k == eol)
			goto next;

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
		while (eol != v && isspace(*eol))
			*eol-- = '\0';

		/* check the key against known keys */
		if (streq(k, "log_level")) {
			     if (streq(v, "error"))   cfg->log_level = LOG_ERRORS;
			else if (streq(v, "warning")) cfg->log_level = LOG_WARNINGS;
			else if (streq(v, "info"))    cfg->log_level = LOG_INFO;
			else {
				errorf("failed to read configuration: invalid `log_level' value '%s'", v);
				return -1;
			}

		} else if (streq(k, "secret_key")) {
			free(cfg->secret_key);
			cfg->secret_key = strdup(v);

		} else if (streq(k, "block_span")) {
			cfg->block_span = 0;
			for (p = v; *p; p++) {
				if (isdigit(*p)) {
					cfg->block_span = (cfg->block_span * 10) + (*p - '0');
				} else {
					while (*p && isspace(*p))
						p++;

					     if (streq(p, "ms")) cfg->block_span *= 1;
					else if (streq(p,  "s")) cfg->block_span *= 1000;
					else if (streq(p,  "m")) cfg->block_span *= 1000 * 60;
					else if (streq(p,  "h")) cfg->block_span *= 1000 * 60 * 60;
					else if (streq(p,  "d")) cfg->block_span *= 1000 * 60 * 60 * 24;
					else {
						errorf("failed to read configuration: invalid unit in block_span value '%s'", v);
						return -1;
					}
					break;
				}
			}

		} else {
			errorf("failed to read configuration: unrecognized configuration directive '%s'", k);
			return -1;
		}

next:
		/* slide the buffer */
		next++;
		n = (next - buf);
		memmove(buf, buf+n, used-n);
		used -= n;
		buf[used] = 0;
	}
}

void deconfigure(struct config *config)
{
	free(config->secret_key);
	config->secret_key = NULL;
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
	lseek(fd, 0, SEEK_SET); \
	ok(configure(&cfg, fd) == 0, \
		"configure() should succeed for '%s'", (raw)); \
\
	is_unsigned(cfg.log_level, LOG_WARNINGS, \
		"properly parsed '%s\\n'", raw); \
\
	close(fd); \
} while (0)
#define try(cfg,raw) do { \
	int fd; \
	memset(&(cfg), 0, sizeof(cfg)); \
\
	fd = memfd("cfg"); \
	put(fd, raw "\n"); \
	lseek(fd, 0, SEEK_SET); \
	ok(configure(&(cfg), fd) == 0, \
		"configure() should succeed for '%s'", (raw)); \
\
	close(fd); \
} while (0)

TESTS {
	//logto(5);
	startlog("test:cfg", 0, LOG_ERRORS); \

	subtest { test1("log_level = warning");  }
	subtest { test1(" log_level = warning"); }
	subtest { test1("log_level=warning");    }
	subtest { test1("log_level =warning");   }
	subtest { test1("log_level= warning");   }
	subtest { test1("log_level = warning "); }
	subtest { test1(" log_level = warning "); }

	subtest {
		int fd;
		struct config cfg;
		memset(&cfg, 0, sizeof(cfg));

		fd = memfd("cfg");
		put(fd, "# bolo configuration\n");
		put(fd, "log_level = warning      # not in prod...\n");
		put(fd, "#log_level = info\n");

		lseek(fd, 0, SEEK_SET);
		ok(configure(&cfg, fd) == 0,
			"configure() should succeed for comment test");

		is_unsigned(cfg.log_level, LOG_WARNINGS,
			"configure() should ignore the comments");

		deconfigure(&cfg);
		close(fd);
	}

	subtest {
		struct config cfg;

		try(cfg, "log_level = error");
		is_unsigned(cfg.log_level, LOG_ERRORS, "log_level error is parsed properly");

		try(cfg, "log_level = warning");
		is_unsigned(cfg.log_level, LOG_WARNINGS, "log_level warning is parsed properly");

		try(cfg, "log_level = info");
		is_unsigned(cfg.log_level, LOG_INFO, "log_level info is parsed properly");

		deconfigure(&cfg);
	}

	subtest {
		struct config cfg;

		try(cfg, "block_span = 1");
		ok(cfg.block_span == 1, "without units, block_span is treated as milliseconds");

		try(cfg, "block_span = 12345");
		ok(cfg.block_span == 12345, "block_span handles multiple digits");

		try(cfg, "block_span = 10ms");
		is_unsigned(cfg.block_span, 10, "unit ms == milliseconds (no multiplier)");
		ok(cfg.block_span == 10, "unit ms == milliseconds (no multiplier)");

		try(cfg, "block_span = 15s");
		ok(cfg.block_span == 15 * 1000, "multi-digit, s-unit block_span is good");

		try(cfg, "block_span = 3m");
		ok(cfg.block_span == 3 * 60000, "unit m == minutes (x60000 multiplier)");

		try(cfg, "block_span = 12h");
		ok(cfg.block_span == 12 * 3600000, "unit h == hours (x3600000 multiplier)");

		try(cfg, "block_span = 90d");
		ok(cfg.block_span == 90lu * 86400000, "unit d == days (x86400000 multiplier)");

		try(cfg, "block_span = 7 s");
		ok(cfg.block_span == 7 * 1000, "whitespace between number and digit is ignored");

		deconfigure(&cfg);
	}

	subtest {
		struct config cfg;

		try(cfg, "secret_key = /path/to/key # should be chmod 0600");
		is_string(cfg.secret_key, "/path/to/key", "secret_key parsed properly");

		deconfigure(&cfg);
	}
}
#endif

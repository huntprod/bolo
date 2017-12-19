#include "bolo.h"
#include <ctype.h>

#ifndef DEFAULT_QUERY_LISTEN
#define DEFAULT_QUERY_LISTEN "*:2001"
#endif

#ifndef DEFAULT_QUERY_MAX_CONNECTIONS
#define DEFAULT_QUERY_MAX_CONNECTIONS 256
#endif

#ifndef DEFAULT_METRIC_LISTEN
#define DEFAULT_METRIC_LISTEN "*:2002"
#endif

#ifndef DEFAULT_METRIC_MAX_CONNECTIONS
#define DEFAULT_METRIC_MAX_CONNECTIONS 8192
#endif

#ifndef DEFAULT_DB_DATA_ROOT
#define DEFAULT_DB_DATA_ROOT "/var/lib/bolo/db"
#endif

int
configure_defaults(struct config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	cfg->log_level = LOG_ERRORS;

	cfg->block_span = 0; /* FIXME - need better default */

	cfg->db_data_root = strdup(DEFAULT_DB_DATA_ROOT);
	if (!cfg->db_data_root) return -1;

	cfg->query_listen = strdup(DEFAULT_QUERY_LISTEN);
	if (!cfg->query_listen) return -1;

	cfg->query_max_connections = DEFAULT_QUERY_MAX_CONNECTIONS;

	cfg->metric_listen = strdup(DEFAULT_METRIC_LISTEN);
	if (!cfg->metric_listen) return -1;

	cfg->metric_max_connections = DEFAULT_METRIC_MAX_CONNECTIONS;

	return 0;
}

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

		} else if (streq(k, "db.secret_key")) {
			free(cfg->secret_key);
			cfg->secret_key = strdup(v);

		} else if (streq(k, "db.block_span")) {
			cfg->block_span = 0;
			for (p = v; *p; p++) {
				if (*p == '_' || *p == ',') continue; /* legibility */
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

		} else if (streq(k, "db.data_root")) {
			free(cfg->db_data_root);
			cfg->db_data_root = strdup(v);

		} else if (streq(k, "query.listen")) {
			free(cfg->query_listen);
			cfg->query_listen = strdup(v);

		} else if (streq(k, "query.max_connections")) {
			cfg->query_max_connections = 0;
			for (p = v; *p; p++) {
				if (*p == '_' || *p == ',') continue; /* legibility */
				if (!isdigit(*p)) {
					errorf("failed to read configuration: query.max_connections value '%s' is not a positive number", v);
					return -1;
				}
				cfg->query_max_connections = (cfg->query_max_connections * 10) + (*p - '0');
			}
			if (cfg->query_max_connections < 8) {
				errorf("failed to read configuration: query.max_connections value '%s' must be at least 8", v);
				return -1;
			}

		} else if (streq(k, "metric.listen")) {
			free(cfg->metric_listen);
			cfg->metric_listen = strdup(v);

		} else if (streq(k, "metric.max_connections")) {
			cfg->metric_max_connections = 0;
			for (p = v; *p; p++) {
				if (*p == '_' || *p == ',') continue; /* legibility */
				if (!isdigit(*p)) {
					errorf("failed to read configuration: metric.max_connections value '%s' is not a positive number", v);
					return -1;
				}
				cfg->metric_max_connections = (cfg->metric_max_connections * 10) + (*p - '0');
			}
			if (cfg->metric_max_connections < 8) {
				errorf("failed to read configuration: metric.max_connections value '%s' must be at least 8", v);
				return -1;
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

	free(config->query_listen);
	config->query_listen = NULL;

	free(config->metric_listen);
	config->metric_listen = NULL;
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
		deconfigure(&cfg);

		try(cfg, "log_level = warning");
		is_unsigned(cfg.log_level, LOG_WARNINGS, "log_level warning is parsed properly");
		deconfigure(&cfg);

		try(cfg, "log_level = info");
		is_unsigned(cfg.log_level, LOG_INFO, "log_level info is parsed properly");
		deconfigure(&cfg);
	}

	subtest {
		struct config cfg;

		try(cfg, "db.block_span = 1");
		ok(cfg.block_span == 1, "without units, block_span is treated as milliseconds");
		deconfigure(&cfg);

		try(cfg, "db.block_span = 12345");
		ok(cfg.block_span == 12345, "block_span handles multiple digits");
		deconfigure(&cfg);

		try(cfg, "db.block_span = 12_345");
		ok(cfg.block_span == 12345, "block_span allows underscore for readability");
		deconfigure(&cfg);

		try(cfg, "db.block_span = 12,345");
		ok(cfg.block_span == 12345, "block_span allows underscore for readability");
		deconfigure(&cfg);

		try(cfg, "db.block_span = 10ms");
		is_unsigned(cfg.block_span, 10, "unit ms == milliseconds (no multiplier)");
		ok(cfg.block_span == 10, "unit ms == milliseconds (no multiplier)");
		deconfigure(&cfg);

		try(cfg, "db.block_span = 15s");
		ok(cfg.block_span == 15 * 1000, "multi-digit, s-unit block_span is good");
		deconfigure(&cfg);

		try(cfg, "db.block_span = 3m");
		ok(cfg.block_span == 3 * 60000, "unit m == minutes (x60000 multiplier)");
		deconfigure(&cfg);

		try(cfg, "db.block_span = 12h");
		ok(cfg.block_span == 12 * 3600000, "unit h == hours (x3600000 multiplier)");
		deconfigure(&cfg);

		try(cfg, "db.block_span = 90d");
		ok(cfg.block_span == 90lu * 86400000, "unit d == days (x86400000 multiplier)");
		deconfigure(&cfg);

		try(cfg, "db.block_span = 7 s");
		ok(cfg.block_span == 7 * 1000, "whitespace between number and digit is ignored");
		deconfigure(&cfg);
	}

	subtest {
		struct config cfg;

		try(cfg, "db.secret_key = /path/to/key # should be chmod 0600");
		is_string(cfg.secret_key, "/path/to/key", "secret_key parsed properly");
		deconfigure(&cfg);
	}

	subtest {
		struct config cfg;

		try(cfg, "db.data_root = /path/to/db");
		is_string(cfg.db_data_root, "/path/to/db", "db.data_root recognized");
		deconfigure(&cfg);
	}

	subtest {
		struct config cfg;

		try(cfg, "query.listen = *:2199");
		is_string(cfg.query_listen, "*:2199", "query.listen should allow full wildcards");
		deconfigure(&cfg);

		try(cfg, "query.listen = 127.0.0.1:2199");
		is_string(cfg.query_listen, "127.0.0.1:2199", "query.listen should allow IPv4 addresses");
		deconfigure(&cfg);

		try(cfg, "query.listen = 0.0.0.0:2199");
		is_string(cfg.query_listen, "0.0.0.0:2199", "query.listen should allow IPv4 wildcard");
		deconfigure(&cfg);

		try(cfg, "query.listen = [::1]:2199");
		is_string(cfg.query_listen, "[::1]:2199", "query.listen should allow IPv6 loopback address");
		deconfigure(&cfg);

		try(cfg, "query.listen = [fe80::a00:27ff:feb2:ad10]:2199");
		is_string(cfg.query_listen, "[fe80::a00:27ff:feb2:ad10]:2199", "query.listen should allow IPv6 addresses");
		deconfigure(&cfg);

		try(cfg, "query.listen = [::]:2199");
		is_string(cfg.query_listen, "[::]:2199", "query.listen should allow IPv6 wildcard");
		deconfigure(&cfg);
	}

	subtest {
		struct config cfg;

		try(cfg, "query.max_connections = 200");
		is_unsigned(cfg.query_max_connections, 200, "query.max_connections accepts positive, non-zero integer >= 8");
		deconfigure(&cfg);

		try(cfg, "query.max_connections = 1_024");
		is_unsigned(cfg.query_max_connections, 1024, "query.max_connections allows embedded underscores for legibility");
		deconfigure(&cfg);

		try(cfg, "query.max_connections = 2,048");
		is_unsigned(cfg.query_max_connections, 2048, "query.max_connections allows commas for legibility");
		deconfigure(&cfg);
	}

	subtest {
		struct config cfg;

		try(cfg, "metric.listen = *:2199");
		is_string(cfg.metric_listen, "*:2199", "metric.listen should allow full wildcards");
		deconfigure(&cfg);

		try(cfg, "metric.listen = 127.0.0.1:2199");
		is_string(cfg.metric_listen, "127.0.0.1:2199", "metric.listen should allow IPv4 addresses");
		deconfigure(&cfg);

		try(cfg, "metric.listen = 0.0.0.0:2199");
		is_string(cfg.metric_listen, "0.0.0.0:2199", "metric.listen should allow IPv4 wildcard");
		deconfigure(&cfg);

		try(cfg, "metric.listen = [::1]:2199");
		is_string(cfg.metric_listen, "[::1]:2199", "metric.listen should allow IPv6 loopback address");
		deconfigure(&cfg);

		try(cfg, "metric.listen = [fe80::a00:27ff:feb2:ad10]:2199");
		is_string(cfg.metric_listen, "[fe80::a00:27ff:feb2:ad10]:2199", "metric.listen should allow IPv6 addresses");
		deconfigure(&cfg);

		try(cfg, "metric.listen = [::]:2199");
		is_string(cfg.metric_listen, "[::]:2199", "metric.listen should allow IPv6 wildcard");
		deconfigure(&cfg);
	}

	subtest {
		struct config cfg;

		try(cfg, "metric.max_connections = 200");
		is_unsigned(cfg.metric_max_connections, 200, "metric.max_connections accepts positive, non-zero integer >= 8");
		deconfigure(&cfg);

		try(cfg, "metric.max_connections = 1_024");
		is_unsigned(cfg.metric_max_connections, 1024, "metric.max_connections allows embedded underscores for legibility");
		deconfigure(&cfg);

		try(cfg, "metric.max_connections = 2,048");
		is_unsigned(cfg.metric_max_connections, 2048, "metric.max_connections allows commas for legibility");
		deconfigure(&cfg);
	}
}
#endif

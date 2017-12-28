#include "bolo.h"
#include <ctype.h>

#ifndef DEFAULT_CORE_LOG_LEVEL
#define DEFAULT_CORE_LOG_LEVEL LOG_ERRORS
#endif

#ifndef DEFAULT_CORE_QUERY_LISTEN
#define DEFAULT_CORE_QUERY_LISTEN "*:2001"
#endif

#ifndef DEFAULT_CORE_QUERY_MAX_CONNECTIONS
#define DEFAULT_CORE_QUERY_MAX_CONNECTIONS 256
#endif

#ifndef DEFAULT_CORE_METRIC_LISTEN
#define DEFAULT_CORE_METRIC_LISTEN "*:2002"
#endif

#ifndef DEFAULT_CORE_METRIC_MAX_CONNECTIONS
#define DEFAULT_CORE_METRIC_MAX_CONNECTIONS 8192
#endif

#ifndef DEFAULT_CORE_DB_SECRET_KEY
#define DEFAULT_CORE_DB_SECRET_KEY "/var/lib/bolo/key"
#endif

#ifndef DEFAULT_CORE_DB_DATA_ROOT
#define DEFAULT_CORE_DB_DATA_ROOT "/var/lib/bolo/db"
#endif

#ifndef DEFAULT_AGENT_SCHEDULE_SPLAY
#define DEFAULT_AGENT_SCHEDULE_SPLAY 30
#endif

#ifndef DEFAULT_AGENT_MAX_RUNNERS
#define DEFAULT_AGENT_MAX_RUNNERS 0
#endif

static int
s_configure_core(struct core_config *cfg, int fd)
{
	char buf[8192], *p;
	char *k, *v, *next, *eol;
	ssize_t n, used;

	memset(cfg, 0, sizeof(*cfg));
	cfg->log_level = DEFAULT_CORE_LOG_LEVEL;
	cfg->query_max_connections = DEFAULT_CORE_QUERY_MAX_CONNECTIONS;
	cfg->metric_max_connections = DEFAULT_CORE_METRIC_MAX_CONNECTIONS;

	cfg->db_data_root = strdup(DEFAULT_CORE_DB_DATA_ROOT);
	if (!cfg->db_data_root) return -1;

	cfg->db_secret_key = strdup(DEFAULT_CORE_DB_SECRET_KEY);
	if (!cfg->db_secret_key) return -1;

	cfg->query_listen = strdup(DEFAULT_CORE_QUERY_LISTEN);
	if (!cfg->query_listen) return -1;

	cfg->metric_listen = strdup(DEFAULT_CORE_METRIC_LISTEN);
	if (!cfg->metric_listen) return -1;

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

		/* ignore trailing whitespace by shifting eol */
		for (eol--; eol >= buf && isspace(*eol); eol--)
			;
		eol++;

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
			free(cfg->db_secret_key);
			cfg->db_secret_key = strdup(v);

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

static int
s_configure_agent(struct agent_config *cfg, int fd)
{
	char buf[8192], *p;
	char *k, *v, *next, *eol;
	char *env;
	ssize_t n, used;

	memset(cfg, 0, sizeof(*cfg));
	cfg->schedule_splay = DEFAULT_AGENT_SCHEDULE_SPLAY;
	cfg->max_runners = DEFAULT_AGENT_MAX_RUNNERS;
	cfg->env = hash_new();

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

		/* ignore trailing whitespace by shifting eol */
		for (eol--; eol >= buf && isspace(*eol); eol--)
			;
		eol++;

		/* find start of the key */
		k = &buf[0];
		while (k != eol && isspace(*k))
			;

		if (k == eol)
			goto next;

		/* might be an "@<time> <command with args>" line... */
		if (k[0] == '@') {
			unsigned int often = 0;
			struct agent_check *checks;

			k++;
			if (k == eol)
				goto next;
			for (p = v = k; *p; p++) {
				if (*p == '_' || *p == ',') continue; /* legibility */
				if (isdigit(*p)) {
					often = (often * 10) + (*p - '0');
				} else {
					k = p;
					while (*p && !isspace(*p))
						p++;
					*p++ = '\0';
					     if (streq(k, "ms")) often *= 1;
					else if (streq(k,  "s")) often *= 1000;
					else if (streq(k,  "m")) often *= 1000 * 60;
					else if (streq(k,  "h")) often *= 1000 * 60 * 60;
					else {
						errorf("failed to read configuration: invalid unit in command frequency '%s'", v);
						return -1;
					}
					break;
				}
			}
			while (*p && isspace(*p))
				;
			if (!*p) {
				errorf("failed to read configuration: syntax error in command scheduling");
				return -1;
			}

			checks = realloc(cfg->checks, (cfg->nchecks + 1) * sizeof(struct agent_check));
			if (!checks) {
				errorf("failed to read configuration: could not allocate memory");
				return -1;
			}
			*eol = '\0';
			checks[cfg->nchecks].interval = often;
			checks[cfg->nchecks].cmdline  = strdup(p);
			checks[cfg->nchecks].env      = cfg->env;
			cfg->checks = checks;
			cfg->nchecks++;

		} else {
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
			if (strncmp(k, "env.", 4) == 0) {
				if (hash_get(cfg->env, &env, k+4)  == 0) free(env);
				if (asprintf(&env, "%s=%s", k+4, v) < 0) return -1;
				if (hash_set(cfg->env, k+4, env)   != 0) return -1;

			} else if (streq(k, "bolo.endpoint")) {
				free(cfg->bolo_endpoint);
				cfg->bolo_endpoint = strdup(v);

			} else if (streq(k, "schedule.splay")) {
				cfg->schedule_splay = 0;
				for (p = v; *p; p++) {
					if (*p == '_' || *p == ',') continue; /* legibility */
					if (isdigit(*p)) {
						cfg->schedule_splay = (cfg->schedule_splay * 10) + (*p - '0');
					} else {
						while (*p && isspace(*p))
							p++;

						     if (streq(p, "ms")) cfg->schedule_splay *= 1;
						else if (streq(p,  "s")) cfg->schedule_splay *= 1000;
						else if (streq(p,  "m")) cfg->schedule_splay *= 1000 * 60;
						else if (streq(p,  "h")) cfg->schedule_splay *= 1000 * 60 * 60;
						else {
							errorf("failed to read configuration: invalid unit in schedule.splay value '%s'", v);
							return -1;
						}
						break;
					}
				}

			} else if (streq(k, "max.runners")) {
				cfg->max_runners = 0;
				for (p = v; *p; p++) {
					if (*p == '_' || *p == ',') continue; /* legibility */
					if (isdigit(*p)) {
						cfg->max_runners = (cfg->max_runners * 10) + (*p - '0');
					} else {
						errorf("failed to read configuration: invalid max.runners value '%s'", v);
						return -1;
					}
				}

			} else {
				errorf("failed to read configuration: unrecognized configuration directive '%s'", k);
				return -1;
			}
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

int
configure(int type, void *cfg, int fd)
{
	switch (type) {
	default:
		return -1;
	case CORE_CONFIG:  return s_configure_core((struct core_config *)cfg, fd);
	case AGENT_CONFIG: return s_configure_agent((struct agent_config *)cfg, fd);
	}
}

static void
s_deconfigure_core(struct core_config *config)
{
	free(config->db_data_root);
	config->db_data_root = NULL;

	free(config->db_secret_key);
	config->db_secret_key = NULL;

	free(config->query_listen);
	config->query_listen = NULL;

	free(config->metric_listen);
	config->metric_listen = NULL;
}

static void
s_deconfigure_agent(struct agent_config *config)
{
	char *k, *v;

	if (config->env) {
		hash_each(config->env, &k, &v)
			free(v);
		hash_free(config->env);
		config->env = NULL;
	}
}

void
deconfigure(int type, void *cfg)
{
	switch (type) {
	case CORE_CONFIG:  s_deconfigure_core((struct core_config *)cfg); break;
	case AGENT_CONFIG: s_deconfigure_agent((struct agent_config *)cfg); break;
	}
}

#ifdef TEST
#define put(fd,s) write((fd), (s), strlen(s))
#define test_whitespace(raw) do { \
	int fd; \
	struct core_config cfg; \
	memset(&cfg, 0, sizeof(cfg)); \
\
	fd = memfd("cfg"); \
	put(fd, raw "\n"); \
\
	lseek(fd, 0, SEEK_SET); \
	ok(configure(CORE_CONFIG, &cfg, fd) == 0, \
		"configure() should succeed for '%s'", (raw)); \
\
	is_unsigned(cfg.log_level, LOG_WARNINGS, \
		"properly parsed '%s\\n'", raw); \
\
	deconfigure(CORE_CONFIG, &cfg); \
\
	close(fd); \
} while (0)
#define try(type, cfg,raw) do { \
	int fd; \
	memset(&(cfg), 0, sizeof(cfg)); \
\
	fd = memfd("cfg"); \
	put(fd, raw "\n"); \
	lseek(fd, 0, SEEK_SET); \
	ok(configure((type), &(cfg), fd) == 0, \
		"configure() should succeed for '%s'", (raw)); \
\
	close(fd); \
} while (0)

TESTS {
	//logto(5);
	startlog("test:cfg", 0, LOG_ERRORS); \

	subtest { test_whitespace("log_level = warning");  }
	subtest { test_whitespace(" log_level = warning"); }
	subtest { test_whitespace("log_level=warning");    }
	subtest { test_whitespace("log_level =warning");   }
	subtest { test_whitespace("log_level= warning");   }
	subtest { test_whitespace("log_level = warning "); }
	subtest { test_whitespace(" log_level = warning "); }

	subtest {
		int fd;
		struct core_config cfg;
		memset(&cfg, 0, sizeof(cfg));

		fd = memfd("cfg");
		put(fd, "# bolo configuration\n");
		put(fd, "log_level = warning      # not in prod...\n");
		put(fd, "#log_level = info\n");

		lseek(fd, 0, SEEK_SET);
		ok(configure(CORE_CONFIG, &cfg, fd) == 0,
			"configure() should succeed for comment test");

		is_unsigned(cfg.log_level, LOG_WARNINGS,
			"configure() should ignore the comments");

		deconfigure(CORE_CONFIG, &cfg);
		close(fd);
	}

	subtest {
		struct core_config cfg;

		try(CORE_CONFIG, cfg, "# defaults only");
#define default_ok(s,t,m,v) is_ ## t (m, (v), s " defaults properly")
		default_ok("log_level",              unsigned, cfg.log_level,              DEFAULT_CORE_LOG_LEVEL);
		default_ok("db.secret_key",          string,   cfg.db_secret_key,          DEFAULT_CORE_DB_SECRET_KEY);
		default_ok("db.data_root",           string,   cfg.db_data_root,           DEFAULT_CORE_DB_DATA_ROOT);
		default_ok("query.listen",           string,   cfg.query_listen,           DEFAULT_CORE_QUERY_LISTEN);
		default_ok("query.max_connections",  unsigned, cfg.query_max_connections,  DEFAULT_CORE_QUERY_MAX_CONNECTIONS);
		default_ok("metric.listen",          string,   cfg.metric_listen,          DEFAULT_CORE_METRIC_LISTEN);
		default_ok("metric.max_connections", unsigned, cfg.metric_max_connections, DEFAULT_CORE_METRIC_MAX_CONNECTIONS);
#undef default_ok
		deconfigure(CORE_CONFIG, &cfg);
	}

	subtest {
		struct core_config cfg;

		try(CORE_CONFIG, cfg, "log_level = error");
		is_unsigned(cfg.log_level, LOG_ERRORS, "log_level error is parsed properly");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "log_level = warning");
		is_unsigned(cfg.log_level, LOG_WARNINGS, "log_level warning is parsed properly");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "log_level = info");
		is_unsigned(cfg.log_level, LOG_INFO, "log_level info is parsed properly");
		deconfigure(CORE_CONFIG, &cfg);
	}

	subtest {
		struct core_config cfg;

		try(CORE_CONFIG, cfg, "db.secret_key = /path/to/key # should be chmod 0600");
		is_string(cfg.db_secret_key, "/path/to/key", "secret_key parsed properly");
		deconfigure(CORE_CONFIG, &cfg);
	}

	subtest {
		struct core_config cfg;

		try(CORE_CONFIG, cfg, "db.data_root = /path/to/db");
		is_string(cfg.db_data_root, "/path/to/db", "db.data_root recognized");
		deconfigure(CORE_CONFIG, &cfg);
	}

	subtest {
		struct core_config cfg;

		try(CORE_CONFIG, cfg, "query.listen = *:2199");
		is_string(cfg.query_listen, "*:2199", "query.listen should allow full wildcards");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "query.listen = 127.0.0.1:2199");
		is_string(cfg.query_listen, "127.0.0.1:2199", "query.listen should allow IPv4 addresses");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "query.listen = 0.0.0.0:2199");
		is_string(cfg.query_listen, "0.0.0.0:2199", "query.listen should allow IPv4 wildcard");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "query.listen = [::1]:2199");
		is_string(cfg.query_listen, "[::1]:2199", "query.listen should allow IPv6 loopback address");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "query.listen = [fe80::a00:27ff:feb2:ad10]:2199");
		is_string(cfg.query_listen, "[fe80::a00:27ff:feb2:ad10]:2199", "query.listen should allow IPv6 addresses");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "query.listen = [::]:2199");
		is_string(cfg.query_listen, "[::]:2199", "query.listen should allow IPv6 wildcard");
		deconfigure(CORE_CONFIG, &cfg);
	}

	subtest {
		struct core_config cfg;

		try(CORE_CONFIG, cfg, "query.max_connections = 200");
		is_unsigned(cfg.query_max_connections, 200, "query.max_connections accepts positive, non-zero integer >= 8");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "query.max_connections = 1_024");
		is_unsigned(cfg.query_max_connections, 1024, "query.max_connections allows embedded underscores for legibility");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "query.max_connections = 2,048");
		is_unsigned(cfg.query_max_connections, 2048, "query.max_connections allows commas for legibility");
		deconfigure(CORE_CONFIG, &cfg);
	}

	subtest {
		struct core_config cfg;

		try(CORE_CONFIG, cfg, "metric.listen = *:2199");
		is_string(cfg.metric_listen, "*:2199", "metric.listen should allow full wildcards");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "metric.listen = 127.0.0.1:2199");
		is_string(cfg.metric_listen, "127.0.0.1:2199", "metric.listen should allow IPv4 addresses");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "metric.listen = 0.0.0.0:2199");
		is_string(cfg.metric_listen, "0.0.0.0:2199", "metric.listen should allow IPv4 wildcard");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "metric.listen = [::1]:2199");
		is_string(cfg.metric_listen, "[::1]:2199", "metric.listen should allow IPv6 loopback address");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "metric.listen = [fe80::a00:27ff:feb2:ad10]:2199");
		is_string(cfg.metric_listen, "[fe80::a00:27ff:feb2:ad10]:2199", "metric.listen should allow IPv6 addresses");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "metric.listen = [::]:2199");
		is_string(cfg.metric_listen, "[::]:2199", "metric.listen should allow IPv6 wildcard");
		deconfigure(CORE_CONFIG, &cfg);
	}

	subtest {
		struct core_config cfg;

		try(CORE_CONFIG, cfg, "metric.max_connections = 200");
		is_unsigned(cfg.metric_max_connections, 200, "metric.max_connections accepts positive, non-zero integer >= 8");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "metric.max_connections = 1_024");
		is_unsigned(cfg.metric_max_connections, 1024, "metric.max_connections allows embedded underscores for legibility");
		deconfigure(CORE_CONFIG, &cfg);

		try(CORE_CONFIG, cfg, "metric.max_connections = 2,048");
		is_unsigned(cfg.metric_max_connections, 2048, "metric.max_connections allows commas for legibility");
		deconfigure(CORE_CONFIG, &cfg);
	}




	subtest {
		struct agent_config cfg;

		try(AGENT_CONFIG, cfg, "schedule.splay = 1");
		ok(cfg.schedule_splay == 1, "without units, schedule_splay is treated as milliseconds");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 12345");
		ok(cfg.schedule_splay == 12345, "schedule_splay handles multiple digits");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 12_345");
		ok(cfg.schedule_splay == 12345, "schedule_splay allows undersagent for readability");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 12,345");
		ok(cfg.schedule_splay == 12345, "schedule_splay allows undersagent for readability");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 10ms");
		is_unsigned(cfg.schedule_splay, 10, "unit ms == milliseconds (no multiplier)");
		ok(cfg.schedule_splay == 10, "unit ms == milliseconds (no multiplier)");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 15s");
		ok(cfg.schedule_splay == 15 * 1000, "multi-digit, s-unit schedule_splay is good");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 3m");
		ok(cfg.schedule_splay == 3 * 60000, "unit m == minutes (x60000 multiplier)");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 12h");
		ok(cfg.schedule_splay == 12 * 3600000, "unit h == hours (x3600000 multiplier)");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 7 s");
		ok(cfg.schedule_splay == 7 * 1000, "whitespace between number and digit is ignored");
		deconfigure(AGENT_CONFIG, &cfg);
	}



	subtest {
		struct agent_config cfg;

		try(AGENT_CONFIG, cfg, "@15s linux nonet");
		is_unsigned(cfg.nchecks, 1, "agent should have parsed one check");

		is_unsigned(cfg.checks[0].interval, 15 * 1000, "first check should be every 15s");
		is_string(cfg.checks[0].cmdline, "linux nonet", "first check command line should be verbatim");
		deconfigure(AGENT_CONFIG, &cfg);
	}

	subtest {
		struct agent_config cfg;

		try(AGENT_CONFIG, cfg, "@15s linux '1q' \"2q\" /slashes/ \\back\\");
		is_unsigned(cfg.nchecks, 1, "agent should have parsed one check");

		is_string(cfg.checks[0].cmdline, "linux '1q' \"2q\" /slashes/ \\back\\", "no shell interpretation occurs");
		deconfigure(AGENT_CONFIG, &cfg);
	}

	subtest {
		struct agent_config cfg;

		try(AGENT_CONFIG, cfg, "@15s linux nonet # interesting\n@25s linux net");
		is_unsigned(cfg.nchecks, 2, "agent should have parsed two checks");

		is_unsigned(cfg.checks[0].interval, 15 * 1000, "first check should be every 15s");
		is_string(cfg.checks[0].cmdline, "linux nonet", "first check command line should be verbatim");
		is_unsigned(cfg.checks[1].interval, 25 * 1000, "second check should be every 25s");
		is_string(cfg.checks[1].cmdline, "linux net", "second check command line should be verbatim");
		deconfigure(AGENT_CONFIG, &cfg);
	}

	subtest {
		struct agent_config cfg;

		try(AGENT_CONFIG, cfg, "@22ms x\n@22s x\n@22m x\n@22h x");
		is_unsigned(cfg.nchecks, 4, "agent should have parsed four checks");

		is_unsigned(cfg.checks[0].interval, 22,                  "first check should be every 22ms");
		is_unsigned(cfg.checks[1].interval, 22 * 1000,           "second check should be every 22s");
		is_unsigned(cfg.checks[2].interval, 22 * 1000 * 60,      "third check should be every 22m");
		is_unsigned(cfg.checks[3].interval, 22 * 1000 * 60 * 60, "fourth check should be every 22h");
		deconfigure(AGENT_CONFIG, &cfg);
	}
}
#endif

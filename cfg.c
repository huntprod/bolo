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

#define T_INIT     0
#define T_KEY      1
#define T_EQUALS   2
#define T_VALUE    3
#define T_INTERVAL 4
#define T_NEWLINE  5
#define T_EOF      6

struct string {
	char   *data;
	size_t  len;
};


static int
s_parseint(struct string *s, int *v)
{
	char *p;

	*v = 0;
	for (p = s->data; p != s->data + s->len; p++) {
		if (*p == '_' || *p == ',') continue; /* legibility */
		if (!isdigit(*p)) return -1;
		*v = (*v * 10) + (*p - '0');
	}
	return 0;
}

static unsigned int
s_parsetime(struct string *s, int *v)
{
	char *p;
	int left;

	*v = 0;
	for (p = s->data; p != s->data + s->len; p++) {
		if (*p == '_' || *p == ',') continue; /* legibility */
		if (!isdigit(*p)) break;
		*v = (*v * 10) + (*p - '0');
	}

	left = s->len - (p - s->data);
	switch (left) {
	case 0: *v *= 1000; return 0; /* no units == s */
	case 1:
		switch (*p) {
		case 'h': *v *= 1000 * 60 * 60; return 0;
		case 'm': *v *= 1000 * 60;      return 0;
		case 's': *v *= 1000;           return 0;
		}
		break;

	case 2:
		if (*p == 'm' && *(p+1) == 's') return 0;
		break;
	}
	errorf("unrecognized time unit '%.*s' (must be one of 'h', 'm', 's' or 'ms')", left, p);
	return -1;
}


struct lexer {
	char   *src;
	size_t  len;
	size_t  dot;

	int token;
	struct {
		int           interval;
		struct string string;
	} value;

	int line;
	int column;
};

static int
s_lexer(struct lexer *l, int fd)
{
	size_t total;
	ssize_t nread;

	l->len = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	l->src = xmalloc(l->len + 1);
	total = 0;
	while (total != l->len) {
		nread = read(fd, l->src + total, l->len - total);
		if (nread <  0) return -1;
		if (nread == 0) return -1; /* FIXME set errno */
		total += nread;
	}
	l->src[total] = '\0';

	l->token = T_INIT;
	l->dot = 0;
	l->line = 1;
	l->column = 1;
	return 0;
}

static int
s_lex(struct lexer *lx)
{
	int i, l, c;

	i = lx->dot;
	l = lx->line;
	c = lx->column;
	#define THIS (lx->src[i])
	#define NEXT (lx->src[i+1])

	for (;;) {
		if (THIS == '\0') { /* EOF */
			lx->token = T_EOF;
			return 0;
		}
		if (THIS == '#') { /* comment to end of line */
			while (THIS != '\n') { i++; c++; }
			continue;
		}
		if (THIS == '\n') { /* collapse newlines */
			while (isspace(THIS)) {
				i++; c++;
				if (THIS == '\n') {
					l++;
					c = 1;
				}
			}
			lx->token = T_NEWLINE;
			goto accept;
		}
		if (isspace(THIS)) {
			i++; c++;
			continue;
		}

		if (THIS == '=') {
			i++; c++;
			lx->token = T_EQUALS;
			goto accept;
		}

		if (THIS == '@') { /* interval notation */
			i++; c++;
			lx->token = T_INTERVAL;

			lx->value.string.data = &THIS;
			lx->value.string.len  = i;
			while (!isspace(THIS)) { i++; c++; }
			lx->value.string.len  = i - lx->value.string.len;

			if (s_parsetime(&lx->value.string, &lx->value.interval) == 0)
				goto accept;
			goto fail;
		}

		switch (lx->token) {
		case T_EQUALS:
		case T_INTERVAL:
			/* read until end of line, return literal */
			lx->value.string.data = &THIS;
			lx->value.string.len = i;

			/* stop at newline; eat trailing whitespace */
			while (THIS != '\n' && THIS != '#') { i++; c++; } i--; c--;
			while (isspace(THIS))               { i--; c--; }
			if (!isspace(THIS && THIS != '#'))  { i++; c++; }

			lx->value.string.len = i - lx->value.string.len;
			lx->token = T_VALUE;
			goto accept;

		default:
			/* read until first whitespace character */
			lx->value.string.data = &THIS;
			lx->value.string.len = i;
			while (!isspace(THIS) && THIS != '=') { i++; c++; }
			lx->value.string.len = i - lx->value.string.len;
			lx->token = T_KEY;
			goto accept;
		}
	}

fail:
	return -1;

accept:
	lx->dot    = i;
	lx->line   = l;
	lx->column = c;
	return 0;
}

typedef int (*kv_fn)(void *u, struct string *key, struct string *value);
typedef int (*iv_fn)(void *u, int interval, struct string *value);

#define S_INIT     0
#define S_KEY      1
#define S_EQUALS   2
#define S_INTERVAL 3
static int
s_configure(int fd, void *u, kv_fn on_kv, iv_fn on_iv)
{
	struct lexer lx;
	struct string k;
	int rc, state;

	s_lexer(&lx, fd);
	state = S_INIT;
	while (s_lex(&lx) == 0) {
		switch (state) {
		default: rc = 3; goto done;
		case S_INIT:
			switch (lx.token) {
			default:        rc = 1; goto done;
			case T_EOF:     rc = 0; goto done;
			case T_NEWLINE: continue;
			case T_KEY:
				k = lx.value.string;
				state = S_KEY;
				continue;
			case T_INTERVAL:
				state = S_INTERVAL;
				continue;
			}
			rc = 2; goto done;

		case S_KEY:
			if (lx.token == T_EQUALS) {
				state = S_EQUALS;
				continue;
			}
			rc = 1; goto done;

		case S_EQUALS:
			if (lx.token == T_VALUE) {
				state = S_INIT;
				if (on_kv(u, &k, &lx.value.string) == 0)
					continue;
				rc = 4; goto done;
			}
			rc = 1; goto done;

		case S_INTERVAL:
			if (lx.token == T_VALUE) {
				state = S_INIT;
				if (on_iv(u, lx.value.interval, &lx.value.string) == 0)
					continue;
				rc = 5; goto done;
			}
			rc = 1; goto done;
		}
	}

done:
	free(lx.src);
	return rc;
}

static int
s_core_kv(void *u, struct string *k, struct string *v)
{
	struct core_config *cfg;

	cfg = (struct core_config *)u;

	if (strncmp("log_level", k->data, k->len) == 0) {
		     if (strncmp("error",   v->data, v->len) == 0) cfg->log_level = LOG_ERRORS;
		else if (strncmp("warning", v->data, v->len) == 0) cfg->log_level = LOG_WARNINGS;
		else if (strncmp("info",    v->data, v->len) == 0) cfg->log_level = LOG_INFO;
		else {
			errorf("failed to read configuration: invalid `log_level' value '%s'", v);
			return -1;
		}
		return 0;
	}

	if (strncmp("db.secret_key", k->data, k->len) == 0) {
		free(cfg->db_secret_key);
		cfg->db_secret_key = strndup(v->data, v->len);
		return 0;
	}

	if (strncmp("db.data_root", k->data, k->len) == 0) {
		free(cfg->db_data_root);
		cfg->db_data_root = strndup(v->data, v->len);
		return 0;
	}

	if (strncmp("query.listen", k->data, k->len) == 0) {
		free(cfg->query_listen);
		cfg->query_listen = strndup(v->data, v->len);
		return 0;
	}

	if (strncmp("query.max_connections", k->data, k->len) == 0) {
		if (s_parseint(v, &cfg->query_max_connections) != 0) {
			errorf("failed to read configuration: query.max_connections value '%.*s' is not a positive number", v->len, v->data);
			return -1;
		}
		if (cfg->query_max_connections < 8) {
			errorf("failed to read configuration: query.max_connections value '%.*s' must be at least 8", v->len, v->data);
			return -1;
		}
		return 0;
	}

	if (strncmp("metric.listen", k->data, k->len) == 0) {
		free(cfg->metric_listen);
		cfg->metric_listen = strndup(v->data, v->len);
		return 0;
	}

	if (strncmp("metric.max_connections", k->data, k->len) == 0) {
		if (s_parseint(v, &cfg->metric_max_connections) != 0) {
			errorf("failed to read configuration: metric.max_connections value '%.*s' is not a positive number", v->len, v->data);
			return -1;
		}
		return 0;
	}

	errorf("failed to read configuration: unrecognized configuration directive '%.*s'", k->len, k->data);
	return -1;
}

static int
s_core_iv(void *u, int iv, struct string *v)
{
	warningf("ignoring check definition in bolo core config...");
	warningf("(check definitions only make sense in the agent config.");
	return 0;
}

static int
s_configure_core(struct core_config *cfg, int fd)
{
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

	return s_configure(fd, cfg, s_core_kv, s_core_iv);
}

static int
s_agent_iv(void *u, int interval, struct string *v)
{
	struct agent_config *cfg;
	struct agent_check *checks;

	cfg = (struct agent_config *)u;
	checks = realloc(cfg->checks, (cfg->nchecks + 1) * sizeof(struct agent_check));
	if (!checks) {
		errorf("failed to read configuration: could not allocate memory");
		return -1;
	}
	checks[cfg->nchecks].interval = interval;
	checks[cfg->nchecks].cmdline  = strndup(v->data, v->len);
	checks[cfg->nchecks].env      = cfg->env;
	cfg->checks = checks;
	cfg->nchecks++;
	return 0;
}

static int
s_agent_kv(void *u, struct string *k, struct string *v)
{
	struct agent_config *cfg;
	char *s, *env;

	cfg = (struct agent_config *)u;

	if (k->len > 4 && strncmp("env.", k->data, 4) == 0) {
		s = strndup(k->data+4, k->len-4);
		if (hash_get(cfg->env, &env, s) == 0)
			free(env);

		if (asprintf(&env, "%s=%.*s", s, (int)v->len, v->data) < 0)
			return -1;

		if (hash_set(cfg->env, s, env) != 0)
			return -1;

		free(s);
		return 0;
	}

	if (strncmp("bolo.endpoint", k->data, k->len) == 0) {
		free(cfg->bolo_endpoint);
		cfg->bolo_endpoint = strndup(v->data, v->len);
		return 0;
	}

	if (strncmp("schedule.splay", k->data, k->len) == 0) {
		if (s_parsetime(v, &cfg->schedule_splay) != 0) {
			errorf("failed to read configuration: schedule.splay value '%.*s' is not a positive number", v->len, v->data);
			return -1;
		}
		if (cfg->schedule_splay < 100) {
			errorf("failed to read configuration: schedule.splay value '%.*s' must be at least 100ms", v->len, v->data);
			return -1;
		}
		return 0;
	}

	if (strncmp("max.runners", k->data, k->len) == 0) {
		if (s_parseint(v, &cfg->max_runners) != 0) {
			errorf("failed to read configuration: invalid max.runners value '%.*s'", v->len, v->data);
			return -1;
		}
		if (cfg->max_runners < 1) {
			errorf("failed to read configuration: max.runners value '%.*s' must be at least 1", v->len, v->data);
			return -1;
		}
		return 0;
	}

	errorf("failed to read configuration: unrecognized configuration directive '%.*s'", k->len, k->data);
	return -1;
}

static int
s_configure_agent(struct agent_config *cfg, int fd)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->schedule_splay = DEFAULT_AGENT_SCHEDULE_SPLAY;
	cfg->max_runners = DEFAULT_AGENT_MAX_RUNNERS;
	cfg->env = hash_new();

	return s_configure(fd, cfg, s_agent_kv, s_agent_iv);
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
	int i;

	if (config->env) {
		hash_each(config->env, &k, &v)
			free(v);
		hash_free(config->env);
		config->env = NULL;
	}

	for (i = 0; i < (int)config->nchecks; i++)
		free(config->checks[i].cmdline);
	free(config->checks);
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
		ok(cfg.schedule_splay == 1000, "without units, schedule_splay is treated as seconds");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 12345");
		ok(cfg.schedule_splay == 12345 * 1000, "schedule_splay handles multiple digits");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 12_345");
		ok(cfg.schedule_splay == 12345 * 1000, "schedule_splay allows undersagent for readability");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 12,345");
		ok(cfg.schedule_splay == 12345 * 1000, "schedule_splay allows undersagent for readability");
		deconfigure(AGENT_CONFIG, &cfg);

		try(AGENT_CONFIG, cfg, "schedule.splay = 210ms");
		is_unsigned(cfg.schedule_splay, 210, "unit ms == milliseconds (no multiplier)");
		ok(cfg.schedule_splay == 210, "unit ms == milliseconds (no multiplier)");
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

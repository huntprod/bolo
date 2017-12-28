#include "bolo.h"
#include <getopt.h>

#ifndef DEFAULT_CONFIG_FILE
#define DEFAULT_CONFIG_FILE "/etc/bolo-agent.conf"
#endif

#define RUNNER_BUFSIZ 8192

struct context {
	struct agent_config config;

	struct fdpoll *poll;

	int            nrunners;
	struct runner *runners;

	struct list    waitq;
	struct list    runq;

	struct {
		int    fd;           /* network socket to send data to bolo on */
		int    watched;      /* whether or not fdpoll is watching bolo.fd */
		char   sndbuf[8192]; /* buffered results to be sent */
		size_t outstanding;  /* how much of sndbuf still needs sent */
	} bolo;
};

struct runner {
	pid_t pid;                /* process ID of executing collector process */
	int outfd, errfd;         /* stdout / stderr file descriptors to read from */

	char buf[RUNNER_BUFSIZ];  /* result read buffer */
	int  nread;               /* how much of the buffer is populated? */

	struct agent_check *check;
	struct context     *ctx;
};

static int relay_handler(int, void*);
static int runner_handler(int, void*);
static int scheduler(int, void*);

static void
s_exec(struct runner *r)
{
	int pfd[2];

	if (pipe(pfd) != 0) {
		errnof("failed to pipe");
		return;
	}

	r->pid = fork();
	if (r->pid < 0) {
		errnof("failed to fork");
		close(pfd[0]);
		close(pfd[1]);
		return;
	}

	if (r->pid == 0) {
		int i;
		char *k, *v, **env;

		if (dup2(pfd[1], 1) < 0) {
			errnof("failed to set up stdout piping");
			exit(255);
		}
		close(0);

		env = xalloc(hash_nset(r->check->env) + 1, sizeof(char *));
		i = 0;
		hash_each(r->check->env, &k, &v)
			env[i++] = v;

		debugf("executing `%s` in pid %d", r->check->cmdline, getpid());
		execle("/bin/sh", "sh", "-c", r->check->cmdline, NULL, env);

		errnof("failed to exec");
		exit(255);
	}

	close(pfd[1]);
	r->outfd = pfd[0];
}

static int
runner_handler(int fd, void *_u)
{
	ssize_t nread;
	struct runner *r;
	char *eol;

	r = (struct runner *)_u;
	for (;;) {
		nread = read(fd, r->buf + r->nread, RUNNER_BUFSIZ - r->nread);
		if (nread == 0) {
			r->outfd = -1;
			r->pid = 0;
			delist(&r->check->q);
			debugf("eof on fd %d", fd);
			return -1;
		}

		if (nread < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
			errnof("failed to read from runner fd %d", fd);
			return -1;
		}

		r->nread += nread;
		while (r->nread > 2) {
			eol = strchr(r->buf, '\n');
			if (!eol) return 0;

			if ((sizeof(r->ctx->bolo.sndbuf) - r->ctx->bolo.outstanding) < (unsigned)(eol - r->buf))
				goto tryagain;

			*eol = '\0';
			if (r->buf[1] != ' ') {
				errorf("malformed collector output [%s]\n", r->buf);
				goto consume;
			}

			switch (r->buf[0]) {
			default:
				errorf("unrecognized metric type '%c' in collector output [%s]\n", r->buf[0], r->buf);
				goto consume;

			case 'S':
				printf("SAMPLE OUT [%s]\n", r->buf + 2);
				*eol++ = '\n';
				memcpy(r->ctx->bolo.sndbuf + r->ctx->bolo.outstanding,
					r->buf + 2, eol - r->buf - 2);
				r->ctx->bolo.outstanding += eol - r->buf - 2;
				if (!r->ctx->bolo.watched) {
					debugf("watching upstream relay fd (we have data to send!)");
					if (fdpoll_watch(r->ctx->poll, r->ctx->bolo.fd, FDPOLL_WRITE, relay_handler, r->ctx) != 0)
						bail("failed to re-watch upstream relay fd");
					r->ctx->bolo.watched = 1;
				} else {
					debugf("already watching upstream relay");
				}
				break;

			case 'R':
				printf("RATE TO KEEP [%s]\n", r->buf + 2);
				/* FIXME: this is so wrong; we aren't actually calculating rates */
				*eol++ = '\n';
				memcpy(r->ctx->bolo.sndbuf + r->ctx->bolo.outstanding,
					r->buf + 2, eol - r->buf - 2);
				r->ctx->bolo.outstanding += eol - r->buf - 2;
				if (!r->ctx->bolo.watched) {
					debugf("watching upstream relay fd (we have data to send!)");
					if (fdpoll_watch(r->ctx->poll, r->ctx->bolo.fd, FDPOLL_WRITE, relay_handler, r->ctx) != 0)
						bail("failed to re-watch upstream relay fd");
					r->ctx->bolo.watched = 1;
				}
				break;
			}
consume:
			if (eol >= r->buf + r->nread) {
				r->nread = 0;
			} else {
				memmove(r->buf, eol, r->nread - (eol - r->buf));
				r->nread -= eol - r->buf;
			}
			continue;

tryagain:
			infof("bolo send buffer is too full, trying again in a little bit.");
			break;
		}
	}

	return 0;
}

static int
scheduler(int ignored, void *_u)
{
	bolo_msec_t now;
	struct context *ctx;
	struct agent_check *check, *tmp_check;
	int i;

	debugf("scheduler firing...");
	ctx = (struct context *)_u;
	now = bolo_ms(NULL);
	for (i = 0; (unsigned)i < ctx->config.nchecks; i++) {
		if (ctx->config.checks[i].next_run > now) continue;
		if (!isempty(&ctx->config.checks[i].q))   continue;

		infof("scheduling check `%s` to run next...", ctx->config.checks[i].cmdline);
		push(&ctx->waitq, &ctx->config.checks[i].q);
		ctx->config.checks[i].next_run = now + ctx->config.checks[i].interval;
	}

	for_eachx(check, tmp_check,  &ctx->waitq, q) {
		for (i = 0; i < ctx->nrunners; i++) {
			if (ctx->runners[i].pid)
				continue;

			ctx->runners[i].check = check;
			s_exec(&ctx->runners[i]);

			debugf("executing `%s` on fd %d", check->cmdline, ctx->runners[i].outfd);
			if (fdpoll_watch(ctx->poll, ctx->runners[i].outfd, FDPOLL_READ, runner_handler, &ctx->runners[i]) != 0)
				return -1;

			delist(&check->q);
			push(&ctx->runq, &check->q);
			break;
		}
		if (i == ctx->nrunners)
			break;
	}

	return 0;
}

static int
relay_handler(int fd, void *_u)
{
	ssize_t nwrit;
	struct context *ctx;

	ctx = (struct context *)_u;

	debugf("relay: spinning up to flush send buffer");
	while (ctx->bolo.outstanding > 0) {
		debugf("relay: %d octets still to be sent to bolo core", ctx->bolo.outstanding);
		nwrit = write(fd, ctx->bolo.sndbuf, ctx->bolo.outstanding);
		debugf("relay: nwrit = %d", nwrit);
		if (nwrit == 0)
			return -1; /* FIXME: reconnect? */
		if (nwrit < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return -1; /* FIXME: recover? */
		}

		memmove(ctx->bolo.sndbuf, ctx->bolo.sndbuf + nwrit, ctx->bolo.outstanding - nwrit);
		ctx->bolo.outstanding -= nwrit;
		debugf("relay: %d octets left to be sent to bolo core", ctx->bolo.outstanding);
	}

	ctx->bolo.watched = 0;
	fdpoll_unwatch(ctx->poll, fd);
	return 0;
}

int
do_agent(int argc, char **argv)
{
	int i;
	struct context ctx;

	memset(&ctx, 0, sizeof(ctx));

	{
		int fd, override_log_level = -1;
		char *config_file = strdup(DEFAULT_CONFIG_FILE);
		char *override_endpoint = NULL;

		int idx = 0;
		char c, *shorts = "hDc:e:l:";
		struct option longs[] = {
			{"help",      no_argument,       0, 'h'},
			{"debug",     no_argument,       0, 'D'},
			{"config",    required_argument, 0, 'c'},
			{"endpoint",  required_argument, 0, 'e'},
			{"log-level", required_argument, 0, 'l'},
			{0, 0, 0, 0},
		};

		while ((c = getopt_long(argc, argv, shorts, longs, &idx)) >= 0) {
			switch (c) {
			case 'h':
				printf("USAGE: %s agent [--config /etc/bolo-agent.conf] [--debug] [--log-level error]\n\n", argv[0]);
				printf("OPTIONS\n\n");
				printf("  -h, --help                 Show this help screen.\n\n");
				printf("  -c, --config FILE          Path to a configuration file.\n"
				       "                             Defaults to " DEFAULT_CONFIG_FILE ".\n\n");

				printf("  -e, --endpoint HOST:PORT   Path to a configuration file.\n"
				       "                             This overrides the value set in the\n"
				       "                             configuration file.\n\n");

				printf("  -l, --log-level LEVEL      Log level.  This overrides the value\n"
				       "                             from the configuration file.\n"
				       "                             Must be one of ERROR, WARNING, or INFO.\n\n");

				printf("  -D, --debug                Enable debugging mode.\n"
				       "                             (mostly useful only to bolo devs).\n\n");
				return 0;

			case 'D':
				debugto(fileno(stderr));
				override_log_level = LOG_INFO;
				break;

			case 'c':
				free(config_file);
				config_file = strdup(optarg);
				break;

			case 'e':
				free(override_endpoint);
				override_endpoint = strdup(optarg);
				break;

			case 'l':
				     if (strcasecmp(optarg, "error")   == 0) override_log_level = LOG_ERRORS;
				else if (strcasecmp(optarg, "warning") == 0) override_log_level = LOG_WARNINGS;
				else if (strcasecmp(optarg, "info")    == 0) override_log_level = LOG_INFO;
				else fprintf(stderr, "invalid log-level '%s': ignoring...\n", optarg);
				break;
			}
		}

		debugf("reading configuration from %s", config_file);
		fd = open(config_file, O_RDONLY);
		if (fd < 0) {
			errnof("unable to open configuration file %s", config_file);
			return 1;
		}
		if (configure(AGENT_CONFIG, &ctx.config, fd) != 0)
			return 1;

		if (override_log_level != -1)
			ctx.config.log_level = override_log_level;
		if (override_endpoint) {
			free(ctx.config.bolo_endpoint);
			ctx.config.bolo_endpoint = override_endpoint;
		}

		debugf("endpoint at %s", ctx.config.bolo_endpoint);
		if (!ctx.config.bolo_endpoint) {
			fprintf(stderr, "no bolo endpoint is set, either via the %s configuration file,\n"
			                "or the --endpoint command-line option.\n",
			                config_file);
			exit(1);
		}
	}
		debugf("endpoint at %s", ctx.config.bolo_endpoint);

	ctx.nrunners = ctx.config.max_runners;
	if (ctx.nrunners == 0) ctx.nrunners = ctx.config.nchecks;
	ctx.runners = xalloc(ctx.nrunners, sizeof(struct runner));

	empty(&ctx.waitq);
	empty(&ctx.runq);
	for (i = 0; (unsigned)i < ctx.config.nchecks; i++) {
		empty(&ctx.config.checks[i].q);
		ctx.config.checks[i].next_run = 0; /* FIXME: splay */
	}
	for (i = 0; i < ctx.nrunners; i++)
		ctx.runners[i].ctx = &ctx;

	startlog(argv[0], getpid(), ctx.config.log_level);

	if (!(ctx.poll = fdpoller(64)))
		bail("unable to initialize fd poller");

	fdpoll_timeout(ctx.poll, 250, scheduler, &ctx);
	fdpoll_every(ctx.poll, scheduler, &ctx);

	debugf("connecting to bolo core at `%s`", ctx.config.bolo_endpoint);
	ctx.bolo.fd = net_connect(ctx.config.bolo_endpoint);
	if (ctx.bolo.fd < 0)
		bail("unable to connect to bolo core");
	ctx.bolo.watched = 0; /* don't start watching until we have data */

	fdpoll(ctx.poll);
	return 0;
}

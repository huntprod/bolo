#include "bolo.h"

#include <sys/types.h>
#include <sys/wait.h>

#include <getopt.h>

#ifndef DEFAULT_CONFIG_FILE
#define DEFAULT_CONFIG_FILE "/etc/bolo-agent.conf"
#endif

#define RUNNER_BUFSIZ 8192

struct sndbuf {
	struct list send;

	size_t len;
	size_t off;
	char   data[];
};

struct context {
	struct agent_config config;

	struct fdpoll *poll;

	int            nrunners;
	struct runner *runners;

	struct list    waitq;
	struct list    runq;

	struct {
		int fd;      /* network socket to send data to bolo on */
		int watched; /* whether or not fdpoll is watching bolo.fd */

		int bufsiz;       /* how much buffer memory have we used? */
		struct list bufs; /* buffers to send */
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
		r->pid = 0;
		delist(&r->check->q);
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

static void
s_connect(struct context *ctx, int isinit)
{
	debugf("%sconnecting to bolo core at `%s`", (isinit ? "" : "re"), ctx->config.bolo_endpoint);
	ctx->bolo.fd = net_connect(ctx->config.bolo_endpoint);
	if (ctx->bolo.fd < 0)
		bail("unable to connect to bolo core");
	ctx->bolo.watched = 0; /* don't start watching until we have data */
}

static int
runner_handler(int fd, void *_u)
{
	ssize_t nread;
	struct runner *r;
	char *eol;
	struct sndbuf *buf, *tmpbuf;

	r = (struct runner *)_u;
	for (;;) {
		debugf("reading from fd %d; into %p at offset %d, up to %d bytes",
			fd, r->buf, r->nread, RUNNER_BUFSIZ - r->nread);
		nread = read(fd, r->buf + r->nread, RUNNER_BUFSIZ - r->nread);
		if (nread == 0) {
			debugf("eof on fd %d", fd);
			r->outfd = -1;
			r->pid = 0;
			delist(&r->check->q);
			return -1;
		}

		if (nread < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
			errnof("failed to read from runner fd %d", fd);
			return -1;
		}
		debugf("read %d bytes...", nread);
		r->nread += nread;
		r->buf[r->nread] = '\0';

		/* find the largest substring that ends in a newline */
		eol = strrchr(r->buf, '\n');
		if (!eol) return 0;
		eol++;

		/* clear space in send buffers by ejecting oldest */
		debugf("sendbuf: have %d/%d bytes left (%3.1lf%%) in send buffer quota",
				r->ctx->config.max_sendbuf - r->ctx->bolo.bufsiz, r->ctx->config.max_sendbuf,
				(100.0 * (r->ctx->config.max_sendbuf - r->ctx->bolo.bufsiz) / r->ctx->config.max_sendbuf));
		for_eachx(buf, tmpbuf, &r->ctx->bolo.bufs, send) {
			if (r->ctx->bolo.bufsiz + (eol - r->buf) <= r->ctx->config.max_sendbuf)
				break;

			debugf("ejecting stale sendbuf %p (reclaiming %d octets)", buf, buf->len);
			r->ctx->bolo.bufsiz -= buf->len;
			delist(&buf->send);
			free(buf);
		}

		/* allocate a new send buffer */
		buf = xmalloc(sizeof(struct sndbuf) + (eol - r->buf));
		buf->off = 0;
		buf->len = (eol - r->buf);
		r->ctx->bolo.bufsiz += buf->len;
		empty(&buf->send);
		memcpy(buf->data, r->buf, buf->len);
		debugf("allocated new sendbuf %p for %d octets of data from runner %p", buf, buf->len, r);

		/* slide the runner buffer */
		debugf("possibly sliding the runner buffer (nread is %d / buf->len is %d)", r->nread, buf->len);
		if (buf->len < (unsigned)r->nread)
			memmove(r->buf, eol, r->nread - buf->len);
		r->nread -= buf->len;
		r->buf[r->nread] = '\0';

		/* append our buffer to the list to send upstream */
		push(&r->ctx->bolo.bufs, &buf->send);

		/* re-watch the bolo upstream fd, if necessary */
		if (!r->ctx->bolo.watched) {
			debugf("watching upstream relay fd (we have data to send!)");
			if (fdpoll_watch(r->ctx->poll, r->ctx->bolo.fd, FDPOLL_WRITE, relay_handler, r->ctx) != 0)
				bail("failed to re-watch upstream relay fd");
			r->ctx->bolo.watched = 1;
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
	int i, rc;
	pid_t kid;

	while ((kid = waitpid(-1, &rc, WNOHANG)) > 0) {
		debugf("child process %d exited [rc %x]", kid, rc);
	}

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
	struct sndbuf *buf, *tmp;

	ctx = (struct context *)_u;

	debugf("relay: spinning up to flush send buffer");
	for_eachx(buf, tmp, &ctx->bolo.bufs, send) {
		debugf("relay: sending %d octets from sendbuf %p...", buf->len - buf->off, buf);
		nwrit = write(fd, buf->data + buf->off, buf->len - buf->off);
		debugf("relay: nwrit = %d", nwrit);
		if (nwrit == 0)
			return -1; /* FIXME: reconnect? */
		if (nwrit < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			if (errno == EPIPE) {
				s_connect(ctx, 0);
				continue;
			}
			return -1;
		}

		debugf("relay: sent %d/%d octets from sendbuf %p", nwrit, buf->len - buf->off, buf);
		buf->off += nwrit;
		if (buf->off == buf->len) {
			debugf("finished sending all of sendbuf %p; removing it from the list (reclaiming %d bytes)...", buf, buf->len);
			ctx->bolo.bufsiz -= buf->len;
			debugf("sendbuf: have %d/%d bytes left (%3.1lf%%) in send buffer quota",
					ctx->config.max_sendbuf - ctx->bolo.bufsiz, ctx->config.max_sendbuf,
					(100.0 * (ctx->config.max_sendbuf - ctx->bolo.bufsiz) / ctx->config.max_sendbuf));
			delist(&buf->send);
			free(buf);
		}
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
	empty(&ctx.bolo.bufs);

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

	signal(SIGPIPE, SIG_IGN);
	s_connect(&ctx, 1);
	fdpoll(ctx.poll);
	return 0;
}

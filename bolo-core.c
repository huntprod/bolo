#include "bolo.h"
#include "bqip.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <getopt.h>

#ifndef DEFAULT_CONFIG_FILE
#define DEFAULT_CONFIG_FILE "/etc/bolo.conf"
#endif

struct daemon {
	int                qmax;
	struct bqip       *qconn;
	struct net_poller *qpoll;
};

static int
query_handler(int fd, void *_u)
{
	int rc;
	struct bqip *bqip;

	bqip = (struct bqip *)_u;
	rc = bqip_read(bqip);
	if (rc < 0) goto fail;
	if (rc == 1) /* not quite */
		return 0;

	fprintf(stderr, "got query '%s' from client...\n", bqip->request.payload);
	bqip_send_result(bqip, 2);
	bqip_send_set(bqip, 4, "x=1:a,2:b,3:c,4:d");
	bqip_send_set(bqip, 4, "y=5:e,6:f,7:g,8:h");
	return 0;

fail:
	bqip->fd = -1;
	return -1;
}

static int
query_listener(int fd, void *_u)
{
	int i, sockfd;
	struct daemon *d;

	d = (struct daemon *)_u;
	sockfd = accept(fd, NULL, NULL);
	printf("S: accepted new inbound connection.\n");

	for (i = 0; i < d->qmax; i++) {
		if (d->qconn[i].fd < 0) {
			bqip_init(&d->qconn[i], sockfd);
			if (net_poll_fd(d->qpoll, sockfd, query_handler, &d->qconn[i]) == 0)
				return 0;

			fprintf(stderr, "S: failed to register accepted socket; closing...\n");
			close(sockfd);
			return 0;
		}
	}

	fprintf(stderr, "S: max connections reached; closing socket...\n");
	close(sockfd);
	return 0;
}

int
do_core(int argc, char **argv)
{
	int i, listenfd;
	struct daemon d;
	struct config cfg;

	{
		int fd, override_log_level = -1;
		char *config_file = strdup(DEFAULT_CONFIG_FILE);

		int idx = 0;
		char c, *shorts = "hDc:l:";
		struct option longs[] = {
			{"help",      no_argument,       0, 'h'},
			{"debug",     no_argument,       0, 'D'},
			{"config",    required_argument, 0, 'c'},
			{"log-level", required_argument, 0, 'l'},
			{0, 0, 0, 0},
		};

		while ((c = getopt_long(argc, argv, shorts, longs, &idx)) >= 0) {
			switch (c) {
			case 'h':
				printf("USAGE: %s daemon [--config /etc/bolo.conf] [--debug] [--log-level error]\n\n", argv[0]);
				printf("OPTIONS\n\n");
				printf("  -h, --help              Show this help screen.\n\n");
				printf("  -c, --config FILE       Path to a configuration file.\n"
					   "                          Defaults to " DEFAULT_CONFIG_FILE ".\n\n");
				printf("  -l, --log-level LEVEL   Log level.  This overrides the value\n"
					   "                          from the configuration file.\n"
					   "                          Must be one of ERROR, WARNING, or INFO.\n\n");
				printf("  -D, --debug             Enable debugging mode.\n"
					   "                          (mostly useful only to bolo devs).\n\n");
				return 0;

			case 'D':
				debugto(fileno(stderr));
				break;

			case 'c':
				free(config_file);
				config_file = strdup(optarg);
				break;

			case 'l':
				     if (strcasecmp(optarg, "error")   == 0) override_log_level = LOG_ERRORS;
				else if (strcasecmp(optarg, "warning") == 0) override_log_level = LOG_WARNINGS;
				else if (strcasecmp(optarg, "info")    == 0) override_log_level = LOG_INFO;
				else fprintf(stderr, "invalid log-level '%s': ignoring...\n", optarg);
				break;
			}
		}

		fd = open(config_file, O_RDONLY);
		if (fd < 0) {
			errnof("unable to open configuration file %s", config_file);
			return 1;
		}
		if (configure_defaults(&cfg) != 0
		 && configure(&cfg, fd) != 0)
			return 1;

		if (override_log_level != -1)
			cfg.log_level = override_log_level;
	}

	d.qmax = cfg.query_max_connections;
	d.qconn = calloc(d.qmax, sizeof(*d.qconn));
	if (!d.qconn)
		bail("calloc(conns) failed");
	for (i = 0; i < d.qmax; i++)
		d.qconn[i].fd = -1;

	d.qpoll = net_poller(d.qmax + 1);
	if (!d.qpoll)
		bail("net_poller failed");

	listenfd = net_bind(cfg.query_listen, 64);
	if (listenfd < 0) {
		errnof("net_bind failed");
		bail("net_bind failed");
	}

	if (net_poll_fd(d.qpoll, listenfd, query_listener, &d) != 0)
		bail("net_poll_fd(listener) failed");

	printf("S: entering main loop.\n");
	net_poll(d.qpoll);
	close(listenfd);
	return 0;
}

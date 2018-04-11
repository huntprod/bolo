#include "bolo.h"
#include "bqip.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <getopt.h>
#include <pthread.h>

#ifndef DEFAULT_CONFIG_FILE
#define DEFAULT_CONFIG_FILE "/etc/bolo.conf"
#endif

static struct core_config cfg;
static struct db         *db;
static pthread_mutex_t    db_lock;

static struct qlsnr {
	int                fd;
	pthread_t          tid;
	int                nconn;
	struct bqip       *conn;
	struct fdpoll     *poll;
} qlsnr;

static void *
qlsnr_thread(void *_u)
{
	struct qlsnr *ql;
	ql = (struct qlsnr *)_u;

	fdpoll(ql->poll);
	close(ql->fd);
	return NULL;
}

static int
query_handler(int fd, void *_u)
{
	int rc;
	size_t i;
	struct bqip *bqip;
	struct query *q;
	struct qfield *f;

	bqip = (struct bqip *)_u;
	rc = bqip_read(bqip);
	if (rc < 0) goto fail;
	if (rc == 1) /* not quite */
		return 0;

	switch (bqip->request.type) {
	default:
		bqip_send_error(bqip, "unrecognized packet type");
		break;

	case 'Q':
		q = query_parse(bqip->request.payload);
		if (!q)
			goto fail;

		pthread_mutex_lock(&db_lock);
			rc = query_plan(q, db);
			if (rc != 0)
				goto fail;

			rc = query_exec(q, db, NULL);
			if (rc != 0)
				goto fail;
		pthread_mutex_unlock(&db_lock);

		bqip_send0(bqip, "R");
		for (f = q->select; f; f = f->next) {
			bqip_send0(bqip, "|");
			bqip_send0(bqip, f->name);
			bqip_send0(bqip, "=");
			for (i = 0; i < f->result->len; i++)
				bqip_send_tuple(bqip, &f->result->results[i]);
		}
		break;

	case 'P':
		q = query_parse(bqip->request.payload);
		if (!q)
			goto fail;

		pthread_mutex_lock(&db_lock);
			rc = query_plan(q, db);
			if (rc != 0)
				goto fail;
		pthread_mutex_unlock(&db_lock);

		bqip_send0(bqip, "R");
		for (f = q->select; f; f = f->next) {
			bqip_send0(bqip, "|");
			bqip_send0(bqip, f->name);
		}
		break;
	}

	bqip->fd = -1;
	return -1; /* force-close after every query */

fail:
	pthread_mutex_unlock(&db_lock);
	bqip->fd = -1;
	return -1;
}

static int
query_listener(int _, void *_u)
{
	int i, sockfd;
	struct qlsnr *ql;

	ql = (struct qlsnr *)_u;
	sockfd = accept(ql->fd, NULL, NULL);
	printf("S: accepted new inbound connection.\n");

	for (i = 0; i < ql->nconn; i++) {
		if (ql->conn[i].fd >= 0) continue;

		bqip_init(&ql->conn[i], sockfd);
		if (fdpoll_watch(ql->poll, sockfd, FDPOLL_READ, query_handler, &ql->conn[i]) == 0)
			return 0;

		fprintf(stderr, "S: failed to register accepted socket; closing...\n");
		close(sockfd);
		return 0;
	}

	fprintf(stderr, "S: max connections reached; closing socket...\n");
	close(sockfd);
	return 0;
}



static struct mlsnr {
	int              fd;
	pthread_t        tid;
	int              nconn;
	struct ingestor *conn;
	struct fdpoll   *poll;
} mlsnr;

static void *
mlsnr_thread(void *_u)
{
	struct mlsnr *ml;
	ml = (struct mlsnr *)_u;

	fdpoll(ml->poll);
	close(ml->fd);
	return NULL;
}

static int
metric_handler(int fd, void *_u)
{
	struct ingestor *in;
	int n;

	in = (struct ingestor *)_u;
	n = ingest_read(in);
	if (n < 0)
		goto fail;
	if (n == 0)
		goto done;

	pthread_mutex_lock(&db_lock);
	while (n-- > 0) {
		debugf("ingesting metric submission from fd %d", in->fd);
		if (ingest(in) != 0) {
			errnof("failed to ingest metric submission from fd %d", in->fd);
			goto lockfail;
		}

		debugf("inserting new measurement [%lu] %s=%e from fd %d",
			in->time, in->metric, in->value, in->fd);
		if (db_insert(db, in->metric, in->time, in->value) != 0) {
			errnof("failed to insert [%lu] %s=%e measurement into database",
				in->time, in->metric, in->value);
			goto lockfail;
		}
	}

	debugf("syncing database...");
	if (db_sync(db) != 0) {
		errnof("failed to sync database after updating metric measurements");
		goto lockfail;
	}
	pthread_mutex_unlock(&db_lock);

done:
	if (ingest_eof(in)) {
		debugf("at eof on ingestor fd %d; shutting down this connection.", in->fd);
		in->eof = in->fd = -1;
		return -1;
	}
	return 0;

lockfail:
	pthread_mutex_unlock(&db_lock);
fail:
	in->fd = -1;
	return -1;
}

static int
metric_listener(int fd, void *_u)
{
	int i, sockfd;
	struct mlsnr *ml;

	ml = (struct mlsnr *)_u;
	sockfd = accept(ml->fd, NULL, NULL);
	printf("S: accepted new inbound connection.\n");

	for (i = 0; i < ml->nconn; i++) {
		if (ml->conn[i].fd >= 0) continue;

		ml->conn[i].fd = sockfd;
		if (fdpoll_watch(ml->poll, sockfd, FDPOLL_READ, metric_handler, &ml->conn[i]) == 0)
			return 0;

		fprintf(stderr, "S: failed to register accepted socket; closing...\n");
		close(sockfd);
		return 0;
	}

	fprintf(stderr, "S: max connections reached; closing socket...\n");
	close(sockfd);
	return 0;
}

int
do_core(int argc, char **argv)
{
	int i;
	struct dbkey *key;

	{
		char *key_str;
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

		key_str = NULL;
		while ((c = getopt_long(argc, argv, shorts, longs, &idx)) >= 0) {
			switch (c) {
			case 'h':
				printf("USAGE: %s core [--config /etc/bolo.conf] [--debug] [--log-level error]\n\n", argv[0]);
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
				override_log_level = LOG_INFO;
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

		debugf("reading configuration from %s", config_file);
		fd = open(config_file, O_RDONLY);
		if (fd < 0) {
			errnof("unable to open configuration file %s", config_file);
			return 1;
		}
		if (configure(CORE_CONFIG, &cfg, fd) != 0)
			return 1;

		if (override_log_level != -1)
			cfg.log_level = override_log_level;

		if (!key_str)
			key_str = cfg.db_secret_key;
		key = read_key(key_str);
		if (!key) {
			fprintf(stderr, "invalid database encryption key given\n");
			return 1;
		}
	}

	startlog(argv[0], getpid(), cfg.log_level);

	infof("mounting database at %s", cfg.db_data_root);
	db = db_mount(cfg.db_data_root, key);
	if (!db && (errno == BOLO_ENODBROOT || errno == BOLO_ENOMAINDB)) {
		warningf("unable to mount database; doesn't look like %s has been initialized yet", cfg.db_data_root);
		infof("initializing database at %s...", cfg.db_data_root);
		db = db_init(cfg.db_data_root, key);
		if (!db) {
			errnof("unable to create new bolo database at %s", cfg.db_data_root);
			return 2;
		}
	}
	if (!db) {
		errnof("unable to mount bolo database at %s", cfg.db_data_root);
		return 2;
	}

	/* configure query listener */
	qlsnr.nconn = cfg.query_max_connections;
	qlsnr.conn  = xalloc(qlsnr.nconn, sizeof(*qlsnr.conn));

	for (i = 0; i < qlsnr.nconn; i++)
		qlsnr.conn[i].fd = -1;

	qlsnr.poll = fdpoller(qlsnr.nconn + 1);
	if (!qlsnr.poll)
		bail("network setup failed.");

	qlsnr.fd = net_bind(cfg.query_listen, 64);
	if (qlsnr.fd < 0)
		bail("network bind failed.");

	if (fdpoll_watch(qlsnr.poll, qlsnr.fd, FDPOLL_READ, query_listener, &qlsnr) != 0)
		bail("network poll failed.");

	/* configure metric listener */
	mlsnr.nconn = cfg.metric_max_connections;
	mlsnr.conn  = xalloc(mlsnr.nconn, sizeof(*mlsnr.conn));

	for (i = 0; i < mlsnr.nconn; i++)
		mlsnr.conn[i].fd = -1;

	mlsnr.poll = fdpoller(mlsnr.nconn + 1);
	if (!mlsnr.poll)
		bail("network setup failed.");

	mlsnr.fd = net_bind(cfg.metric_listen, 64);
	if (mlsnr.fd < 0)
		bail("network bind failed.");

	if (fdpoll_watch(mlsnr.poll, mlsnr.fd, FDPOLL_READ, metric_listener, &mlsnr) != 0)
		bail("network poll failed.");

	/* initialize mutices */
	pthread_mutex_init(&db_lock, NULL);

	/* spin both threads */
	pthread_create(&qlsnr.tid, NULL, qlsnr_thread, &qlsnr);
	pthread_create(&mlsnr.tid, NULL, mlsnr_thread, &mlsnr);
	pthread_join(qlsnr.tid, NULL);
	return 0;
}

#include "bolo.h"
#include <getopt.h>

int
do_import(int argc, char **argv)
{
	struct ingestor in;
	struct db *db;
	struct dbkey *key;
	int n;

	{
		char *key_str;
		int idx = 0;
		char c, *shorts = "hDk:";
		struct option longs[] = {
			{"help",      no_argument,       0, 'h'},
			{"debug",     no_argument,       0, 'D'},
			{"key",       required_argument, 0, 'k'},
			{0, 0, 0, 0},
		};

		while ((c = getopt_long(argc, argv, shorts, longs, &idx)) >= 0) {
			switch (c) {
			case 'h':
				printf("USAGE: %s import [--key \"key-in-hex\"] [--debug] /path/to/db/\n\n", argv[0]);
				printf("OPTIONS\n\n");
				printf("  -h, --help              Show this help screen.\n\n");
				printf("  -k, --key KEY-IN-HEX    The literal, hex-encoded database encryption key.\n");
				printf("  -D, --debug             Enable debugging mode.\n"
					   "                          (mostly useful only to bolo devs).\n\n");
				return 0;

			case 'D':
				debugto(fileno(stderr));
				break;

			case 'k':
				free(key_str);
				key_str = strdup(optarg);
				break;
			}
		}

		if (key_str) {
			key = read_key(key_str);
			if (!key) {
				fprintf(stderr, "invalid database encryption key given\n");
				return 1;
			}
		} else {
			printf("USAGE: %s import [--key \"key-in-hex\"] [--debug] /path/to/db/\n\n", argv[0]);
			return 1;
		}
	}

	memset(&in, 0, sizeof(in));
	if (argc != optind+2) {
		printf("USAGE: %s import [--key \"key-in-hex\"] [--debug] /path/to/db/\n\n", argv[0]);
		return 1;
	}

	db = db_mount(argv[optind+1], key);
	if (!db && (errno == BOLO_ENODBROOT || errno == BOLO_ENOMAINDB))
		db = db_init(argv[optind+1], key);
	if (!db) {
		errorf("%s: %s", argv[2], error(errno));
		return 2;
	}

	in.fd = 0; /* stdin */
	while (!ingest_eof(&in)) {
		n = ingest_read(&in);
		if (n < 0) {
			errnof("failed to ingest from stdin");
			return 1;
		}

		while (n-- > 0) {
			if (ingest(&in) != 0) {
				errnof("failed to ingest from stdin");
				return 1;
			}

			infof("inserting [%s %lu %f]", in.metric, in.time, in.value);
			if (db_insert(db, in.metric, in.time, in.value) != 0)
				errnof("failed to insert [%s %lu %f]", in.metric, in.time, in.value);
		}
		if (db_sync(db) != 0)
			errnof("failed to sync database to disk");
	}

	if (db_unmount(db) != 0)
		errnof("failed to unmount database");

	return 0;
}

#include "bolo.h"
#include <getopt.h>
#include <time.h>

int
do_metrics(int argc, char **argv)
{
	struct db *db;
	struct dbkey *key;

	{
		char *key_str = NULL;
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
				printf("USAGE: %s metrics [--key \"key-in-hex\"] [--debug] /path/to/db/\n\n", argv[0]);
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

		key = NULL;
		if (key_str) {
			key = read_key(key_str);
			if (!key) {
				fprintf(stderr, "invalid database encryption key given\n");
				return 1;
			}
		}
	}

	if (argc != optind+2) {
		fprintf(stderr, "USAGE: %s metrics [--key \"key-in-hex\"] [--debug] /path/to/db/\n\n", argv[0]);
		return 1;
	}

	db = db_mount(deslash(argv[optind+1]), key);
	if (!db) {
		fprintf(stderr, "%s: %s\n", argv[optind+1], error(errno));
		return 2;
	}

	char *metric;
	hash_each(db->main, &metric, NULL) {
		printf("%s\n", metric);
	}

	if (db_unmount(db) != 0) {
		fprintf(stderr, "warning: had trouble unmounting database at %s: %s\n",
		                argv[optind+1], error(errno));
	}
	return 0;
}


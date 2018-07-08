#include "bolo.h"
#include <getopt.h>

int
do_dbinfo(int argc, char **argv)
{
	struct db *db;
	struct dbkey *key;
	struct tslab *slab;
	struct idx *idx;
	struct multidx *set;
	const char *k;

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
				printf("USAGE: %s dbinfo [--key \"key-in-hex\"] [--debug] /path/to/db/\n\n", argv[0]);
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
		fprintf(stderr, "USAGE: %s dbinfo [--key \"key-in-hex\"] [--debug] /path/to/db/\n\n", argv[0]);
		return 1;
	}

	db = db_mount(deslash(argv[optind+1]), key);
	if (!db) {
		fprintf(stderr, "%s: %s\n", argv[optind+1], error(errno));
		return 2;
	}

	fprintf(stdout, "%s:\n", argv[optind+1]);
	fprintf(stdout, "next tblock #: [%#06lx]\n", db->next_tblock);

	if (isempty(&db->slab)) {
		fprintf(stdout, "slabs: (none)\n");
	} else {
		fprintf(stdout, "slabs:\n");
		for_each(slab, &db->slab, l) {
			fprintf(stdout, "  - [%#06lx] %dk\n", slab->number, slab->block_size / 1024);
		}
		fprintf(stdout, "\n");
	}

	if (isempty(&db->idx)) {
		fprintf(stdout, "indices: (none)\n");
	} else {
		fprintf(stdout, "indices:\n");
		for_each(idx, &db->idx, l) {
			fprintf(stdout, "  - [%#06lx]", idx->number);
			if (btree_isempty(idx->btree)) {
				fprintf(stdout, " (empty)\n");
			} else {
				bolo_msec_t first, last;
				first = btree_first(idx->btree);
				last  = btree_last(idx->btree);
				fprintf(stdout, " spans %lu (from %lu to %lu)\n", last - first, first, last);
			}
		}
		fprintf(stdout, "\n");
	}

	if (hash_isempty(db->main)) {
		fprintf(stdout, "series: (none)\n");
	} else {
		fprintf(stdout, "series:\n");
		hash_each(db->main, &k, &idx) {
			fprintf(stdout, "  - %s @[%#06lx]\n", k, idx->number);
		}
		fprintf(stdout, "\n");
	}

	if (hash_isempty(db->metrics)) {
		fprintf(stdout, "metrics: (none)\n");
	} else {
		fprintf(stdout, "metrics:\n");
		hash_each(db->metrics, &k, &set) {
			fprintf(stdout, "  - %s\n", k);
			for (; set; set = set->next) {
				fprintf(stdout, "      @[%#06lx]\n", set->idx->number);
			}
		}
		fprintf(stdout, "\n");
	}

	if (hash_isempty(db->tags)) {
		fprintf(stdout, "tags: (none)\n");
	} else {
		fprintf(stdout, "tags:\n");
		hash_each(db->tags, &k, &set) {
			fprintf(stdout, "  - %s\n", k);
			for (; set; set = set->next) {
				fprintf(stdout, "      @[%#06lx]\n", set->idx->number);
			}
		}
		fprintf(stdout, "\n");
	}

	if (db_unmount(db) != 0) {
		fprintf(stderr, "warning: had trouble unmounting database at %s: %s\n",
		                argv[optind+1], error(errno));
	}
	return 0;
}

#include "bolo.h"
#include <getopt.h>
#include <time.h>

static void
dump_qcond(struct qcond *qc, int depth)
{
	char buf[201];
	struct multidx *set;

	if (depth > 200)
		depth = 200;

	memset(buf, ' ', depth);
	buf[depth] = '\0';

	switch (qc->op) {
	case COND_AND:
		fprintf(stderr, "%sAND:\n", buf);
		dump_qcond(qc->a, depth + 2);
		dump_qcond(qc->b, depth + 2);
		break;
	case COND_OR:
		fprintf(stderr, "%sOR:\n", buf);
		dump_qcond(qc->a, depth + 2);
		dump_qcond(qc->b, depth + 2);
		break;
	case COND_NOT:
		fprintf(stderr, "%sNOT:\n", buf);
		dump_qcond(qc->a, depth + 2);
		break;
	case COND_EQ:
		fprintf(stderr, "%sEQ: [%s] = '%s'\n", buf, (char *)qc->a, (char *)qc->b);
		for (set = qc->midx; set; set = set->next)
			fprintf(stderr, "%s  - idx [%#06lx]\n", buf, set->idx->number);
		break;
	case COND_EXIST:
		fprintf(stderr, "%sEXIST: [%s]\n", buf, (char *)qc->a);
		for (set = qc->midx; set; set = set->next)
			fprintf(stderr, "%s  - idx [%#06lx]\n", buf, set->idx->number);
		break;
	}
}

int
do_query(int argc, char **argv)
{
	struct db *db;
	struct dbkey *key;
	struct query *query;
	struct query_ctx ctx;

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
				printf("USAGE: %s query [--key \"key-in-hex\"] [--debug] /path/to/db/\n\n", argv[0]);
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

	char *envnow;
	ctx.now = time(NULL) * 1000;
	if ((envnow = getenv("BOLO_NOW")) != NULL) {
		char *end;
		struct tm rnow;

		end = strptime(envnow, "%Y-%m-%d %H:%M:%S", &rnow);
		if (end && *end) {
			fprintf(stderr, "skipping unrecognized BOLO_NOW value '%s' (must be in 'YYYY-MM-DD hh:mm:ss' format)\n", envnow);
		} else {
			ctx.now = mktime(&rnow) * 1000;
		}
	}

	if (argc != optind+3) {
		fprintf(stderr, "USAGE: %s query [--key \"key-in-hex\"] [--debug] /path/to/db/ QUERY\n\n", argv[0]);
		return 1;
	}

	db = db_mount(argv[optind+1], key);
	if (!db) {
		fprintf(stderr, "%s: %s\n", argv[optind+1], error(errno));
		return 2;
	}

	query = query_parse(argv[optind+2]);
	if (!query) {
		fprintf(stderr, "invalid query.\n");

	} else if (query_plan(query, db) != 0) {
		fprintf(stderr, "query `%s` failed:\n", argv[optind+2]);
		fprintf(stderr, "  %s (%s)\n", query_strerror(query), query->err_data);
		bail("failed to plan query...");

	} else if (query_exec(query, db, &ctx) != 0) {
		bail("failed to execute query...");

	} else {
		struct qexpr *qx;

		fprintf(stderr, "fields:\n");
		for (qx = query->select; qx; qx = qx->next) {
			struct multidx *set;
			fprintf(stderr, "  found matching series '%s':\n", (char *)qx->a);
			for (set = qx->set; set; set = set->next)
				fprintf(stderr, "    - [%#06lx] %p\n", set->idx->number, (void *)set->idx);
			fprintf(stderr, "\n");
		}
		fprintf(stderr, "\n");

		fprintf(stderr, "aggregate %is\n\n", query->aggr);

		if (query->where) {
			fprintf(stderr, "conditions:\n");
			dump_qcond(query->where, 2);
			fprintf(stderr, "\n");
		}

		for (qx = query->select; qx; qx = qx->next) {
			int i;

			fprintf(stderr, "%s:\n", qx->result->key);
			for (i = 0; (unsigned)i < qx->result->len; i++)
				fprintf(stderr, "  - {ts: %lu, value: %lf, n: %i}\n",
					qx->result->results[i].start,
					qx->result->results[i].value,
					qx->result->results[i].n);
		}

		fprintf(stderr, "...\n");

		query_free(query);
	}

	if (db_unmount(db) != 0) {
		fprintf(stderr, "warning: had trouble unmounting database at %s: %s\n",
		                argv[optind+1], error(errno));
	}
	return 0;
}


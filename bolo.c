#include "bolo.h"
#include <time.h>

/* USAGE:

    bolo [-v|--version]

    bolo dbinfo DATADIR
    bolo idxinfo FILE
    bolo slabinfo FILE
    bolo import
    bolo parse BQL-QUERY
    bolo version
    bolo core

    (other commands and options added as necessary)

 */

#define EXT(x) extern int do_ ## x (int argc, char **argv)
EXT(dbinfo);
EXT(core);
EXT(help);
EXT(idxinfo);
EXT(import);
EXT(parse);
EXT(slabinfo);
EXT(version);
#undef EXT

static void
_dump_qcond(struct qcond *qc, int depth)
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
		_dump_qcond(qc->a, depth + 2);
		_dump_qcond(qc->b, depth + 2);
		break;
	case COND_OR:
		fprintf(stderr, "%sOR:\n", buf);
		_dump_qcond(qc->a, depth + 2);
		_dump_qcond(qc->b, depth + 2);
		break;
	case COND_NOT:
		fprintf(stderr, "%sNOT:\n", buf);
		_dump_qcond(qc->a, depth + 2);
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

static int
do_query(int argc, char **argv)
{
	struct db *db;
	struct query *query;
	struct query_ctx ctx;

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

	if (argc != 4) {
		fprintf(stderr, "USAGE: bolo query DATABASE 'QUERY...'\n");
		return 1;
	}

	db = db_mount(argv[2]);
	if (!db) {
		fprintf(stderr, "%s: %s\n", argv[2], error(errno));
		return 2;
	}

	query = query_parse(argv[3]);
	if (!query) {
		fprintf(stderr, "invalid query.\n");

	} else if (query_plan(query, db) != 0) {
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
			_dump_qcond(query->where, 2);
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
		                argv[2], error(errno));
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *command;
	int log_level;
	char *s;

	if (argc < 2)
		command = "version";
	else
		command = argv[1];

	log_level = LOG_ERRORS;
	if ((s = getenv("BOLO_LOGLEVEL")) != NULL) {
		     if (streq(s, "error")   || streq(s, "ERROR"))  log_level = LOG_ERRORS;
		else if (streq(s, "warning") || streq(s, "WARNING")
		      || streq(s, "warn")    || streq(s, "WARN"))   log_level = LOG_WARNINGS;
		else if (streq(s, "info")    || streq(s, "INFO"))   log_level = LOG_INFO;
		/* silently ignore incorrect values */
	}
	startlog(argv[0], getpid(), log_level);
	if ((s = getenv("BOLO_DEBUG")) != NULL)
		debugto(2);

	if (strcmp(command, "-v") == 0
	 || strcmp(command, "--version") == 0)
		command = "version";

	if (strcmp(command, "version") == 0)
		return do_version(argc, argv);

	if (strcmp(command, "import") == 0)
		return do_import(argc, argv);

	if (strcmp(command, "dbinfo") == 0)
		return do_dbinfo(argc, argv);

	if (strcmp(command, "idxinfo") == 0)
		return do_idxinfo(argc, argv);

	if (strcmp(command, "slabinfo") == 0)
		return do_slabinfo(argc, argv);

	if (strcmp(command, "parse") == 0)
		return do_parse(argc, argv);

	if (strcmp(command, "query") == 0)
		return do_query(argc, argv);

	if (strcmp(command, "core") == 0)
		return do_core(argc, argv);

	return do_help(argc, argv);
}

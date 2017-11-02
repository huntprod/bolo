#include "bolo.h"
#include <sys/stat.h>
#include <time.h>

/* USAGE:

    bolo [-v|--version]

    bolo dbinfo DATADIR
    bolo idxinfo FILE
    bolo slabinfo FILE
    bolo parse BQL-QUERY
    bolo version
    bolo stdin

    (other commands and options added as necessary)

 */

static int
do_dbinfo(int argc, char **argv)
{
	struct db *db;
	struct tslab *slab;
	struct idx *idx;
	const char *k;

	if (argc != 3) {
		fprintf(stderr, "USAGE: bolo dbinfo DATADIR\n");
		return 1;
	}

	db = db_mount(argv[2]);
	if (!db) {
		fprintf(stderr, "%s: %s\n", argv[2], error(errno));
		return 2;
	}

	fprintf(stdout, "%s:\n", argv[2]);
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
			fprintf(stdout, "  - [%#06lx]\n", idx->number);
		}
		fprintf(stdout, "\n");
	}

	if (hash_isempty(db->tags)) {
		fprintf(stdout, "tags: (none)\n");
	} else {
		fprintf(stdout, "tags:\n");
		hash_each(db->tags, &k, NULL) {
			fprintf(stdout, "  - %s\n", k);
		}
		fprintf(stdout, "\n");
	}

	if (db_unmount(db) != 0) {
		fprintf(stderr, "warning: had trouble unmounting database at %s: %s\n",
		                argv[2], error(errno));
	}
	return 0;
}

static int
do_idxinfo(int argc, char **argv)
{
	struct btree *idx;
	const char *path;
	int i, fd;

	if (argc < 3) {
		fprintf(stderr, "USAGE: bolo idxinfo FILE\n");
		return 1;
	}

	for (i = 2; i < argc; i++) {
		path = argv[i];
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "failed to open %s: %s\n", path, error(errno));
			continue;
		}

		idx = btree_read(fd);
		if (!idx) {
			fprintf(stderr, "failed to read %s: %s\n", path, error(errno));
			continue;
		}

		fprintf(stdout, "IDX %s\n", path);
		btree_print(idx);

		btree_close(idx);
		close(fd);
	}
	return 0;
}

static int
do_slabinfo(int argc, char **argv)
{
	struct tslab slab;
	const char *path;
	int i, j, k, rc, fd;
	int nvalid;

	if (argc < 3) {
		fprintf(stderr, "USAGE: bolo slabinfo FILE\n");
		return 1;
	}

	for (i = 2; i < argc; i++) {
		path = argv[i];
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "failed to open %s: %s\n", path, error(errno));
			continue;
		}

		rc = tslab_map(&slab, fd);
		if (rc != 0) {
			fprintf(stderr, "failed to map %s: %s\n", path, error(errno));
			continue;
		}

		nvalid = 0;
		for (j = 0; j < TBLOCKS_PER_TSLAB; j++) {
			if (!slab.blocks[j].valid)
				break;
			nvalid++;
		}

		fprintf(stdout, "%s:\n", path);
		fprintf(stdout, "slab %lu (%#016lx) %dk %d/%d blocks present\n",
			slab.number, slab.number, slab.block_size / 1024, nvalid, TBLOCKS_PER_TSLAB);
		for (j = 0; j < TBLOCKS_PER_TSLAB; j++) {
			double full, bitsper;
			uint32_t span, tmp;
			char unit, date[64];
			struct tm tm;
			time_t ts;
			unsigned d, h, m, s, ms;

			if (!slab.blocks[j].valid)
				break;

			full = 100.0 * slab.blocks[j].cells / TCELLS_PER_TBLOCK;

			unit = 'b';
			bitsper = (TBLOCK_SIZE * 8.0) / slab.blocks[j].cells;
			if (bitsper > 1024.0) {
				bitsper /= 1024.0;
				unit = 'k';
			}
			span = 0;
			for (k = 0; k < slab.blocks[j].cells; k++) {
				tmp = tblock_read32(slab.blocks+j, 24 + k * 12);
				if (tmp > span)
					span = tmp;
			}
			ms = span % 1000; span /= 1000;
			s  = span % 60;   span /= 60;
			m  = span % 60;   span /= 60;
			h  = span % 24;   span /= 24;
			d  = span;

			ts = (time_t)(slab.blocks[j].base / 1000);
			if (!localtime_r(&ts, &tm))
				strcpy(date, "xxx, xx xx xxxx xx:xx:xx+xxxx");
			else
				strftime(date, 64, "%a, %d %b %Y %H:%M:%S%z", &tm);

			fprintf(stdout, "    @%lu (%#016lx) ts %lu [%s] % 6i measurements;"
			                   " %6.2lf%% full, spanning %ud %02u:%02u:%02u.%04u;"
			                   " %7.2lf%c/measurement\n",
				slab.blocks[j].number, slab.blocks[j].number,
				slab.blocks[j].base, date,
				slab.blocks[j].cells,
				full, d, h, m, s, ms,
				bitsper, unit);
		}

		if (tslab_unmap(&slab) != 0)
			fprintf(stderr, "failed to unmap %s...\n", path);
	}
	return 0;
}

static int
do_stdin(int argc, char **argv)
{
	char buf[8192];
	char *metric, *tags, *time, *value, *end;
	struct db *db;
	bolo_msec_t when;
	bolo_value_t what;

	if (argc != 3) {
		fprintf(stderr, "USAGE: bolo stdin path/to/db\n");
		return 1;
	}

	db = db_mount(argv[2]);
	if (!db && (errno == BOLO_ENODBROOT || errno == BOLO_ENOMAINDB))
		db = db_init(argv[2]);
	if (!db) {
		errorf("%s: %s", argv[2], error(errno));
		return 2;
	}

	while (fgets(buf, 8192, stdin) != NULL) {
		metric = strtok(buf,  " \n");
		tags   = strtok(NULL, " \n");
		time   = strtok(NULL, " \n");
		value  = strtok(NULL, " \n");

		if (!metric || !tags || !time || !value)
			continue;

		when = strtoull(time, &end, 10);
		if (end && *end) {
			errorf("failed to parse [%s %s] timestamp '%s'",
			       metric, tags, time);
			continue;
		}

		what = strtod(time, &end);
		if (end && *end) {
			errorf("failed to parse [%s %s] measurement value '%s'",
			       metric, tags, value);
			continue;
		}

		if (tags_valid(tags) != 0
		 || tags_canonicalize(tags) != 0) {
			errorf("failed to parse tag set from [%s %s %s]; skipping",
			       metric, tags, value);
			continue;
		}

		/* compose metric|tags */
		metric[strlen(metric)] = '|';
		infof("inserting [%s %s %s]",
			       metric, time, value);
		if (db_insert(db, metric, when, what) != 0)
			errorf("failed to insert [%s %s %s]: %s (error %d)",
			       metric, time, value, error(errno), errno);

		if (db_sync(db) != 0)
			errorf("failed to sync database to disk: %s (error %d)",
			       error(errno), errno);
	}

	if (db_unmount(db) != 0)
		errorf("failed to unmount database: %s", error(errno));

	return 0;
}

static int
do_parse(int argc, char **argv)
{
	struct query *query;
	char buf[501], datetime[100];
	size_t len;
	int i;
	struct tm from, until;
	time_t now;

	if (argc < 3) {
		fprintf(stderr, "USAGE: bolo parse 'QUERY ...'\n");
		return 1;
	}

	now = time(NULL);
	memset(buf, '-', 500);
	buf[500] = '\0';
	for (i = 2; i < argc; i++) {
		len = strlen(argv[i]);
		if (len < 500) buf[len] = '\0';
		fprintf(stdout, "%s\n%s\n", argv[i], buf);
		if (len < 500) buf[len] = '-';

		query = query_parse(argv[i]);
		if (!query) {
			fprintf(stdout, "  error: BQL query is syntactically incorrect, or semantically invalid\n\n");
			continue;
		}

		fprintf(stdout, "  input is a well-formed BQL query.\n");
		//fprintf(stdout, "  canonical form is\n    '%s'\n", query_stringify(query));
		if (query->aggr) {
			fprintf(stdout, "  aggregate window %ds (%.1fm / %.1fh)\n",
				query->aggr, query->aggr / 60.0, query->aggr / 3600.0);
		} else {
			fprintf(stdout, "  no aggregation will be performed.\n");
		}

		now += query->from;
		if (!localtime_r(&now, &from))
			bail("failed to determine localtime");
		now = now - query->from + query->until;
		if (!localtime_r(&now, &until))
			bail("failed to determine localtime");
		now -= query->until;

		fprintf(stdout, "  data will only be considered if it was submitted\n");
		strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S %z", &from);
		fprintf(stdout, "    between %s (%.1fh ago)\n", datetime, query->from / -3600.0);
		if (query->until == 0) {
			fprintf(stdout, "        and now\n");
		} else {
			strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S %z", &until);
			fprintf(stdout, "        and %s (%.1fh ago)\n", datetime, query->until / -3600.0);
		}
		fprintf(stdout, "\n");

		// print details about select
		// print details about where
	}

	return 0;
}

static int
do_usage(int argc, char **argv)
{
	fprintf(stderr, "USAGE: ...\n");
	return 1;
}

static int
do_version(int argc, char **argv)
{
	fprintf(stdout, "bolo v0.0\n");
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

	if (strcmp(command, "-v") == 0
	 || strcmp(command, "--version") == 0)
		command = "version";

	if (strcmp(command, "version") == 0)
		return do_version(argc, argv);

	if (strcmp(command, "stdin") == 0)
		return do_stdin(argc, argv);

	if (strcmp(command, "dbinfo") == 0)
		return do_dbinfo(argc, argv);

	if (strcmp(command, "idxinfo") == 0)
		return do_idxinfo(argc, argv);

	if (strcmp(command, "slabinfo") == 0)
		return do_slabinfo(argc, argv);

	if (strcmp(command, "parse") == 0)
		return do_parse(argc, argv);

	return do_usage(argc, argv);
}

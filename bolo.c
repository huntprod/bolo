#include "bolo.h"
#include <sys/stat.h>

/* USAGE:

    bolo [-v|--version]

    bolo dbinfo DATADIR
    bolo idxinfo FILE
    bolo slabinfo FILE
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
	int i, j, rc, fd;
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

		fprintf(stdout, "SLAB %lu (%lx)\n", slab.number, slab.number);
		fprintf(stdout, "  block-size %u bytes\n", slab.block_size);
		fprintf(stdout, "  %d valid blocks\n", nvalid);
		for (j = 0; j < TBLOCKS_PER_TSLAB; j++) {
			double full;

			if (!slab.blocks[j].valid)
				break;

			full = 100.0 * slab.blocks[j].cells / TCELLS_PER_TBLOCK;
			fprintf(stdout, "    Block %lu (%lx) [#%d]\n", slab.blocks[j].number, slab.blocks[j].number, j+1);
			fprintf(stdout, "      %i measurements, %5.2lf%% full\n", slab.blocks[j].cells, full);
			fprintf(stdout, "      base timestamp %lu\n", slab.blocks[j].base);
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

	return do_usage(argc, argv);
}

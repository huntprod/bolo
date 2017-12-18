#include "bolo.h"

int
do_import(int argc, char **argv)
{
	struct ingestor in;
	struct db *db;
	int n;

	memset(&in, 0, sizeof(in));

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

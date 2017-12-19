#include "bolo.h"

int
do_init(int argc, char **argv)
{
	struct db *db;

	if (argc != 3) {
		fprintf(stderr, "USAGE: bolo init DATADIR\n");
		return 1;
	}

	db = db_init(argv[2]);
	if (!db) {
		fprintf(stderr, "%s: %s\n", argv[2], error(errno));
		return 2;
	}

	if (db_unmount(db) != 0) {
		fprintf(stderr, "warning: had trouble unmounting database at %s: %s\n",
		                argv[2], error(errno));
	}
	return 0;
}

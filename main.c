#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "bolo.h"

int main(int argc, char **argv)
{
	char buf[32], *end;
	double v;
	struct bolo_tsdb *db;
	bolo_msec_t now;

	if (debugto(3) != 0)
		fprintf(stderr, "note: debugging not available; re-run with `3>&2' to activate it.\n");

	db = bolo_tsdb_create("bolo.db");
	if (!db) {
		fprintf(stderr, "failed to create bolo database: %s\n", strerror(errno));
		exit(1);
	}

	while (fgets(buf, 32, stdin) != NULL) {
		end = NULL;
		v = strtod(buf, &end);

		if (end != NULL && *end != '\n') {
			while (*end) {
				if (*end == '\n') *end = '\0';
				end++;
			}
			fprintf(stderr, "invalid value '%s'; skipping...\n", buf);
			continue;
		}

		now = bolo_ms(NULL);
		if (now != INVALID_MS) {
			fprintf(stderr, "{%lu, %lf}\n", now, v);
			if (bolo_tsdb_log(db, now, v) != 0)
				fprintf(stderr, "oops. log tuple failed: %s\n", strerror(errno));
			if (bolo_tsdb_commit(db) != 0)
				fprintf(stderr, "oops. commit failed: %s\n", strerror(errno));
		}
	}

	fprintf(stderr, "shutting down...\n");
	if (bolo_tsdb_close(db) != 0)
		fprintf(stderr, "oops. close failed: %s\n", strerror(errno));
	return 0;
}

#include "bolo.h"
#include <errno.h>

int main(int argc, char **argv)
{
	char buf[32], *end;
	const char *path;
	double v;
	struct tslab slab;
	int fd, rc;
	bolo_msec_t now;

	if (debugto(3) != 0)
		fprintf(stderr, "note: debugging not available; re-run with `3>&2' to activate it.\n");

	path = "bolo.db";
	fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0666);
	if (fd < 0) {
		fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
		exit(1);
	}

	rc = tslab_init(&slab, fd, 123450000, (1 << 19)); /* FIXME */
	if (rc != 0) {
		fprintf(stderr, "failed to map %s: %s\n", path, strerror(errno));
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
			if (FIXME_log(&slab, now, v) != 0)
				fprintf(stderr, "oops. log tuple failed: %s\n", strerror(errno));
			if (tslab_sync(&slab) != 0)
				fprintf(stderr, "oops. commit failed: %s\n", strerror(errno));
		}
	}

	fprintf(stderr, "shutting down...\n");
	if (tslab_unmap(&slab) != 0)
		fprintf(stderr, "oops. close failed: %s\n", strerror(errno));

	return 0;
}

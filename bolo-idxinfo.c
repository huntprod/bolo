#include "bolo.h"

int
do_idxinfo(int argc, char **argv)
{
#if 0
	struct btree *idx;
	const char *path;
	int i, fd;
#endif

	if (argc < 3) {
		fprintf(stderr, "USAGE: bolo idxinfo FILE\n");
		return 1;
	}

	fprintf(stderr, "bolo idxinfo is deprecated\n");
	return 2;

#if 0
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
#endif
}

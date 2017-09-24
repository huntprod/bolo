#include "bolo.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* USAGE:

    bolo [-v|--version]

    bolo slabinfo FILE
    bolo version

    (other commands and options added as necessary)

 */

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
			fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
			continue;
		}

		rc = tslab_map(&slab, fd);
		if (rc != 0) {
			fprintf(stderr, "failed to map %s: %s\n", path, strerror(errno));
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
do_usage(int argc, char **argv)
{
	fprintf(stderr, "USAGE: ...\n");
	return 1;
}

static int
do_version(int argc, char **argv)
{
	printf("bolo v0.0\n");
	return 0;
}

int main(int argc, char **argv)
{
	const char *command;

	if (argc < 2)
		command = "version";
	else
		command = argv[1];

	if (strcmp(command, "-v") == 0
	 || strcmp(command, "--version") == 0)
		command = "version";

	if (strcmp(command, "version") == 0)
		return do_version(argc, argv);

	if (strcmp(command, "slabinfo") == 0)
		return do_slabinfo(argc, argv);

	return do_usage(argc, argv);
}

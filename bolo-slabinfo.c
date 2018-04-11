#include "bolo.h"
#include <time.h>

int
do_slabinfo(int argc, char **argv)
{
	struct tslab slab;
	const char *path;
	int i, j, k, rc, fd;
	int tcells, nvalid;
	double full, bitsper;
	char unit;

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

		memset(&slab, 0, sizeof(slab));
		rc = tslab_map(&slab, fd);
		if (rc != 0) {
			fprintf(stderr, "failed to map %s: %s\n", path, error(errno));
			continue;
		}

		tcells = nvalid = 0;
		for (j = 0; j < TBLOCKS_PER_TSLAB; j++) {
			if (!slab.blocks[j].valid)
				break;
			nvalid++;
		}

		fprintf(stdout, "%s:\n", path);
		fprintf(stdout, "slab %lu (%#016lx) %dk %d/%d blocks present\n",
			slab.number, slab.number, slab.block_size / 1024, nvalid, TBLOCKS_PER_TSLAB);
		for (j = 0; j < TBLOCKS_PER_TSLAB; j++) {
			uint32_t span, tmp;
			char date[64];
			struct tm tm;
			time_t ts;
			unsigned d, h, m, s, ms;

			if (!slab.blocks[j].valid)
				break;

			tcells += slab.blocks[j].cells;
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
			                   " %6.2lf%% full, spanning %2ud %02u:%02u:%02u.%04u;"
			                   " %7.2lf%c/measurement\n",
				slab.blocks[j].number, slab.blocks[j].number,
				slab.blocks[j].base, date,
				slab.blocks[j].cells,
				full, d, h, m, s, ms,
				bitsper, unit);
		}

		/* totals */
		full = 100.0 * tcells / (TCELLS_PER_TBLOCK * nvalid);

		unit = 'b';
		bitsper = (nvalid * TBLOCK_SIZE * 8.0) / tcells;
		if (bitsper > 1024.0) {
			bitsper /= 1024.0;
			unit = 'k';
		}
		fprintf(stdout, "  total %i measurements (%6.2lf%% full)\n", tcells, full);
		fprintf(stdout, "  average %0.2lf%c/measurement\n", bitsper, unit);

		if (tslab_unmap(&slab) != 0)
			fprintf(stderr, "failed to unmap %s...\n", path);
	}
	return 0;
}

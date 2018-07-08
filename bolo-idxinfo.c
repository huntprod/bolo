#include "bolo.h"

/* reserve the first 8 octets for header data */
#define BTREE_HEADER_SIZE 8

#define BTREE_LEAF 0x80

/* where do the keys start in the mapped page? */
#define BTREE_KEYS_OFFSET (BTREE_HEADER_SIZE)
/* where do the values start in the mapped page? */
#define BTREE_VALS_OFFSET (BTREE_KEYS_OFFSET + BTREE_DEGREE * sizeof(bolo_msec_t))

#define koffset(i) (BTREE_KEYS_OFFSET + (i) * sizeof(bolo_msec_t))
#define voffset(i) (BTREE_VALS_OFFSET + (i) * sizeof(uint64_t))

#define keyat(b,i)   (*(uint64_t*)(b+koffset(i)))
#define valueat(b,i) (*(uint64_t*)(b+voffset(i)))

int
do_idxinfo(int argc, char **argv)
{
	const char *path;
	int i, k, id, fd, flags, used;
	char buf[BTREE_PAGE_SIZE];
	off_t flen, j;
	ssize_t n;

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

		flen = lseek(fd, 0, SEEK_END);
		for (id = j = 0; j < flen; j += BTREE_PAGE_SIZE, id++) {
			lseek(fd, j, SEEK_SET);

			printf("-----------.\n");
			n = read(fd, buf, BTREE_PAGE_SIZE);
			if (n < 0) {
				printf("%10x |  ***CORRUPT*** BTREE NODE\n", id);
				errnof("failed to read BTREE page at offset %d", j);
				continue;
			}
			if (n != BTREE_PAGE_SIZE) {
				errno = BOLO_EBADTREE;
				printf("%10x |  ***CORRUPT*** BTREE NODE\n", id);
				errnof("failed to read BTREE page at offset %d, only got %d of expected %d bytes", j, n, BTREE_PAGE_SIZE);
				continue;
			}

			printf("%10x |  BTREE NODE %#x\n", id, id);
			flags = *(uint8_t*)(buf+5);
			used = *(uint16_t*)(buf+6);

			printf("%10x |  flags: [%c.......]\n", id, flags & BTREE_LEAF ? 'L' : 'I');
			printf("%10x |          - %s node\n", id, flags & BTREE_LEAF ? "leaf" : "interior");
			printf("%10x |\n", id);
			printf("%10x |  used: %d keys\n", id, used);

			if (flags & BTREE_LEAF) {
				for (k = 0; k < used; k++)
					printf("%10x |  key [% 10d]: %14lu = tblock #%-16ld (+%lu / +%lu)\n",
					       id, k, keyat(buf, k), valueat(buf, k), j+koffset(k), j+voffset(k));
			} else {
				for (k = 0; k < used; k++)
					printf("%10x |  key [% 10d]: %14lu -> [0x%08lx] (+%lu / +%lu)\n",
					       id, k, keyat(buf, k), valueat(buf, k), j+koffset(k), j+voffset(k));
			}
			printf("\n");
		}
		close(fd);
	}
	return 0;
}

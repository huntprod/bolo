#include "bolo.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

/* hex 7e d1 32 4c */
#define ENDIAN_CHECK 2127639116U

#define BOLO_TSDB_HDR_SIZE    4 * 1024
#define BOLO_TSDB_BLK_SIZE  512 * 1024

struct bolo_tsdb {
	int fd;
	struct tsdb_slab *slab;
};

struct tsdb_slab {
	int fd;                  /* file descriptor of the slab file */
	uint32_t block_size;     /* how big is each data block page? */
	uint64_t number;         /* slab number, with the least significant
	                            11-bits cleared. */

	struct tblock                 /* list of all blocks in this slab.    */
	  blocks[TBLOCKS_PER_TSLAB];  /* present blocks will have .valid = 1 */
};

CATALOG
static int
s_scan_slab(struct tsdb_slab *slab, int fd)
{
	assert(slab != NULL);
	assert(fd >= 0);

	char header[TSLAB_HEADER_SIZE];
	ssize_t nread;
	uint32_t endian_check;

	nread = read(fd, header, TSLAB_HEADER_SIZE);
	if (nread < 0) /* read error! */
		return -1;

	if (nread != TSLAB_HEADER_SIZE) /* short read! */
		return -1;

	errno = EINVAL;
	if (memcmp(header, "SLABv1", 6) != 0) /* not a slab! */
		return -1;

	/* check the HMAC */
	if (hmac_check(FIXME_DEFAULT_KEY, FIXME_DEFAULT_KEY_LEN, header, TSLAB_HEADER_SIZE) != 0)
		return -1;

	/* check host endianness vs file endianness */
	endian_check = read32(header, 8);
	if (endian_check != ENDIAN_CHECK)
		return -1;

	slab->fd         = fd;
	slab->block_size = (1 << read8(header, 6));
	slab->number     = read64(header, 16);

	/* scan blocks! */

	return 0;
}

CATALOG
static int
s_close_slab(struct tsdb_slab *slab)
{
	int i, ok;

	assert(slab != NULL);

	ok = 0;
	for (i = 0; i < TBLOCKS_PER_TSLAB; i++) {
		if (!slab->blocks[i].valid)
			break;

		slab->blocks[i].valid = 0;
		if (tblock_unmap(&slab->blocks[i]) != 0)
			ok = -1;
	}

	close(slab->fd);
	return ok;
}

CATALOG
static int
s_sync_slab(struct tsdb_slab *slab)
{
	int i, ok;

	assert(slab != NULL);

	ok = 0;
	for (i = 0; i < TBLOCKS_PER_TSLAB; i++) {
		if (!slab->blocks[i].valid)
			break;

		if (tblock_sync(&slab->blocks[i]) != 0)
			ok = -1;
	}

	return ok;
}

CATALOG
static struct tsdb_slab *
s_new_slab(const char *path, uint64_t number, int block_size_exp)
{
	struct tsdb_slab *slab;
	int fd;
	char header[TSLAB_HEADER_SIZE];
	size_t nwrit;

	assert(path != NULL);
	assert(block_size_exp == 19);

	fd = open(path, O_RDWR|O_CREAT, 0666);
	if (fd < 0)
		return NULL;

	memset(header, 0, sizeof(header));
	memcpy(header, "SLABv1", 6);
	write8(header,   6, block_size_exp);
	write32(header,  8, ENDIAN_CHECK);
	write64(header, 16, number);
	hmac_seal(FIXME_DEFAULT_KEY, FIXME_DEFAULT_KEY_LEN, header, sizeof(header));

	slab = calloc(1, sizeof(*slab));
	if (!slab)
		bail("memory allocation failed");

	nwrit = write(fd, header, sizeof(header));
	if (nwrit != sizeof(header))
		goto fail;

	/* align to a page boundary */
	lseek(fd, sysconf(_SC_PAGESIZE) - 1, SEEK_SET);
	if (write(fd, "\0", 1) != 1)
		goto fail;

	slab->fd         = fd;
	slab->block_size = (1 << block_size_exp);
	slab->number     = number;
	return slab;

fail:
	free(slab);
	close(fd);
	return NULL;
}

CATALOG
static int
s_slab_isfull(struct tsdb_slab *slab)
{
	int i;

	assert(slab != NULL);

	for (i = 0; i < TBLOCKS_PER_TSLAB; i++)
		if (!slab->blocks[i].valid)
			return 0; /* this block is avail; slab is not full */

	return 1; /* no blocks avail; slab is full */
}

CATALOG
static int
s_add_block(struct tsdb_slab *slab, bolo_msec_t base)
{
	int i;
	off_t start;
	size_t len;

	assert(slab != NULL);

	/* seek to the end of the fd, so we can extend it */
	if (lseek(slab->fd, 0, SEEK_END) < 0)
		return -1;

	/* find the first available (!valid) block */
	for (i = 0; i < TBLOCKS_PER_TSLAB; i++) {
		if (slab->blocks[i].valid)
			continue;

		len   = slab->block_size;
		start = sysconf(_SC_PAGESIZE) + i * len;
		assert(len == (1 << 19));

		/* extend the file descriptor one TBLOCK's worth */
		if (lseek(slab->fd, start + len - 1, SEEK_SET) < 0)
			return -1;
		if (write(slab->fd, "\0", 1) != 1)
			return -1;

		/* map the new block into memory */
		if (tblock_map(&slab->blocks[i], slab->fd, start, len) == 0) {
			tblock_init(&slab->blocks[i], (slab->number & ~0x7ff) | i, base);
			return 0;
		}

		/* map failed; truncate the fd back to previous size */
		ftruncate(slab->fd, start);
		lseek(slab->fd, 0, SEEK_END);

		/* ... and signal failure */
		return -1;
	}

	/* this slab is full; signal failure */
	return -1;
}

struct bolo_tsdb *
bolo_tsdb_create(const char *path)
{
	struct bolo_tsdb *db;

	assert(path != NULL);

	db = malloc(sizeof(struct bolo_tsdb));
	if (!db) bail("malloc failed");

	db->slab = s_new_slab(path, 123450000, 19); /* FIXME */
	if (!db->slab)
		goto fail;

	return db;

fail:
	free(db);
	return NULL;
}

int
bolo_tsdb_close(struct bolo_tsdb *db)
{
	int rc;

	assert(db != NULL);
	assert(db->slab != NULL);

	rc = s_close_slab(db->slab);
	close(db->fd);
	return rc;
}

int
bolo_tsdb_log(struct bolo_tsdb *db, bolo_msec_t ts, bolo_value_t value)
{
	int i;

	assert(db != NULL);
	assert(db->slab != NULL);

	if (s_slab_isfull(db->slab))
		bail("slab full!");

	for (i = 0; i < TBLOCKS_PER_TSLAB; i++) {
		if (!db->slab->blocks[i].valid) {
			if (s_add_block(db->slab, ts) != 0)
				bail("failed to add another block");
			break;
		}

		if (!tblock_isfull(&db->slab->blocks[i])
		  && tblock_canhold(&db->slab->blocks[i], ts))
			break; /* found it! */
	}
	assert(i < TBLOCKS_PER_TSLAB);

	fprintf(stderr, "using block #%d\n", i);
	if (tblock_log(&db->slab->blocks[i], ts, value) != 0)
		fprintf(stderr, "warning: failed to log {%lu,%lf} in block #%d: %s\n", ts, value, i, strerror(errno));

	/* START HERE:
	   - need to mmap the file in create/open         DONE
	   - maintain a pointer to the last open block
	   - skip to that block via pointer addressing
	   - calculate relative ts from block base ts
	     - if relative ts would overflow,
	       start a new block at ts
	   - store relative ts and value
	   - increment number of metrics
	   - recalculate HMAC                             DONE

	   alternatively, we can reserve space after the header for a work
	   area, and store the (ts,v) tuples there as we insert, allowing
	   for a certain amount of reordering before commiting to a full
	   ordered block
	 */
	return 0;
}

int
bolo_tsdb_commit(struct bolo_tsdb *db)
{
	assert(db != NULL);
	assert(db->slab != NULL);

	return s_sync_slab(db->slab);
}

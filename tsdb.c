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

#define BOLO_TSDB_HDR_SIZE    4 * 1024
#define BOLO_TSDB_BLK_SIZE  512 * 1024

/* hex 7e d1 32 4c */
#define ENDIAN_CHECK 2127639116U

struct bolo_tsdb {
	int fd;
	void *ptr;
	size_t mlen;
};

static void
bail(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(2);
}

static int
s_extend(struct bolo_tsdb *db, size_t n)
{
	int rc;
	off_t pos, size;

	assert(db != NULL);
	assert(n != 0);

	if (db->ptr) {
		rc = munmap(db->ptr, db->mlen);
		if (rc != 0) return rc;
	}

	pos = lseek(db->fd, 0, SEEK_END);
	if (pos < 0)
		return -1;
	debugf("tsdb %p is currently at seek offset %ld\n", db, pos);

	debugf("tsdb %p extending by %ld bytes to offset %ld\n", db, n, pos + n);
	if (lseek(db->fd, n - 1, SEEK_CUR) < 0)
		goto fail;

	if (write(db->fd, "\0", 1) != 1)
		goto fail;
	size = lseek(db->fd, 0, SEEK_CUR);
	if (size < 0)
		return -1;

	lseek(db->fd, pos, SEEK_SET);
	debugf("tsdb %p is %ld bytes long\n", db, size);
	assert(size > 0);

	db->mlen = size;
	db->ptr = mmap(
		NULL,                   /* let kernel choose address */
		db->mlen,             /* map the whole file        */
		PROT_READ|PROT_WRITE,   /* allow reads and writes    */
		MAP_SHARED,             /* contents visible on-disk  */
		db->fd,               /* the file descriptor...    */
		0                       /* from the start of file    */
	);
	if (db->ptr == MAP_FAILED) {
		db->ptr = NULL;
		return -1;
	}

	if (bolo_tsdb_commit(db) != 0)
		return -1;

	return 0;

fail:
	lseek(db->fd, pos, SEEK_SET);
	return -1;
}

CATALOG
static uint8_t
s_read8(struct bolo_tsdb *db, off_t offset)
{
	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert((size_t)offset < db->mlen);

	return (uint8_t)(*((uint8_t *)db->ptr + offset));
}

static void
s_write8(struct bolo_tsdb *db, off_t offset, int v)
{
	uint8_t u;

	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert((size_t)offset < db->mlen);
	assert(v >= 0 && v <= 0xff);

	u = v & 0xff;
	memcpy((uint8_t *)db->ptr + offset, &u, 1);
}

CATALOG
static uint16_t
s_read16(struct bolo_tsdb *db, off_t offset)
{
	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert((size_t)offset + 2 < db->mlen);

	return (uint16_t)(*((uint8_t *)db->ptr + offset));
}

CATALOG
static void
s_write16(struct bolo_tsdb *db, off_t offset, int v)
{
	uint16_t u;

	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert((size_t)offset + 2 < db->mlen);
	assert(v >= 0 && v <= 0xffff);

	u = v & 0xffff;
	memcpy((uint8_t *)db->ptr + offset, &u, 2);
}

CATALOG
static uint32_t
s_read32(struct bolo_tsdb *db, off_t offset)
{
	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert((size_t)offset + 4 < db->mlen);

	return (uint32_t)(*((uint8_t *)db->ptr + offset));
}

CATALOG
static void
s_write32(struct bolo_tsdb *db, off_t offset, unsigned int v)
{
	uint32_t u;

	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert((size_t)offset + 4 < db->mlen);

	u = v & 0xffffffff;
	memcpy((uint8_t *)db->ptr + offset, &u, 4);
}

CATALOG
static uint64_t
s_read64(struct bolo_tsdb *db, off_t offset)
{
	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert((size_t)offset + 8 < db->mlen);

	return (uint64_t)(*((uint8_t *)db->ptr + offset));
}

CATALOG
static void
s_write64(struct bolo_tsdb *db, off_t offset, uint64_t v)
{
	uint64_t u;

	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert((size_t)offset + 8 < db->mlen);

	u = v & 0xffffffffffffffff;
	memcpy((uint8_t *)db->ptr + offset, &u, 8);
}

CATALOG
static double
s_read64f(struct bolo_tsdb *db, off_t offset)
{
	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert((size_t)offset + 8 < db->mlen);

	return (double)(*((uint8_t *)db->ptr + offset));
}

CATALOG
static void
s_write64f(struct bolo_tsdb *db, off_t offset, double v)
{
	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert((size_t)offset + 8 < db->mlen);

	memcpy((uint8_t *)db->ptr + offset, &v, 8);
}

static void
s_write(struct bolo_tsdb *db, off_t offset, const char *b, size_t len)
{
	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert(b != NULL);
	assert(len > 0);
	assert((size_t)offset + len < db->mlen);

	memcpy((uint8_t *)db->ptr + offset, b, len);
}

static void
s_zero(struct bolo_tsdb *db, off_t offset, size_t len)
{
	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(offset >= 0);
	assert(len > 0);
	assert((size_t)offset + len < db->mlen);

	memset((uint8_t *)db->ptr + offset, 0, len);
}

static void
s_hmac(struct bolo_tsdb *db)
{
	assert(db != NULL);
	assert(db->ptr != NULL);
	assert(db->mlen == 4096);

	hmac_sha512_seal("BOLO SECRET KEY", 15, db->ptr, db->mlen);
}

struct bolo_tsdb *
bolo_tsdb_create(const char *path)
{
	struct bolo_tsdb *db;

	assert(path != NULL);

	db = malloc(sizeof(struct bolo_tsdb));
	if (!db) bail("malloc failed");

	db->fd = open(path, O_CREAT|O_TRUNC|O_RDWR, 0666);
	if (db->fd < 0)
		goto fail;

	db->ptr = NULL;
	if (s_extend(db, BOLO_TSDB_HDR_SIZE) != 0)
		goto fail;

	/*

	     +--------+--------+--------+--------+--------+--------+--------+--------+
	   0 | "BFD1"                            | ENDIANNESS                        |
	     +--------+--------+--------+--------+--------+--------+--------+--------+
	   8 | NUM BLOCKS                        | BSE    | (reserved)               |
	     +--------+--------+--------+--------+--------+--------+--------+--------+
	  16 | FILE NUMBER                                                           |
	     +--------+--------+--------+--------+--------+--------+--------+--------+
	   * | (pad out to 4k)                                                       |
	     +--------+--------+--------+--------+--------+--------+--------+--------+

	 */

	s_write  (db,  0, "BFD1", 4);    /* MAGIC "BFD1"      */
	s_write32(db,  4, ENDIAN_CHECK); /* ENDIANNESS        */
	s_write32(db,  8, 0);            /* NUM BLOCKS (none) */
	s_write8 (db, 12, 19);           /* BSE, 2^19 = 512k  */
	s_zero   (db, 13, 3);            /* (reserved)        */
	s_zero   (db, 16, 8);            /* FILE NUMBER FIXME */

	s_hmac(db);

	return db;

fail:
	if (db->fd >= 0) close(db->fd);
	free(db);
	return NULL;
}

int
bolo_tsdb_close(struct bolo_tsdb *db)
{
	int rc;

	assert(db != NULL);
	assert(db->ptr != NULL);

	rc = munmap(db->ptr, db->mlen);
	close(db->fd);
	return rc;
}

int
bolo_tsdb_log(struct bolo_tsdb *db, bolo_msec_t ts, bolo_value_t value)
{
	uint16_t n;

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
	n = s_read16(db, 24) + 1;
	s_write16(db, 24, n);
	s_write64(db, 26 + n * 16,    ts);
	s_write64f(db, 26 + n * 16 + 8, value);
	s_hmac(db);
	return 0;
}

int
bolo_tsdb_commit(struct bolo_tsdb *db)
{
	assert(db != NULL);
	assert(db->ptr != NULL);

	return msync(db->ptr, db->mlen, MS_SYNC);
}

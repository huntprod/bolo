#include "bolo.h"
#include <stdlib.h>
#include <errno.h>

int
tblock_map(struct tblock *b, int fd, off_t offset, size_t len)
{
	int rc;

	assert(b != NULL);
	memset(b, 0, sizeof(*b));

	rc = page_map(&b->page, fd, offset, len);
	if (rc != 0)
		return rc;

	b->cells  = tblock_read16(b,  6);
	b->base   = tblock_read64(b,  8);
	b->number = tblock_read64(b, 16);

	assert(b->cells <= TCELLS_PER_TBLOCK);
	return 0;
}

void tblock_init(struct tblock *b, uint64_t number, bolo_msec_t base)
{
	assert(b != NULL);
	assert(b->page.data != NULL);

	b->valid  = 1;
	b->cells  = 0;
	b->number = number;
	b->base   = base;

	memset(b->page.data, 0, TBLOCK_HEADER_SIZE);
	memcpy(b->page.data, "BLOKv1", 6);

	tblock_write64(b,  6, b->cells);
	tblock_write64(b,  8, b->base);
	tblock_write64(b, 16, b->number);

	tblock_seal(FIXME_DEFAULT_KEY, FIXME_DEFAULT_KEY_LEN, b);
}

int tblock_isfull(struct tblock *b)
{
	assert(b != NULL);

	errno = ENOBUFS;
	return b->cells == TCELLS_PER_TBLOCK;
}

int
tblock_canhold(struct tblock *b, bolo_msec_t when)
{
	assert(b != NULL);

	errno = ERANGE;
	return when - b->base <  MAX_U32;
}

int
tblock_log(struct tblock *b, bolo_msec_t when, double what)
{
	assert(b != NULL);

	if (tblock_isfull(b))
		return -1;

	if (!tblock_canhold(b, when))
		return -1;

	assert(when - b->base < MAX_U32);
	tblock_write32 (b, 24 + b->cells * 12,     when - b->base);
	tblock_write64f(b, 24 + b->cells * 12 + 4, what);
	tblock_write16 (b, 6, ++b->cells);
	tblock_seal(FIXME_DEFAULT_KEY, FIXME_DEFAULT_KEY_LEN, b);
	return 0;
}

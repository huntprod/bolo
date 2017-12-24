#include "bolo.h"

int
tblock_map(struct tblock *b, int fd, off_t offset, size_t len)
{
	BUG(b != NULL, "tblock_map() given a NULL tblock to map");

	memset(b, 0, sizeof(*b));

	if (page_map(&b->page, fd, offset, len) != 0)
		return -1;

	b->cells  = tblock_read16(b,  6);
	b->base   = tblock_read64(b,  8);
	b->number = tblock_read64(b, 16);
	b->next   = tblock_read64(b, 24);

	insist(b->cells <= TCELLS_PER_TBLOCK, "tblock_map() detected a corrupt block; number of used cells is larger than allowed");
	return 0;
}

void tblock_init(struct tblock *b, uint64_t number, bolo_msec_t base)
{
	BUG(b != NULL,            "tblock_init() given a NULL tblock to initialize");
	BUG(b->page.data != NULL, "tblock_init() given an unmapped tblock to initialize");

	b->valid  = 1;
	b->cells  = 0;
	b->number = number;
	b->base   = base;

	memset(b->page.data, 0, TBLOCK_HEADER_SIZE);
	memcpy(b->page.data, "BLOKv1", 6);

	tblock_write64(b,  6, b->cells);
	tblock_write64(b,  8, b->base);
	tblock_write64(b, 16, b->number);
	tblock_write64(b, 24, b->next);

	if (b->key)
		tblock_seal(b, b->key);
}

int tblock_isfull(struct tblock *b)
{
	BUG(b != NULL, "tblock_isfull() given a NULL tblock to query");

	errno = BOLO_EBLKFULL;
	return b->cells == TCELLS_PER_TBLOCK;
}

int
tblock_canhold(struct tblock *b, bolo_msec_t when)
{
	BUG(b != NULL, "tblock_canhold() given a NULL tblock to query");

	errno = BOLO_EBLKRANGE;
	return when - b->base <  MAX_U32;
}

int
tblock_insert(struct tblock *b, bolo_msec_t when, double what)
{
	BUG(b != NULL, "tblock_insert() given a NULL tblock to insert into");

	if (tblock_isfull(b))
		return -1;

	if (!tblock_canhold(b, when))
		return -1;

	BUG(when - b->base < MAX_U32, "tblock_insert() given a timestamp that is beyond the range of this block");
	tblock_write32 (b, 32 + b->cells * 12,     when - b->base);
	tblock_write64f(b, 32 + b->cells * 12 + 4, what);
	tblock_write16 (b, 6, ++b->cells);
	if (b->key)
		tblock_seal(b, b->key);
	return 0;
}

void
tblock_next(struct tblock *b, struct tblock *next)
{
	b->next = next->number;
	tblock_write64(b, 24, b->next);
	if (b->key)
		tblock_seal(b, b->key);
}

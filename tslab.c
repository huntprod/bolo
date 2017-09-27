#include "bolo.h"

int tslab_map(struct tslab *s, int fd)
{
	assert(s != NULL);
	assert(fd >= 0);

	int i, rc;
	char header[TSLAB_HEADER_SIZE];
	ssize_t nread;
	off_t n;
	uint32_t endian_check;

	errno = BOLO_EBADSLAB;
	nread = read(fd, header, TSLAB_HEADER_SIZE);
	if (nread < 0) /* read error! */
		return -1;

	if (nread != TSLAB_HEADER_SIZE) /* short read! */
		return -1;

	if (memcmp(header, "SLABv1", 6) != 0) /* not a slab! */
		return -1;

	/* check the HMAC */
	if (hmac_check(FIXME_DEFAULT_KEY, FIXME_DEFAULT_KEY_LEN, header, TSLAB_HEADER_SIZE) != 0)
		return -1;

	/* check host endianness vs file endianness */
	endian_check = read32(header, 8);
	if (endian_check != TSLAB_ENDIAN_MAGIC)
		return -1;

	s->fd         = fd;
	s->block_size = (1 << read8(header, 6));
	s->number     = read64(header, 16);

	n = lseek(s->fd, 0, SEEK_END);
	if (n < 0)
		return -1;
	n -= 4096;

	/* scan blocks! */
	memset(s->blocks, 0, sizeof(s->blocks));
	lseek(s->fd, 4096, SEEK_SET);
	for (i = 0; i < TBLOCKS_PER_TSLAB && n > 0; i++, n -= s->block_size) {
		rc = tblock_map(&s->blocks[i], s->fd,
		                4096 + i * s->block_size, /* grab the i'th block */
		                s->block_size);
		if (rc != 0)
			return -1;

		s->blocks[i].valid = 1;
	}

	return 0;
}

int tslab_unmap(struct tslab *s)
{
	int i, ok;

	assert(s != NULL);

	ok = 0;
	for (i = 0; i < TBLOCKS_PER_TSLAB; i++) {
		if (!s->blocks[i].valid)
			break;

		s->blocks[i].valid = 0;
		if (tblock_unmap(&s->blocks[i]) != 0)
			ok = -1;
	}

	close(s->fd);
	return ok;
}

int tslab_sync(struct tslab *s)
{
	int i, ok;

	assert(s != NULL);

	ok = 0;
	for (i = 0; i < TBLOCKS_PER_TSLAB; i++) {
		if (!s->blocks[i].valid)
			break;

		if (tblock_sync(&s->blocks[i]) != 0)
			ok = -1;
	}

	return ok;
}

int tslab_init(struct tslab *s, int fd, uint64_t number, uint32_t block_size)
{
	char header[TSLAB_HEADER_SIZE];
	size_t nwrit;

	assert(s != NULL);
	assert(block_size == (1 << 19));

	memset(header, 0, sizeof(header));
	memcpy(header, "SLABv1", 6);
	write8(header,   6, 19); /* from block_size, ostensibly */
	write32(header,  8, TSLAB_ENDIAN_MAGIC);
	write64(header, 16, number & ~0xff);
	hmac_seal(FIXME_DEFAULT_KEY, FIXME_DEFAULT_KEY_LEN, header, sizeof(header));

	lseek(fd, 0, SEEK_SET);
	nwrit = write(fd, header, sizeof(header));
	if (nwrit != sizeof(header))
		return -1;

	/* align to a page boundary */
	lseek(fd, sysconf(_SC_PAGESIZE) - 1, SEEK_SET);
	if (write(fd, "\0", 1) != 1)
		return -1;

	s->fd         = fd;
	s->block_size = block_size;
	s->number     = number;
	memset(s->blocks, 0, sizeof(s->blocks));

	return 0;
}

int tslab_isfull(struct tslab *s)
{
	int i;

	assert(s != NULL);

	for (i = 0; i < TBLOCKS_PER_TSLAB; i++)
		if (!s->blocks[i].valid)
			return 0; /* this block is avail; slab is not full */

	return 1; /* no blocks avail; slab is full */
}

int tslab_extend(struct tslab *s, bolo_msec_t base)
{
	int i;
	off_t start;
	size_t len;

	assert(s != NULL);

	/* seek to the end of the fd, so we can extend it */
	if (lseek(s->fd, 0, SEEK_END) < 0)
		return -1;

	/* find the first available (!valid) block */
	for (i = 0; i < TBLOCKS_PER_TSLAB; i++) {
		if (s->blocks[i].valid)
			continue;

		len   = s->block_size;
		start = sysconf(_SC_PAGESIZE) + i * len;
		assert(len == (1 << 19));

		/* extend the file descriptor one TBLOCK's worth */
		if (lseek(s->fd, start + len - 1, SEEK_SET) < 0)
			return -1;
		if (write(s->fd, "\0", 1) != 1)
			return -1;

		/* map the new block into memory */
		if (tblock_map(&s->blocks[i], s->fd, start, len) == 0) {
			tblock_init(&s->blocks[i], (s->number & ~0xff) | i, base);
			return 0;
		}

		/* map failed; truncate the fd back to previous size */
		ftruncate(s->fd, start);
		lseek(s->fd, 0, SEEK_END);

		/* ... and signal failure */
		return -1;
	}

	/* this slab is full; signal failure */
	return -1;
}

int FIXME_log(struct tslab *s, bolo_msec_t when, double what)
{
	int i;

	assert(s != NULL);

	if (tslab_isfull(s))
		bail("slab full!");

	for (i = 0; i < TBLOCKS_PER_TSLAB; i++) {
		if (!s->blocks[i].valid) {
			if (tslab_extend(s, when) != 0)
				bail("failed to add another block");
			break;
		}

		if (!tblock_isfull(&s->blocks[i])
		  && tblock_canhold(&s->blocks[i], when))
			break; /* found it! */
	}
	assert(i < TBLOCKS_PER_TSLAB);

	if (tblock_log(&s->blocks[i], when, what) != 0)
		return -1;

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

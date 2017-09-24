#include "bolo.h"
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

int
page_map(struct page *p, int fd, off_t start, size_t len)
{
	int prot, flags;

	assert(p != NULL);
	assert(p->data == NULL);
	assert(fd >= 0);
	assert(start >= 0);
	assert(len > 0);

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return -1;

	switch (flags & O_ACCMODE) {
	case O_RDONLY: prot = PROT_READ;              break;
	case O_WRONLY: prot =             PROT_WRITE; break;
	case O_RDWR:   prot = PROT_READ | PROT_WRITE; break;
	default: bail("page_map: failed to determine r/w status of fd");
	}

	p->fd   = fd;
	p->len  = len;
	p->data = mmap(
		NULL,         /* let kernel choose address */
		p->len,       /* map the whole file        */
		prot,         /* use file descriptor mode  */
		MAP_SHARED,   /* contents visible on-disk  */
		p->fd,        /* the file descriptor...    */
		start         /* from the start of file    */
	);

	if (p->data == MAP_FAILED) {
		p->data = NULL;
		return -1;
	}

	return 0;
}

int
page_sync(struct page *p)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(p->len > 0);

	return msync(p->data, p->len, MS_SYNC);
}

int
page_unmap(struct page *p)
{
	int rc;

	if (!p || !p->data || !p->len)
		return 0; /* already unmapped */

	rc = munmap(p->data, p->len);
	if (rc != 0)
		return -1;

	p->data = NULL;
	p->len = 0;
	p->fd = -1;

	return 0;
}

uint8_t page_read8(struct page *p, size_t offset)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + 1 < p->len);

	return read8(p->data, offset);
}

uint16_t page_read16(struct page *p, size_t offset)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + 2 < p->len);

	return read16(p->data, offset);
}

uint32_t page_read32(struct page *p, size_t offset)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + 4 < p->len);

	return read32(p->data, offset);
}

uint64_t page_read64(struct page *p, size_t offset)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + 8 < p->len);

	return read64(p->data, offset);
}

double page_read64f(struct page *p, size_t offset)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + 8 < p->len);

	return read64f(p->data, offset);
}

void page_write8(struct page *p, size_t offset, uint8_t v)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + 1 < p->len);

	write8(p->data, offset, v);
}

void page_write16(struct page *p, size_t offset, uint16_t v)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + 2 < p->len);

	write16(p->data, offset, v);
}

void page_write32(struct page *p, size_t offset, uint32_t v)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + 4 < p->len);

	write32(p->data, offset, v);
}

void page_write64(struct page *p, size_t offset, uint64_t v)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + 8 < p->len);

	write64(p->data, offset, v);
}

void page_write64f(struct page *p, size_t offset, double v)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + 8 < p->len);

	write64f(p->data, offset, v);
}

void page_writen(struct page *p, size_t offset, const void *buf, size_t len)
{
	assert(p != NULL);
	assert(p->data != NULL);
	assert(offset + len < p->len);

	writen(p->data, offset, buf, len);
}

#include "bolo.h"
#include <sys/mman.h>

int
page_map(struct page *p, int fd, off_t start, size_t len)
{
	int prot, flags;

	CHECK(p != NULL,       "page_map() given a NULL page to map");
	CHECK(p->data == NULL, "page_map() given a page that has already been mapped");
	CHECK(fd >= 0,         "page_map() given an invalid file descriptor to map");
	CHECK(start >= 0,      "page_map() given an invalid offset to start mapping from");
	CHECK(len > 0,         "page_map() given an invalid length to map");

	if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
		return -1;

	switch (flags & O_ACCMODE) {
	case O_RDONLY: prot = PROT_READ;              break;
	case O_WRONLY: prot =             PROT_WRITE; break;
	case O_RDWR:   prot = PROT_READ | PROT_WRITE; break;
	default:
		errno = EBADF;
		return -1;
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
	CHECK(p != NULL,       "page_sync() given a NULL page to synchronize");
	CHECK(p->data != NULL, "page_sync() given a page without a mapped region");
	CHECK(p->len > 0,      "page_sync() given a page with an invalid region length");

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
	CHECK(p != NULL,           "page_read8() given a NULL page to read from");
	CHECK(p->data != NULL,     "page_read8() given an unmapped page to read from");
	CHECK(offset + 1 < p->len, "page_read8() given an out-of-range offset to read from");

	return read8(p->data, offset);
}

uint16_t page_read16(struct page *p, size_t offset)
{
	CHECK(p != NULL,           "page_read16() given a NULL page to read from");
	CHECK(p->data != NULL,     "page_read16() given an unmapped page to read from");
	CHECK(offset + 2 < p->len, "page_read16() given an out-of-range offset to read from");

	return read16(p->data, offset);
}

uint32_t page_read32(struct page *p, size_t offset)
{
	CHECK(p != NULL,           "page_read32() given a NULL page to read from");
	CHECK(p->data != NULL,     "page_read32() given an unmapped page to read from");
	CHECK(offset + 4 < p->len, "page_read32() given an out-of-range offset to read from");

	return read32(p->data, offset);
}

uint64_t page_read64(struct page *p, size_t offset)
{
	CHECK(p != NULL,           "page_read64() given a NULL page to read from");
	CHECK(p->data != NULL,     "page_read64() given an unmapped page to read from");
	CHECK(offset + 8 < p->len, "page_read64() given an out-of-range offset to read from");

	return read64(p->data, offset);
}

double page_read64f(struct page *p, size_t offset)
{
	CHECK(p != NULL,           "page_read64f() given a NULL page to read from");
	CHECK(p->data != NULL,     "page_read64f() given an unmapped page to read from");
	CHECK(offset + 8 < p->len, "page_read64f() given an out-of-range offset to read from");

	return read64f(p->data, offset);
}

void page_write8(struct page *p, size_t offset, uint8_t v)
{
	CHECK(p != NULL,           "page_write8() given a NULL page to write to");
	CHECK(p->data != NULL,     "page_write8() given an unmapped page to write to");
	CHECK(offset + 1 < p->len, "page_write8() given an out-of-range offset to write to");

	write8(p->data, offset, v);
}

void page_write16(struct page *p, size_t offset, uint16_t v)
{
	CHECK(p != NULL,           "page_write16() given a NULL page to write to");
	CHECK(p->data != NULL,     "page_write16() given an unmapped page to write to");
	CHECK(offset + 2 < p->len, "page_write16() given an out-of-range offset to write to");

	write16(p->data, offset, v);
}

void page_write32(struct page *p, size_t offset, uint32_t v)
{
	CHECK(p != NULL,           "page_write32() given a NULL page to write to");
	CHECK(p->data != NULL,     "page_write32() given an unmapped page to write to");
	CHECK(offset + 4 < p->len, "page_write32() given an out-of-range offset to write to");

	write32(p->data, offset, v);
}

void page_write64(struct page *p, size_t offset, uint64_t v)
{
	CHECK(p != NULL,           "page_write64() given a NULL page to write to");
	CHECK(p->data != NULL,     "page_write64() given an unmapped page to write to");
	CHECK(offset + 8 < p->len, "page_write64() given an out-of-range offset to write to");

	write64(p->data, offset, v);
}

void page_write64f(struct page *p, size_t offset, double v)
{
	CHECK(p != NULL,           "page_write64f() given a NULL page to write to");
	CHECK(p->data != NULL,     "page_write64f() given an unmapped page to write to");
	CHECK(offset + 8 < p->len, "page_write64f() given an out-of-range offset to write to");

	write64f(p->data, offset, v);
}

void page_writen(struct page *p, size_t offset, const void *buf, size_t len)
{
	CHECK(p != NULL,             "page_writen() given a NULL page to write to");
	CHECK(p->data != NULL,       "page_writen() given an unmapped page to write to");
	CHECK(buf != NULL,           "page_writen() given a NULL buffer to copy data from");
	CHECK(offset + len < p->len, "page_writen() given an out-of-range offset to write to");

	writen(p->data, offset, buf, len);
}

ssize_t page_readn(struct page *p, size_t offset, void *buf, size_t len)
{
	CHECK(p != NULL,             "page_readn() given a NULL page to read from");
	CHECK(p->data != NULL,       "page_readn() given an unmapped page to read from");
	CHECK(buf != NULL,           "page_readn() given a NULL buffer to copy read data to");
	CHECK(offset + len < p->len, "page_readn() given an out-of-range offset to read from");

	memcpy(buf, (uint8_t *)p->data + offset, len);
	return len;
}

#ifdef TEST
/* LCOV_EXCL_START */
TESTS {
	int fd;
	struct page p;
	char buf[64];

	memset(&p, 0, sizeof(p));

	fd = memfd("page");
	lseek(fd, sysconf(_SC_PAGESIZE) - 1, SEEK_SET);
	if (write(fd, "\0", 1) != 1)
		BAIL_OUT("failed to extend memfd backing file");

	ok(page_map(&p, fd, 0, sysconf(_SC_PAGESIZE)) == 0,
		"page_map() should succeed");

	page_write8  (&p,  0, 0x41);
	page_write16 (&p,  1, 0x4242);
	page_write32 (&p,  3, 0x43434343);
	page_write64 (&p,  7, 0x4545454545454545);
	page_write64f(&p, 15, 12345.6789);

	page_writen(&p, 0x100, "Hello, World", 12);

	ok(page_sync(&p)  == 0, "page_sync() should succeed");

	is_unsigned(page_read8(&p, 0), 0x41,
		"page_read8() should read what we page_wrote8()");
	is_unsigned(page_read16(&p, 1), 0x4242,
		"page_read16() should read what we page_wrote8()");
	is_unsigned(page_read32(&p, 3), 0x43434343,
		"page_read32() should read what we page_wrote32()");
	is_unsigned(page_read64(&p, 7), 0x4545454545454545,
		"page_read64() should read what we page_wrote64()");
	ok(page_read64f(&p, 15) == 12345.6789,
		"page_read64f() should read what we page_wrote64()");

	memset(buf, 0, sizeof(buf));
	is_unsigned(page_readn(&p, 0x100, buf, 12), 12,
		"page_readn() of 12 bytes should return 12");
	is_string(buf, "Hello, World",
		"page_readn() should read what we page_wroten()");


	ok(page_sync(&p)  == 0, "page_sync() should succeed");
	ok(page_unmap(&p) == 0, "page_unmap() should succeed");
}
/* LCOV_EXCL_STOP */
#endif

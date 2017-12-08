#include "bolo.h"

struct io {
	char   *buf;
	size_t  len;
	size_t  pos;

	int     fd;
	size_t  hwm;
	char   *tmp;

	int     own_buf;
};

struct io *
io_new(const char *tmp, size_t hwm)
{
	struct io *io;

	io = calloc(1, sizeof(*io));
	if (io && io_init(io, tmp, hwm) == 0) return io;
	io_free(io);
	return NULL;
}

int
io_init(struct io *io, const char *tmp, size_t hwm)
{
	io->fd  = -1;
	io->hwm = hwm;
	io->len = io->pos = 0;
	io->buf = io->tmp = NULL;

	if (!tmp) tmp = getenv("TMPDIR");
	if (!tmp) tmp = "/tmp";
	if (asprintf(&io->tmp, "%s/bolo.io.XXXXXXXX", tmp) < 0)
		goto fail;

	io->own_buf = 1;
	io->buf = calloc(hwm, sizeof(char));
	if (!io->buf) goto fail;

	return 0;

fail:
	return -1;
}

void
io_reinit(struct io *io)
{
	io->len = io->pos = 0;
	if (io->fd >= 0) {
		lseek(io->fd, 0, SEEK_SET);
		ftruncate(io->fd, 0);
	}
}

void
io_close(struct io *io)
{
	if (io->fd >= 0)
		close(io->fd);

	free(io->tmp);
	if (io->own_buf)
		free(io->buf);
}

void
io_free(struct io *io) {
	io_close(io);
	free(io);
}

struct io *
io_bufnew(char *buf, size_t len)
{
	struct io *io;

	io = calloc(1, sizeof(*io));
	if (io && io_buf(io, buf, len) == 0) return io;
	io_free(io);
	return NULL;
}

int
io_buf(struct io *io, char *buf, size_t len)
{
	io->fd  = -1;
	io->hwm = len;
	io->buf = buf;
	io->pos = 0;
	io->len = len;
	io->tmp = NULL;

	io->own_buf = 0;
	return 0;
}

int
io_catfd(struct io *io, int fd)
{
	char buf[8192];
	ssize_t nread, nwrit;
	size_t off;

	while ((nread = read(fd, buf, 8192)) != 0) {
		if (nread < 0)
			return -1;

		off = 0;
		for (off = 0; off < (size_t)nread; ) {
			nwrit = io_write(io, ((char *)buf)+off, nread);
			if (nwrit <= 0)
				return -1;
			off += nwrit;
		}
	}

	return 0;
}

int
io_copyfd(struct io *io, int fd)
{
	io_reinit(io);
	if (io_catfd(io, fd) != 0)
		return -1;
	io_rewind(io);
	return 0;
}

int
io_catbuf(struct io *io, const void *buf, size_t len)
{
	ssize_t nwrit;
	size_t off;

	for (off = 0; off < len; ) {
		nwrit = io_write(io, ((char *)buf)+off, len-off);
		if (nwrit <= 0)
			return -1;
		off += nwrit;
	}

	return 0;
}

int
io_copybuf(struct io *io, const void *buf, size_t len)
{
	io_reinit(io);
	if (io_catbuf(io, buf, len) != 0)
		return -1;
	io_rewind(io);
	return 0;
}

void
io_rewind(struct io *io)
{
	if (io->fd >= 0)
		lseek(io->fd, 0, SEEK_SET);
	else
		io->pos = 0;
}

static ssize_t
_io_write_fd(struct io *io, const void *buf, size_t len)
{
	ssize_t nwrit;

	nwrit = write(io->fd, buf, len);
	if (nwrit > 0)
		io->len += nwrit;
	return nwrit;
}

ssize_t
io_write(struct io *io, const void *buf, size_t len)
{
	ssize_t nwrit;

	if (io->fd >= 0)
		return _io_write_fd(io, buf, len);

	if (io->len + len > io->hwm) {
		errno = ENOENT;
		if (!io->tmp) return -1;

		io->fd = mkostemp(io->tmp, O_CLOEXEC);
		if (io->fd < 0) return -1;

		io->pos = 0;
		while (io->pos < io->len) {
			nwrit = write(io->fd, io->buf + io->pos, io->len - io->pos);
			if (nwrit <= 0) return -1; /* io->fd is blocking */
			io->pos += nwrit;
		}

		return _io_write_fd(io, buf, len);
	}

	memcpy(io->buf + io->len, buf, len);
	io->len += len;
	return len;
}

ssize_t
io_read(struct io *io, void *buf, size_t len)
{
	ssize_t ncopied;

	if (io->fd >= 0)
		return read(io->fd, buf, len);

	ncopied = MIN(len, io->len - io->pos);
	memcpy(buf, io->buf + io->pos, ncopied);
	io->pos += ncopied;
	return ncopied;
}

#ifdef TEST
/* LCOV_EXCL_START */
TESTS {
	subtest {
		struct io *new;

		new = io_new(NULL, 257);
		isnt_null(new, "io_new() returns a non-null io pointer");
		isnt_null(new->buf, "io_new() allocates the memory buffer");
		isnt_null(new->tmp, "io_new() determines it's temp space");
		ok(new->fd < 0, "io_new() clears the file descriptor");
		is_unsigned(new->hwm, 257, "io_new() set the high water mark");
		io_free(new);
	}

	subtest {
		int rc;
		struct io init;
		memset(&init, 0, sizeof(init));

		rc = io_init(&init, NULL, 257);
		ok(rc == 0, "io_init() returns zero");
		isnt_null(init.buf, "io_init() allocates the memory buffer");
		isnt_null(init.tmp, "io_init() determines it's temp space");
		ok(init.fd < 0, "io_init() clears the file descriptor");
		is_unsigned(init.hwm, 257, "io_init() set the high water mark");
		io_close(&init);
	}

	subtest {
		struct io *io;

		io = io_new("/var/tmp", 8192);
		if (!io) BAIL_OUT("io_new() returned a NULL pointer!");
		is_string(io->tmp, "/var/tmp/bolo.io.XXXXXXXX", "io_new() sets the mkstemp template");
		io_free(io);
	}

	subtest {
		struct io *io;
		ssize_t n;
		char buf[64];

		io = io_new(NULL, 8);
		if (!io) BAIL_OUT("io_new() returned a NULL pointer!");

		n = io_write(io, "Hello,", 6);
		is_signed(n, 6, "io_write(..., 6) writes 6 bytes");
		is_signed(io->len, 6, "length of stream should be 6 bytes");
		ok(io->fd < 0, "writing 6 bytes to an hwm=8 io stream doesn't create a temp file");

		n = io_write(io, " World!\n", 8);
		is_signed(n, 8, "io_write(..., 8) writes 8 bytes");
		is_signed(io->len, 14, "length of stream should be 14 (6+8) bytes");
		ok(io->fd >= 0, "writing beyond hwm should overflow to temp file");

		n = io_write(io, "EOT\n", 4);
		is_signed(n, 4, "io_write(..., 4) writes 4 bytes");
		is_signed(io->len, 18, "length of stream should be 18 (6+8+4) bytes");
		ok(io->fd >= 0, "writing after overflow should continue to overflow");

		io_rewind(io);
		n = io_read(io, buf, 64);
		is_signed(n, 18, "read 18 octets from io struct");
		buf[n] = '\0';
		is_string(buf, "Hello, World!\nEOT\n", "should io_read() everything we io_wrote()");

		n = io_read(io, buf, 64);
		is_signed(n, 0, "read 0 octets from io struct at 'EOF'");

		io_free(io);
	}

	subtest {
		struct io *io;
		ssize_t n;
		char buf[64];

		io = io_new(NULL, 8);
		if (!io) BAIL_OUT("io_new() returned a NULL pointer!");

		n = io_write(io, "Hello, World!\nEOT\n", 18);
		is_signed(n, 18, "io_write(..., 18) writes 18 bytes");
		is_signed(io->len, 18, "length of stream should be 18 bytes");
		ok(io->fd >= 0, "writing beyond hwm should overflow to temp file");

		io_rewind(io);
		n = io_read(io, buf, 64);
		is_signed(n, 18, "read 18 octets from io struct");
		buf[n] = '\0';
		is_string(buf, "Hello, World!\nEOT\n", "should io_read() everything we io_wrote()");

		n = io_read(io, buf, 64);
		is_signed(n, 0, "read 0 octets from io struct at 'EOF'");

		io_free(io);
	}

	subtest {
		struct io *io;
		ssize_t n;
		char buf[64];

		io = io_new(NULL, 64);
		if (!io) BAIL_OUT("io_new() returned a NULL pointer!");

		n = io_write(io, "Hello,", 6);
		is_signed(n, 6, "io_write(..., 6) writes 6 bytes");
		is_signed(io->len, 6, "length of stream should be 6 bytes");
		ok(io->fd < 0, "writing 6 bytes to an hwm=64 io stream doesn't create a temp file");

		n = io_write(io, " World!\n", 8);
		is_signed(n, 8, "io_write(..., 8) writes 8 bytes");
		is_signed(io->len, 14, "length of stream should be 14 (6+8) bytes");
		ok(io->fd < 0, "writing 14 bytes to an hwm=64 io stream doesn't create a temp file");

		n = io_write(io, "EOT\n", 4);
		is_signed(n, 4, "io_write(..., 4) writes 4 bytes");
		is_signed(io->len, 18, "length of stream should be 18 (6+8+4) bytes");
		ok(io->fd < 0, "writing 18 bytes to an hwm=64 io stream doesn't create a temp file");

		io_rewind(io);
		n = io_read(io, buf, 64);
		is_signed(n, 18, "read 18 octets from io struct");
		buf[n] = '\0';
		is_string(buf, "Hello, World!\nEOT\n", "should io_read() everything we io_wrote()");

		n = io_read(io, buf, 64);
		is_signed(n, 0, "read 0 octets from io struct at 'EOF'");

		io_free(io);
	}

	subtest {
		struct io *io;
		ssize_t n;
		char buf[64];

		io = io_new(NULL, 64);
		if (!io) BAIL_OUT("io_new() returned a NULL pointer!");

		ok(io_copybuf(io, "Hello, World!\n", 14) == 0, "io_copybuf returns 0 on success");
		n = io_read(io, buf, 64);
		is_signed(n, 14, "read 14 octets from io struct");
		buf[n] = '\0';
		is_string(buf, "Hello, World!\n", "should io_read() everything we io_copied()");

		n = io_read(io, buf, 64);
		is_signed(n, 0, "read 0 octets from io struct at 'EOF'");

		io_free(io);
	}

	subtest {
		struct io *io;
		ssize_t n;
		char buf[64];

		io = io_new(NULL, 8);
		if (!io) BAIL_OUT("io_new() returned a NULL pointer!");

		ok(io_copybuf(io, "Hello, World!\n", 14) == 0, "io_copybuf returns 0 on success");
		n = io_read(io, buf, 64);
		is_signed(n, 14, "read 14 octets from io struct");
		buf[n] = '\0';
		is_string(buf, "Hello, World!\n", "should io_read() everything we io_copied()");

		n = io_read(io, buf, 64);
		is_signed(n, 0, "read 0 octets from io struct at 'EOF'");

		io_free(io);
	}

	subtest {
		struct io *io;
		ssize_t n;
		char buf[64];
		int fd;

		fd = memfd("io");
		n = write(fd, "Hello, World!\n", 14);
		if (n < 0 || n != 14)
			BAIL_OUT("failed to set up file descriptor to copyfd from...");
		lseek(fd, 0, SEEK_SET);

		io = io_new(NULL, 64);
		if (!io) BAIL_OUT("io_new() returned a NULL pointer!");

		ok(io_copyfd(io, fd) == 0, "io_copyfd returns 0 on success");
		n = io_read(io, buf, 64);
		is_signed(n, 14, "read 14 octets from io struct");
		buf[n] = '\0';
		is_string(buf, "Hello, World!\n", "should io_read() everything we io_copied()");

		n = io_read(io, buf, 64);
		is_signed(n, 0, "read 0 octets from io struct at 'EOF'");

		io_free(io);
	}

	subtest {
		struct io *io;
		ssize_t n;
		char buf[64];
		int fd;

		fd = memfd("io");
		n = write(fd, "Hello, World!\n", 14);
		if (n < 0 || n != 14)
			BAIL_OUT("failed to set up file descriptor to copyfd from...");
		lseek(fd, 0, SEEK_SET);

		io = io_new(NULL, 8);
		if (!io) BAIL_OUT("io_new() returned a NULL pointer!");

		ok(io_copyfd(io, fd) == 0, "io_copyfd returns 0 on success");
		n = io_read(io, buf, 64);
		is_signed(n, 14, "read 14 octets from io struct");
		buf[n] = '\0';
		is_string(buf, "Hello, World!\n", "should io_read() everything we io_copied()");

		n = io_read(io, buf, 64);
		is_signed(n, 0, "read 0 octets from io struct at 'EOF'");

		io_free(io);
	}

	subtest {
		struct io *io;
		ssize_t n;
		char in[14] = "Hello, World!\n";
		char buf[64];

		io = io_bufnew(in, 14);
		if (!io) BAIL_OUT("io_bufnew() returned a NULL pointer!");

		n = io_read(io, buf, 64);
		is_signed(n, 14, "read 14 octets from io struct");
		buf[n] = '\0';
		is_string(buf, "Hello, World!\n", "should io_read() everything we io_copied()");

		n = io_read(io, buf, 64);
		is_signed(n, 0, "read 0 octets from io struct at 'EOF'");

		io_free(io);
	}
}
/* LCOV_EXCL_STOP */
#endif

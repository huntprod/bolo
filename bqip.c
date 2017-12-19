#include "bqip.h"
#include <ctype.h>

int
bqip_buf_read(struct bqip_buf *b, int fd)
{
	ssize_t nread;

	nread = read(fd, b->data + b->len, BQIP_BUFSIZ - b->len);
	if (nread == 0) return  0; /* EOF */
	if (nread <  0) return -1; /* ERR */

	b->len += (size_t)nread;
	return 1; /* OK */
}

size_t
bqip_buf_copy(struct bqip_buf *b, const char *s, size_t len)
{
	size_t ncopied = len;
	if (len > BQIP_BUFSIZ || b->len + len > BQIP_BUFSIZ)
		ncopied = BQIP_BUFSIZ - b->len;

	memcpy(b->data, s, ncopied);
	b->len += ncopied;
	return ncopied;
}

int
bqip_buf_write(struct bqip_buf *b, int fd)
{
	ssize_t nwrit;

	nwrit = write(fd, b->data, b->len);
	if (nwrit == 0) return  0; /* EOF */
	if (nwrit <  0) return -1; /* ERR */

	b->len -= (size_t)nwrit;
	memmove(b->data, b->data + nwrit, b->len);
	return 1; /* OK */
}

int
bqip_buf_skip(struct bqip_buf *b, size_t skip)
{
	errno = EINVAL;
	if (skip > b->len) return -1; /* ERR */

	if (skip != b->len)
		memmove(b->data, b->data + skip, b->len - skip);

	b->len -= skip;
	return 0; /* OK */
}

int
bqip_buf_streamout(struct bqip_buf *b, int fd, const char *s, size_t len)
{
	size_t n;

	while (len > 0) {
		n = bqip_buf_copy(b, s, len);
		if (n <= 0) return -1; /* ERR */
		while (b->len > 0) {
			if (bqip_buf_write(b, fd) <= 0)
				return -1; /* ERR */
		}

		s   += n;
		len -= n;
	}

	return 0; /* OK */
}

void
bqip_init(struct bqip *c, int fd)
{
	free(c->request.payload);
	memset(c, 0, sizeof(*c));
	c->fd = fd;
}

void
bqip_deinit(struct bqip *c)
{
	free(c->request.payload);
}

int
bqip_read(struct bqip *c)
{
	int i, len;

	if (bqip_buf_read(&c->rcvbuf, c->fd) != 1)
		return -1; /* ERR */

	if (!c->request.payload) {
		/* "phase 1" parsing */
		if (c->rcvbuf.len < 5)
			return 1; /* WAIT */

		i = 0;
		if (c->rcvbuf.data[i] != 'Q')
			return -1; /* ERR */

		i++;
		if (c->rcvbuf.data[i] != '|')
			return -1; /* ERR */

		for (len = 0, i++; (size_t)i < c->rcvbuf.len; i++) {
			if (c->rcvbuf.data[i] == '|')
				break;

			if (!isdigit(c->rcvbuf.data[i]))
				return -1; /* ERR */

			/* FIXME: detect overflow */
			len = len * 10 + (c->rcvbuf.data[i] - '0');
		}

		if (c->rcvbuf.data[i] != '|')
			return 1; /* WAIT */
		i++;

		c->request.dot = 0;
		c->request.len = len;
		c->request.payload = malloc(len + 1);
		if (!c->request.payload)
			return -1; /* ERR */

		if (bqip_buf_skip(&c->rcvbuf, i) != 0)
			return -1; /* ERR */
	}

	i = 0;
	len = c->request.len - c->request.dot;
	if ((size_t)len > c->rcvbuf.len)
		len = c->rcvbuf.len;

	if (len > 0) {
		memcpy(c->request.payload + c->request.dot, c->rcvbuf.data, len);
		if (bqip_buf_skip(&c->rcvbuf, len) != 0)
			return -1; /* ERR */

		c->request.dot += len;
	}

	if (c->request.dot != c->request.len)
		return 1; /* WAIT */

	c->request.payload[c->request.len] = '\0';
	return 0; /* DONE */
}

int
bqip_send_error(struct bqip *c, const char *e)
{
	char pre[32];
	int len, n;

	len = strlen(e);

	errno = EINVAL;
	n = snprintf(pre, 32, "E|%d|", len);
	if (n < 4 || n > 32) return -1; /* ERR */

	if (bqip_buf_streamout(&c->sndbuf, c->fd, pre,  n)   != 0) return -1;
	if (bqip_buf_streamout(&c->sndbuf, c->fd, e,    len) != 0) return -1;
	if (bqip_buf_streamout(&c->sndbuf, c->fd, "\n", 1)   != 0) return -1;
	return 0;
}

int
bqip_send_result(struct bqip *c, int nsets)
{
	char reply[32];
	int n;

	errno = EINVAL;
	n = snprintf(reply, 32, "R|%d\n", nsets);
	if (n < 4 || n > 32) return -1; /* ERR */

	if (bqip_buf_streamout(&c->sndbuf, c->fd, reply, n) != 0) return -1;
	return 0;
}

#define TUPLE_LEN (13 + 1 + 10 + 1)
#define TUPLE_FMT "%013lu:%010e"
int bqip_send_set(struct bqip *c, int ntuples, const char *key)
{
	char pre[64];
	int n, len;

	errno = EINVAL;
	len = strlen(key) + 1 + ntuples * (TUPLE_LEN + 1) - 1;
	n = snprintf(pre, 64, "S|%d|%d|", ntuples, len);
	if (n < 6 || n > 64) return -1; /* ERR */

	len = strlen(key);
	if (bqip_buf_streamout(&c->sndbuf, c->fd, pre, n)   != 0) return -1;
	if (bqip_buf_streamout(&c->sndbuf, c->fd, key, len) != 0) return -1;
	if (bqip_buf_streamout(&c->sndbuf, c->fd, "=", 1)   != 0) return -1;
	return 0;
}

int bqip_send_tuple(struct bqip *c, struct result *r, int first)
{
	char buf[TUPLE_LEN+2], *p;

	p = buf;
	if (!first) *p++ = ',';

	if (snprintf(p, TUPLE_LEN+1, TUPLE_FMT, r->start, r->value) != TUPLE_LEN+1)
		/*return -1;*/fprintf(stderr, "got wrong bytes count for [%s]\n", buf);

	fprintf(stderr, "buf[%s]\n", buf);
	if (bqip_buf_streamout(&c->sndbuf, c->fd, buf, (p-buf)+TUPLE_LEN) != 0)
		return -1;
	return 0;
}

int bqip_flush(struct bqip *c)
{
	if (bqip_buf_streamout(&c->sndbuf, c->fd, "\n", 1) != 0)
		return -1;
	return 0;
}

#ifdef TEST
/* LCOV_EXCL_START */
#define put(fd,s) write((fd), (s), strlen(s))

static inline
void get(char *buf, size_t len, int fd)
{
	ssize_t n;
	n = read(fd, buf, len - 1);
	if (n < 1 || (size_t)n > len - 1)
		BAIL_OUT("read from memfd (to get error) failed!");
	buf[n] = '\0';
}

TESTS {
	int rc, fd;
	struct bqip b;
	char buf[8192];

	fd = memfd("bqip");
	#define reset() do { \
		memset(buf, 0, sizeof(buf)); \
\
		fd = memfd("bqip"); \
\
		memset(&b, 0, sizeof(b)); \
		bqip_init(&b, fd); \
} while (0)

	subtest {
		reset();

		rc = bqip_send_error(&b, "oops");
		is_signed(rc, 0, "bqip_send_error() succeeds");

		lseek(fd, 0, SEEK_SET);
		get(buf, 8192, fd);
		is_string(buf, "E|4|oops\n", "bqip_send_error() encodes properly");

		close(fd);
		bqip_deinit(&b);
	}

	subtest {
		reset();

		rc = bqip_send_result(&b, 14);
		is_signed(rc, 0, "bqip_send_result() succeeds");

		lseek(fd, 0, SEEK_SET);
		get(buf, 8192, fd);
		is_string(buf, "R|14\n", "bqip_send_result() encodes properly");

		close(fd);
		bqip_deinit(&b);
	}

	subtest {
		struct result r;
		reset();

		ok(bqip_send_set(&b, 3, "host") == 0, "bqip_send_set() succeeds");

		r.start = 1; r.value = 1.0;
		ok(bqip_send_tuple(&b, &r, 1) == 0, "bqip_send_tuple() succeeds");

		r.start = 2; r.value = 2.0;
		ok(bqip_send_tuple(&b, &r, 0) == 0, "bqip_send_tuple() succeeds");

		r.start = 3; r.value = 3.0;
		ok(bqip_send_tuple(&b, &r, 0) == 0, "bqip_send_tuple() succeeds");
		ok(bqip_flush(&b) == 0, "bqip_flush() succeeds");

		lseek(fd, 0, SEEK_SET);
		get(buf, 8192, fd);
		is_string(buf,
			"S|3|73|host=0000000001:1.000000e+0,0000000002:2.000000e+0,0000000003:3.000000e+0\n",
			"bqip_send_set() encodes properly");

		close(fd);
		bqip_deinit(&b);
	}

	subtest {
		reset();
		put(fd, "Q|30|SELECT cpu FROM host=localhost\n");
		lseek(fd, 0, SEEK_SET);

		while ((rc = bqip_read(&b)) == 1)
			;
		is_signed(rc, 0, "bqip_read() should succeed");
		is_string(b.request.payload, "SELECT cpu FROM host=localhost",
				"query is interpreted properly");

		close(fd);
		bqip_deinit(&b);
	}

	subtest {
		reset();
		put(fd, "X||\n");
		lseek(fd, 0, SEEK_SET);

		while ((rc = bqip_read(&b)) == 1)
			;
		is_signed(rc, -1, "bqip_read() should fail");

		close(fd);
		bqip_deinit(&b);
	}
}
/* LCOV_EXCL_STOP */
#endif

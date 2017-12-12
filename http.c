#include "bolo.h"
#include <stdarg.h>

#define HTTP_UNRECOGNIZED  0
#define HTTP_0_9           1
#define HTTP_1_0           2
#define HTTP_1_1           3

#define S_INIT             0
#define S_METHOD           1
#define S_EXPECT_URI       2
#define S_URI              3
#define S_VERSION          4
#define S_HEADER_INIT1     5
#define S_HEADER_INIT      6
#define S_HEADER_FIELD     7
#define S_HEADER_SEP       8
#define S_HEADER_VALUE     9
#define S_HEADER_CONT1     10
#define S_HEADER_CONT      11
#define S_BODY1            12
#define S_BAD_REQUEST      13
#define S_BODY             14
#define S_READY            15

static int
is_token(unsigned char c)
{
	switch (c) {
	case '(': case ')': case '<': case '>':
	case '[': case ']': case '{': case '}':
	case '@': case ',': case ';': case ':':
	case '\\': case '\"': case '/': case '?':
	case '=': case ' ': case '\t':
		return 0;
	default:
		return c < 127 && c > 31;
	}
}

static void
hexdump(char *data, size_t len)
{
	size_t i;
	/* format: "XXXX | XX XX XX ... XX\n\0" */
	char *p, line[7 + 16 * 3];
	static char *hex = "0123456789abcdef";

	for (i = 0; i < len; i++) {
		if (i % 16 == 0) {
			if (i != 0) fprintf(stderr, "%s\n", line);
			memset(p = line, 0, sizeof(len));
			*p++ = hex[(i & 0xf000) >> 12];
			*p++ = hex[(i & 0x0f00) >>  8];
			*p++ = hex[(i & 0x00f0) >>  4];
			*p++ = hex[(i & 0x000f)];
			*p++ = ' '; *p++ = '|';
		}

		*p++ = ' ';
		*p++ = hex[(data[i] & 0xf0) >> 4];
		*p++ = hex[(data[i] & 0x0f)];
		*p = '\0';
	}
	if (len % 16 != 0) fprintf(stderr, "%s\n", line);
}

/***********************************************/

void
http_conn_init(struct http_conn *c, int fd)
{
	c->fd = fd;

	/* (re-)initialize the HTTP reply */
	hash_free(c->rep.headers);
	c->rep.headers = hash_new();
	c->pos = 0;

	/* (re-)initialize the HTTP request */
	c->req.method = c->req.uri = NULL;
	hash_free(c->req.headers);
	c->req.headers = hash_new();
	c->req.state = S_INIT;

	if (!c->req.body) c->req.body = io_new(NULL, 65535);
	io_reinit(c->req.body);

	/* carry raw.cap and raw.data over,
	   to avoid thrashing the mmu */
	c->req.raw.dot = 0;
	c->req.raw.len = 0;

	/* initialize raw.*, in case this is
	   our first go-round with this connection */
	if (c->req.raw.cap == 0) {
		c->req.raw.cap = 8192;
		c->req.raw.data = malloc(c->req.raw.cap);
		if (!c->req.raw.data)
			bail("malloc failed");
	}
}


int
http_conn_read(struct http_conn *c)
{
	ssize_t nread;
	char *tmp, x, *k, *v;
	size_t i, lstart, skip, hstart, hstop, dot;
	int state;

	nread = read(c->fd, c->buf, sizeof(c->buf));
	if (nread < 0)  return nread;
	if (nread == 0) return -1;
	fprintf(stderr, "S: read %li bytes from %d\n", nread, c->fd);

	/* sizeof(c->buf) means we only ever
	   have to extend c->data once */
	if ((size_t)nread > c->req.raw.cap - c->req.raw.len) {
		tmp = realloc(c->req.raw.data, c->req.raw.cap * 2);
		if (!tmp) return -1;
		c->req.raw.data = tmp;
		c->req.raw.cap *= 2;
	}

	memcpy(c->req.raw.data + c->req.raw.len, c->buf, nread);
	c->req.raw.len += nread;

	skip = 0;
	state = c->req.state;
	dot = lstart = i = c->req.raw.dot;
	fprintf(stderr, "S: parsing; state = %i, buffer is at [%li] of %li/%li octets\n",
			state, i, c->req.raw.len, c->req.raw.cap);
	hexdump(c->req.raw.data, c->req.raw.len);
	while (state != S_BODY && i < c->req.raw.len) {
		x = c->req.raw.data[i++];
		switch (state) {
		default: goto error;
		case S_INIT:
			if (is_token(x)) {
				lstart = i - 1;
				fprintf(stderr, "S: transitioning S_INIT -> S_METHOD\n");
				state = S_METHOD;
				continue;
			}
			goto error;
		case S_METHOD:
			if (is_token(x))
				continue;
			if (x == ' ') {
				fprintf(stderr, "S: transitioning S_METHOD -> S_EXPECT_URI\n");

				/* snag the request method */
				c->req.method = c->req.raw.data + lstart;
				c->req.raw.data[i-1] = '\0';
				     if (strcmp(c->req.method, "GET")     == 0) c->req.known_method = HTTP_GET;
				else if (strcmp(c->req.method, "POST")    == 0) c->req.known_method = HTTP_POST;
				else if (strcmp(c->req.method, "PUT")     == 0) c->req.known_method = HTTP_PUT;
				else if (strcmp(c->req.method, "PATCH")   == 0) c->req.known_method = HTTP_PATCH;
				else if (strcmp(c->req.method, "DELETE")  == 0) c->req.known_method = HTTP_DELETE;
				else if (strcmp(c->req.method, "HEAD")    == 0) c->req.known_method = HTTP_HEAD;
				else if (strcmp(c->req.method, "OPTIONS") == 0) c->req.known_method = HTTP_OPTIONS;

				c->req.state = state = S_EXPECT_URI;
				dot = i;
				continue;
			}
			goto error;

		case S_EXPECT_URI:
			if (x != ' ') {
				lstart = i - 1;
				fprintf(stderr, "S: transitioning S_EXPECT_URI -> S_URI\n");
				state = S_URI;
				continue;
			}
			goto error;

		case S_URI:
			if (x == ' ') {
				fprintf(stderr, "S: transitioning S_URI -> S_VERSION\n");

				/* snag the URI */
				c->req.uri = c->req.raw.data + lstart;
				c->req.raw.data[i-1] = '\0';

				c->req.state = state = S_VERSION;
				dot = lstart = i;
			}
			continue;

		case S_VERSION:
			switch (x) {
			case 'H': case 'T': case 'P': case '/':
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
			case '8': case '9': case '.':
				continue;
			case '\r':
			case '\n':
				fprintf(stderr, "S: transitioning S_VERSION -> S_HEADER_INIT1\n");
				fprintf(stderr, "S: (detected version '%.*s')\n", (int)(i - lstart - 1), c->req.raw.data + lstart);
				if (memcmp(c->req.raw.data + lstart, "HTTP/0.9", i - lstart - 1) == 0) {
					c->req.protocol = HTTP_0_9;
				} else if (memcmp(c->req.raw.data + lstart, "HTTP/1.0", i - lstart - 1) == 0) {
					c->req.protocol = HTTP_1_0;
				} else if (memcmp(c->req.raw.data + lstart, "HTTP/1.1", i - lstart - 1) == 0) {
					c->req.protocol = HTTP_1_1;
				} else {
					fprintf(stderr, "S: unrecognized protocol...\n");
					c->req.protocol = HTTP_UNRECOGNIZED;
				}
				state = (x == '\r' ? S_HEADER_INIT1 : S_HEADER_INIT);
				continue;
			}
			goto error;

		case S_HEADER_INIT1:
			if (x != '\n') goto error;
			fprintf(stderr, "S: transitioning S_HEADER_INIT1 -> S_HEADER_INIT\n");
			c->req.state = state = S_HEADER_INIT;;
			dot = i;
			continue;

		case S_HEADER_INIT:
			if (x == '\r' || x == '\n') {
				fprintf(stderr, "S: transitioning S_HEADER_INIT -> S_BODY%s\n", x == '\r' ? "1" : "");
				c->req.state = state = (x == '\r' ? S_BODY1 : S_BODY);
				dot = i;
				continue;
			}
			if (is_token(x)) {
				fprintf(stderr, "S: transitioning S_HEADER_INIT -> S_HEADER_FIELD\n");
				c->req.state = state = S_HEADER_FIELD;
				dot = lstart = i - 1;
				continue;
			}
			goto error;

		case S_HEADER_FIELD:
			if (is_token(x)) continue;
			if (x == ':') {
				fprintf(stderr, "S: (detected header field '%.*s')\n", (int)(i - lstart - 1), c->req.raw.data + lstart);
				fprintf(stderr, "S: transitioning S_HEADER_FIELD -> S_HEADER_SEP\n");
				hstart = lstart; hstop = i - 1;
				state = S_HEADER_SEP;
				continue;
			}
			goto error;

		case S_HEADER_SEP:
			if (x == ' ') continue;
			if (x != '\r' && x != '\n') {
				fprintf(stderr, "S: transitioning S_HEADER_SEP -> S_HEADER_VALUE\n");
				lstart = i - 1;
				state = S_HEADER_VALUE;
				continue;
			}
			goto error;

		case S_HEADER_VALUE:
			switch (x) {
			default: continue;
			case '\r':
			case '\n':
				fprintf(stderr, "S: transitioning S_HEADER_VALUE -> S_HEADER_CONT%s\n", x == '\r' ? "1" : "");
				skip = 1;
				state = (x == '\r' ? S_HEADER_CONT1 : S_HEADER_CONT);
				continue;
			}
			goto error;

		case S_HEADER_CONT1:
			if (x != '\n') goto error;
			fprintf(stderr, "S: transitioning S_HEADER_CONT1 -> S_HEADER_CONT\n");
			skip++;
			state = S_HEADER_CONT;
			continue;

		case S_HEADER_CONT:
			switch (x) {
			case ' ':
			case '\t':
				fprintf(stderr, "S: folding header value (skipping %li bytes)\n", skip);
				c->req.raw.data[i-1] = ' ';
				fprintf(stderr, "S: current len = %li; current i = %li\n", c->req.raw.len, i);
				fprintf(stderr, "S: overwriting %p with %p (%li octets)\n",
					(void *)(c->req.raw.data + i - 1 - skip),
					(void *)(c->req.raw.data + i - 1),
					c->req.raw.len - i + 1);
				fprintf(stderr, "S: @%p is %02x\n",
						(void *)(c->req.raw.data + i - 1 - skip),
						c->req.raw.data[i - 1 - skip]);
				fprintf(stderr, "S: @%p is %02x\n",
						(void *)(c->req.raw.data + i - 1),
						c->req.raw.data[i - 1]);
				memmove(c->req.raw.data + i - 1 - skip, c->req.raw.data + i - 1,
					c->req.raw.len - i + 1);
				c->req.raw.len -= skip;
				i              -= skip;
				fprintf(stderr, "S: folded header value (skipping %li bytes)\n", skip);
				fprintf(stderr, "S: raw len is now %li; i is now %li\n", c->req.raw.len, i);

				fprintf(stderr, "S: transitioning S_HEADER_CONT -> S_HEADER_VALUE\n");
				state = S_HEADER_VALUE;
				continue;

			case '\r':
			case '\n':
				fprintf(stderr, "S: transitioning S_HEADER_CONT -> S_BODY%s\n", x == '\r' ? "1" : "");
				c->req.raw.data[hstop]        = '\0'; /* null-terminate the header field */
				c->req.raw.data[i - skip - 1] = '\0'; /* null-terminate the header value */
				fprintf(stderr, "S: (detected final header '%s' => '%s')\n",
					c->req.raw.data + hstart, c->req.raw.data + lstart);
				hash_set(c->req.headers, c->req.raw.data + hstart,
				                         c->req.raw.data + lstart);

				c->req.state = state = (x == '\r' ? S_BODY1 : S_BODY);
				dot = i;
				continue;

			default:
				if (!is_token(x)) goto error;
				fprintf(stderr, "S: transitioning S_HEADER_CONT -> S_HEADER_FIELD\n");

				c->req.raw.data[hstop]        = '\0'; /* null-terminate the header field */
				c->req.raw.data[i - skip - 1] = '\0'; /* null-terminate the header value */
				fprintf(stderr, "S: (detected final header '%s' => '%s')\n",
					c->req.raw.data + hstart, c->req.raw.data + lstart);
				hash_set(c->req.headers, c->req.raw.data + hstart,
				                         c->req.raw.data + lstart);

				c->req.state = state = S_HEADER_FIELD;
				dot = lstart = i - 1;
				continue;
			}
			goto error;

		case S_BODY1:
			if (x != '\n') goto error;
			fprintf(stderr, "S: transitioning S_BODY1 -> S_BODY\n");
			c->req.state = state = S_BODY;
			dot = i;
			continue;
		}
	}
	if (state == S_BODY) {
		/* FIXME: doesn't handle TE yet... */
		/* read from the fd until we get to Content-Length */

		if (c->req.content_length == 0) {
			int was_errno;
			const char *v;
			char *err;

			fprintf(stderr, "S: detecting content-length of request...\n");
			if (hash_get(c->req.headers, &v, "Content-Length") != 0) {
				/* no body */
				fprintf(stderr, "S: NO content-length found; assuming no body and a READY connection.\n");
				c->req.state = S_READY;
				return 0;
			}

			fprintf(stderr, "S: detected content-length as '%s'; parsing as an integer.\n", v);
			was_errno = errno; errno = 0;
			c->req.content_length = strtol(v, &err, 10);
			if (errno != 0 || *err) {
				fprintf(stderr, "S: content-length of '%s' is malformed; this is a bad request.\n", v);
				c->req.state = S_BAD_REQUEST;
				return 0;
			}
			errno = was_errno;
			fprintf(stderr, "S: parsed content-length as %lu\n", c->req.content_length);
		}

		if (c->req.content_length > 0) {
			off_t sofar;
			size_t len;

			sofar = io_seek(c->req.body, 0, IO_SEEK_CUR);
			fprintf(stderr, "S: have read %li/%li octets, so far\n", sofar, c->req.content_length);
			if (sofar == (off_t)c->req.content_length) {
				c->req.state = S_READY;
				return 0;
			}
			if (sofar < 0) {
				c->req.state = S_BAD_REQUEST;
				return 0;
			}

			/* copy the rest of the buffer into the body io */
			len = c->req.raw.len - dot;
			if ((c->req.content_length - sofar) < len)
				len = c->req.content_length - sofar;

			fprintf(stderr, "S: copying %li octets from raw buffer into req.body\n", len);
			if (io_catbuf(c->req.body, c->req.raw.data + dot, len) != 0) {
				c->req.state = S_BAD_REQUEST;
				return 0;
			}
			c->req.raw.dot = c->req.raw.len;

			if (sofar + len == c->req.content_length) {
				c->req.state = S_READY;
				return 0;
			}
		}

		/* io_read the remainder of content-length into the body io */
		/* What we need:
		    - how much have we read?  (io len)
		    - to differentiate EOF from EINTR
		 */
	} else {
		c->req.raw.dot = dot;
	}

	fprintf(stderr, "S: HEADERS so far:\n");
	k = v = NULL;
	hash_each(c->req.headers, &k, &v) {
		fprintf(stderr, "S:    |%s:%s|\n", k, v);
	}

	return 0;

error:
	fprintf(stderr, "S: ERROR transition activated at [%li] from %d on %02x; BAD REQUEST!\n",
		i-1, state, x);
	c->req.state = S_BAD_REQUEST;
	return 0;
}

int
http_conn_ready(struct http_conn *c)
{
	return c->req.state == S_READY;
}

void
http_conn_write(struct http_conn *c, const void *buf, int len)
{
	int i, n;

	for (i = 0; i < len;) {
		n = MIN(8192 - c->pos, len - i);
		memcpy(c->buf + c->pos, (char *)buf + i, n);
		c->pos += n;
		i += n;

		if (c->pos == 8192)
			http_conn_flush(c);
	}
}

void
http_conn_write0(struct http_conn *c, const char *s)
{
	http_conn_write(c, s, strlen(s));
}

void
http_conn_printf(struct http_conn *c, const char *fmt, ...)
{
	va_list ap1, ap2;
	char fixed[1024], *buf;
	ssize_t n;

	buf = NULL;
	va_start(ap1, fmt);
	n = vsnprintf(fixed, 1024, fmt, ap1);
	va_end(ap1);

	/* static buffer was too small */
	if (n >= 1024) {
		buf = malloc(n + 1);
		if (!buf)
			bail("malloc failed");

		va_start(ap2, fmt);
		vsnprintf(buf, n, fmt, ap2);
		va_end(ap2);
	}

	http_conn_write0(c, buf ? buf : fixed);
	if (buf)
		free(buf);
}

void
http_conn_flush(struct http_conn *c)
{
	ssize_t n;
	size_t i;

	i = 0;
	while (c->pos) {
		n = write(c->fd, c->buf + i, c->pos);
		if (n <= 0) {
			c->pos = 0;
			return;
		}
		i      += n;
		c->pos -= n;
	}
}

void
http_conn_set_header(struct http_conn *c, const char *header, const char *value)
{
	hash_set(c->rep.headers, header, strdup(value));
}

const char *
http_conn_get_header(struct http_conn *c, const char *header)
{
	const char *v;
	if (hash_get(c->rep.headers, &v, header) != 0)
		return NULL;
	return v;
}

int
http_conn_reply(struct http_conn *c, int status)
{
	char *header, *value;

	if (c->rep.protocol == HTTP_UNRECOGNIZED)
		c->rep.protocol = c->req.protocol;

	switch(c->rep.protocol) {
	case HTTP_0_9: http_conn_printf(c, "HTTP/0.9 %d %s\r\n", status, ""); break;
	case HTTP_1_0: http_conn_printf(c, "HTTP/1.0 %d %s\r\n", status, ""); break;
	default:
	case HTTP_1_1: http_conn_printf(c, "HTTP/1.1 %d %s\r\n", status, ""); break;
	}

	hash_each(c->rep.headers, &header, &value) {
		http_conn_printf(c, "%s: %s\r\n", header, value);
	}
	http_conn_printf(c, "\r\n");
	http_conn_flush(c);
	return 0;
}

int
http_conn_replyio(struct http_conn *c, int status, struct io *io)
{
	char cl[10], buf[8192];
	ssize_t len;

	len = io_seek(io, 0, IO_SEEK_END);
	fprintf(stderr, "io to reply from is %li octets in length; setting content-length...\n", len);
	if (snprintf(cl, 10, "%lu", len) > 10)
		return 1;
	http_conn_set_header(c, "Content-Length", cl);
	http_conn_reply(c, status);

	/* FIXME: i think this blocks */
	io_seek(io, 0, IO_SEEK_SET);
	while ((len = io_read(io, buf, 8192)) > 0)
		http_conn_write(c, buf, len);
	return 0;
}

void
http_mux_route(struct http_mux *mux, const char *prefix, http_handler handler)
{
	if (mux->nroutes % 256 == 0) {
		mux->routes = realloc(mux->routes, sizeof(struct http_route) * (256 + sizeof(mux->routes)));
		if (!mux->routes)
			bail("realloc failed");
	}

	mux->routes[mux->nroutes].prefix  = strdup(prefix);
	mux->routes[mux->nroutes].handler = handler;
	mux->routes[mux->nroutes].open    = *(prefix + strlen(prefix) - 1) == '/';
	mux->nroutes++;
}

static struct http_route *
s_http_find_route(struct http_mux *mux, const char *uri)
{
	int i;
	const char *p;
	int matches; /* how many prefix matches are still viable */
	int *pos;    /* list of positions into the prefix pattern.
	                indexed parallel with mux->routes.
	                a value of -1 means "no longer matches" */

	matches = mux->nroutes;
	pos = calloc(mux->nroutes, sizeof(int));
	if (!pos)
		bail("calloc failed");

	fprintf(stderr, "S: dispatching uri '%s'\n", uri);
	for (p = uri; matches > 0 && *p; p++) {
		for (i = 0; i < mux->nroutes; i++) {
			if (pos[i] < 0) continue;
			switch (*p) {
			case '/':
				switch (mux->routes[i].prefix[pos[i]]) {
				case '*': pos[i]++;
				case '/': pos[i]++;
				          break;

				case '\0': if (mux->routes[i].open) break;
					/* fallthrough */
				default:
					/* no match */
					pos[i] = -1;
					matches--;
					break;
				}
				break;

			default:
				if (mux->routes[i].prefix[pos[i]] == '\0' && mux->routes[i].open)
					break; /* open match */
				if (mux->routes[i].prefix[pos[i]] == '*')
					break; /* widcard match */
				if (mux->routes[i].prefix[pos[i]] == *p) {
					pos[i]++;
					break; /* literal match */
				}
				/* no match */
				pos[i] = -1;
				matches--;
				break;
			}
		}
	}
	for (i = mux->nroutes - 1; i >= 0; i--) {
		if (pos[i] < 0) continue;
		if (mux->routes[i].prefix[pos[i]] == '*') pos[i]++;
		if (mux->routes[i].prefix[pos[i]] == '\0')
			return &(mux->routes[i]);
	}

	/* 404 */
	return NULL;
}

int
http_dispatch(struct http_mux *mux, struct http_conn *c)
{
	int rc;
	struct http_route *route;

	route = s_http_find_route(mux, c->req.uri);
	if (route) {
		http_conn_set_header(c, "Server", "bolo/1");
		rc = route->handler(c);
		http_conn_flush(c);
		return rc;
	}

	/* reply 404 */
	http_conn_write0(c, "HTTP/1.1 404 Not Found\r\n"
	                    "Server: bolo/1\r\n"
	                    "Content-Length: 16\r\n"
	                    "Connection: close\r\n"
	                    "\r\n"
	                    "404 not found.\r\n");
	http_conn_flush(c);
	return 0;
}

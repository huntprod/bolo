#include "bolo.h"

int
ingest_eof(struct ingestor *in)
{
	return in->eof > 0;
}

int
ingest_read(struct ingestor *in)
{
	ssize_t nread;
	size_t i;
	int n;

	nread = read(in->fd, in->buf + in->len, INGEST_BUF_SIZE - in->len);
	if (nread < 0)
		return -1;
	if (nread == 0)
		in->eof = 1;

	in->len += (size_t)nread;
	for (n = i = 0; i < in->len; i++)
		if (in->buf[i] == '\n')
			n++;

	/* return how many newlines (and how many submissions) */
	return n;
}

int
ingest(struct ingestor *in)
{
	char *p, *time, *value;
	char *end, **next;
	int done;

	if (in->last) {
		//debugf("ziping [%s] to just [%s]\n", in->buf, in->last);
		in->len -= (in->last - in->buf);
		memmove(in->buf, in->last, in->len);
		in->buf[in->len] = '\0';
	}

	in->last = time = value = in->tags = NULL;
	in->metric = in->buf;

	done = 0;
	next = &in->tags;
	for (p = in->metric; *p; p++) {
		if (*p == ' ' || *p == '\n') {
			done = (*p == '\n');
			*p++ = ((next == &in->tags) ? '|' : '\0');
			if (next) *next = p;

			     if (done)              break;
			else if (next == &in->tags) next = &time;
			else if (next == &time)     next = &value;
			else if (next == &value)    next = NULL;
			else                        break;
		}
	}

	if (!in->metric || !in->tags || !time || !value) {
		debugf("malformed subsmission packet [%s]", in->buf);
		goto fail;
	}

	if (tags_valid(in->tags) != 0
	 || tags_canonicalize(in->tags) != 0) {
		debugf("failed to parse [%s] tags '%s'", in->metric, in->tags);
		goto fail;
	}

	in->time = strtoull(time, &end, 10);
	if (end && *end) {
		debugf("failed to parse [%s %s] timestamp '%s'", in->metric, in->tags, time);
		goto fail;
	}

	in->value = strtod(value, &end);
	if (end && *end) {
		debugf("failed to parse [%s %s] measurement value '%s'", in->metric, in->tags, value);
		goto fail;
	}

	in->last = p;
	return 0;

fail:
	errno = EINVAL;
	return -1;
}

#ifdef TEST
/* LCOV_EXCL_START */
#define put(fd,s) write((fd), (s), strlen(s))

TESTS {
	subtest {
		struct ingestor in;

		memset(&in, 0, sizeof(in));
		in.fd = memfd("ingest");
		put(in.fd, "cpu host=localhost,env=dev,os=linux 123456789 34.567\n");
		lseek(in.fd, 0, SEEK_SET);

		ok(ingest_read(&in) == 1, "ingest_read() should find exactly one packet.");
		ok(ingest(&in) == 0, "ingest() should succeed.");
		is_string(in.metric, "cpu|env=dev,host=localhost,os=linux",
			"ingest() should set the metric to `name|canon(tags)`.");
		is_unsigned(in.time, 123456789, "ingest() should parse timestamp.");
		is_within(in.value, 34.567, 0.000001, "ingest() should parse value.");

		close(in.fd);
	}

	subtest {
		struct ingestor in;

#define shouldfail(s) do {\
	memset(&in, 0, sizeof(in)); \
	in.fd = memfd("ingest"); \
	put(in.fd, s); \
	lseek(in.fd, 0, SEEK_SET); \
\
	ok(ingest_read(&in) == 1, "ingest_read(\"" s "\") should find exactly one packet."); \
	ok(ingest(&in) == -1, "ingest(\"" s "\") should fail."); \
	close(in.fd); \
} while (0)

		shouldfail("too-short\n");
		shouldfail("cpu badtag 100 3.5\n");
		shouldfail("cpu a=b not-a-timestamp 3.5\n");
		shouldfail("cpu a=b 12345 not-a-float\n");
		shouldfail("cpu a=b 1234.5 45.1\n");

#undef shouldfail
	}

	subtest {
		struct ingestor in;

		memset(&in, 0, sizeof(in));
		in.fd = memfd("ingest");
		put(in.fd, "cpu a=b 123456789 34.567\ncpu a=b 123456790 34.887\n");
		lseek(in.fd, 0, SEEK_SET);

		ok(ingest_read(&in) == 2, "ingest_read() should find two packets.");
		ok(ingest(&in) == 0, "ingest() should succeed.");
		is_string(in.metric, "cpu|a=b", "ingest() should set the metric to `name|canon(tags)`.");
		is_unsigned(in.time, 123456789, "ingest() should parse timestamp.");
		is_within(in.value, 34.567, 0.000001, "ingest() should parse value.");

		ok(ingest(&in) == 0, "ingest() should succeed again.");
		is_string(in.metric, "cpu|a=b", "ingest() should set the metric to `name|canon(tags)`.");
		is_unsigned(in.time, 123456790, "ingest() should parse timestamp.");
		is_within(in.value, 34.887, 0.000001, "ingest() should parse value.");

		ok(ingest(&in) == -1, "ingest() should fail (too many calls; no buffer left).");

		close(in.fd);
	}

	subtest {
		struct ingestor in;

		memset(&in, 0, sizeof(in));
		in.fd = memfd("ingest");              /* this one is a partial */
		put(in.fd, "cpu a=b 123456789 34.567\ncpu a=b 123456790 34.887");
		lseek(in.fd, 0, SEEK_SET);

		ok(ingest_read(&in) == 1, "ingest_read() should find one complete packet.");
		ok(ingest(&in) == 0, "ingest() should succeed.");
		is_string(in.metric, "cpu|a=b", "ingest() should set the metric to `name|canon(tags)`.");
		is_unsigned(in.time, 123456789, "ingest() should parse timestamp.");
		is_within(in.value, 34.567, 0.000001, "ingest() should parse value.");

		close(in.fd);
	}
}
/* LCOV_EXCL_STOP */
#endif

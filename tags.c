#include "bolo.h"

/* FIXME investigate lookup tables for tag validation */
static inline int
s_iskeystart(char c)
{
	return ((c >= 'a' && c <= 'z')
	     || (c >= 'A' && c <= 'Z'));
}

static inline int
s_iskey(char c)
{
	return ((c >= 'a' && c <= 'z')
	     || (c >= 'A' && c <= 'Z')
	     || (c >= '0' && c <= '9')
	     || (c == '_')
	     || (c == '-')
	     || (c == '.')
	     || (c == ':')
	     || (c == '%')
	     || (c == '@'));
}

static inline int
s_isvalue(char c)
{
	return c != ',';
}

static inline int
s_iskvsep(char c)
{
	return c == '=';
}

#define STATE_BEFORE  1
#define STATE_KSTART  2
#define STATE_KEY     3
#define STATE_KVSEP   4
#define STATE_VALUE   5

/* "tag=value,tag=value"
   ^^^  ^    ^^^  ^
   |||  |    |||  |
   |||  |    |||  `---- STATE_VALUE
   |||  |    ||`------- STATE_KEY
   |||  |    |`-------- STATE_KSTART
   |||  |    `--------- STATE_BEFORE
   |||  |
   |||  `-------------- STATE_VALUE
   ||`----------------- STATE_KEY
   |`------------------ STATE_KSTART
   `--------------------STATE_BEFORE
                                      */


int
tags_valid(const char *tags)
{
	const char *p;
	int state;

	state = STATE_KSTART;
	for (p = tags; *p; p++) {
		switch (state) {
		case STATE_KSTART:
			if (s_iskeystart(*p)) state = STATE_KEY;
			else                  return -1;
			break;

		case STATE_KEY:
			     if (s_iskvsep(*p)) state = STATE_VALUE;
			else if (!s_iskey(*p))  return -1;
			break;

		case STATE_VALUE:
			if (!s_isvalue(*p)) state = STATE_KSTART;
			break;
		}
	}

	return state == STATE_VALUE ? 0 : -1;
}

void
s_reverse(char *buf, size_t len)
{
	char *p, *q;
	p = buf;
	q = p   + len;

	for (; p < q; ++p, --q) {
		*p = *p ^ *q;
		*q = *p ^ *q;
		*p = *p ^ *q;
	}
}

int
tags_canonicalize(char *tags)
{
	int swapped;
	char *a, *b, *c, t;
	size_t n0, n1, n2;

	do {  /* iterate until we stop having to swap */
		swapped = 0;
		a = tags;

		for (;;) {
			b = strchr(a, ','); /* find the next tag pair */
			if (!b)
				break;
			b++;

			c = strchr(b, ','); /* find the end of the next tag pair */
			if (!c)
				c = strchr(b, '\0');

			n0 = c-a-1;    /* ignore the trailing \0-terminator */
			n1 = b-a-1-1;  /* ignore the trailing ,-separator and the start of b */
			n2 = c-b-1;    /* ignore the trailing \0-terminator */

			t = *c; *c = '\0'; /* temporarily terminate b */
			if (strcmp(a,b) > 0) {
				*c = t; /* unterminate b */
				s_reverse(a, n0); /* reverse the whole string   */
				s_reverse(a, n2); /* reverse the "first" k=v    */
				a += n2 + 2;      /* skip the "first" key + ',' */
				s_reverse(a, n1); /* reverse the "second" k=v   */

				swapped++;
			}
			*c = t; /* unterminate b */

			a = strchr(a, ',');
			if (!a++)
				break;
		}
	} while (swapped);

	return 0;
}

char *
tags_next(char *tags, char **tag, char **val)
{
	char *p;

	BUG(tags != NULL, "tags_next() given a NULL tag string to iterate over");
	BUG(tag != NULL,  "tags_next() given a NULL destination pointer for the next tag");
	BUG(val != NULL,  "tags_next() given a NULL destination pointer for the next value");

	BUG(s_iskeystart(*tags), "tags_next() given a malformed tag string");

	*tag = tags;
	for (p = tags; *p && !s_iskvsep(*p); p++);
	*p++ = '\0'; *val = p;

	for (; *p && s_isvalue(*p); p++);
	if (*p) {
		*p++ = '\0';
		return p;
	}

	return NULL;
}

#ifdef TEST
/* LCOV_EXCL_START */
#define tags_ok(t) ok(tags_valid(t) == 0, "tag set '" #t "' should be valid")
#define tags_notok(t) ok(tags_valid(t) != 0, "tag set '" #t "' should not be valid")

#define SELF NULL
#define canonicalize_ok(in,expect) do {\
	char *buf = strdup(in); \
	ok(tags_canonicalize(buf) == 0, "tag set '" #in "' should canonicalize ok"); \
	is_string(buf, expect ? expect : buf, "tag set '" #in "' should canonicalize properly"); \
	free(buf); \
} while (0)
TESTS {
	subtest {
		tags_ok("a=b");
		tags_ok("a=b,c=d");
		tags_ok("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPWRSTUVWXYZ0123456789_-.:%@=test");
		tags_ok("value=?!?");

		tags_notok("");
		tags_notok("just-a-key");
		tags_notok("=b");
		tags_notok(",,,");
		tags_notok("-a=b");
		tags_notok("a = b");
	}

	subtest {
		canonicalize_ok("a=b", SELF);
		canonicalize_ok("a=value", SELF);
		canonicalize_ok("key=value,other=value", SELF);

		canonicalize_ok("c=d,a=b", "a=b,c=d");
		canonicalize_ok("c=dd,a=bb", "a=bb,c=dd");
		canonicalize_ok("beta=22,alpha=1", "alpha=1,beta=22");
		canonicalize_ok("beta=2,alpha=1", "alpha=1,beta=2");
		canonicalize_ok("a=one,c=three,b=two", "a=one,b=two,c=three");
		canonicalize_ok("zebra=Z1,yak=Y2,xenops=X3", "xenops=X3,yak=Y2,zebra=Z1");

		/* FIXME: handle duplicate tags */
	}

	subtest {
		char *tags, *next, *tag, *val;

		tags = strdup("host=localhost,env=prod,cluster=web,dc=buf1,os=linux");
		if (!tags)
			BAIL_OUT("failed to strdup in test setup!");

		next = tags_next(tags, &tag, &val);
		if (!next) BAIL_OUT("tags_next() returned a NULL pointer");
		is_string(tag, "host", "first tag from tag_next()");
		is_string(val, "localhost", "first tag value from tag_next()");

		next = tags_next(next, &tag, &val);
		if (!next) BAIL_OUT("tags_next() returned a NULL pointer");
		is_string(tag, "env", "second tag from tag_next()");
		is_string(val, "prod", "second tag value from tag_next()");

		next = tags_next(next, &tag, &val);
		if (!next) BAIL_OUT("tags_next() returned a NULL pointer");
		is_string(tag, "cluster", "third tag from tag_next()");
		is_string(val, "web", "third tag value from tag_next()");

		next = tags_next(next, &tag, &val);
		if (!next) BAIL_OUT("tags_next() returned a NULL pointer");
		is_string(tag, "dc", "fourth tag from tag_next()");
		is_string(val, "buf1", "fourth tag value from tag_next()");

		next = tags_next(next, &tag, &val);
		is_null(next, "tags_next() should return NULL when tags have been exhausted");
		is_string(tag, "os", "fifth tag from tag_next()");
		is_string(val, "linux", "fifth tag value from tag_next()");

		free(tags);
	}
}
/* LCOV_EXCL_STOP */
#endif

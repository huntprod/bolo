#include "bolo.h"
#include <ctype.h>
#include <math.h>

#define JSON_STACK_DEPTH   128

struct json {
	int    fd;
	char   buf[8192];
	size_t len;
	size_t off;

	char  *strbuf;
	int    strlen;

	int    state[JSON_STACK_DEPTH];
	int    top;
	int    error;
};

/*
   JSON Parser Deterministic Finite Automaton

   (s_init) is the start state.  Every transition on a (value)
   involves pushing state to a stack, and "recursing" in the 
   DFA, to recognize a value which starts from (s_init) again.

   see docs/diag/json.dot for more details.


  +------------------------------------------------------------------------------------------------------------+
  |                                                                                                            |
+-----------+  :   +-----------+  STRING   +-------------+  {   +-------------+                                |
| s_obj_val | <--- | s_obj_key | <-------- | s_obj_start | <--- |   s_init    |                                |
+-----------+      +-----------+           +-------------+      +-------------+                                |
                     ^                       |                    |                                            |
                     |                       |                    | [                                          |
                     |                       |                    v                                            |
                     |                       |                  +-------------+                                |
                     |                       |                  | s_lst_start | -+                             |
                     |                       |                  +-------------+  |                             |
                     |                       |                    |              |                             |
                     |                       |                    | value        |                             |
                     |                       |                    v              |                             |
                     |                       |                  +-------------+  |         value               | value
                     |            +----------+----------------- | s_lst_post  | <+------------------------+    |
                     |            |          |                  +-------------+  |                        |    |
                     |            |          |                    |              |                        |    |
                     |            |          |                    | ]            | ]                      |    |
                     |            |          |                    v              v                        |    |
                     |            |          |             }    +--------------------------------------+  |    |
                     |            |          +----------------> |                s_done                |  |    |
                     |            |                             +--------------------------------------+  |    |
                     |            |                                                         ^             |    |
                     |            |                                                         | }           |    |
                     |            |                                                         |             |    |
                     |            |                        ,    +-------------+           +------------+  |    |
                     |            +---------------------------> | s_lst_item  | -+        | s_obj_post | <+----+
                     |                                          +-------------+  |        +------------+  |
                     |                                                           |          |             |
                     |                                                           +----------+-------------+
                     |                                                                      |
                     |                                                                      |
                     |                                                                      | ,
                     |                                                                      v
                     |                                                          STRING    +------------+
                     +------------------------------------------------------------------- | s_obj_next |
                                                                                          +------------+
 */

#define S_INIT       0
#define S_OBJ_START  1
#define S_OBJ_KEY    2
#define S_OBJ_POST   4
#define S_OBJ_NEXT   5
#define S_LST_START  6
#define S_LST_POST   7
#define S_LST_ITEM   8

#ifdef JSON_DEBUG
static const char *STATES[] = {
	"s_init",
	"s_obj_start", "s_obj_key", "s_obj_val", "s_obj_post", "s_obj_next",
	"s_lst_start", "s_lst_post", "s_lst_item",
	"s_done",
	NULL,
};
#endif

static void
s_readbuf(struct json *b)
{
	ssize_t n;

	n = read(b->fd, b->buf, 8192);
	if (n < 0)
		bail("error reading. FIXME");
	b->off = 0;
	b->len = n;
}

#define s_getc(b, c, l) do {\
	if ((b)->off == (b)->len) s_readbuf(b); \
	if ((b)->len == 0)        goto l;\
	if ((b)->off < (b)->len)  (c) = (b)->buf[(b)->off++];\
} while (isspace((c)))

#define s_putc(b, c) do {\
	(b)->off--;\
} while (0)

#ifdef JSON_DEBUG
static void
s_dumpstack(struct json *b, const char *msg)
{
	int i;
	fprintf(stderr, "%s\n", msg);
	for (i = 1; i <= b->top; i++)
		fprintf(stderr, "  STACK [%d] -> %d (%s)\n", i, b->state[i], STATES[b->state[i]]);
}
#else
#define s_dumpstack(b,msg)
#endif

static inline void
s_push(struct json *b, int st)
{
	if (b->top + 1 == JSON_STACK_DEPTH) {
		b->error = 1;
	} else {
		b->state[++(b)->top] = (st);
		s_dumpstack(b,"s_push()");
	}
}

static inline void
s_pop(struct json *b)
{
	if (b->top) --b->top;
	s_dumpstack(b,"s_pop()");
}

static inline void
s_set(struct json *b, int st)
{
	b->state[b->top] = (st);
	s_dumpstack(b,"s_set()");
}

void
json_init_fd(struct json *b, int fd)
{
	memset(b, 0, sizeof(*b));
	b->fd = fd;
	b->strlen = 8192;
	b->strbuf = malloc(b->strlen);
	if (!b->strbuf)
		bail("malloc failed");

	b->top = 1;
	b->state[1] = S_INIT;
	s_dumpstack(b,"init()");\
}

void
json_init_buf(struct json *b, char *buf, size_t len)
{
	memset(b, 0, sizeof(*b));
	b->fd = -1;
	b->strlen = len;
	b->strbuf = buf;
	if (!b->strbuf)
		bail("malloc failed");

	b->top = 1;
	b->state[1] = S_INIT;
	s_dumpstack(b,"init()");\
}


#define TOKEN_ERROR   256
#define TOKEN_NULL    257
#define TOKEN_TRUE    258
#define TOKEN_FALSE   259
#define TOKEN_INTEGER 260
#define TOKEN_DECIMAL 261
#define TOKEN_STRING  262

#ifdef JSON_DEBUG
static const char *TOKENS[] = {
	"ERROR", "NULL", "TRUE", "FALSE", "INTEGER", "DECIMAL", "STRING", NULL,
};
#endif

static int
s_lex(struct json *b, struct json_value *v)
{
	char c = '\0';
	int esc, sign, i;
	long x;
	double d;

	sign = 1;
	v->type = JSON_NONE;

	s_getc(b, c, eof);
	switch (c) {
	case '[': case ']':
	case '{': case '}':
	case ':': case ',':
		return c;

	case 't':
		s_getc(b, c, error); if (c != 'r') goto error;
		s_getc(b, c, error); if (c != 'u') goto error;
		s_getc(b, c, error); if (c != 'e') goto error;
		return TOKEN_TRUE;

	case 'f':
		s_getc(b, c, error); if (c != 'a') goto error;
		s_getc(b, c, error); if (c != 'l') goto error;
		s_getc(b, c, error); if (c != 's') goto error;
		s_getc(b, c, error); if (c != 'e') goto error;
		return TOKEN_FALSE;

	case 'n':
		s_getc(b, c, error); if (c != 'u') goto error;
		s_getc(b, c, error); if (c != 'l') goto error;
		s_getc(b, c, error); if (c != 'l') goto error;
		return TOKEN_NULL;

	case '"':
		s_getc(b, c, error);
		esc = i = 0;
		while (esc || c != '"') {
			if (b->strlen < i + 4) {
				b->strlen += 8192;
				b->strbuf = realloc(b->strbuf, b->strlen);
				if (!b->strbuf)
					bail("memory allocation failed");
			}
			if (esc) {
				switch (c) {
				default:  b->strbuf[i++] = c;    break;
				case 't': b->strbuf[i++] = '\t'; break;
				case 'r': b->strbuf[i++] = '\r'; break;
				case 'n': b->strbuf[i++] = '\n'; break;
				case 'f': b->strbuf[i++] = '\f'; break;
				case 'b': b->strbuf[i++] = '\b'; break;
				}
			} else if (c != '\\') b->strbuf[i++] = c;
			esc = !esc && c == '\\';
			s_getc(b, c, error);
		}
		b->strbuf[i] = '\0';
		v->type = JSON_STRING;
		v->data.string = b->strbuf;
		return TOKEN_STRING;

	case '-': sign = -1;
	case '+':
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		x = 0;
		if (isdigit(c)) x = (c - '0');
		for (;;) {
			s_getc(b, c, integer);
			if (c == '.')        goto fraction;
			else if (isdigit(c)) x = x * 10 + (c - '0');
			else {
				s_putc(b, c);
				goto integer;
			}
		}

fraction:
		d = x; x = 10;
		for (;;) {
			s_getc(b, c, decimal);
			if (c == 'e' || c == 'E') goto exponent;
			else if (isdigit(c))      d += (c - '0') * 1.0 / x;
			else {
				s_putc(b, c);
				goto decimal;
			}
			x *= 10;
		}
exponent:
		x = 0;
		for (;;) {
			s_getc(b, c, scinote);
			if (isdigit(c)) x = x * 10 + (c - '0');
			else {
				s_putc(b, c);
				goto scinote;
			}
		}

scinote:
	v->data.decimal = d * pow(10, x) * sign;
	v->type = JSON_DECIMAL;
	return TOKEN_DECIMAL;

decimal:
	v->data.decimal = d * sign;
	v->type = JSON_DECIMAL;
	return TOKEN_DECIMAL;

integer:
	v->data.integer = x * sign;
	v->type = JSON_INTEGER;
	return TOKEN_INTEGER;
	}

error:
	return TOKEN_ERROR;

eof:
	return EOF;
}

int
json_read(struct json *b, struct json_value *v)
{
	int tok;

	if (b->error)
		return JSON_ERROR;

again:
	tok = s_lex(b, v);
#ifdef JSON_DEBUG
	     if (tok == EOF) fprintf(stderr, "TOK: EOF\n");
	else if (tok < 256)  fprintf(stderr, "TOK: '%c'\n", tok);
	else                 fprintf(stderr, "TOK: [%d] %s\n", tok, TOKENS[tok - 256]);
#endif

	if (tok == TOKEN_ERROR)
		goto error;

	if (!b->top) {
		if (tok == EOF) return JSON_EOF;
		goto error;
	}

	if (tok == EOF)
		goto error;

	switch (b->state[b->top]) {
	default:
		return JSON_ERROR;

	case S_INIT:
		switch (tok) {
		default: goto error;

		case TOKEN_NULL:    s_pop(b); /* done */ return JSON_NULL;
		case TOKEN_TRUE:    s_pop(b); /* done */ return JSON_TRUE;
		case TOKEN_FALSE:   s_pop(b); /* done */ return JSON_FALSE;
		case TOKEN_INTEGER: s_pop(b); /* done */ return JSON_INTEGER;
		case TOKEN_DECIMAL: s_pop(b); /* done */ return JSON_DECIMAL;
		case TOKEN_STRING:  s_pop(b); /* done */ return JSON_STRING;  /* FIXME */

		case '{':
			s_set(b, S_OBJ_START);
			return JSON_OBJECT_START;
		case '[':
			s_set(b, S_LST_START);
			return JSON_LIST_START;
		}
		break;

	case S_OBJ_START:
		switch (tok) {
		default: goto error;

		case TOKEN_STRING:
			s_set(b, S_OBJ_KEY);
			return JSON_KEY;

		case '}':
			s_pop(b); /* done */
			return JSON_OBJECT_FINISH;
		}
		break;

	case S_OBJ_KEY:
		switch (tok) {
		default: goto error;

		case ':':
			s_set(b, S_OBJ_POST);
			s_push(b, S_INIT);
			goto again;
		}
		break;

	case S_OBJ_POST:
		switch (tok) {
		default: goto error;

		case ',':
			s_set(b, S_OBJ_NEXT);
			goto again;

		case '}':
			s_pop(b); /* done */
			return JSON_OBJECT_FINISH;
		}
		break;

	case S_OBJ_NEXT:
		switch (tok) {
		default: goto error;

		case TOKEN_STRING:
			s_set(b, S_OBJ_KEY);
			return JSON_KEY;
		}
		break;


	case S_LST_START:
		switch (tok) {
		default: goto error;

		case TOKEN_NULL:    s_set(b, S_LST_POST); return JSON_NULL;
		case TOKEN_TRUE:    s_set(b, S_LST_POST); return JSON_TRUE;
		case TOKEN_FALSE:   s_set(b, S_LST_POST); return JSON_FALSE;
		case TOKEN_INTEGER: s_set(b, S_LST_POST); return JSON_INTEGER;
		case TOKEN_DECIMAL: s_set(b, S_LST_POST); return JSON_DECIMAL;
		case TOKEN_STRING:  s_set(b, S_LST_POST); return JSON_STRING;  /* FIXME */

		case '[':
			s_set(b, S_LST_POST);
			s_push(b, S_LST_START);
			return JSON_LIST_START;

		case ']':
			s_pop(b); /* done */
			return JSON_LIST_FINISH;

		case '{':
			s_set(b, S_LST_POST);
			s_push(b, S_OBJ_START);
			return JSON_OBJECT_START;
		}
		break;

	case S_LST_POST:
		switch (tok) {
		default: goto error;

		case ',':
			s_set(b, S_LST_ITEM);
			goto again;

		case ']':
			s_pop(b); /* done */
			return JSON_LIST_FINISH;
		}
		break;

	case S_LST_ITEM:
		switch (tok) {
		default: goto error;

		case TOKEN_NULL:    s_set(b, S_LST_POST); return JSON_NULL;
		case TOKEN_TRUE:    s_set(b, S_LST_POST); return JSON_TRUE;
		case TOKEN_FALSE:   s_set(b, S_LST_POST); return JSON_FALSE;
		case TOKEN_INTEGER: s_set(b, S_LST_POST); return JSON_INTEGER;
		case TOKEN_DECIMAL: s_set(b, S_LST_POST); return JSON_DECIMAL;
		case TOKEN_STRING:  s_set(b, S_LST_POST); return JSON_STRING;  /* FIXME */

		case '[':
			s_set(b, S_LST_POST);
			s_push(b, S_LST_START);
			return JSON_LIST_START;

		case '{':
			s_set(b, S_LST_POST);
			s_push(b, S_OBJ_START);
			return JSON_OBJECT_START;
		}
		break;
	}

error:
	b->error = 1;
	return JSON_ERROR;
}

static inline int
s_write(struct json *b, const char *buf, size_t len)
{
	size_t nwrit, n;

	for (n = 0; n != len; ) {
		nwrit = write(b->fd, buf + n, len - n);
		if (nwrit <= 0) return -1;
		n += nwrit;
	}
	return 0;
}

static inline int
s_write_null(struct json *b, const char *pre)
{
	if (pre && s_write(b, pre, strlen(pre)) != 0)
		return -1;
	return s_write(b, "null", 4);
}

static inline int
s_write_true(struct json *b, const char *pre)
{
	if (pre && s_write(b, pre, strlen(pre)) != 0)
		return -1;
	return s_write(b, "true", 4);
}

static inline int
s_write_false(struct json *b, const char *pre)
{
	if (pre && s_write(b, pre, strlen(pre)) != 0)
		return -1;
	return s_write(b, "false", 5);
}

static inline int
s_write_integer(struct json *b, const char *pre, void *data, size_t len)
{
	int n;
	long v;

	if (pre && s_write(b, pre, strlen(pre)) != 0)
		return -1;

	BUG(len == sizeof(int) || len == sizeof(long),
		"json_write given an INTEGER that was neither an int nor a long");

	v = (len == sizeof(int))
	         ? (long)*(int *)data
	         :      *(long *)data;
	n = snprintf(b->buf, 256, "%ld", v);
	return s_write(b, b->buf, n);
}

static inline int
s_write_decimal(struct json *b, const char *pre, void *data, size_t len)
{
	int n;
	double v;

	if (pre && s_write(b, pre, strlen(pre)) != 0)
		return -1;

	BUG(len == sizeof(double) || len == sizeof(float),
		"json_write given an DECIMAL that was neither an float nor a double");

	v = (len == sizeof(int))
	         ? (double)*(float *)data
	         :        *(double *)data;
	n = snprintf(b->buf, 256, "%lg", v);
	return s_write(b, b->buf, n);
}


static inline int
s_write_string(struct json *b, const char *pre, void *data, size_t len, const char *post)
{
	int i, j;

	if (pre && s_write(b, pre, strlen(pre)) != 0)
		return -1;

	b->buf[0] = '"';
	for (i = 0, j = 1; i < (int)len; i++) {
		if (j >= 254) {
			if (s_write(b, b->buf, j) != 0)
				return -1;
			j = 0;
		}

		switch (((char *)data)[i]) {
		case '"' : b->buf[j++] = '\\'; b->buf[j++] = '"';  break;
		case '\\': b->buf[j++] = '\\'; b->buf[j++] = '\\'; break;
		case '\t': b->buf[j++] = '\\'; b->buf[j++] = 't';  break;
		case '\r': b->buf[j++] = '\\'; b->buf[j++] = 'r';  break;
		case '\n': b->buf[j++] = '\\'; b->buf[j++] = 'n';  break;
		case '\f': b->buf[j++] = '\\'; b->buf[j++] = 'f';  break;
		case '\b': b->buf[j++] = '\\'; b->buf[j++] = 'b';  break;
		default:   b->buf[j++] = ((char *)data)[i];
		}
	}
	b->buf[j++] = '"';
	if (s_write(b, b->buf, j) != 0)
		return -1;

	if (post && s_write(b, post, strlen(post)) != 0)
		return -1;

	return 0;
}

int
json_write(struct json *b, int event, void *data, size_t len)
{
	if (b->error)
		return -1;

	if (!b->top)
		goto error;

	switch (b->state[b->top]) {
	default:
		return -1;

	case S_INIT:
		switch (event) {
		default: goto error;

		case JSON_NULL:    s_pop(b); return s_write_null(b, NULL);
		case JSON_TRUE:    s_pop(b); return s_write_true(b, NULL);
		case JSON_FALSE:   s_pop(b); return s_write_false(b, NULL);
		case JSON_INTEGER: s_pop(b); return s_write_integer(b, NULL, data, len);
		case JSON_DECIMAL: s_pop(b); return s_write_decimal(b, NULL, data, len);
		case JSON_STRING:  s_pop(b); return s_write_string(b, NULL, data, len, NULL);

		case JSON_OBJECT_START: s_set(b, S_OBJ_START); return s_write(b, "{", 1);
		case JSON_LIST_START:   s_set(b, S_LST_START); return s_write(b, "[", 1);
		}
		break;

	case S_OBJ_START:
		switch (event) {
		default: goto error;

		case JSON_KEY:
			s_set(b, S_OBJ_POST);
			s_push(b, S_INIT);
			return s_write_string(b, NULL, data, len, ":");

		case JSON_OBJECT_FINISH:
			s_pop(b); /* done */
			return s_write(b, "}", 1);
		}
		break;

	case S_OBJ_POST:
		switch (event) {
		default: goto error;

		case JSON_KEY:
			s_set(b, S_OBJ_POST);
			s_push(b, S_INIT);
			return s_write_string(b, ",", data, len, ":");

		case JSON_OBJECT_FINISH:
			s_pop(b); /* done */
			return s_write(b, "}", 1);
		}
		break;


	case S_LST_START:
		switch (event) {
		default: goto error;

		case JSON_NULL:    s_set(b, S_LST_POST); return s_write_null(b, NULL);
		case JSON_TRUE:    s_set(b, S_LST_POST); return s_write_true(b, NULL);
		case JSON_FALSE:   s_set(b, S_LST_POST); return s_write_false(b, NULL);
		case JSON_INTEGER: s_set(b, S_LST_POST); return s_write_integer(b, NULL, data, len);
		case JSON_DECIMAL: s_set(b, S_LST_POST); return s_write_decimal(b, NULL, data, len);
		case JSON_STRING:  s_set(b, S_LST_POST); return s_write_string(b, NULL, data, len, NULL);

		case JSON_OBJECT_START:
			s_set(b, S_LST_POST);
			s_push(b, S_OBJ_START);
			return s_write(b, "{", 1);

		case JSON_LIST_START:
			s_set(b, S_LST_POST);
			s_push(b, S_LST_START);
			return s_write(b, "[", 1);

		case JSON_LIST_FINISH:  s_pop(b); /* done */  return s_write(b, "]", 1);
		}
		break;

	case S_LST_POST:
		switch (event) {
		default: goto error;

		case JSON_NULL:    return s_write_null(b, ",");
		case JSON_TRUE:    return s_write_true(b, ",");
		case JSON_FALSE:   return s_write_false(b, ",");
		case JSON_INTEGER: return s_write_integer(b, ",", data, len);
		case JSON_DECIMAL: return s_write_decimal(b, ",", data, len);
		case JSON_STRING:  return s_write_string(b, ",", data, len, NULL);

		case JSON_OBJECT_START: s_push(b, S_OBJ_START); return s_write(b, ",{", 2);
		case JSON_LIST_START:   s_push(b, S_LST_START); return s_write(b, ",[", 2);
		case JSON_LIST_FINISH:  s_pop(b); /* done */    return s_write(b, "]", 1);

		}
		break;
	}

error:
	b->error = 1;
	return JSON_ERROR;
}

#ifdef TEST
/* LCOV_EXCL_START */
static void
setup(struct json *b, const char *json)
{
	int fd;
	ssize_t n;
	size_t len;

	fd = memfd("json");
	len = strlen(json);
	n = write(fd, json, strlen(json));
	if (n < 0 || (size_t)n < len)
		BAIL_OUT("failed to set up file descriptor with JSON data...");
	lseek(fd, 0, SEEK_SET);
	json_init_fd(b, fd);
}

void
readfrom(int fd, char *buf, size_t len)
{
	ssize_t n;

	n = lseek(fd, 0, SEEK_END);
	is_unsigned(n, len, "expected to read %d from memfd file descriptor, but fd had %d bytes", len, n);

	lseek(fd, 0, SEEK_SET);
	n = read(fd, buf, len);
	if (n != (ssize_t)len)
		BAIL_OUT("short read from memfd file descriptor...");
	buf[n] = '\0';
}

void
teardown(struct json *b)
{
	free(b->strbuf);
	close(b->fd);
}

TESTS {
	struct json b;
	struct json_value v;
	char buf[8192];
	long x;
	double d;

	subtest {
		setup(&b, "null");
		is_unsigned(json_read(&b, &v), JSON_NULL, "read null: (1)->NULL ('null')");
		is_unsigned(json_read(&b, &v), JSON_EOF,  "read null: (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,  "read null: (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_NULL, NULL, 0) == 0, "write null: (1)<-NULL");
		readfrom(b.fd, buf, 4);
		is_string(buf, "null", "write null: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "true");
		is_unsigned(json_read(&b, &v), JSON_TRUE, "read true: (1)->TRUE ('true')");
		is_unsigned(json_read(&b, &v), JSON_EOF,  "read true: (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,  "read true: (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_TRUE, NULL, 0) == 0, "write true: (1)<-TRUE");
		readfrom(b.fd, buf, 4);
		is_string(buf, "true", "write true: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "false");
		is_unsigned(json_read(&b, &v), JSON_FALSE, "read false: (1)->FALSE ('false')");
		is_unsigned(json_read(&b, &v), JSON_EOF,   "read false: (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,   "read false: (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_FALSE, NULL, 0) == 0, "write false: (1)<-FALSE");
		readfrom(b.fd, buf, 5);
		is_string(buf, "false", "write false: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "0");
		is_unsigned(json_read(&b, &v), JSON_INTEGER, "read 0: (1)->INTEGER");
		is_unsigned(v.type, JSON_INTEGER,             "read 0: (1) returns an INTEGER v");
		is_signed(v.data.integer, 0,                   "read 0: (1) that INTEGER v is 0");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read 0: (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read 0: (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		x = 0;
		ok(json_write(&b, JSON_INTEGER, &x, sizeof(x)) == 0, "write 0: (1)<-INTEGER");
		readfrom(b.fd, buf, 1);
		is_string(buf, "0", "write 0: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "10");
		is_unsigned(json_read(&b, &v), JSON_INTEGER, "read 10: (1)->INTEGER");
		is_unsigned(v.type, JSON_INTEGER,             "read 10: (1) returns an INTEGER v");
		is_signed(v.data.integer, 10,                  "read 10: (1) that INTEGER v is 10");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read 10: (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read 10: (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		x = 10;
		ok(json_write(&b, JSON_INTEGER, &x, sizeof(x)) == 0, "write 10: (1)<-INTEGER");
		readfrom(b.fd, buf, 2);
		is_string(buf, "10", "write 10: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "1.533");
		is_unsigned(json_read(&b, &v), JSON_DECIMAL, "read 1.533: (1)->DECIMAL");
		is_unsigned(v.type, JSON_DECIMAL,             "read 1.533: (1) returns a DECIMAL v");
		is_within(v.data.decimal, 1.533, 0.0001,       "read 1.533: (1) that DECIMAL v is 1.533");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read 1.533: (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read 1.533: (2+)->EOF");
		teardown(&b);

		setup(&b, "1.533e2");
		is_unsigned(json_read(&b, &v), JSON_DECIMAL, "read 1.533e2: (1)->DECIMAL");
		is_unsigned(v.type, JSON_DECIMAL,             "read 1.533e2: (1) returns a DECIMAL v");
		is_within(v.data.decimal, 153.3, 0.0001,       "read 1.533e2: (1) that DECIMAL v is 153.3");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read 1.533e2: (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read 1.533e2: (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		d = 1.533;
		ok(json_write(&b, JSON_DECIMAL, &d, sizeof(d)) == 0, "write 1.533: (1)<-DECIMAL");
		readfrom(b.fd, buf, 5);
		is_string(buf, "1.533", "write 1.533: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "1234567890");
		is_unsigned(json_read(&b, &v), JSON_INTEGER, "read 1234567890: (1)->INTEGER");
		is_unsigned(v.type, JSON_INTEGER,             "read 1234567890: (1) returns an INTEGER v");
		is_signed(v.data.integer, 1234567890,          "read 1234567890: (1) that INTEGER v is 1234567890");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read 1234567890: (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read 1234567890: (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		x = 1234567890;
		ok(json_write(&b, JSON_INTEGER, &x, sizeof(x)) == 0, "write 1234567890: (1)<-INTEGER");
		readfrom(b.fd, buf, 10);
		is_string(buf, "1234567890", "write 1234567890: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "-42");
		is_unsigned(json_read(&b, &v), JSON_INTEGER, "read -42: (1)->INTEGER");
		is_unsigned(v.type, JSON_INTEGER,             "read -42: (1) returns an INTEGER v");
		is_signed(v.data.integer, -42,                 "read -42: (1) that INTEGER v is -42");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read -42: (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read -42: (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		x = -42;
		ok(json_write(&b, JSON_INTEGER, &x, sizeof(x)) == 0, "write -42: (1)<-INTEGER");
		readfrom(b.fd, buf, 3);
		is_string(buf, "-42", "write -42: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "+00009");
		is_unsigned(json_read(&b, &v), JSON_INTEGER, "read +00009: (1)->INTEGER");
		is_unsigned(v.type, JSON_INTEGER,             "read +00009: (1) returns an INTEGER v");
		is_signed(v.data.integer, 9,                   "read +00009: (1) that INTEGER v is 9");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read +00009: (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read +00009: (2+)->EOF");
		teardown(&b);
	}

	subtest {
		setup(&b, "\"\"");
		is_unsigned(json_read(&b, &v), JSON_STRING,  "read \"\": (1)->STRING");
		is_unsigned(v.type, JSON_STRING,              "read \"\": (1) returns a STRING v");
		is_string(v.data.string, "",                   "read \"\": (1) that STRING v is ...");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read \"\": (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read \"\": (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_STRING, "", 0) == 0, "write \"\": (1)<-STRING");
		readfrom(b.fd, buf, 2);
		is_string(buf, "\"\"", "write \"\": wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "\"test\"");
		is_unsigned(json_read(&b, &v), JSON_STRING,  "read \"test\": (1)->STRING");
		is_unsigned(v.type, JSON_STRING,              "read \"test\": (1) returns a STRING v");
		is_string(v.data.string, "test",               "read \"test\": (1) that STRING v is ...");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read \"test\": (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read \"test\": (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_STRING, "test", 4) == 0, "write \"test\": (1)<-STRING");
		readfrom(b.fd, buf, 6);
		is_string(buf, "\"test\"", "write \"test\": wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "\"\\\"quoted\\\"\"");
		is_unsigned(json_read(&b, &v), JSON_STRING,  "read \"\\\"quoted\\\"\": (1)->STRING");
		is_unsigned(v.type, JSON_STRING,              "read \"\\\"quoted\\\"\": (1) returns a STRING v");
		is_string(v.data.string, "\"quoted\"",         "read \"\\\"quoted\\\"\": (1) that STRING v is ...");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read \"\\\"quoted\\\"\": (2)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,     "read \"\\\"quoted\\\"\": (2+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_STRING, "\"quoted\"", 8) == 0, "write \"\\\"quoted\\\"\": (1)<-STRING");
		readfrom(b.fd, buf, 12);
		is_string(buf, "\"\\\"quoted\\\"\"", "write \"\\\"quoted\\\"\": wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "[]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "read []: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "read []: (2)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read []: (3)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read []: (3+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write []: (1)<-LIST START");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write []: (2)<-LIST FINISH");
		readfrom(b.fd, buf, 2);
		is_string(buf, "[]", "write []: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "[null]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "read [null]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_NULL,        "read [null]: (2)->NULL ('null')");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "read [null]: (3)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [null]: (4)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [null]: (4+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write [null]: (1)<-LIST START");
		ok(json_write(&b, JSON_NULL,        NULL, 0) == 0, "write [null]: (2)<-NULL");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write [null]: (3)<-LIST FINISH");
		readfrom(b.fd, buf, 6);
		is_string(buf, "[null]", "write [null]: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "[22]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "read [22]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_INTEGER,     "read [22]: (2)->INTEGER");
		is_unsigned(v.type, JSON_INTEGER,                 "read [22]: (2) returns an INTEGER v");
		is_signed(v.data.integer, 22,                      "read [22]: (2) that INTEGER v is 22");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "read [22]: (3)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [22]: (4)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [22]: (4+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write [22]: (1)<-LIST START");
		x = 22;
		ok(json_write(&b, JSON_INTEGER, &x, sizeof(x)) == 0, "write [22]: (2)<-INTEGER");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write [22]: (3)<-LIST FINISH");
		readfrom(b.fd, buf, 4);
		is_string(buf, "[22]", "write [22]: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "[2.2]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "read [2.2]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_DECIMAL,     "read [2.2]: (2)->DECIMAL");
		is_unsigned(v.type, JSON_DECIMAL,                 "read [2.2]: (2) returns an DECIMAL v");
		is_signed(v.data.decimal, 2.2,                     "read [2.2]: (2) that DECIMAL v is 2.2");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "read [2.2]: (3)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [2.2]: (4)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [2.2]: (4+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write [2.2]: (1)<-LIST START");
		d = 2.2;
		ok(json_write(&b, JSON_DECIMAL, &d, sizeof(d)) == 0, "write [2.2]: (2)<-DECIMAL");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write [2.2]: (3)<-LIST FINISH");
		readfrom(b.fd, buf, 5);
		is_string(buf, "[2.2]", "write [2.2]: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "[\"str\"]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "read [\"str\"]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_STRING,      "read [\"str\"]: (2)->STRING");
		is_unsigned(v.type, JSON_STRING,                  "read [\"str\"]: (2) returns a STRING v");
		is_string(v.data.string, "str",                    "read [\"str\"]: (2) that STRING v is ...");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "read [\"str\"]: (3)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [\"str\"]: (4)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [\"str\"]: (4+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write [\"str\"]: (1)<-LIST START");
		ok(json_write(&b, JSON_STRING,     "str", 3) == 0, "write [\"str\"]: (2)<-STRING");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write [\"str\"]: (3)<-LIST FINISH");
		readfrom(b.fd, buf, 7);
		is_string(buf, "[\"str\"]", "write [\"str\"]: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "[\"q\\\";bs\\\\;tab\\t;cr\\r;nl\\n;ff\\f;bks\\b;\"]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "read [... esc ...]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_STRING,      "read [... esc ...]: (2)->STRING");
		is_unsigned(v.type, JSON_STRING,                  "read [... esc ...]: (2) returns a STRING v");
		is_string(v.data.string, "q\";bs\\;tab\t;cr\r;nl\n;ff\f;bks\b;",
		                                                   "read [... esc ...]: (2) that STRING v is ...");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "read [... esc ...]: (3)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [... esc ...]: (4)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [... esc ...]: (4+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write [... esc ...]: (1)<-LIST START");
		ok(json_write(&b, JSON_STRING,     "q\";bs\\;tab\t;cr\r;nl\n;ff\f;bks\b;",
		                                           29) == 0, "write [... esc ...]: (2)<-STRING");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write [... esc ...]: (3)<-LIST FINISH");
		readfrom(b.fd, buf, 40);
		is_string(buf, "[\"q\\\";bs\\\\;tab\\t;cr\\r;nl\\n;ff\\f;bks\\b;\"]", "write [... esc ...]: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "[true,33,false,7.654,null,\"string\"]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "read [...]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_TRUE,        "read [...]: (2)->TRUE ('true')");
		is_unsigned(json_read(&b, &v), JSON_INTEGER,     "read [...]: (3)->INTEGER");
		is_unsigned(v.type, JSON_INTEGER,                 "read [...]: (2) returns an INTEGER v");
		is_signed(v.data.integer, 33,                      "read [...]: (2) that INTEGER v is 33");
		is_unsigned(json_read(&b, &v), JSON_FALSE,       "read [...]: (4)->FALSE ('false')");
		is_unsigned(json_read(&b, &v), JSON_DECIMAL,     "read [...]: (5)->DECIMAL");
		is_unsigned(v.type, JSON_DECIMAL,                 "read [...]: (5) returns an DECIMAL v");
		is_within(v.data.decimal, 7.654, 0.0001,           "read [...]: (5) that DECIMAL v is 7.654");
		is_unsigned(json_read(&b, &v), JSON_NULL,        "read [...]: (6)->NULL ('null')");
		is_unsigned(json_read(&b, &v), JSON_STRING,      "read [...]: (7)->STRING");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "read [...]: (8)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [...]: (9)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [...]: (9+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,    NULL, 0) == 0, "write [...]: (1)<-LIST START");
		ok(json_write(&b, JSON_TRUE,          NULL, 0) == 0, "write [...]: (2)<-TRUE");
		x = 33;
		ok(json_write(&b, JSON_INTEGER, &x, sizeof(x)) == 0, "write [...]: (3)<-INTEGER");
		ok(json_write(&b, JSON_FALSE,         NULL, 0) == 0, "write [...]: (4)<-FALSE");
		d = 7.654;
		ok(json_write(&b, JSON_DECIMAL, &d, sizeof(d)) == 0, "write [...]: (5)<-DECIMAL");
		ok(json_write(&b, JSON_NULL,          NULL, 0) == 0, "write [...]: (6)<-NULL");
		ok(json_write(&b, JSON_STRING,    "string", 6) == 0, "write [...]: (7)<-STRING");
		ok(json_write(&b, JSON_LIST_FINISH,   NULL, 0) == 0, "write [...]: (8)<-LIST FINISH");
		readfrom(b.fd, buf, 35);
		is_string(buf, "[true,33,false,7.654,null,\"string\"]", "write [...]: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "[[]]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "read [[]]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "read [[]]: (2)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "read [[]]: (3)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "read [[]]: (4)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [[]]: (5)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "read [[]]: (5+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write [[]]: (1)<-LIST START");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write [[]]: (2)<-LIST START");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write [[]]: (3)<-LIST FINISH");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write [[]]: (4)<-LIST FINISH");
		readfrom(b.fd, buf, 4);
		is_string(buf, "[[]]", "write [[]]: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "[true,[false],[null],true]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "[t,[f],[n],t]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_TRUE,        "[t,[f],[n],t]: (2)->TRUE ('true')");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "[t,[f],[n],t]: (3)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_FALSE,       "[t,[f],[n],t]: (4)->FALSE ('false')");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "[t,[f],[n],t]: (5)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,  "[t,[f],[n],t]: (6)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_NULL,        "[t,[f],[n],t]: (7)->NULL ('null')");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "[t,[f],[n],t]: (8)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_TRUE,        "[t,[f],[n],t]: (9)->TRUE ('true')");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH, "[t,[f],[n],t]: (10)->LIST FINISH (']')");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "[t,[f],[n],t]: (11)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,         "[t,[f],[n],t]: (11+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write [t,[f],[n],t]]: (1)<-LIST START");
		ok(json_write(&b, JSON_TRUE,        NULL, 0) == 0, "write [t,[f],[n],t]]: (2)<-TRUE");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write [t,[f],[n],t]]: (3)<-LIST START");
		ok(json_write(&b, JSON_FALSE,       NULL, 0) == 0, "write [t,[f],[n],t]]: (4)<-FALSE");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write [t,[f],[n],t]]: (5)<-LIST FINISH");
		ok(json_write(&b, JSON_LIST_START,  NULL, 0) == 0, "write [t,[f],[n],t]]: (6)<-LIST START");
		ok(json_write(&b, JSON_NULL,        NULL, 0) == 0, "write [t,[f],[n],t]]: (7)<-NULL");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write [t,[f],[n],t]]: (8)<-LIST FINISH");
		ok(json_write(&b, JSON_TRUE,        NULL, 0) == 0, "write [t,[f],[n],t]]: (9)<-TRUE");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) == 0, "write [t,[f],[n],t]]: (10)<-LIST FINISH");
		readfrom(b.fd, buf, 26);
		is_string(buf, "[true,[false],[null],true]", "write [t,[f],[n],t]]: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "{}");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START,  "read {}: (1)->OBJECT START ('{')");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_FINISH, "read {}: (2)->OBJECT FINISH ('}')");
		is_unsigned(json_read(&b, &v), JSON_EOF,           "read {}: (3)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,           "read {}: (3+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_OBJECT_START,  NULL, 0) == 0, "write {}: (1)<-OBJECT START");
		ok(json_write(&b, JSON_OBJECT_FINISH, NULL, 0) == 0, "write {}: (2)<-OBJECT FINISH");
		readfrom(b.fd, buf, 2);
		is_string(buf, "{}", "write {}: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "{\"key\":\"value\"}");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START,  "read {k:v}: (1)->OBJECT START ('{')");
		is_unsigned(json_read(&b, &v), JSON_KEY,           "read {k:v}: (2)->KEY");
		is_unsigned(json_read(&b, &v), JSON_STRING,        "read {k:v}: (3)->STRING");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_FINISH, "read {k:v}: (4)->OBJECT FINISH ('}')");
		is_unsigned(json_read(&b, &v), JSON_EOF,           "read {k:v}: (5)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,           "read {k:v}: (5+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_OBJECT_START,  NULL, 0) == 0, "write {k:v}: (1)<-OBJECT START");
		ok(json_write(&b, JSON_KEY,          "key", 3) == 0, "write {k:v}: (2)<-KEY");
		ok(json_write(&b, JSON_STRING,     "value", 5) == 0, "write {k:v}: (3)<-STRING");
		ok(json_write(&b, JSON_OBJECT_FINISH, NULL, 0) == 0, "write {k:v}: (4)<-OBJECT FINISH");
		readfrom(b.fd, buf, 15);
		is_string(buf, "{\"key\":\"value\"}", "write {k:v}: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "{\"s\":\"...\",\"t\":true,\"f\":false,\"n\":null,\"i\":42}");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START,  "read {...}: (1)->OBJECT START ('{')");
		is_unsigned(json_read(&b, &v), JSON_KEY,           "read {...}: (2)->KEY");
		is_unsigned(json_read(&b, &v), JSON_STRING,        "read {...}: (3)->STRING");
		is_unsigned(json_read(&b, &v), JSON_KEY,           "read {...}: (4)->KEY");
		is_unsigned(json_read(&b, &v), JSON_TRUE,          "read {...}: (5)->TRUE ('true')");
		is_unsigned(json_read(&b, &v), JSON_KEY,           "read {...}: (6)->KEY");
		is_unsigned(json_read(&b, &v), JSON_FALSE,         "read {...}: (7)->FALSE ('false')");
		is_unsigned(json_read(&b, &v), JSON_KEY,           "read {...}: (8)->KEY");
		is_unsigned(json_read(&b, &v), JSON_NULL,          "read {...}: (9)->NULL ('null')");
		is_unsigned(json_read(&b, &v), JSON_KEY,           "read {...}: (10)->KEY");
		is_unsigned(json_read(&b, &v), JSON_INTEGER,       "read {...}: (11)->INTEGER");
		is_unsigned(v.type, JSON_INTEGER,                   "read {...}: (11) returns an INTEGER v");
		is_signed(v.data.integer, 42,                        "read {...}: (11) that INTEGER v is 42");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_FINISH, "read {...}: (12)->OBJECT FINISH ('}')");
		is_unsigned(json_read(&b, &v), JSON_EOF,           "read {...}: (13)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,           "read {...}: (13+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_OBJECT_START,  NULL, 0) == 0, "write {...}: (1)<-OBJECT START");
		ok(json_write(&b, JSON_KEY,            "s", 1) == 0, "write {...}: (2)<-KEY");
		ok(json_write(&b, JSON_STRING,       "...", 3) == 0, "write {...}: (3)<-STRING");
		ok(json_write(&b, JSON_KEY,            "t", 1) == 0, "write {...}: (4)<-KEY");
		ok(json_write(&b, JSON_TRUE,          NULL, 0) == 0, "write {...}: (5)<-TRUE");
		ok(json_write(&b, JSON_KEY,            "f", 1) == 0, "write {...}: (6)<-KEY");
		ok(json_write(&b, JSON_FALSE,         NULL, 0) == 0, "write {...}: (7)<-FALSE");
		ok(json_write(&b, JSON_KEY,            "n", 1) == 0, "write {...}: (8)<-KEY");
		ok(json_write(&b, JSON_NULL,          NULL, 0) == 0, "write {...}: (9)<-NULL");
		ok(json_write(&b, JSON_KEY,            "i", 1) == 0, "write {...}: (10)<-KEY");
		x = 42;
		ok(json_write(&b, JSON_INTEGER, &x, sizeof(x)) == 0, "write {...}: (11)<-INTEGER");

		ok(json_write(&b, JSON_OBJECT_FINISH, NULL, 0) == 0, "write {...}: (12)<-OBJECT FINISH");
		readfrom(b.fd, buf, 46);
		is_string(buf, "{\"s\":\"...\",\"t\":true,\"f\":false,\"n\":null,\"i\":42}", "write {...}: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "\t\t{  \"key\"  :\n  \"value\" \t }\n");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START,  "{ k : v }: (1)->OBJECT START ('{')");
		is_unsigned(json_read(&b, &v), JSON_KEY,           "{ k : v }: (2)->KEY");
		is_unsigned(json_read(&b, &v), JSON_STRING,        "{ k : v }: (3)->STRING");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_FINISH, "{ k : v }: (4)->OBJECT FINISH ('}')");
		is_unsigned(json_read(&b, &v), JSON_EOF,           "{ k : v }: (5)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,           "{ k : v }: (5+)->EOF");
		teardown(&b);
	}

	subtest {
		setup(&b, "[{},{},{}]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START,    "read: [{},{},{}]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START,  "read: [{},{},{}]: (2)->OBJECT START ('{')");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_FINISH, "read: [{},{},{}]: (3)->OBJECT FINISH ('}')");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START,  "read: [{},{},{}]: (4)->OBJECT START ('{')");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_FINISH, "read: [{},{},{}]: (5)->OBJECT FINISH ('}')");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START,  "read: [{},{},{}]: (6)->OBJECT START ('{')");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_FINISH, "read: [{},{},{}]: (7)->OBJECT FINISH ('}')");
		is_unsigned(json_read(&b, &v), JSON_LIST_FINISH,   "read: [{},{},{}]: (8)->LIST FINISH ('[')");
		is_unsigned(json_read(&b, &v), JSON_EOF,           "read: [{},{},{}]: (9)->EOF");
		is_unsigned(json_read(&b, &v), JSON_EOF,           "read: [{},{},{}]: (9+)->EOF");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,    NULL, 0) == 0, "write [{},{},{}]: (1)<-LIST START");
		ok(json_write(&b, JSON_OBJECT_START,  NULL, 0) == 0, "write [{},{},{}]: (2)<-OBJECT START");
		ok(json_write(&b, JSON_OBJECT_FINISH, NULL, 0) == 0, "write [{},{},{}]: (3)<-OBJECT FINISH");
		ok(json_write(&b, JSON_OBJECT_START,  NULL, 0) == 0, "write [{},{},{}]: (4)<-OBJECT START");
		ok(json_write(&b, JSON_OBJECT_FINISH, NULL, 0) == 0, "write [{},{},{}]: (5)<-OBJECT FINISH");
		ok(json_write(&b, JSON_OBJECT_START,  NULL, 0) == 0, "write [{},{},{}]: (6)<-OBJECT START");
		ok(json_write(&b, JSON_OBJECT_FINISH, NULL, 0) == 0, "write [{},{},{}]: (7)<-OBJECT FINISH");
		ok(json_write(&b, JSON_LIST_FINISH,   NULL, 0) == 0, "write [{},{},{}]: (8)<-LIST FINISH");
		readfrom(b.fd, buf, 10);
		is_string(buf, "[{},{},{}]", "write [{},{},{}]: wrote expected JSON");
		teardown(&b);
	}

	subtest {
		setup(&b, "nullfalse");
		is_unsigned(json_read(&b, &v), JSON_NULL,  "read: nullfalse: (1)->NULL");
		is_unsigned(json_read(&b, &v), JSON_ERROR, "read: nullfalse: (2)->ERROR");
		is_unsigned(json_read(&b, &v), JSON_ERROR, "read: nullfalse: (2+)->ERROR");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_NULL,  NULL, 0) == 0, "write nullfalse: (1)<-NULL");
		ok(json_write(&b, JSON_FALSE, NULL, 0) != 0, "write nullfalse: (2)<-FALSE is an ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "]");
		is_unsigned(json_read(&b, &v), JSON_ERROR, "read ]: (1)->ERROR");
		is_unsigned(json_read(&b, &v), JSON_ERROR, "read ]: (1+)->ERROR");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_FINISH, NULL, 0) != 0, "write ]: (1)<-LIST FINISH is an ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "}");
		is_unsigned(json_read(&b, &v), JSON_ERROR, "read }: (1)->ERROR");
		is_unsigned(json_read(&b, &v), JSON_ERROR, "read }: (1+)->ERROR");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_OBJECT_FINISH, NULL, 0) != 0, "write }: (1)<-OBJECT FINISH is an ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "[0,]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START, "[0,]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_INTEGER,    "[0,]: (2)->INTEGER");
		is_unsigned(json_read(&b, &v), JSON_ERROR,      "[0,]: (3)->ERROR");
		is_unsigned(json_read(&b, &v), JSON_ERROR,      "[0,]: (3+)->ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "[0,,1]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START, "[0,,1]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_INTEGER,    "[0,,1]: (2)->INTEGER");
		is_unsigned(json_read(&b, &v), JSON_ERROR,      "[0,,1]: (3)->ERROR");
		is_unsigned(json_read(&b, &v), JSON_ERROR,      "[0,,1]: (3+)->ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "[,1]");
		is_unsigned(json_read(&b, &v), JSON_LIST_START, "[,1]: (1)->LIST START ('[')");
		is_unsigned(json_read(&b, &v), JSON_ERROR,      "[,1]: (2)->ERROR");
		is_unsigned(json_read(&b, &v), JSON_ERROR,      "[,1]: (2+)->ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "nil");
		is_unsigned(json_read(&b, &v), JSON_ERROR, "nil: (1)->ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "{{");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START, "{{: (1)->OBJECT START");
		is_unsigned(json_read(&b, &v), JSON_ERROR,        "{{: (2)->ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "{\"key\"{");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START, "{k{: (1)->OBJECT START");
		is_unsigned(json_read(&b, &v), JSON_KEY,          "{k{: (2)->KEY");
		is_unsigned(json_read(&b, &v), JSON_ERROR,        "{k{: (3)->ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "{0{");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START, "{0{: (1)->OBJECT START");
		is_unsigned(json_read(&b, &v), JSON_ERROR,        "{0{: (2)->ERROR");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_OBJECT_START,  NULL, 0) == 0, "write {0{: (1)<-OBJECT START");
		x = 0;
		ok(json_write(&b, JSON_INTEGER, &x, sizeof(x)) != 0, "write {0{: (2)<-INTEGER is an ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "{true{");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START, "read {true{: (1)->OBJECT START");
		is_unsigned(json_read(&b, &v), JSON_ERROR,        "read {true{: (2)->ERROR");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_OBJECT_START,  NULL, 0) == 0, "write {true{: (1)<-OBJECT START");
		ok(json_write(&b, JSON_TRUE,          NULL, 0) != 0, "write {true{: (2)<-TRUE is an ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "{\"key\":\"value\"{");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START, "read {k:v{: (1)->OBJECT START");
		is_unsigned(json_read(&b, &v), JSON_KEY,          "read {k:v{: (2)->KEY");
		is_unsigned(json_read(&b, &v), JSON_STRING,       "read {k:v{: (3)->STRING");
		is_unsigned(json_read(&b, &v), JSON_ERROR,        "read {k:v{: (4)->ERROR");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_OBJECT_START, NULL, 0) == 0, "write {k:v{: (1)<-OBJECT START");
		ok(json_write(&b, JSON_KEY,         "key", 3) == 0, "write {k:v{: (2)<-KEY");
		ok(json_write(&b, JSON_STRING,    "value", 5) == 0, "write {k:v{: (3)<-STRING");
		ok(json_write(&b, JSON_OBJECT_START, NULL, 0) != 0, "write {k:v{: (4)<-OBJECT START is an ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "{\"key\":\"value\",{");
		is_unsigned(json_read(&b, &v), JSON_OBJECT_START, "{k:v,{: (1)->OBJECT START");
		is_unsigned(json_read(&b, &v), JSON_KEY,          "{k:v,{: (2)->KEY");
		is_unsigned(json_read(&b, &v), JSON_STRING,       "{k:v,{: (3)->STRING");
		is_unsigned(json_read(&b, &v), JSON_ERROR,        "{k:v,{: (4)->ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "[0[");
		is_unsigned(json_read(&b, &v), JSON_LIST_START, "read: [0[: (1)->LIST START");
		is_unsigned(json_read(&b, &v), JSON_INTEGER,    "read: [0[: (2)->INTEGER");
		is_unsigned(json_read(&b, &v), JSON_ERROR,      "read: [0[: (3)->ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "[}");
		is_unsigned(json_read(&b, &v), JSON_LIST_START, "read: [}: (1)->LIST START");
		is_unsigned(json_read(&b, &v), JSON_ERROR,      "read: [}: (2)->ERROR");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,    NULL, 0) == 0, "write [}: (1)<-LIST START");
		ok(json_write(&b, JSON_OBJECT_FINISH, NULL, 0) != 0, "write [}: (1)<-OBJECT FINISH is an ERROR");
		teardown(&b);
	}

	subtest {
		setup(&b, "[true}");
		is_unsigned(json_read(&b, &v), JSON_LIST_START, "read: [...}: (1)->LIST START");
		is_unsigned(json_read(&b, &v), JSON_TRUE,       "read: [...}: (2)->TRUE");
		is_unsigned(json_read(&b, &v), JSON_ERROR,      "read: [...}: (3)->ERROR");
		teardown(&b);

		setup(&b, "");
		ok(json_write(&b, JSON_LIST_START,    NULL, 0) == 0, "write [}: (1)<-LIST START");
		ok(json_write(&b, JSON_TRUE,          NULL, 0) == 0, "write [}: (1)<-TRUE");
		ok(json_write(&b, JSON_OBJECT_FINISH, NULL, 0) != 0, "write [}: (2)<-OBJECT FINISH is an ERROR");
		teardown(&b);
	}
}
/* LCOV_EXCL_STOP */
#endif

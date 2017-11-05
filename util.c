#include "bolo.h"
#include <limits.h>

/* LCOV_EXCL_START */
void
bail(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(2);
}
/* LCOV_EXCL_STOP */

static const char *errors[] = {
	/* BOLO_EUNKNOWN */  "An unknown error has occurred",
	/* BOLO_ENOTSET */   "Key not set in hash table",
	/* BOLO_EBADHASH */  "Corrupt hash table detected",
	/* BOLO_EBADTREE */  "Corrupt btree detected",
	/* BOLO_EBADSLAB */  "Corrupt database slab detected",
	/* BOLO_EBLKFULL */  "Database block is full",
	/* BOLO_EBLKRANGE */ "Database block range insufficient",
	/* BOLO_ENOMAINDB */ "main.db index not found in database root",
	/* BOLO_ENODBROOT */ "Database root directory not found",
	/* BOLO_EBADHMAC */  "HMAC authentication check failed",
	/* BOLO_EENDIAN */   "Datafile endian mismatch detected",
	/* BOLO_ENOSLAB */   "No such database slab",
	/* BOLO_ENOBLOCK */  "No such database block",
};

const char *
error(int num)
{
	if (num < 0 || num > BOLO_ERROR_TOP)
		num = BOLO_EUNKNOWN;

	if (num < BOLO_ERROR_BASE)
		return strerror(num);

	return errors[num - BOLO_ERROR_BASE];
}

static int  _urand_fd  = -1;
static char _urand_buf[8192];
static int  _urand_off = 0;

static void
_urand_init() {
	ssize_t n;

	/* make sure we have a file handle to /dev/urandom */
	if (_urand_fd < 0) {
		_urand_fd = open(DEV_URANDOM, O_RDONLY);
		if (_urand_fd < 0)
			bail("failed to open " DEV_URANDOM " to find randomness");
		_urand_off = 8192; /* pretend we've already exhausted the buffer */
	}

	/* do we need to get some new randomness? */
	if (_urand_off > 8192 - 8) { /* not enough to pull 64 bits... */
		n = read(_urand_fd, _urand_buf, 8192);
		if (n != 8192)
			bail("unable to read sufficient randomness from " DEV_URANDOM);
		_urand_off = 0;
	}
}

uint32_t
urand32()
{
	uint32_t v;

	_urand_init();

	memcpy(&v, _urand_buf + _urand_off, 4);
	_urand_off += 4;

	return v;
}

uint64_t
urand64()
{
	uint64_t v;

	_urand_init();

	memcpy(&v, _urand_buf + _urand_off, 8);
	_urand_off += 8;

	return v;
}

uint32_t
urandn(uint32_t n)
{
	uint32_t v, min;

	/* pathological callers get what they deserve. */
	if (n < 2) return 0;

	/* 2**32 % n == (2**32 - n) % n */
	min = -n % n;
	/* loop until we find a number in-range
	   that doesn't suffer from modulo bias. */
	for (v = 0; v < min; v = urand32())
		;

	/* we can safely modulo v. */
	return v % n;
}

int
mktree(int dirfd, const char *path, mode_t mode)
{
	char buf[PATH_MAX], *p, c;

	BUG(path != NULL, "mktree() given a NULL path to make");
	BUG(mode >= 0700, "mktree() given a suspicious mode (not rwx for owner)");

	if (!strncpy(buf, path, PATH_MAX - 1))
		return -1;

	for (p = strchr(buf, '/'); p && *p; p = strchr(p+1, '/')) {
		c = *p; *p = '\0';
		if (mkdirat(dirfd, buf, mode) != 0 && errno != EEXIST)
			return -1;
		*p = c;
	}

	return 0;
}

size_t
len(const struct list *lst)
{
	struct list *i;
	size_t n;

	BUG(lst != NULL, "len() given a NULL list to query");

	for (n = 0, i = lst->next; i != lst; i = i->next)
		n++;

	return n;
}

static void
_splice(struct list *prev, struct list *next)
{
	BUG(prev != NULL, "splice() given a NULL previous pointer");
	BUG(next != NULL, "splice() given a NULL next pointer");

	prev->next = next;
	next->prev = prev;
}

void
push(struct list *lst, struct list *add)
{
	BUG(lst != NULL, "psuh() given a NULL list to append to");
	BUG(add != NULL, "psuh() given a NULL list to append");

	_splice(lst->prev, add);
	_splice(add,       lst);
}

#ifdef TEST
/* LCOV_EXCL_START */
struct data {
	struct list l;
	char        value;
};

static int s_listis(struct list *lst, const char *expect)
{
	struct data *d;
	for_each(d, lst, l)
		if (d->value != *expect++)
			return 0;
	return 1; /* mismatch */
}

TESTS {
	subtest {
		struct stat st;
		int fd;

		if (system("./t/setup/util-mktree") != 0)
			BAIL_OUT("t/setup/util-mktree failed!");

		fd = open("./t/tmp", O_RDONLY|O_DIRECTORY);
		if (fd < 0)
			BAIL_OUT("failed to open t/tmp!");

		ok(stat("t/tmp/a/b/c/d", &st) != 0,
			"stat(t/tmp/a/b/c/d) should fail before we have called mktree()");

		ok(mktree(fd, "a/b/c/d/FILE", 0777) == 0,
			"mktree(t/tmp/a/b/c/d/FILE) should succeed");

		ok(stat("t/tmp/a/b/c/d", &st) == 0,
			"stat(t/tmp/a/b/c/d) should succeed after we call mktree()");

		ok(stat("t/tmp/a/b/c/d/FILE", &st) != 0,
			"stat(t/tmp/a/b/c/d/FILE) should fail, even after we call mktree()");
	}

	subtest {
		struct list lst;
		struct data A, B, C, D;

		A.value = 'a';
		B.value = 'b';
		C.value = 'c';
		D.value = 'd';

		empty(&lst);
		is_unsigned(len(&lst), 0, "empty lists should be empty");
		ok(s_listis(&lst, ""), "empty lists should equal []");

		push(&lst, &A.l);
		is_unsigned(len(&lst), 1, "list should be 1 element long after 1 push() op");
		ok(s_listis(&lst, "a"), "after push(a), list should be [a]");

		push(&lst, &B.l);
		is_unsigned(len(&lst), 2, "list should be 2 element long after 2 push() ops");
		ok(s_listis(&lst, "ab"), "after push(b), list should be [a, b]");

		push(&lst, &C.l);
		is_unsigned(len(&lst), 3, "list should be 3 element long after 3 push() ops");
		ok(s_listis(&lst, "abc"), "after push(c), list should be [a, b, c]");

		push(&lst, &D.l);
		is_unsigned(len(&lst), 4, "list should be 4 element long after 4 push() ops");
		ok(s_listis(&lst, "abcd"), "after push(d), list should be [a, b, c, d]");
	}

	subtest {
		is_string(error(ENOENT), strerror(ENOENT),
			"error() should hand off to system error stringification");
		is_string(error(BOLO_EBADHASH), errors[BOLO_EBADHASH - BOLO_ERROR_BASE],
			"error() should divert to its own error stringification");
		is_string(error(BOLO_ERROR_TOP + 0x100), errors[BOLO_EUNKNOWN - BOLO_ERROR_BASE],
			"error() should revert to the unknown error for out-of-range errnos");
		is_string(error(-3), errors[BOLO_EUNKNOWN - BOLO_ERROR_BASE],
			"error() should revert to the unknown error for out-of-range errnos");
	}

	subtest {
		int i;

		/* simple 32-bit wrap-around at 2048 urand32() calls... */
		for (i = 0; i < 2049; i++) urand32();
		pass("urand32() wrap-around works");
		/* re-align to 0 */
		for (     ; i < 4096; i++) urand32();

		/* simple 64-but wrap-around at 1024 urand64() calls... */
		for (i = 0; i < 1025; i++) urand64();
		pass("urand64() wrap-around works");
		/* re-align to 0 */
		for (     ; i < 2048; i++) urand64();

		/* 64-bit premature wrap-around after 2047 urand32() calls...
		   (4 octets will be left in _urand_buf; they will be discarded) */
		for (i = 0; i < 2047; i++) urand32();
		urand64();
		pass("urand64() wrap-around (with discard) works");
		/* re-align to 0 */
		for (i = 1; i < 1024; i++) urand64();
	}

	subtest {
		int i;
		uint32_t v;
#define always_eq(x,y) do {\
	v = (x);\
	for (i = 0; i < 100; i++) \
		if (v != (y)) \
			is_unsigned(v, (y), #x " always returns " #y); \
	if (i == 100)\
		pass(#x " always returns " #y);\
} while(0)

		always_eq(urandn(0), 0);
		always_eq(urandn(1), 0);
		always_eq(urandn(2), 0);

#define always_lt(x,y) do {\
	v = (x);\
	for (i = 0; i < 100; i++) \
		if (v > (y)) \
			cmp_ok(v, ">", (y), #x " always returns a value strictly less than " #y); \
	if (i == 100)\
		pass(#x " always returns a value strictly less than " #y);\
} while(0)

		always_lt(urandn(5),       5);
		always_lt(urandn(10),     10);
		always_lt(urandn(100),   100);
		always_lt(urandn(1000), 1000);

#undef always_eq
	}
}
/* LCOV_EXCL_STOP */
#endif

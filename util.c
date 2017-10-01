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

int
mktree(int dirfd, const char *path, mode_t mode)
{
	char buf[PATH_MAX], *p, c;

	assert(path != 0);

	if (!strncpy(buf, path, PATH_MAX))
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

	assert(lst != NULL);

	for (n = 0, i = lst->next; i != lst; i = i->next)
		n++;

	return n;
}

static void
_splice(struct list *prev, struct list *next)
{
	assert(prev != NULL);
	assert(next != NULL);

	prev->next = next;
	next->prev = prev;
}

void
push(struct list *lst, struct list *add)
{
	assert(lst != NULL);
	assert(add != NULL);

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
}
/* LCOV_EXCL_STOP */
#endif

#include "bolo.h"
#include <limits.h>

void
bail(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(2);
}

static const char *errors[] = {
	/* BOLO_EUNKNOWN */  "An unknown error has occurred",
	/* BOLO_ENOTSET */   "Key not set in hash table",
	/* BOLO_EBADHASH */  "Corrupt hash table detected",
	/* BOLO_EBADTREE */  "Corrupt btree detected",
	/* BOLO_EBADSLAB */  "Corrupt database slab detected",
	/* BOLO_EBLKFULL */  "Database block is full",
	/* BOLO_EBLKRANGE */ "Database block range insufficient",
	/* BOLO_ENOMAINDB */ "main.db index not found in database root",
};

const char *
error(int num)
{
	if (num < 0 || num > BOLO_ERROR_TOP)
		return errors[BOLO_EUNKNOWN];

	if (num <= BOLO_ERROR_BASE)
		return strerror(num);

	return errors[num];
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
}
#endif

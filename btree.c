#include "bolo.h"

/* reserve the first 8 octets for header data */
#define BTREE_HEADER_SIZE 8

#define BTREE_LEAF 0x80

/* where do the keys start in the mapped page? */
#define BTREE_KEYS_OFFSET (BTREE_HEADER_SIZE)
/* where do the values start in the mapped page? */
#define BTREE_VALS_OFFSET (BTREE_KEYS_OFFSET + BTREE_DEGREE * sizeof(bolo_msec_t))

#define koffset(i) (BTREE_KEYS_OFFSET + (i) * sizeof(bolo_msec_t))
#define voffset(i) (BTREE_VALS_OFFSET + (i) * sizeof(uint64_t))

#define keyat(t,i)   (page_read64(&(t)->page, koffset(i)))
#define valueat(t,i) (page_read64(&(t)->page, voffset(i)))

#define setkeyat(t,i,k)   (page_write64(&(t)->page, koffset(i), k))
#define setvalueat(t,i,v) (page_write64(&(t)->page, voffset(i), v))
#define setchildat(t,i,c) do {\
  setvalueat((t),(i),(c)->id); \
  (t)->kids[(i)] = (c); \
} while (0)

static void
_print(struct btree *t, int indent)
{
	int i;

	CHECK(t != NULL,   "btree_print() given a NULL btree to print");
	CHECK(indent >= 0, "btree_print() given a negative indent");
	CHECK(indent < 7,  "btree_print() recursed more than 7 levels deep; possible recursion loop");

	fprintf(stderr, "%*s[btree %p @%lu page %p // %d keys %s]\n",
		indent * 8, "", (void *)t, t->id,  t->page.data, t->used, t->leaf ? "LEAF" : "interior");

	for (i = 0; i < t->used; i++) {
		if (t->leaf) {
			fprintf(stderr, "%*s[%03d] % 10ld / %010lx (= %lu / %010lx)\n",
				indent * 8 + 2, "", i, keyat(t,i), keyat(t,i), valueat(t,i), valueat(t,i));
		} else {
			fprintf(stderr, "%*s[%03d] % 10ld / %010lx (%p) -->\n",
				indent * 8 + 2, "", i, keyat(t,i), keyat(t,i), (void *)t->kids[i]);
			if (t->kids[i])
				_print(t->kids[i], indent + 1);
		}
	}
	if (t->leaf) {
		fprintf(stderr, "%*s[%03d]          ~ (= %lu / %010lx)\n",
			indent * 8 + 2, "", i, valueat(t,t->used), valueat(t,t->used));
	} else {
		fprintf(stderr, "%*s[%03d]          ~ (%p) -->\n",
			indent * 8 + 2, "", i, (void *)t->kids[t->used]);
		if (t->kids[t->used])
			_print(t->kids[t->used], indent + 1);
	}
}

void
btree_print(struct btree *bt)
{
	_print(bt, 0);
}

static struct btree *
s_mapat1(int fd, off_t offset)
{
	struct btree *t;

	t = xmalloc(sizeof(*t));
	t->id = (uint64_t)offset;

	if (page_map(&t->page, fd, offset, BTREE_PAGE_SIZE) != 0)
		goto fail;

	t->leaf = page_read8 (&t->page, 5) & BTREE_LEAF;
	t->used = page_read16(&t->page, 6);
	return t;

fail:
	free(t);
	return NULL;
}

static struct btree *
s_mapat(int fd, off_t offset)
{
	struct btree *t;
	int i;

	t = s_mapat1(fd, offset);
	if (!t)
		return NULL;

	if (t->leaf)
		return t;

	for (i = 0; i <= t->used; i++) {
		t->kids[i] = s_mapat(fd, valueat(t,i));
		if (!t->kids[i]) {
			/* clean up the ones that succeeded */
			for (i = i - 1; i >= 0; i--)
				free(t->kids[i]);
			free(t);
			return NULL;
		}
	}

	return t;
}

static struct btree *
s_extend(int fd)
{
	uint64_t id;

	id = lseek(fd, 0, SEEK_END);
	lseek(fd, BTREE_PAGE_SIZE - 1, SEEK_CUR);
	if (write(fd, "\0", 1) != 1)
		return NULL;

	lseek(fd, -1 * BTREE_PAGE_SIZE, SEEK_END);
	CHECK(BTREE_HEADER_SIZE == 8, "BTREE_HEADER_SIZE constant is under- or oversized");
	if (write(fd, "BTREE\x80\x00\x00", BTREE_HEADER_SIZE) != BTREE_HEADER_SIZE)
		return NULL;

	return s_mapat(fd, id);
}

static int
s_flush(struct btree *t)
{
	CHECK(t != NULL,            "btree_write() given a NULL btree to flush");
	CHECK(t->page.data != NULL, "btree_write() given a btree without a backing page");

	page_write8 (&t->page, 5, t->leaf ? BTREE_LEAF : 0);
	page_write16(&t->page, 6, t->used);
	return page_sync(&t->page);
}

int
btree_write(struct btree *t)
{
	int i, rc;

	CHECK(t != NULL, "btree_write() given a NULL btree to write");

	rc = 0;

	if (s_flush(t) != 0)
		rc = -1;

	if (!t->leaf)
		for (i = 0; i <= t->used; i++)
			if (btree_write(t->kids[i]) != 0)
				rc = -1;

	return rc;
}

int
btree_close(struct btree *t)
{
	int i, rc;

	if (!t)
		return 0;

	rc = 0;
	if (!t->leaf)
		for (i = 0; i <= t->used; i++)
			if (btree_close(t->kids[i]) != 0)
				rc = -1;

	if (page_unmap(&t->page) != 0)
		rc = -1;
	free(t);

	return rc;
}

static int
s_find(struct btree *t, bolo_msec_t key)
{
	CHECK(t != NULL, "s_find (by way of btree_find or btree_insert) given a NULL btree node");

	int lo, mid, hi;

	lo = -1;
	hi = t->used;
	while (lo + 1 < hi) {
		mid = (lo + hi) / 2;
		if (keyat(t,mid) == key) return mid;
		if (keyat(t,mid) >  key) hi = mid;
		else                     lo = mid;
	}
	return hi;
}

static void
s_shift(struct btree *t, int n)
{
	if (t->used - n <= 0)
		return;

	/* slide all keys above [n] one slot to the right */
	memmove((uint8_t *)t->page.data + koffset(n + 1),
	        (uint8_t *)t->page.data + koffset(n),
	        sizeof(bolo_msec_t) * (t->used - n));

	/* slide all values above [n] one slot to the right */
	memmove((uint8_t *)t->page.data + voffset(n + 2),
	        (uint8_t *)t->page.data + voffset(n + 1),
	        sizeof(uint64_t) * (t->used - n));
	memmove(&t->kids[n + 2],
	        &t->kids[n + 1],
	        sizeof(struct btree*) * (t->used - n));
}

static struct btree *
s_clone(struct btree *t)
{
	struct btree *c;

	c = s_extend(t->page.fd);
	if (!c)
		bail("btree extension failed");

	c->leaf = t->leaf;
	return c;
}

static void
s_divide(struct btree *l, struct btree *r, int mid)
{
	CHECK(l != NULL,                    "btree_insert() divide given a NULL left node");
	CHECK(r != NULL,                    "btree_insert() divide given a NULL right node");
	CHECK(l != r,                       "btree_insert() divide given identical left and right nodes");
	CHECK(l->page.data != NULL,         "btree_insert() divide given a left node with no backing page");
	CHECK(r->page.data != NULL,         "btree_insert() divide given a right node with no backing page");
	CHECK(l->page.data != r->page.data, "btree_insert() divide given left and right nodes that use the same backing page");
	CHECK(mid != 0,                     "btree_insert() divide attempted to divide with midpoint of 0");
	CHECK(l->used >= mid,               "btree_insert() divide attempted to divide with out-of-range midpoint");

	r->used = l->used - mid - 1;
	l->used = mid;

	/* divide the keys at midpoint */
	memmove((uint8_t *)r->page.data + koffset(0),
	        (uint8_t *)l->page.data + koffset(mid + 1),
	        sizeof(bolo_msec_t) * r->used);

	/* divide the values at midpoint */
	memmove((uint8_t *)r->page.data + voffset(0),
	        (uint8_t *)l->page.data + voffset(mid + 1),
	        sizeof(uint64_t) * (r->used + 1));
	memmove(r->kids,
	        &l->kids[mid + 1],
	        sizeof(struct btree *) * (r->used + 1));

	/* note: we don't have to clean up l above mid, so we don't.
	         keep that in mind if you go examining memory in gdb */
#if PEDANTIC
	memset((uint8_t *)l->page.data + koffset(l->used), 0,
	       sizeof(bolo_msec_t) * (BTREE_DEGREE - l->used));
	memset((uint8_t *)l->page.data + voffset(l->used + 1), 0,
	       sizeof(uint64_t)    * (BTREE_DEGREE - l->used + 1));
#endif
}

static struct btree *
s_insert(struct btree *t, bolo_msec_t key, uint64_t block_number, bolo_msec_t *median)
{
	int i, mid;
	struct btree *r;

	CHECK(t != NULL,               "btree_insert() given a NULL node to insert into");
	CHECK(t->used <= BTREE_DEGREE, "btree_insert() given a node that was impossibly full");
	/* invariant: Each node in the btree will always have enough
	              free space in it to insert at least one value
	              (either a literal, or a node pointer).

	              Splitting is done later in this function (right
	              before returning) as necessary. */

	i = s_find(t, key);

	if (t->leaf) { /* insert into this node */
		if (i < t->used && keyat(t,i) == key) {
			setvalueat(t,i,block_number);
			return NULL;
		}

		s_shift(t, i);
		t->used++;
		setkeyat(t,i,key);
		setvalueat(t,i,block_number);

	} else { /* insert in child */
		if (!t->kids[i])
			t->kids[i] = s_extend(t->page.fd);
		if (!t->kids[i])
			return NULL; /* FIXME this is wrong */

		r = s_insert(t->kids[i], key, block_number, median);
		if (r) {
			s_shift(t, i);
			t->used++;
			setkeyat(t,i,*median);
			setchildat(t,i+1,r);
			return NULL;
		}
	}

	/* split the node now, if it is full, to save complexity */
	if (t->used == BTREE_DEGREE) {
		mid = t->used * BTREE_SPLIT_FACTOR;
		*median = keyat(t,mid);

		r = s_clone(t);
		s_divide(t,r,mid);
		return r;
	}

	return NULL;
}

int
btree_insert(struct btree *t, bolo_msec_t key, uint64_t block_number)
{
	struct btree *l, *r;
	bolo_msec_t m;

	CHECK(t != NULL, "btree_insert() given a NULL node to insert into");

	r = s_insert(t, key, block_number, &m);
	if (r) {
		/* pivot root to the left */
		l = s_clone(t);
		l->used = t->used;
		l->leaf = t->leaf;
		memmove(l->kids, t->kids, sizeof(t->kids));
		memmove(l->page.data, t->page.data, BTREE_PAGE_SIZE);

		/* re-initialize root as [ l . m . r ] */
		t->used = 1;
		t->leaf = 0;
		setchildat(t, 0, l);
		setkeyat  (t, 0, m);
		setchildat(t, 1, r);
	}

	return 0;
}

int
btree_find(struct btree *t, uint64_t *dst, bolo_msec_t key)
{
	int i;

	CHECK(t != NULL, "btree_find() given a NULL btree node");
	CHECK(dst != NULL, "btree_find() told to place results in a NULL pointer");

	if (t->leaf && t->used == 0)
		return -1; /* empty root node */

	i = s_find(t, key);

	if (t->leaf) {
		if (i == 0 || key == keyat(t,i))
			*dst = valueat(t, i);
		else
			*dst = valueat(t, i-1);
		return 0;
	}

	return t->kids[i] ? btree_find(t->kids[i], dst, key)
	                  : -1;
}

int
btree_isempty(struct btree *t)
{
	CHECK(t != NULL, "btree_isempty() given a NULL btree node");
	return t->used == 0;
}

bolo_msec_t
btree_first(struct btree *t)
{
	CHECK(t != NULL, "btree_first() given a NULL btree node");
	CHECK(!btree_isempty(t), "btree_first() given an empty btree");

	while (!t->leaf)
		t = t->kids[0];
	return keyat(t, 0);
}

bolo_msec_t
btree_last(struct btree *t)
{
	CHECK(t != NULL, "btree_last() given a NULL btree node");
	CHECK(!btree_isempty(t), "btree_last() given an empty btree");

	while (!t->leaf)
		t = t->kids[t->used];
	return keyat(t, t->used - 1);
}

int
btallocator(struct btallocator *a, int rootfd)
{
	int fd;
	unsigned int i;
	off_t off;
	uint64_t id;
	struct btblock *blk;
	struct btree *t;
	char path[64];

	CHECK(a != NULL,   "btallocator() given a NULL allocator object to initialize");
	CHECK(rootfd >= 0, "btallocator() given an invalid root directory file descriptor");

	a->rootfd = rootfd;
	empty(&a->blocks);

	for (id = 0; ; id += BTREE_PAGE_SIZE * BTBLOCK_DENSITY) {

		snprintf(path, sizeof(path), "idx/%04lx.%04lx/%04lx.%04lx.%04lx.%04lx.idx",
			((id & 0xffff000000000000ul) >> 48),
			((id & 0x0000ffff00000000ul) >> 32),
			/* --- */
			((id & 0xffff000000000000ul) >> 48),
			((id & 0x0000ffff00000000ul) >> 32),
			((id & 0x00000000ffff0000ul) >> 16),
			((id & 0x000000000000fffful)));

		fd = openat(a->rootfd, path, O_RDWR);
		if (fd < 0)
			break;

		off = lseek(fd, 0, SEEK_END);
		if (off < 0)
			goto fail;

		errno = BOLO_EBADTREE;
		if (off % BTREE_PAGE_SIZE != 0)
			goto fail;

		blk = xmalloc(sizeof(*blk));
		blk->fd = fd;
		blk->used = off / BTREE_PAGE_SIZE;
		push(&a->blocks, &blk->l);
		empty(&blk->btrees);

		/* map all of the btrees */
		for (i = 0; i < blk->used; i++) {
			CHECK(BTREE_HEADER_SIZE == 8, "BTREE_HEADER_SIZE constant is under- or oversized");
			if (write(blk->fd, "BTREE\x80\x00\x00", BTREE_HEADER_SIZE) != BTREE_HEADER_SIZE)
				return -1;

			/* map the btree region */
			t = xmalloc(sizeof(*t));
			memset(t, 0, sizeof(*t));
			if (page_map(&t->page, blk->fd, i * BTREE_PAGE_SIZE, BTREE_PAGE_SIZE) != 0)
				return -1;

			/* initialize the btree node */
			t->id = id + i;
			t->leaf = page_read8 (&t->page, 5) & BTREE_LEAF;
			t->used = page_read16(&t->page, 6);
		}
	}

	return 0;

fail:
	/* FIXME: deallocate memory */
	return -1;
}

struct btree *
btmake(struct btallocator *a)
{
	off_t off;
	uint64_t id;
	struct btblock *blk;
	struct btree *t;
	char path[64];

	CHECK(a != NULL, "btmake() given a NULL btallocator");

	id = 0;
	for_each(blk, &a->blocks, l) {
		if (blk->used < BTBLOCK_DENSITY)
			goto alloc;
		id += BTREE_PAGE_SIZE;
	}

	/* we have no empty blocks; allocate a new one, starting at `id` */
	blk = xmalloc(sizeof(*blk));
	push(&a->blocks, &blk->l);

	snprintf(path, sizeof(path), "idx/%04lx.%04lx/%04lx.%04lx.%04lx.%04lx.idx",
		((id & 0xffff000000000000ul) >> 48),
		((id & 0x0000ffff00000000ul) >> 32),
		/* --- */
		((id & 0xffff000000000000ul) >> 48),
		((id & 0x0000ffff00000000ul) >> 32),
		((id & 0x00000000ffff0000ul) >> 16),
		((id & 0x000000000000fffful)));
	if (mktree(a->rootfd, path, 0777) != 0)
		return NULL;

	blk->fd = openat(a->rootfd, path, O_RDWR|O_CREAT, 0666);
	if (blk->fd < 0)
		return NULL;

alloc:
	/* extend the underlying fd */
	off = lseek(blk->fd, 0, SEEK_END);
	lseek(blk->fd, BTREE_PAGE_SIZE - 1, SEEK_CUR);
	if (write(blk->fd, "\0", 1) != 1)
		return NULL;
	lseek(blk->fd, -1 * BTREE_PAGE_SIZE, SEEK_END);
	CHECK(BTREE_HEADER_SIZE == 8, "BTREE_HEADER_SIZE constant is under- or oversized");
	if (write(blk->fd, "BTREE\x80\x00\x00", BTREE_HEADER_SIZE) != BTREE_HEADER_SIZE)
		return NULL;

	/* map the btree region */
	t = xmalloc(sizeof(*t));
	if (page_map(&t->page, blk->fd, off, BTREE_PAGE_SIZE) != 0)
		goto fail;

	/* initialize the btree node */
	t->id = id;
	t->leaf = page_read8 (&t->page, 5) & BTREE_LEAF;
	t->used = page_read16(&t->page, 6);

	/* increment the block reference counter */
	blk->used++;
	return t;

fail:
	free(t);
	return NULL;
}

struct btree *
btfind(struct btallocator *a, uint64_t id)
{
	struct btblock *blk;
	struct btree *t;

	for_each(blk, &a->blocks, l) {
		if (id > blk->used) {
			id -= blk->used;
			continue;
		}

		for_each(t, &blk->btrees, l) {
			if (id > 0) {
				id--;
				continue;
			}

			return t;
		}
	}

	return NULL;
}

#ifdef TEST
/* LCOV_EXCL_START */
/* Tests will be inserting arbitrary values,
   so we will iterate over a range of keys.
   To generate the values from the keys, we
   will add an arbitrary, non-even constant
   (PERTURB) to make things interesting */
#define PERTURB  0xbad
#define KEYSTART 0x0c00
#define KEYEND   (KEYSTART + 7 * BTREE_DEGREE)

static int
iszero(const void *buf, off_t offset, size_t size)
{
	size_t i;

	insist(buf != NULL, "buf must not be NULL");
	insist(offset >= 0, "offset must be positive or zero");

	for (i = 0; i < size; i++)
		if (((const uint8_t *)buf)[offset + i] != 0)
			return 0; /* found a non-zero; fail */

	return 1; /* checks out! */
}

TESTS {
#if 0
	subtest {
		int fd;
		struct btree *t, *tmp;
		bolo_msec_t key;
		uint64_t value;

		fd = memfd("btree");
		t = btree_create(fd);
		if (!t)
			BAIL_OUT("btree_create(fd) returned NULL");

		if (!btree_isempty(t))
			BAIL_OUT("btree_create(fd) created a non-empty btree, somehow");

		if (!t->leaf)
			BAIL_OUT("initial root node is not considered a leaf");

		if (memcmp(t->page.data, "BTREE\x80\x00\x00", 8) != 0)
			BAIL_OUT("initial root node header incorrect");

		if (!iszero(t->page.data, 8, BTREE_PAGE_SIZE - 8))
			BAIL_OUT("initial root node (less header) should be zeroed (but wasn't!)");

		is_unsigned(lseek(fd, 0, SEEK_END), BTREE_PAGE_SIZE,
			"new btree file should be exactly ONE page long");

		for (key = KEYSTART; key <= KEYEND; key++) {
			if (btree_find(t, &value, key) == 0)
				fail("btree find(%lx) before insertion should fail, but got %#lx\n", key, value);
		}
		pass("lookups should fail before insertion");

		for (key = KEYSTART; key <= KEYEND; key++) {
			if (btree_insert(t, key, key + PERTURB) != 0)
				fail("failed to insert %#lx => %#lx", key, key + PERTURB);
		}
		pass("btree insertions should succeed");

		for (key = KEYSTART; key <= KEYEND; key++) {
			if (btree_find(t, &value, key) != 0)
				fail("find(%lx) should succeed", key);
			else if (value != key + PERTURB)
				is_unsigned(value, key + PERTURB, "find(%lx)", key);
		}
		pass("lookups should succeed");

		if (btree_write(t) != 0)
			BAIL_OUT("btree_write failed");

#if 0
		tmp = t;
		t = btree_read(fd);
		if (!t)
			BAIL_OUT("btree_read(fd) failed!");

		for (key = KEYSTART; key <= KEYEND; key++) {
			if (btree_find(t, &value, key) != 0)
				fail("find(%lx) failed after re-read", key);
			else if (value != key + PERTURB)
				is_unsigned(value, key + PERTURB, "find(%lx) after re-read", key);
		}
		pass("lookups after re-read should succeed");
#endif


		if (btree_close(t) != 0)
			BAIL_OUT("btree_close failed");

		btree_close(tmp);
		close(fd);
	}

	subtest {
		int fd;
		struct btree *t;
		uint64_t value;

		fd = memfd("btree");
		t = btree_create(fd);
		if (!t)
			BAIL_OUT("btree_create(fd) returned NULL");

		ok(btree_find(t, &value,   100) != 0, "find(100) should fail before insertion");
		ok(btree_find(t, &value,  1000) != 0, "find(1000) should fail before insertion");
		ok(btree_find(t, &value, 10000) != 0, "find(10000) should fail before insertion");

		ok(btree_insert(t,  500,  501) == 0, "insert(500) should succeed");
		ok(btree_insert(t, 1500, 1501) == 0, "insert(1500) should succeed");

		ok(btree_find(t, &value,   100) == 0, "find(100) should succeed after insertion(s)");
		is_unsigned(value,  501, "find(100) should find first key, 500 => 501");
		ok(btree_find(t, &value,  1000) == 0, "find(1000) should succeed after insertion(s)");
		is_unsigned(value,  501, "find(1000) should find nearest lesser key, 500 => 501");
		ok(btree_find(t, &value, 10000) == 0, "find(10000) should succeed after insertion(s)");
		is_unsigned(value, 1501, "find(10000) should find nearest lesser key, 1500 => 1501");

		btree_close(t);
		close(fd);
	}

	subtest {
		int fd;
		struct btree *t;

		fd = memfd("btree");
		t = btree_create(fd);
		if (!t)
			BAIL_OUT("btree_create(fd) returned NULL");

		btree_print(t);

		btree_close(t);
		close(fd);
	}
#endif
}
/* LCOV_EXCL_STOP */
#endif

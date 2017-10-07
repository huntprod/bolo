#include "bolo.h"

static unsigned int
s_hash_djb2(const char *key)
{
	unsigned int h = 5381;

	for (; *key; key++)
		h = ((h << 5) + h) + *key; /* h * 33 + c */

	return h;
}

#define s_hash s_hash_djb2

struct bucket {
	char *   key;
	void *   ptr;
	uint64_t val;

	struct bucket *next;
};

#define HASH_STRIDE  255
struct hash {
	struct bucket *buckets[HASH_STRIDE];
};

struct hash *
hash_new()
{
	struct hash *h;

	h = calloc(1, sizeof(*h));
	if (!h)
		return NULL;

	return h;
}

void
hash_free(struct hash *h)
{
	int i;
	struct bucket *b, *tmp;

	if (!h)
		return;

	for (i = 0; i < HASH_STRIDE; i++) {
		for (b = h->buckets[i]; b; ) {
			free(b->key);

			tmp = b->next;
			free(b);
			b = tmp;
		}
	}

	free(h);
}

int
hash_set(struct hash *h, const char *key, void *val)
{
	unsigned int k;
	struct bucket *b;

	assert(h != NULL);
	assert(key != NULL);

	k = s_hash(key) % HASH_STRIDE;

	/* check existing data */
	for (b = h->buckets[k]; b; b = b->next) {
		if (!streq(b->key, key))
			continue;

		b->ptr = val;
		return 0;
	}

	/* not in existing data, prepend a new bucket */
	b = calloc(1, sizeof(struct bucket));
	if (!b)
		return -1;

	b->key = strdup(key);
	b->ptr = val;
	b->next = h->buckets[k];
	h->buckets[k] = b;
	return 0;
}

int
hash_get(struct hash *h, void *dst, const char *key)
{
	unsigned int k;
	struct bucket *b;

	assert(h != NULL);
	assert(key != NULL);

	k = s_hash(key) % HASH_STRIDE;

	/* check existing data */
	for (b = h->buckets[k]; b; b = b->next) {
		if (!streq(b->key, key))
			continue;

		if (dst)
			*(void **)dst = b->ptr;
		return 0;
	}

	/* key not set in the hash */
	errno = BOLO_ENOTSET;
	return -1;
}

struct hash *
hash_read(int from, hash_reader_fn reader, void *udata)
{
	struct hash *h;
	int fd;
	FILE *in;
	char buf[8192], *a, *b;
	uint64_t value;
	void *ptr;

	assert(from >= 0);

	lseek(from, 0, SEEK_SET);

	fd = dup(from);
	if (fd < 0)
		return NULL;

	in = fdopen(fd, "r");
	if (!in)
		goto fail;

	h = hash_new();
	if (!h)
		goto fail;

	while (fgets(buf, 8192, in) != NULL) {
		a = strchr(buf, '\t');
		b = strchr(buf, '\n');

		errno = BOLO_EBADHASH;
		if (!a && !b)
			goto fail;

		*a++ = *b = '\0';
		value = strtoul(a, &b, 10);
		if (b && *b)
			goto fail;

		ptr = reader(buf, value, udata);
		if (!ptr)
			goto fail;

		if (hash_set(h, buf, ptr) != 0)
			goto fail;
	}

	fclose(in);
	return h;

fail:
	fclose(in);
	hash_free(h);
	return NULL;
}

int
hash_write(struct hash *h, int to, hash_writer_fn writer, void *udata)
{
	FILE *out;
	int i, fd;
	struct bucket *b;

	assert(h != NULL);
	assert(to >= 0);

	ftruncate(to, 0);
	lseek(to, 0, SEEK_SET);

	fd = dup(to);
	if (fd < 0)
		return -1;

	out = fdopen(fd, "w");
	if (!out) {
		close(fd);
		return -1;
	}

	for (i = 0; i < HASH_STRIDE; i++)
		for (b = h->buckets[i]; b; b = b->next)
			fprintf(out, "%s\t%lu\n", b->key, writer(b->key, b->ptr, udata));

	fclose(out);
	return 0;
}


#ifdef TEST
/* LCOV_EXCL_START */
#define new_hash(h) do {\
	h = hash_new(); \
	if (!h) \
		BAIL_OUT("hash_new() returned a NULL pointer"); \
} while (0)

struct data {
	uint64_t id;
	/* don't need any other fields... */
};

static uint64_t test_writer1(const char *k, void *_ptr, void *_)
{
	assert(_ptr != NULL);
	return ((struct data *)_ptr)->id;
}
/* _u (userdata) will be a contiguous array of
   struct data pointers, NULL-terminated. */
static void * test_reader1(const char *k, uint64_t v, void *_u)
{
	struct data **u, *i;

	assert(_u != NULL);

	u = (struct data **)_u;
	for (i = *u; i; i++)
		if (i->id == v)
			return i;

	/* not found! */
	return NULL;
}

TESTS {
	subtest {
		isnt_unsigned(
			s_hash("some string"),
			s_hash("some other string"),
			"s_hash() shouldn't collide on different keys");

		is_unsigned(
			s_hash("identical"),
			s_hash("identical"),
			"s_hash() should collide on identical keys");
	}

	subtest {
		struct hash *h;
		const char *v;

		new_hash(h);

		ok(!hash_isset(h, "one"), "h[one] should not be set");

		v = NULL;
		ok(hash_get(h, &v, "one") != 0,
			"hash_get(k) should fail, since k is not set in the hash");

		ok(hash_set(h, "one", "the first value") == 0,
			"hash_set(k,v) should succeed");

		ok(hash_isset(h, "one"), "h[one] should be set");

		v = NULL;
		ok(hash_get(h, &v, "one") == 0,
			"hash_get(k) should succeed");
		is_string(v, "the first value",
			"hash_get(k) should retrieve what we hash_set(k)");

		hash_free(h);
	}

	subtest {
		struct hash *h;
		int fd;
		struct data *data, *lst[2], *result;

		new_hash(h);
		fd = memfd("hash");

		data = malloc(sizeof(struct data));
		if (!data)
			BAIL_OUT("memory allocation failed");

		data->id = 0xdecafbad;
		lst[0] = data;
		lst[1] = NULL;

		if (hash_set(h, "key", data) != 0)
			BAIL_OUT("failed to set up the test for hash_read/write");

		ok(hash_write(h, fd, test_writer1, lst) == 0,
			"hash_write() should succeed");

		hash_free(h);

		h = hash_read(fd, test_reader1, lst);
		if (!h)
			BAIL_OUT("failed to read hash back in from the fd");

		ok(hash_get(h, &result, "key") == 0,
			"hash_get() should succeed after re-read");
		is_ptr(result, data, "hash_get() should return pointer set by reader fn");

		hash_free(h);
		close(fd);
		free(data);
	}

	subtest {
		struct hash *h;
		char *key;
		int i;
		struct data d;

		new_hash(h);
		for (i = 0; i < HASH_STRIDE + 1; i++) {
			if (asprintf(&key, "key%d", i) <= 0)
				BAIL_OUT("failed to generate a key for pigeon-hole test");

			if (hash_set(h, key, &d) != 0)
				fail("failed to set key %s => (data) in pigeon-hole test");
			free(key);
		}

		hash_free(h);
	}

	subtest {
		struct hash *h;
		struct data d1, d2, *v;

		new_hash(h);
		ok(hash_set(h, "x", &d1) == 0, "should set x => d1");
		ok(hash_get(h, &v, "x") == 0, "should be able to retrieve 'x'");
		is_ptr(v, &d1, "'x' should be mapped to d1");

		ok(hash_set(h, "x", &d2) == 0, "should set x => d2");
		ok(hash_get(h, &v, "x") == 0, "should be able to retrieve 'x'");
		is_ptr(v, &d2, "'x' should now be mapped to d2");

		hash_free(h);
	}
}
/* LCOV_EXCL_STOP */
#endif

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
	int flags;
	struct bucket *buckets[HASH_STRIDE];
};

#define s_ismanaged(h) (((h)->flags & HASH_MANAGED) == HASH_MANAGED)

struct hash *
hash_new(int flags)
{
	struct hash *h;

	h = calloc(1, sizeof(*h));
	if (!h)
		return NULL;

	h->flags = flags;
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

			if (s_ismanaged(h))
				free(b->ptr);

			tmp = b->next;
			free(b);
			b = tmp;
		}
	}

	free(h);
}

int
hash_setp(struct hash *h, const char *key, void *val)
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

		if (s_ismanaged(h))
			free(b->ptr);
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
hash_getp(struct hash *h, void **dst, const char *key)
{
	unsigned int k;
	struct bucket *b;

	assert(h != NULL);
	assert(key != NULL);
	assert(dst != NULL);

	k = s_hash(key) % HASH_STRIDE;

	/* check existing data */
	for (b = h->buckets[k]; b; b = b->next) {
		if (!streq(b->key, key))
			continue;

		*dst = b->ptr;
		return 0;
	}

	/* key not set in the hash */
	return -1;
}

int
hash_setv(struct hash *h, const char *key, uint64_t val)
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

		b->val = val;
		return 0;
	}

	/* not in existing data, prepend a new bucket */
	b = calloc(1, sizeof(struct bucket));
	if (!b)
		return -1;

	b->key = strdup(key);
	b->val = val;
	b->next = h->buckets[k];
	h->buckets[k] = b;
	return 0;
}

int
hash_getv(struct hash *h, uint64_t *dst, const char *key)
{
	unsigned int k;
	struct bucket *b;

	assert(h != NULL);
	assert(key != NULL);
	assert(dst != NULL);

	k = s_hash(key) % HASH_STRIDE;

	/* check existing data */
	for (b = h->buckets[k]; b; b = b->next) {
		if (!streq(b->key, key))
			continue;

		*dst = b->val;
		return 0;
	}

	/* key not set in the hash */
	return -1;
}

struct hash *
hash_read(int from, int flags)
{
	struct hash *h;
	int fd;
	FILE *in;
	char buf[8192], *a, *b;
	uint64_t value;

	assert(from >= 0);

	lseek(from, 0, SEEK_SET);

	fd = dup(from);
	if (fd < 0)
		return NULL;

	in = fdopen(fd, "r");
	if (!in)
		goto fail;

	h = hash_new(flags);
	if (!h)
		goto fail;

	while (fgets(buf, 8192, in) != NULL) {
		a = strchr(buf, '\t');
		b = strchr(buf, '\n');

		if (!a && !b)
			goto fail;

		*a++ = *b = '\0';
		value = strtoul(a, &b, 10);
		if (b && *b)
			goto fail;

		if (hash_setv(h, buf, value) != 0)
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
hash_write(struct hash *h, int to)
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
			fprintf(out, "%s\t%lu\n", b->key, b->val);

	fclose(out);
	return 0;
}


#ifdef TEST
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
		void *v;

		h = hash_new(0);
		isnt_null(h, "hash_new(0) should return a valid pointer");

		v = NULL;
		ok(hash_getp(h, &v, "one") != 0,
			"hash_getp(k) should fail, since k is not set in the hash");

		ok(hash_setp(h, "one", (void *)1) == 0,
			"hash_setp(k,v) should succeed");

		v = NULL;
		ok(hash_getp(h, &v, "one") == 0,
			"hash_getp(k) should succeed");
		is_ptr(v, (void *)1,
			"hash_getp(k) should retrieve what we hash_setp(k)");

		hash_free(h);
	}

	subtest {
		struct hash *h;
		unsigned int i, *values[4], *v;
		const char *keys[4] = { "first", "second", "third", "last" };

		h = hash_new(HASH_MANAGED);
		isnt_null(h, "hash_new(0) should return a valid pointer");

		for (i = 0; i < 4; i++) {
			values[i] = malloc(sizeof(unsigned int));
			if (!values[i])
				BAIL_OUT("failed to allocate integers for managed hash tests");

			*values[i] = i + 0x0200; /* arbitrary constant, no meaning */
			ok(hash_setp(h, keys[i], values[i]) == 0,
				"hash_setp() should set %s -> %p (%i)", keys[i], values[i], values[i]);
		}

		for (i = 0; i < 4; i++) {
			v = NULL;
			ok(hash_getp(h, (void **)&v, keys[i]) == 0,
				"hash_getp() should retrieve %s", keys[i]);

			is_ptr(v, values[i],
				"hash_getp() should retrieve the correct pointer for %s", keys[i]);
		}

		hash_free(h);
	}

	subtest {
		struct hash *h;
		unsigned int *value, *v;

		h = hash_new(HASH_MANAGED);
		isnt_null(h, "hash_new(0) should return a valid pointer");

		value = malloc(sizeof(*value));
		if (!value)
			BAIL_OUT("failed to allocate an integer for the hash memory leak test");
		ok(hash_setp(h, "reused-key", value) == 0, "1st hash_setp(k,v) should succeed");

		value = malloc(sizeof(*value));
		if (!value)
			BAIL_OUT("failed to allocate an integer for the hash memory leak test");
		ok(hash_setp(h, "reused-key", value) == 0, "2nd hash_setp(k,v) should succeed");

		v = NULL;
		ok(hash_getp(h, (void **)&v, "reused-key") == 0, "hash_getp() should succeed");

		hash_free(h);
	}

	subtest {
		struct hash *h;
		int fd;
		uint64_t v;

		fd = memfd("hash");
		h = hash_new(0);

		if (hash_setv(h, "key", 0xdecafbaddecafbadUL) != 0)
			BAIL_OUT("failed to set up the test for hash_read/write");

		ok(hash_write(h, fd) == 0,
			"hash_write() should succeed");

		hash_free(h);

		h = hash_read(fd, 0);
		if (!h)
			BAIL_OUT("failed to read hash back in from the fd");

		v = 0xabad1deaUL;
		ok(hash_getv(h, &v, "key") == 0,
			"hash_getv() should succeed after re-read");
		is_unsigned(v, 0xdecafbaddecafbadUL,
			"hash_getv() should return a value");

		hash_free(h);
		close(fd);
	}
}
#endif

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
	char * key;
	void * val;
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
				free(b->val);

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

		if (s_ismanaged(h))
			free(b->val);
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
hash_get(struct hash *h, void **val, const char *key)
{
	unsigned int k;
	struct bucket *b;

	assert(h != NULL);
	assert(key != NULL);
	assert(val != NULL);

	k = s_hash(key) % HASH_STRIDE;

	/* check existing data */
	for (b = h->buckets[k]; b; b = b->next) {
		if (!streq(b->key, key))
			continue;

		*val = b->val;
		return 0;
	}

	/* key not set in the hash */
	return -1;
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
		ok(hash_get(h, &v, "one") != 0,
			"hash_get(k) should fail, since k is not set in the hash");

		ok(hash_set(h, "one", (void *)1) == 0,
			"hash_set(k,v) should succeed");

		v = NULL;
		ok(hash_get(h, &v, "one") == 0,
			"hash_get(k) should succeed");
		is_ptr(v, (void *)1,
			"hash_get(k) should retrieve what we hash_set(k)");

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
			ok(hash_set(h, keys[i], values[i]) == 0,
				"hash_set() should set %s -> %p (%i)", keys[i], values[i], values[i]);
		}

		for (i = 0; i < 4; i++) {
			v = NULL;
			ok(hash_get(h, (void **)&v, keys[i]) == 0,
				"hash_get() should retrieve %s", keys[i]);

			is_ptr(v, values[i],
				"hash_get() should retrieve the correct pointer for %s", keys[i]);
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
		ok(hash_set(h, "reused-key", value) == 0, "1st hash_set(k,v) should succeed");

		value = malloc(sizeof(*value));
		if (!value)
			BAIL_OUT("failed to allocate an integer for the hash memory leak test");
		ok(hash_set(h, "reused-key", value) == 0, "2nd hash_set(k,v) should succeed");

		v = NULL;
		ok(hash_get(h, (void **)&v, "reused-key") == 0, "hash_get() should succeed");

		hash_free(h);
	}
}
#endif

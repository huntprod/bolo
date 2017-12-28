#include "bolo.h"

struct rsv *
rsv_new(size_t cap)
{
	struct rsv *rsv;

	rsv = xalloc(1, sizeof(*rsv) + cap * sizeof(double));
	rsv->cap = cap;
	rsv_reset(rsv);
	return rsv;
}

void
rsv_free(struct rsv *rsv)
{
	free(rsv);
}

void rsv_reset(struct rsv *rsv)
{
	CHECK(rsv != NULL, "rsv_reset() given a NULL reservoir to reset");

	rsv->n = rsv->len = 0;
}

void
rsv_sample(struct rsv *rsv, double v)
{
	uint32_t j;
	CHECK(rsv != NULL, "rsv_sample() given a NULL reservoir to sample into");

	if (rsv->n == rsv->len && rsv->len < rsv->cap) {
		rsv->items[rsv->len++] = v;
		rsv->n++;
		return;
	}

	j = urandn(rsv->n);
	if (j < rsv->cap)
		rsv->items[j] = v;
	rsv->n++;
}

static int cmp_median(const void *_a, const void *_b)
{
	double a, b;
	a = *(double*)_a;
	b = *(double*)_b;

	return a == b ? 0
	     : a >  b ? 1 : -1;
}

double rsv_median(struct rsv *rsv)
{
	int mid;

	CHECK(rsv != NULL, "rsv_median() given a NULL reservoir to summarize");

	if (rsv->len == 0)
		return NAN;

	qsort(rsv->items, rsv->len, sizeof(double), cmp_median);
	mid = rsv->len / 2;
	if (rsv->len % 2 == 0)
		return rsv->items[mid - 1] / 2.0
		     + rsv->items[mid]     / 2.0;

	return rsv->items[mid];
}

double rsv_average(struct rsv *rsv)
{
	double v;
	size_t i;

	CHECK(rsv != NULL, "rsv_average() given a NULL reservoir to summarize");

	if (rsv->len == 0)
		return NAN;

	v = 0.0;
	for (i = 0; i < rsv->len; i++)
		v += rsv->items[i] / rsv->len;
	return v;
}

double rsv_sum(struct rsv *rsv)
{
	double v;
	size_t i;

	CHECK(rsv != NULL, "rsv_sum() given a NULL reservoir to summarize");

	if (rsv->len == 0)
		return NAN;

	v = 0.0;
	for (i = 0; i < rsv->len; i++)
		v += rsv->items[i];
	return v;
}

double rsv_min(struct rsv *rsv)
{
	double v;
	size_t i;

	CHECK(rsv != NULL, "rsv_min() given a NULL reservoir to summarize");

	if (rsv->len == 0)
		return NAN;

	v = rsv->items[0];
	for (i = 1; i < rsv->len; i++)
		if (v > rsv->items[i])
			v = rsv->items[i];
	return v;
}

double rsv_max(struct rsv *rsv)
{
	double v;
	size_t i;

	CHECK(rsv != NULL, "rsv_max() given a NULL reservoir to summarize");

	if (rsv->len == 0)
		return NAN;

	v = rsv->items[0];
	for (i = 1; i < rsv->len; i++)
		if (v < rsv->items[i])
			v = rsv->items[i];
	return v;
}

#ifdef TEST
/* LCOV_EXCL_START */
TESTS {
	subtest {
		struct rsv *rsv;

		rsv = rsv_new(5);
		isnt_null(rsv, "rsv_new() should return a new reservoir");
		is_unsigned(rsv->cap, 5, "rsv_new(5) makes a reservoir with capacity 5");
		is_unsigned(rsv->len, 0, "rsv_new() makes an empty reservoir");

		/* make sure our calculations all return (quiet) NaN */
		ok(isnan(rsv_min(rsv)),     "rsv_min(<empty>) is NaN");
		ok(isnan(rsv_max(rsv)),     "rsv_max(<empty>) is NaN");
		ok(isnan(rsv_sum(rsv)),     "rsv_sum(<empty>) is NaN");
		ok(isnan(rsv_average(rsv)), "rsv_average(<empty>) is NaN");
		ok(isnan(rsv_median(rsv)),  "rsv_median(<empty>) is NaN");

		rsv_sample(rsv, 0.0);
		is_unsigned(rsv->cap, 5, "rsv_sample(...) doesn't affect reservoir capacity");
		is_unsigned(rsv->len, 1, "rsv_sample(...) bumps len to 1");

		rsv_sample(rsv, 1.0);
		is_unsigned(rsv->cap, 5, "rsv_sample(...) doesn't affect reservoir capacity");
		is_unsigned(rsv->len, 2, "rsv_sample(...) bumps len to 2");

		rsv_sample(rsv, 2.0);
		rsv_sample(rsv, 3.0);
		/* do some summarization tests before we reach capacity */
		is_within(rsv_min(rsv),      0.0, 0.001, "rsv_min(0,1,2,3)");
		is_within(rsv_max(rsv),      3.0, 0.001, "rsv_max(0,1,2,3)");
		is_within(rsv_sum(rsv),      6.0, 0.001, "rsv_sum(0,1,2,3)");
		is_within(rsv_average(rsv),  1.5, 0.001, "rsv_average(0,1,2,3)");
		is_within(rsv_median(rsv),   1.5, 0.001, "rsv_median(0,1,2,3)");

		rsv_sample(rsv, 4.0);
		is_unsigned(rsv->cap, 5, "rsv_sample(...) doesn't affect reservoir capacity");
		is_unsigned(rsv->len, 5, "rsv_sample(...) bumps len to 5");

		/* do some summarization tests at capacity */
		is_within(rsv_min(rsv),      0.0, 0.001, "rsv_min(0,1,2,3,4)");
		is_within(rsv_max(rsv),      4.0, 0.001, "rsv_max(0,1,2,3,4)");
		is_within(rsv_sum(rsv),     10.0, 0.001, "rsv_sum(0,1,2,3,4)");
		is_within(rsv_average(rsv),  2.0, 0.001, "rsv_average(0,1,2,3,4)");
		is_within(rsv_median(rsv),   2.0, 0.001, "rsv_median(0,1,2,3,4)");

		rsv_sample(rsv, 5.0);
		is_unsigned(rsv->cap, 5, "rsv_sample(...) doesn't affect reservoir capacity");
		is_unsigned(rsv->len, 5, "rsv_sample(...) doesn't exceed reservoir capacity");

		rsv_sample(rsv, 6.0);
		is_unsigned(rsv->cap, 5, "rsv_sample(...) doesn't affect reservoir capacity");
		is_unsigned(rsv->len, 5, "rsv_sample(...) doesn't exceed reservoir capacity");

		rsv_sample(rsv, 7.0);
		rsv_sample(rsv, 8.0);
		rsv_sample(rsv, 9.0);

		ok(rsv->items[0] + rsv->items[1] + rsv->items[2] + rsv->items[3] + rsv->items[4] > 10.0,
			"rsv_sample should replace some elements in [0..4]: "
			"[%1.0lf, %1.0lf, %1.0lf, %1.0lf, %1.0lf]",
					rsv->items[0], rsv->items[1], rsv->items[2], rsv->items[3], rsv->items[4]);

		rsv_free(rsv);
	}
}
/* LCOV_EXCL_STOP */
#endif

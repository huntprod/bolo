#include "bolo.h"
#include <math.h>

struct cf *
cf_new(int type, size_t n)
{
	struct cf *cf;

	switch (type) {
	case CF_MIN:
	case CF_MAX:
	case CF_SUM:
		n = 1; /* running value */
		break;

	case CF_DELTA:
		n = 2; /* first and last */
		break;

	case CF_MEAN:
	case CF_STDEV:
	case CF_VAR:
		n = 4; /* mean, m2, d1, and d2 */
		break;
	}

	cf = xalloc(1, sizeof(*cf) + n * sizeof(double));
	cf->type  = type;
	cf->slots = n;
	return cf;
}

void
cf_free(struct cf *cf)
{
	free(cf);
}

void
cf_reset(struct cf *cf)
{
	CHECK(cf != NULL, "cf_reset() given a NULL reservoir to reset");

	cf->active = 1;
	cf->carry = cf->rsv[1];
	cf->i = cf->used = cf->n = 0;
	memset(cf->rsv, 0, sizeof(double) * cf->slots);
}

void
cf_sample(struct cf *cf, double v)
{
	uint32_t i;

	CHECK(cf != NULL, "cf_sample() given a NULL cf context to sample into");

	switch (cf->type) {
	case CF_MIN: if (cf->n == 0 || v < cf->rsv[0]) cf->rsv[0] = v; break;
	case CF_MAX: if (cf->n == 0 || v > cf->rsv[0]) cf->rsv[0] = v; break;

	case CF_SUM: cf->rsv[0] += v; break;

	case CF_DELTA:
		if (cf->n == 0) cf->rsv[0] = (cf->active ? cf->carry : v);
		cf->rsv[1] = v;
		break;

	case CF_MEDIAN:
		if (cf->used < cf->slots) {
			cf->rsv[cf->i++] = v;
			cf->used++;
		} else {
			i = urandn(cf->n);
			if (i < cf->slots) cf->rsv[i] = v;
		}
		break;

	case CF_MEAN:
	case CF_STDEV:
	case CF_VAR:
		/* for calculating mean, variance and standard deviation
		   losslessly, we use Welford's algorithm (see TAOCPv2 p232)

		   we cleverly (ab)use the cf->rsv array and cf->n to hold
		   all of our working variables.  we definitely abuse the C
		   preprocessor to make this clearer.
		 */
#define count  (cf->n)
#define mean   (cf->rsv[0])
#define m2     (cf->rsv[1])
#define delta1 (cf->rsv[2])
#define delta2 (cf->rsv[3])

		/* minor tweak: we don't increment count (cf->n) here,
		   because we're going to increment it uniformly after
		   the switch block. */
		delta1 = v - mean;
		mean = mean + delta1 / (count + 1);
		delta2 = v - mean;
		m2 = m2 + delta1 * delta2;

#undef count
#undef mean
#undef m2
#undef delta1
#undef delta2
	}

	cf->n++;
}

static int
cmp_sorted(const void *_a, const void *_b)
{
	double a, b;
	a = *(double*)_a;
	b = *(double*)_b;

	return a == b ? 0
	     : a >  b ? 1 : -1;
}

double
cf_value(struct cf *cf)
{
	int mid;

	switch (cf->type) {
	case CF_MIN:
	case CF_MAX:
		return cf->n ? cf->rsv[0] : NAN;
	case CF_SUM:
		return cf->n ? cf->rsv[0] : 0.0;

	case CF_DELTA:
		return cf->n ? cf->rsv[1] - cf->rsv[0] : 0.0;

	case CF_MEDIAN:
		if (cf->n == 0) return NAN;

		qsort(cf->rsv, cf->used, sizeof(double), cmp_sorted);
		mid = cf->used / 2;
		if (cf->used % 2 == 0)
			return cf->rsv[mid - 1] / 2.0
			     + cf->rsv[mid]     / 2.0;

		return cf->rsv[mid];

#define count (cf->n)
#define mean  (cf->rsv[0])
#define m2    (cf->rsv[1])
	case CF_MEAN:  return count ? mean : NAN;

	case CF_STDEV: return count > 1 ? sqrt(m2 / (count - 1)) : NAN;
	case CF_VAR:   return count > 1 ?      m2 / (count - 1)  : NAN;
#undef mean
#undef m2
	}

	return NAN;
}

#ifdef TEST
/* LCOV_EXCL_START */
TESTS {
	subtest {
		struct cf *cf;

		cf = cf_new(CF_MIN, 5);
		isnt_null(cf, "cf_new() should return a new reservoir");

		ok(isnan(cf_value(cf)), "min cf(<empty>) is NaN");

		cf_sample(cf, 2.0);
		is_within(cf_value(cf), 2.0, 0.0001, "min cf(2) is 2");

		cf_sample(cf, 1.0);
		is_within(cf_value(cf), 1.0, 0.0001, "min cf(2,1) is 1");

		cf_sample(cf, 3.0);
		is_within(cf_value(cf), 1.0, 0.0001, "min cf(2,1,3) is 1");

		cf_free(cf);
	}

	subtest {
		struct cf *cf;

		cf = cf_new(CF_MAX, 5);
		isnt_null(cf, "cf_new() should return a new reservoir");

		ok(isnan(cf_value(cf)), "max cf(<empty>) is NaN");

		cf_sample(cf, 2.0);
		is_within(cf_value(cf), 2.0, 0.0001, "max cf(2) is 2");

		cf_sample(cf, 1.0);
		is_within(cf_value(cf), 2.0, 0.0001, "max cf(2,1) is 2");

		cf_sample(cf, 3.0);
		is_within(cf_value(cf), 3.0, 0.0001, "max cf(2,1,3) is 3");

		cf_free(cf);
	}

	subtest {
		struct cf *cf;

		cf = cf_new(CF_SUM, 5);
		isnt_null(cf, "cf_new() should return a new reservoir");

		is_within(cf_value(cf), 0.0, 0.0001, "sum cf(<empty>) is 0");

		cf_sample(cf, 2.0);
		is_within(cf_value(cf), 2.0, 0.0001, "sum cf(2) is 2");

		cf_sample(cf, 1.0);
		is_within(cf_value(cf), 3.0, 0.0001, "sum cf(2,1) is 3");

		cf_sample(cf, 3.0);
		is_within(cf_value(cf), 6.0, 0.0001, "sum cf(2,1,3) is 6");

		cf_free(cf);
	}

	subtest {
		struct cf *cf;

		cf = cf_new(CF_DELTA, 5);
		isnt_null(cf, "cf_new() should return a new reservoir");

		is_within(cf_value(cf), 0.0, 0.0001, "delta cf(<empty>) is 0");

		cf_sample(cf, 2.0);
		is_within(cf_value(cf), 0.0, 0.0001, "delta cf(2) is 0");

		cf_sample(cf, 1.0);
		is_within(cf_value(cf), -1.0, 0.0001, "delta cf(2,1) is -1");

		cf_sample(cf, 3.0);
		is_within(cf_value(cf), 1.0, 0.0001, "sum cf(2,1,3) is 1");

		cf_free(cf);
	}

	subtest {
		struct cf *cf;

		cf = cf_new(CF_MEAN, 5);
		isnt_null(cf, "cf_new() should return a new reservoir");

		ok(isnan(cf_value(cf)), "mean cf(<empty>) is NaN");

		cf_sample(cf, 0.0);
		is_within(cf_value(cf), 0.0, 0.0001, "mean cf(0) is 0");

		cf_sample(cf, 1.0);
		is_within(cf_value(cf), 0.5, 0.0001, "mean cf(0,1) is 0.5");

		cf_sample(cf, 2.0);
		is_within(cf_value(cf), 1.0, 0.0001, "mean cf(0,1,2) is 1.0");

		cf_sample(cf, 3.0);
		is_within(cf_value(cf), 1.5, 0.001, "mean cf(0,1,2,3) is 1.5");

		cf_sample(cf, 15.0);
		is_within(cf_value(cf), 4.2, 0.001, "cf_median(0,1,2,3,15) is 4.2");

		cf_sample(cf, 5.0);
		is_within(cf_value(cf), 4.3333, 0.001, "cf_median(0,1,2,3,15,5) is 4.3");

		cf_free(cf);
	}

	subtest {
		struct cf *cf;

		cf = cf_new(CF_MEDIAN, 5);
		isnt_null(cf, "cf_new() should return a new reservoir");
		is_unsigned(cf->slots, 5, "cf_new(t,5) makes a reservoir with capacity 5");
		is_unsigned(cf->used,  0, "cf_new() makes an empty reservoir");

		/* make sure our calculations all return (quiet) NaN */
		ok(isnan(cf_value(cf)), "cf_value(<empty>) is NaN");

		cf_sample(cf, 0.0);
		is_unsigned(cf->slots, 5, "cf_sample(...) doesn't affect reservoir capacity");
		is_unsigned(cf->used,  1, "cf_sample(...) bumps used-count to 1");

		cf_sample(cf, 1.0);
		is_unsigned(cf->slots, 5, "cf_sample(...) doesn't affect reservoir capacity");
		is_unsigned(cf->used,  2, "cf_sample(...) bumps used-count to 2");

		cf_sample(cf, 2.0);
		cf_sample(cf, 3.0);
		is_within(cf_value(cf), 1.5, 0.001, "median(0,1,2,3) is 1.5 (almost full)");

		cf_sample(cf, 4.0);
		is_unsigned(cf->slots, 5, "cf_sample(...) doesn't affect reservoir capacity");
		is_unsigned(cf->used,  5, "cf_sample(...) bumps used-count to 5");
		is_within(cf_value(cf), 2.0, 0.001, "cf_median(0,1,2,3,4) is 2.0 (full)");

		cf_sample(cf, 5.0);
		is_unsigned(cf->slots, 5, "cf_sample(...) doesn't affect reservoir capacity");
		is_unsigned(cf->used,  5, "cf_sample(...) doesn't exceed reservoir capacity");
		cf_sample(cf, 6.0);
		is_unsigned(cf->slots, 5, "cf_sample(...) doesn't affect reservoir capacity");
		is_unsigned(cf->used,  5, "cf_sample(...) doesn't exceed reservoir capacity");
		cf_sample(cf, 7.0);
		cf_sample(cf, 8.0);
		cf_sample(cf, 9.0);

		ok(cf->rsv[0] + cf->rsv[1] + cf->rsv[2] + cf->rsv[3] + cf->rsv[4] > 10.0,
			"cf_sample should replace some elements in [0..4]: "
			"[%1.0lf, %1.0lf, %1.0lf, %1.0lf, %1.0lf]",
					cf->rsv[0], cf->rsv[1], cf->rsv[2], cf->rsv[3], cf->rsv[4]);

		cf_free(cf);
	}

	subtest {
		struct cf *cf;

		cf = cf_new(CF_STDEV, 5);
		isnt_null(cf, "cf_new() should return a new reservoir");

		ok(isnan(cf_value(cf)), "stdev cf(<empty>) is NaN");

		cf_sample(cf, 10.0);
		ok(isnan(cf_value(cf)), "stdev cf(10) is NaN");

		cf_sample(cf, 2.0);
		is_within(cf_value(cf), 5.6568, 0.0001, "stdev cf(10,2) is 5.65");

		cf_sample(cf, 38.0);
		is_within(cf_value(cf), 18.9033, 0.0001, "stdev cf(10,2,38) is 18.9");

		cf_sample(cf, 23.0);
		is_within(cf_value(cf), 15.75595, 0.0001, "stdev cf(10,2,38,23) is 15.76");

		cf_sample(cf, 38.0);
		is_within(cf_value(cf), 16.2542, 0.0001, "stdev cf(10,2,38,23,38) is 16.25");

		cf_sample(cf, 23.0);
		is_within(cf_value(cf), 14.54189, 0.0001, "stdev cf(10,2,38,23,38,23) is 14.54");

		cf_sample(cf, 21.0);
		is_within(cf_value(cf), 13.2844, 0.001, "stdev cf(10,2,38,23,38,23,21) is 13.28");

		cf_free(cf);
	}

	subtest {
		struct cf *cf;

		cf = cf_new(CF_VAR, 5);
		isnt_null(cf, "cf_new() should return a new reservoir");

		ok(isnan(cf_value(cf)), "var cf(<empty>) is NaN");

		cf_sample(cf, 10.0);
		ok(isnan(cf_value(cf)), "var cf(10) is NaN");

		cf_sample(cf, 2.0);
		is_within(cf_value(cf), 32.0, 0.0001, "var cf(10,2) is 32");

		cf_sample(cf, 38.0);
		is_within(cf_value(cf), 357.3333, 0.0001, "var cf(10,2,38) is 357.3");

		cf_sample(cf, 23.0);
		is_within(cf_value(cf), 248.25, 0.0001, "var cf(10,2,38,23) is 248.25");

		cf_sample(cf, 38.0);
		is_within(cf_value(cf), 264.2, 0.0001, "var cf(10,2,38,23,38) is 264.2");

		cf_sample(cf, 23.0);
		is_within(cf_value(cf), 211.4666, 0.0001, "var cf(10,2,38,23,38,23) is 211.5");

		cf_sample(cf, 21.0);
		is_within(cf_value(cf), 176.47619, 0.001, "var cf(10,2,38,23,38,23,21) is 176.5");

		cf_free(cf);
	}
}
/* LCOV_EXCL_STOP */
#endif

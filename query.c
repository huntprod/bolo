#include "bolo.h"
#include <time.h>

#ifndef DEFAULT_QUERY_SAMPLES
#define DEFAULT_QUERY_SAMPLES 2048
#endif

#ifndef DEFAULT_QUERY_CF
#define DEFAULT_QUERY_CF CF_MEDIAN
#endif

#ifndef DEFAULT_BUCKET_STRIDE
#define DEFAULT_BUCKET_STRIDE 60
#endif

#ifndef DEFAULT_QUERY_WINDOW
#define DEFAULT_QUERY_WINDOW 14400
#endif

static struct resultset *
new_resultset(int stride, int from, int until)
{
	struct resultset *rset;
	size_t n;

	n = (until - from + stride - 1) / stride;
	rset = xalloc(1, sizeof(*rset) + sizeof(struct result) * n);
	rset->len = n;
	for (n = 0; n < rset->len; n++) {
		rset->results[n].start  = 1000 * (from + n * stride);
		rset->results[n].finish = 1000 * (from + n * stride + stride) - 1;
	}
	return rset;
}

static void
free_resultset(struct resultset *rset)
{
	free(rset);
}

struct query *
query_parse(const char *q)
{
	int n;
	struct query *query;
	struct qfield *f;

	query = bql_parse(q);
	if (!query) return NULL;

	/* the SELECT clause is required */
	if (!query->select)
		goto fail;

	/* fill in default names */
	for (n = 1, f = query->select; f; f = f->next)
		if (!f->name)
			asprintf(&f->name, "metric_%d", n++);

	/* verify that we don't have any invalid exprs */
	for (f = query->select; f; f = f->next)
		if (!f->ops)
			goto fail;

	/* fill in default timeframe */
	if (query->until == 0 && query->from == 0)
		query->from = -DEFAULT_QUERY_WINDOW;

	/* fill in default aggregate */
	if (!query->aggr.cf)      query->aggr.cf      = DEFAULT_QUERY_CF;
	if (!query->aggr.samples) query->aggr.samples = DEFAULT_QUERY_SAMPLES;
	/* don't set a default aggregate stride; some queries may not aggregate */

	/* fill in default bucketing parameters */
	if (!query->bucket.cf)      query->bucket.cf      = DEFAULT_QUERY_CF;
	if (!query->bucket.samples) query->bucket.samples = DEFAULT_QUERY_SAMPLES;
	if (!query->bucket.stride)  query->bucket.stride  = DEFAULT_BUCKET_STRIDE;

	/* check the from..until range */
	if (query->until <= query->from)
		goto fail;

	/* all good */
	return query;

fail:
	query_free(query);
	return NULL;
}

static void
qcond_plan(struct qcond *qc, struct db *db)
{
	static char buf[8192]; /* FIXME define a max len for key=value */

	switch (qc->op) {
	default: return;
	case COND_AND:
	case COND_OR:     qcond_plan(qc->b, db);
	case COND_NOT:    qcond_plan(qc->a, db);
	                  return;

	case COND_EQ:     snprintf(buf, 8192, "%s=%s", (char *)qc->a, (char *)qc->b);
	                  break;

	case COND_EXIST:  snprintf(buf, 8192, "%s", (char *)qc->a);
	                  break;
	}

	/* look up the tag and copy in the midx */
	if (hash_get(db->tags, &qc->midx, buf) != 0)
		qc->midx = NULL;
}

static int
qcond_check(struct qcond *qc, struct idx *idx)
{
	struct multidx *set;

	switch (qc->op) {
	case COND_AND:
		return (qcond_check(qc->a, idx) == 0
		     && qcond_check(qc->b, idx) == 0) ? 0 : 1;
	case COND_OR:
		return (qcond_check(qc->a, idx) == 0
		     || qcond_check(qc->b, idx) == 0) ? 0 : 1;
	case COND_NOT:
		return  qcond_check(qc->a, idx) == 0  ? 1 : 0;
	case COND_EQ:
	case COND_EXIST:
		for (set = qc->midx; set; set = set->next)
			if (set->idx == idx)
				return 0;
		return 1;

	default:
		return 1;
	}
}

static int
s_qfield_plan(struct query *q, struct db *db, struct qfield *f)
{
	int i;
	struct multidx *set, *tmp, *full;

	CHECK(q  != NULL, "s_qfield_plan() given a nil query");
	CHECK(db != NULL, "s_qfield_plan() given a nil database");
	CHECK(f  != NULL, "s_qfield_plan() given a nil query field");

	for (i = 0; f->ops[i].code != QOP_RETURN; i++) {
		switch (f->ops[i].code) {
		case QOP_PUSH:
			if (hash_get(db->metrics, &full, f->ops[i].data.push.metric) != 0) {
				q->err_num = QERR_NOSUCHREF;
				q->err_data = strdup(f->ops[i].data.push.metric);
				return -1;
			}

			f->ops[i].data.push.set = NULL;
			for (tmp = full; tmp; tmp = tmp->next) {
				if (qcond_check(q->where, tmp->idx) == 0) {
					set = xmalloc(sizeof(*set));
					set->next = f->ops[i].data.push.set;
					set->idx  = tmp->idx;
					f->ops[i].data.push.set = set;
				}
			}
			break;
		}
	}
	return 0;
}

typedef void (*combiner)(struct result *a, struct result *b);
typedef void (*scaler)(struct result *a, double c);

static void
s_resultset_combine(struct resultset *a, struct resultset *b, combiner fn)
{
	size_t i;

	CHECK(a  != NULL, "s_resultset_combine() given a nil resultset");
	CHECK(b  != NULL, "s_resultset_combine() given a nil resultset");
	CHECK(a->len == b->len, "s_resultset_combine() given two resultsets of different degree");

	for (i = 0; i < a->len; i++)
		fn(&a->results[i], &b->results[i]);
}

static void
s_resultset_scale(struct resultset *set, double v, scaler fn)
{
	size_t i;
	for (i = 0; i < set->len; i++)
		fn(&set->results[i], v);
}

static void s_addc(struct result *a, double v) { a->value += v; }
static void s_subc(struct result *a, double v) { a->value -= v; }
static void s_mulc(struct result *a, double v) { a->value *= v; }
static void s_divc(struct result *a, double v) {
	if (v == 0.0) a->value = NAN;
	else          a->value /= v;
}

static void s_add(struct result *a, struct result *b) { a->value += b->value; }
static void s_sub(struct result *a, struct result *b) { a->value -= b->value; }
static void s_mul(struct result *a, struct result *b) { a->value *= b->value; }
static void s_div(struct result *a, struct result *b) {
	if (b->value == 0.0) a->value = NAN;
	else                 a->value /= b->value;
}

int
query_plan(struct query *q, struct db *db)
{
	struct qfield *f;

	/* compile conditions into the subset of
	   applicable index subsets for each clause */
	if (q->where)
		qcond_plan(q->where, db);

	/* compile metric references (PUSH) into
	   the index subsets they reference. */
	if (q->select)
		for (f = q->select; f; f = f->next)
			s_qfield_plan(q, db, f);

	return 0;
}

static void
qcond_free(struct qcond *qcond)
{
	if (!qcond) return;

	switch (qcond->op) {
	case COND_AND:
	case COND_OR:  qcond_free(qcond->b);
	case COND_NOT: qcond_free(qcond->a);
	               break;

	case COND_EQ:  free(qcond->a);
	               free(qcond->b);
	               break;

	case COND_EXIST: free(qcond->a);
	                 break;
	}

	free(qcond);
}

void
query_free(struct query *q)
{
	int i;
	struct qfield *f, *_f;
	struct multidx *set, *_set;

	if (!q) return;
	qcond_free(q->where);

	f = q->select;
	while (f) {
		for (i = 0; f->ops && f->ops[i].code != QOP_RETURN; i++) {
			switch (f->ops[i].code) {
			case QOP_PUSH:
				free(f->ops[i].data.push.metric);
				for (set = f->ops[i].data.push.set; set; ) {
					_set = set->next;
					free(set);
					set = _set;
				}
				break;
			}
		}
		free(f->ops);
		free(f->name);
		free(f->result);

		_f = f->next;
		free(f);
		f = _f;
	}

	free(q->err_data);
	free(q);
}

#define QOPS_STACK_MAX 64

static int
s_qfield_exec(struct query *q, struct db *db, struct query_ctx *ctx, struct qfield *f)
{
	int i, j, k, strides, top, aggregated;
	struct resultset *stack[QOPS_STACK_MAX], *tmp;
	struct cf *bkt, *aggr;
	struct multidx *set;

	/* allocate the consolidation function context */
	bkt = cf_new(q->bucket.cf, q->bucket.samples);

	aggregated = 0;
	top = -1;
	for (i = 0; ; i++) {
		switch (f->ops[i].code) {
		case QOP_RETURN:
			if (top < 0) bail("query eval: stack underflow in return");
			if (top > 0) bail("query eval: leftover stack in return");

			/* implicit / automatic aggregation */
			if (!aggregated && q->aggr.stride) {
				/* aggregate the top of the stack down to a smaller resultset */
				tmp = new_resultset(q->aggr.stride, ctx->now / 1000 + q->from,
				                                    ctx->now / 1000 + q->until);
				strides = q->aggr.stride / q->bucket.stride; /* FIXME: make sure aggr % stride == 0 ALWAYS */
				aggr = cf_new(q->aggr.cf, q->aggr.samples);
				for (j = 0; j < (int)tmp->len; j++) {
					cf_reset(aggr);
					for (k = 0; k < strides; k++) {
						cf_sample(aggr, stack[top]->results[j*strides+k].value);
					}
					tmp->results[j].value = cf_value(aggr);
				}
				cf_free(aggr);

				/* replace the top of the stack with the aggregate (pop+push) */
				free(stack[top]);
				stack[top] = tmp;
			}

			f->result = stack[top];
			cf_free(bkt);
			return 0;

		case QOP_PUSH:
			/* scale and push a new resultset onto the stack */
			top++;
			if (top == QOPS_STACK_MAX)
				bail("query eval: stack depth exceeded"); /* FIXME */
			stack[top] = new_resultset(q->bucket.stride,
			                           ctx->now / 1000 + q->from,
			                           ctx->now / 1000 + q->until);

			/* consolidate the sample set on bucketing parameters */
			for (j = 0; (unsigned)j < stack[top]->len; j++) {
				cf_reset(bkt);
				for (set = f->ops[i].data.push.set; set; set = set->next) {
					struct tblock *block;
					uint64_t blkid;

					if (btree_find(set->idx->btree, &blkid, stack[top]->results[j].start) != 0) {
						fprintf(stderr, "failed to btree_find on metric %s\n", f->ops[i].data.push.metric);
						cf_free(bkt);
						free_resultset(stack[top]);
						return -1;
					}

					block = db_findblock(db, blkid);
					while (block) {
						int k;
						bolo_msec_t ts;

						for (k = 0; k < block->cells; k++) {
							ts = tblock_ts(block, k);
							if (ts >= stack[top]->results[j].start && ts <= stack[top]->results[j].finish)
								cf_sample(bkt, tblock_value(block, k));
						}

						block = db_findblock(db, block->next);
					}
				}
				stack[top]->results[j].value = cf_value(bkt);
			}
			break;

		case QOP_AGGR:
			/* sanity check: nesting aggregate functions makes no sense */
			if (aggregated)
				bail("query eval: nested aggregate calls");

			/* sanity check: we should always have at least one rset on stack */
			if (top < 0)
				bail("query eval: insufficient stack for AGGR op");

			/* aggregate the top of the stack down to a smaller resultset */
			tmp = new_resultset(q->aggr.stride, ctx->now / 1000 + q->from,
			                                    ctx->now / 1000 + q->until);
			strides = q->aggr.stride / q->bucket.stride; /* FIXME: make sure aggr % stride == 0 ALWAYS */
			aggr = cf_new(f->ops[i].data.aggr.cf, q->aggr.samples);
			for (j = 0; j < (int)tmp->len; j++) {
				cf_reset(aggr);
				for (k = 0; k < strides; k++) {
					cf_sample(aggr, stack[top]->results[j*strides+k].value);
				}
				tmp->results[j].value = cf_value(aggr);
			}
			cf_free(aggr);

			/* replace the top of the stack with the aggregate (pop+push) */
			free(stack[top]);
			stack[top] = tmp;
			aggregated = 1; /* skip auto-aggregation */
			break;

		case QOP_ADD:
			/* sanity check: we should always have at least two rsets on stack */
			if (top < 1)
				bail("query eval: insufficient stack for ADD op");

			s_resultset_combine(stack[top-1], stack[top], s_add);
			free(stack[top]); stack[top--] = NULL;
			break;

		case QOP_ADDC:
			/* sanity check: we should always have at least one rset on stack */
			if (top < 0)
				bail("query eval: insufficient stack for ADDC op");

			s_resultset_scale(stack[top], f->ops[i].data.imm, s_addc);
			break;

		case QOP_SUB:
			/* sanity check: we should always have at least two rsets on stack */
			if (top < 1)
				bail("query eval: insufficient stack for SUB op");

			s_resultset_combine(stack[top-1], stack[top], s_sub);
			free(stack[top]); stack[top--] = NULL;
			break;

		case QOP_SUBC:
			/* sanity check: we should always have at least one rset on stack */
			if (top < 0)
				bail("query eval: insufficient stack for SUBC op");

			s_resultset_scale(stack[top], f->ops[i].data.imm, s_subc);
			break;

		case QOP_MUL:
			/* sanity check: we should always have at least two rsets on stack */
			if (top < 1)
				bail("query eval: insufficient stack for MUL op");

			s_resultset_combine(stack[top-1], stack[top], s_mul);
			free(stack[top]); stack[top--] = NULL;
			break;

		case QOP_MULC:
			/* sanity check: we should always have at least one rset on stack */
			if (top < 0)
				bail("query eval: insufficient stack for MULC op");

			s_resultset_scale(stack[top], f->ops[i].data.imm, s_mulc);
			break;

		case QOP_DIV:
			/* sanity check: we should always have at least two rsets on stack */
			if (top < 1)
				bail("query eval: insufficient stack for DIV op");

			s_resultset_combine(stack[top-1], stack[top], s_div);
			free(stack[top]); stack[top--] = NULL;
			break;

		case QOP_DIVC:
			/* sanity check: we should always have at least one rset on stack */
			if (top < 0)
				bail("query eval: insufficient stack for DIVC op");

			s_resultset_scale(stack[top], f->ops[i].data.imm, s_divc);
			break;
		}
	}
	return -1; /* unknown error? */
}

int
query_exec(struct query *q, struct db *db, struct query_ctx *ctx)
{
	struct qfield *f;
	struct query_ctx default_ctx;

	if (!ctx) {
		ctx = &default_ctx;
		memset(ctx, 0, sizeof(*ctx));
	}

	if (ctx->now == 0)
		ctx->now = time(NULL) * 1000;

	/* evaluate every selected field */
	for (f = q->select; f; f = f->next)
		if (s_qfield_exec(q, db, ctx, f) != 0)
			return -1;

	return 0;
}

static const char * QERR_strings[] = {
	"(no error)",
	"No such metric",
};
const char *
query_strerror(struct query *q)
{
	if (q->err_num < 1 || q->err_num > QERR__TOP)
		return QERR_strings[0];
	return QERR_strings[q->err_num];
}

#ifdef TEST
/* LCOV_EXCL_START */
TESTS {
	startlog("{{query-test}}", 0, LOG_ERRORS);

	subtest {
		struct query *q;
		int i;
		const char *valid[] = {
			"select cpu",
			"SELECT cpu",
			"SElEcT cpu",

			"select a, b",
			"select a, b, c",

			"select cpu, swap",
			"select cpu, swap, disk",

			"select mem.free",
			"select disk.io.rd@/",

			"select cpu where host = localhost",
			"select cpu where some-tag exists",
			"select cpu where some-tag exist",
			"select cpu where not (some-tag exists)",

			"select cpu where some-tag does not exist",
			"select cpu where some-tag does not exists",

			"select cpu where a=b",
			"select cpu where (a = b)",
			"select cpu where a = b and c = d",
			"select cpu where a = b && c = d",
			"select cpu where a = b or c = d",
			"select cpu where a = b || c = d",
			"select cpu where (a = b || c = d) AND e = f",
			"select cpu where (a=b||c=d)&&e=f",
			"select cpu where (( (a = b) || (c = d) ) && (e = f))",

			"select mem aggregate 1h",
			"select mem aggregate 1.5h",
			"select mem aggregate 1 hour",
			"select mem aggregate 1.5 hour",
			"select mem aggregate 42 hours",
			"select mem aggregate -1.5 hours",

			"select x after 4h ago and before now",
			"select x before now and after 4h ago",
			"select x between 4h ago and now",
			"select x between 4h ago and 2h ago",
			"select x between 4 hours ago and 2 hours ago",
			"select x between -4h and -2h",
			"select x between -4 hours and -2 hours",
			"select x after -4 hours and before -2 hours",
			"select x after -4h",
			"select x after 4h ago",

			"select x between 1.5h ago and now",
			"select x between -1.5h and now",

			/* you can re-arrange the select, where, when, and
			   aggregate clauses to your little hearts content. */
			"select x where a=b between 4h ago and now aggregate 10m",
			"select x where a=b aggregate 10m between 4h ago and now",
			"select x between 4h ago and now where a=b aggregate 10m",
			"select x between 4h ago and now aggregate 10m where a=b",
			"select x aggregate 10m where a=b between 4h ago and now",
			"select x aggregate 10m between 4h ago and now where a=b",
			/* ... */
			"where a=b select x between 4h ago and now aggregate 10m",
			"where a=b select x aggregate 10m between 4h ago and now",
			"where a=b between 4h ago and now select x aggregate 10m",
			"where a=b between 4h ago and now aggregate 10m select x",
			"where a=b aggregate 10m select x between 4h ago and now",
			"where a=b aggregate 10m between 4h ago and now select x",
			/* ... */
			"between 4h ago and now select x where a=b aggregate 10m",
			"between 4h ago and now select x aggregate 10m where a=b",
			"between 4h ago and now where a=b select x aggregate 10m",
			"between 4h ago and now where a=b aggregate 10m select x",
			"between 4h ago and now aggregate 10m select x where a=b",
			"between 4h ago and now aggregate 10m where a=b select x",
			/* ... */
			"aggregate 10m select x where a=b between 4h ago and now",
			"aggregate 10m select x between 4h ago and now where a=b",
			"aggregate 10m where a=b select x between 4h ago and now",
			"aggregate 10m where a=b between 4h ago and now select x",
			"aggregate 10m between 4h ago and now select x where a=b",
			"aggregate 10m between 4h ago and now where a=b select x",

			/* math is a thing we can do */
			"select mem.used + mem.free as mem.total",
			"select cpu.total / cpu.count as cpu.each",
			"select one.less + 1 as one",
			"select 1 + one.less as one",
			"select 1 + 1 + two.less as one",
			"select ((1 * 2) + ((3) * 4)) / what.ever as metric",

			/* functions too */
			"select max(mem.used), max(mem.free) aggregate 5m",

			NULL
		};

		for (i = 0; valid[i]; i++) {
			q = query_parse(valid[i]);
			isnt_null(q, "`%s` should be syntactically valid BQL", valid[i]);
			query_free(q);
		}
	}

	subtest { /* semantic validity */
		struct query *q;
		int i;
		const char *valid[] = {
			"select bytes.used where id = da7fb between 7d ago and now aggregate 15m",

			/* where clause is optional */
			"select bytes.used between 7d ago and now aggregate 15m",

			/* aggregate clause is optional */
			"select bytes.used where id = da7fb between 7d ago and now",

			/* both when and aggregate clauses are optional */
			"select bytes.used where id = da7fb",
			NULL,
		};
		const char *invalid[] = {
			/* select clause is required */
			"where id = blah",
			"aggregate 1h",
			"between 4h ago and now",
			"where id = blah aggregate 1h between 4h ago and now",

			/* cannot run queries from beginning of time */
			"select x before -4h",
			"select x before 4h ago",

			/* cannot run with timeframes that end before they begin */
			"select x before 5h ago and after 4h ago",
			"select x before 4h ago and after 4h ago",
			"select x between 4h ago and 4h ago",

			NULL,
		};

		for (i = 0; valid[i]; i++) {
			q = query_parse(valid[i]);
			isnt_null(q, "`%s` should be semantically valid BQL", valid[i]);
			query_free(q);
		}
		for (i = 0; invalid[i]; i++) {
			q = query_parse(invalid[i]);
			is_null(q, "`%s` should not be semantically valid BQL", invalid[i]);
			query_free(q);
		}
	}

	subtest { /* semantic translation */
		struct query *q;
		const char *query;

		query = "select x aggregate 1d";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr.stride, 86400, "aggregate 1d translates to 86400s");
		query_free(q);

		query = "select x aggregate 2d";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr.stride, 172800, "aggregate 2d translates to 172800s");
		query_free(q);

		query = "select x aggregate 1.1d";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr.stride, 95040, "aggregate 1.1d translates to 95040s");
		query_free(q);


		query = "select x aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr.stride, 3600, "aggregate 1h translates to 3600s");
		query_free(q);

		query = "select x aggregate 2h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr.stride, 7200, "aggregate 2h translates to 7200s");
		query_free(q);

		query = "select x aggregate 1.1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr.stride, 3960, "aggregate 1.1h translates to 3960s");
		query_free(q);

		query = "select x aggregate 1m";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr.stride, 60, "aggregate 1m translates to 60s");
		query_free(q);

		query = "select x aggregate 2m";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr.stride, 120, "aggregate 2m translates to 120s");
		query_free(q);

		query = "select x aggregate 10.05m";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr.stride, 603, "aggregate 0.33m translates to 603s");
		query_free(q);

		query = "select x between 4h ago and now";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->from, -4 * 3600, "4h ago is -14,400s");
		is_int(q->until, 0, "now is 0s");
		query_free(q);

		query = "select x between now and 4h ago";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->from, -4 * 3600, "4h ago is -14,400s");
		is_int(q->until, 0, "now is 0s");
		query_free(q);

		query = "select x after 3h ago";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->from, -3 * 3600, "3h ago is -10,800s");
		is_int(q->until, 0, "now is 0s");
		query_free(q);
	}

	subtest { /* live database querying */
		struct db *db;
		struct dbkey *key;
		struct query *q;
		struct query_ctx ctx;
		const char *query;
		struct resultset *rs;

		memset(&ctx, 0, sizeof(ctx));
		ctx.now = 983552821000; /* Fri, 02 Mar 2001 17:07:01+0000 */

		if (!(db = db_mount("t/data/db/1", key = read_key("decafbad"))))
			BAIL_OUT("failed to mount database at t/data/db/1 successfully");

		/* i.e. `BOLO_NOW='2001-03-02 17:07:01' ./bolo query t/data/db/1/ 'select median(cpu) where env=staging after 6h ago aggregate 1h'` */
		query = "select median(cpu) where env=staging after 6h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 6, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (6 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (5 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4600.24, 0.1,
				"data point #1 (median) value is correct");

		is_unsigned(rs->results[1].start, ctx.now - (5 * 3600000),
				"data point #2 starts on time");
		is_unsigned(rs->results[1].finish, ctx.now - (4 * 3600000) - 1,
				"data point #2 finishes on time");
		is_within(rs->results[1].value, 5574.95, 0.1,
				"data point #2 (median) value is correct");

		is_unsigned(rs->results[2].start, ctx.now - (4 * 3600000),
				"data point #3 starts on time");
		is_unsigned(rs->results[2].finish, ctx.now - (3 * 3600000) - 1,
				"data point #3 finishes on time");
		is_within(rs->results[2].value, 5323.23, 0.1,
				"data point #3 (median) value is correct");

		is_unsigned(rs->results[3].start, ctx.now - (3 * 3600000),
				"data point #4 starts on time");
		is_unsigned(rs->results[3].finish, ctx.now - (2 * 3600000) - 1,
				"data point #4 finishes on time");
		is_within(rs->results[3].value, 5053.85, 0.1,
				"data point #4 (median) value is correct");

		is_unsigned(rs->results[4].start, ctx.now - (2 * 3600000),
				"data point #5 starts on time");
		is_unsigned(rs->results[4].finish, ctx.now - (1 * 3600000) - 1,
				"data point #5 finishes on time");
		is_within(rs->results[4].value, 4730.37, 0.1,
				"data point #5 (median) value is correct");

		is_unsigned(rs->results[5].start, ctx.now - (1 * 3600000),
				"data point #6 starts on time");
		is_unsigned(rs->results[5].finish, ctx.now - (0 * 3600000) - 1,
				"data point #6 finishes on time");
		is_within(rs->results[5].value, 4507.95, 0.1,
				"data point #6 (median) value is correct");

		query_free(q);


		query = "select cpu where env=staging after 6h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 6, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (6 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (5 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4600.24, 0.1,
				"data point #1 (median) value is correct");

		is_unsigned(rs->results[1].start, ctx.now - (5 * 3600000),
				"data point #2 starts on time");
		is_unsigned(rs->results[1].finish, ctx.now - (4 * 3600000) - 1,
				"data point #2 finishes on time");
		is_within(rs->results[1].value, 5574.95, 0.1,
				"data point #2 (median) value is correct");

		is_unsigned(rs->results[2].start, ctx.now - (4 * 3600000),
				"data point #3 starts on time");
		is_unsigned(rs->results[2].finish, ctx.now - (3 * 3600000) - 1,
				"data point #3 finishes on time");
		is_within(rs->results[2].value, 5323.23, 0.1,
				"data point #3 (median) value is correct");

		is_unsigned(rs->results[3].start, ctx.now - (3 * 3600000),
				"data point #4 starts on time");
		is_unsigned(rs->results[3].finish, ctx.now - (2 * 3600000) - 1,
				"data point #4 finishes on time");
		is_within(rs->results[3].value, 5053.85, 0.1,
				"data point #4 (median) value is correct");

		is_unsigned(rs->results[4].start, ctx.now - (2 * 3600000),
				"data point #5 starts on time");
		is_unsigned(rs->results[4].finish, ctx.now - (1 * 3600000) - 1,
				"data point #5 finishes on time");
		is_within(rs->results[4].value, 4730.37, 0.1,
				"data point #5 (median) value is correct");

		is_unsigned(rs->results[5].start, ctx.now - (1 * 3600000),
				"data point #6 starts on time");
		is_unsigned(rs->results[5].finish, ctx.now - (0 * 3600000) - 1,
				"data point #6 finishes on time");
		is_within(rs->results[5].value, 4507.95, 0.1,
				"data point #6 (median) value is correct");

		query_free(q);




		query = "select median(cpu) where host=web1 and env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4871.035, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu * 2) where host=web1 and env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 2 * 4871.035, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu / 2) where host=web1 and env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4871.035 / 2.0, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu - 1000) where host=web1 and env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4871.035 - 1000, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu + 1000) where host=web1 and env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4871.035 + 1000, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu + cpu) where host=web1 and env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4871.035 * 2, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu - cpu) where host=web1 and env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 0, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu / cpu) where host=web1 and env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 1.0, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu * cpu) where host=web1 and env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4871.035 * 4871.035, 10000.0,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu) where host=web1 or env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4337.6437, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu) where host!=web1 after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4107.63, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select median(cpu) where host exists after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_within(rs->results[0].value, 4337.6437, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);

		ok(db_unmount(db) == 0,
			"db_unmount() should succeed");
		free(key->key);
		free(key);
	}
}
/* LCOV_EXCL_STOP */
#endif

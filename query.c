#include "bolo.h"
#include <time.h>

#ifndef DEFAULT_QUERY_RESERVOIR
#define DEFAULT_QUERY_RESERVOIR 2048
#endif

#ifndef DEFAULT_QUERY_AGGREGATE
#define DEFAULT_QUERY_AGGREGATE 300
#endif

#ifndef DEFAULT_QUERY_WINDOW
#define DEFAULT_QUERY_WINDOW 14400
#endif

static struct resultset *
new_resultset(int aggr, int from, int until)
{
	struct resultset *rset;
	size_t n;

	n = (until - from + aggr - 1) / aggr;
	rset = calloc(1, sizeof(*rset) + sizeof(struct result) * n);
	if (!rset)
		bail("malloc failed");

	rset->len = n;
	for (n = 0; n < rset->len; n++) {
		rset->results[n].start  = 1000 * (from + n * aggr);
		rset->results[n].finish = 1000 * (from + n * aggr + aggr) - 1;
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
	struct query *query;

	query = bql_parse(q);
	if (!query) return NULL;

	/* the SELECT clause is required */
	if (!query->select)
		goto fail;

	/* fill in default timeframe */
	if (query->until == 0 && query->from == 0)
		query->from = -DEFAULT_QUERY_WINDOW;

	/* fill in default aggregate */
	if (query->aggr == 0)
		query->aggr = DEFAULT_QUERY_AGGREGATE;

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

int
query_plan(struct query *q, struct db *db)
{
	struct qexpr *expr;
	struct multidx *set, *full, *tmp;

	/* compile conditions into the subset of
	   applicable index subsets for each clause */
	if (q->where)
		qcond_plan(q->where, db);

	/* compile field specifications into
	   the index subsets they reference. */
	for (expr = q->select; expr; expr = expr->next) {
		if (expr->type != EXPR_REF)
			return -1;

		if (hash_get(db->metrics, &full, expr->a) != 0)
			return -1;

		expr->set = NULL;
		for (tmp = full; tmp; tmp = tmp->next) {
			if (qcond_check(q->where, tmp->idx) == 0) {
				if (!(set = malloc(sizeof(*set))))
					bail("malloc failed");
				set->next = expr->set;
				set->idx  = tmp->idx;
				expr->set = set;
			}
		}
	}

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

static void
qexpr_free(struct qexpr *qexpr)
{
	struct qexpr *next;

	while (qexpr) {
		switch (qexpr->type) {
		case EXPR_ADD:
		case EXPR_SUB:
		case EXPR_MULT:
		case EXPR_DIV:
			qexpr_free(qexpr->b);
			qexpr_free(qexpr->a);
			break;

		case EXPR_FUNC:
			free(qexpr->a);
			qexpr_free(qexpr->b);
			break;

		case EXPR_ALIAS:
			qexpr_free(qexpr->a);
			free(qexpr->b);
			break;

		case EXPR_REF:
			free(qexpr->a);
			break;
		}

		free_resultset(qexpr->result);

		next = qexpr->next;
		free(qexpr);
		qexpr = next;
	}
}

void
query_free(struct query *q)
{
	if (!q) return;
	qcond_free(q->where);
	qexpr_free(q->select);
	free(q);
}


int
query_exec(struct query *q, struct db *db, struct query_ctx *ctx)
{
	struct query_ctx default_ctx;
	if (!ctx) {
		ctx = &default_ctx;
		memset(ctx, 0, sizeof(*ctx));
	}

	if (ctx->rsv_depth <= 0)
		ctx->rsv_depth = DEFAULT_QUERY_RESERVOIR;

	if (ctx->now == 0)
		ctx->now = time(NULL) * 1000;

	{
		struct qexpr *qx;

		for (qx = q->select; qx; qx = qx->next) {
			if (qx->type != EXPR_REF) {
				fprintf(stderr, "unable to handle complex select clauses at this time.\n");
				return 1;
			}
		}

		/* for each field, allocate a result series,
		   and retrieve the data for the given time
		   frame from the tblocks. */
		for (qx = q->select; qx; qx = qx->next) {
			struct multidx *set;
			struct rsv *rsv;
			struct resultset *rset;
			int i;

			rsv = rsv_new(ctx->rsv_depth);
			rset = new_resultset(q->aggr,
			                     ctx->now / 1000 + q->from,
			                     ctx->now / 1000 + q->until);
			rset->key = qx->a;

			for (i = 0; (unsigned)i < rset->len; i++) {
				rsv_reset(rsv);

				for (set = qx->set; set; set = set->next) {
					struct tslab  *slab;
					struct tblock *block;
					uint64_t blkid;

					if (btree_find(set->idx->btree, &blkid, rset->results[i].start) != 0) {
						fprintf(stderr, "failed to btree_find on metric %s\n", (char *)qx->a);
						rsv_free(rsv);
						free_resultset(rset);
						return 1;
					}

					/* this is s_findblock + s_findslab, from db.o... */
					block = NULL;
					for_each(slab, &db->slab, l) {
						if (slab->number == tslab_number(blkid)) {
							block = slab->blocks + tblock_number(blkid);
							break;
						}
					}

					while (block) {
						int j;
						bolo_msec_t ts;

						for (j = 0; j < block->cells; j++) {
							ts = tblock_ts(block, j);
							if (ts >= rset->results[i].start && ts <= rset->results[i].finish)
								rsv_sample(rsv, tblock_value(block, j));
						}

						/* FIXME: i think we need to thread the blocks */
						block = NULL; /* FIXME */
					}

					rset->results[i].value = rsv_median(rsv);
					rset->results[i].n     = rsv->n;
				}
			}

			qx->result = rset;
		}
	}
	return 0;
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
		is_int(q->aggr, 86400, "aggregate 1d translates to 86400s");
		query_free(q);

		query = "select x aggregate 2d";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr, 172800, "aggregate 2d translates to 172800s");
		query_free(q);

		query = "select x aggregate 1.1d";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr, 95040, "aggregate 1.1d translates to 95040s");
		query_free(q);


		query = "select x aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr, 3600, "aggregate 1h translates to 3600s");
		query_free(q);

		query = "select x aggregate 2h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr, 7200, "aggregate 2h translates to 7200s");
		query_free(q);

		query = "select x aggregate 1.1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr, 3960, "aggregate 1.1h translates to 3960s");
		query_free(q);

		query = "select x aggregate 1m";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr, 60, "aggregate 1m translates to 60s");
		query_free(q);

		query = "select x aggregate 2m";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr, 120, "aggregate 2m translates to 120s");
		query_free(q);

		query = "select x aggregate 10.05m";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		is_int(q->aggr, 603, "aggregate 0.33m translates to 603s");
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
		struct query *q;
		struct query_ctx ctx;
		const char *query;
		struct resultset *rs;

		memset(&ctx, 0, sizeof(ctx));
		ctx.now = 983552821000; /* Fri, 02 Mar 2001 17:07:01+0000 */

		if (!(db = db_mount("t/data/db/1")))
			BAIL_OUT("failed to mount database at t/data/db/1 successfully");

		/* i.e. `BOLO_NOW='2001-03-02 17:07:01' ./bolo query t/data/db/1/ 'select cpu where env=staging after 6h ago aggregate 1h'` */
		query = "select cpu where env=staging after 6h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_string(rs->key, "cpu", "resultset is for cpu metric");
		is_unsigned(rs->len, 6, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (6 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (5 * 3600000) - 1,
				"data point #1 finishes on time");
		is_unsigned(rs->results[0].n, 60,
				"data point #1 has 60 minutely (1h worth) of samples");
		is_within(rs->results[0].value, 4600.24, 0.1,
				"data point #1 (median) value is correct");

		is_unsigned(rs->results[1].start, ctx.now - (5 * 3600000),
				"data point #2 starts on time");
		is_unsigned(rs->results[1].finish, ctx.now - (4 * 3600000) - 1,
				"data point #2 finishes on time");
		is_unsigned(rs->results[1].n, 60,
				"data point #2 has 60 minutely (1h worth) of samples");
		is_within(rs->results[1].value, 5574.95, 0.1,
				"data point #2 (median) value is correct");

		is_unsigned(rs->results[2].start, ctx.now - (4 * 3600000),
				"data point #3 starts on time");
		is_unsigned(rs->results[2].finish, ctx.now - (3 * 3600000) - 1,
				"data point #3 finishes on time");
		is_unsigned(rs->results[2].n, 60,
				"data point #3 has 60 minutely (1h worth) of samples");
		is_within(rs->results[2].value, 5323.23, 0.1,
				"data point #3 (median) value is correct");

		is_unsigned(rs->results[3].start, ctx.now - (3 * 3600000),
				"data point #4 starts on time");
		is_unsigned(rs->results[3].finish, ctx.now - (2 * 3600000) - 1,
				"data point #4 finishes on time");
		is_unsigned(rs->results[3].n, 60,
				"data point #4 has 60 minutely (1h worth) of samples");
		is_within(rs->results[3].value, 5053.85, 0.1,
				"data point #4 (median) value is correct");

		is_unsigned(rs->results[4].start, ctx.now - (2 * 3600000),
				"data point #5 starts on time");
		is_unsigned(rs->results[4].finish, ctx.now - (1 * 3600000) - 1,
				"data point #5 finishes on time");
		is_unsigned(rs->results[4].n, 60,
				"data point #5 has 60 minutely (1h worth) of samples");
		is_within(rs->results[4].value, 4730.37, 0.1,
				"data point #5 (median) value is correct");

		is_unsigned(rs->results[5].start, ctx.now - (1 * 3600000),
				"data point #6 starts on time");
		is_unsigned(rs->results[5].finish, ctx.now - (0 * 3600000) - 1,
				"data point #6 finishes on time");
		is_unsigned(rs->results[5].n, 60,
				"data point #6 has 60 minutely (1h worth) of samples");
		is_within(rs->results[5].value, 4507.95, 0.1,
				"data point #6 (median) value is correct");

		query_free(q);



		query = "select cpu where host=web1 and env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_string(rs->key, "cpu", "resultset is for cpu metric");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_unsigned(rs->results[0].n, 60,
				"data point #1 has 60 minutely (1h worth) of samples");
		is_within(rs->results[0].value, 4871.035, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select cpu where host=web1 or env=prod after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_string(rs->key, "cpu", "resultset is for cpu metric");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_unsigned(rs->results[0].n, 180,
				"data point #1 has 180 minutely (3x 1h worth) of samples");
		is_within(rs->results[0].value, 4567.1775, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select cpu where host!=web1 after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_string(rs->key, "cpu", "resultset is for cpu metric");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_unsigned(rs->results[0].n, 60,
				"data point #1 has 60 minutely (1h worth) of samples");
		is_within(rs->results[0].value, 4107.63, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);



		query = "select cpu where host exists after 1h ago aggregate 1h";
		q = query_parse(query);
		isnt_null(q, "`%s` should be semantically valid BQL", query);
		ok(query_plan(q, db) == 0, "planning `%s` against database should succeed", query);
		ok(query_exec(q, db, &ctx) == 0, "executing `%s` against database should succeed", query);

		isnt_null(q->select, "`%s` has at least one selected series", query);
		is_null(q->select->next, "`%s` has only one selected series", query);

		rs = q->select->result;
		isnt_null(rs, "executed query has a resultset");
		is_string(rs->key, "cpu", "resultset is for cpu metric");
		is_unsigned(rs->len, 1, "resultset has approprate number of data points");

		/* check the actual values */
		is_unsigned(rs->results[0].start, ctx.now - (1 * 3600000),
				"data point #1 starts on time");
		is_unsigned(rs->results[0].finish, ctx.now - (0 * 3600000) - 1,
				"data point #1 finishes on time");
		is_unsigned(rs->results[0].n, 180,
				"data point #1 has 180 minutely (3x 1h worth) of samples");
		is_within(rs->results[0].value, 4567.1775, 0.1,
				"data point #1 (median) value is correct");

		query_free(q);
	}
}
/* LCOV_EXCL_STOP */
#endif

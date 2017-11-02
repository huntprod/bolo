#include "bolo.h"

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
		query->from = -14400; /* 4h ago */

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

#ifdef TEST
TESTS {
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
		isnt_null(q, "`$s` should be semantically valid BQL", query);
		is_int(q->from, -4 * 3600, "4h ago is -14,400s");
		is_int(q->until, 0, "now is 0s");
		query_free(q);

		query = "select x between now and 4h ago";
		q = query_parse(query);
		isnt_null(q, "`$s` should be semantically valid BQL", query);
		is_int(q->from, -4 * 3600, "4h ago is -14,400s");
		is_int(q->until, 0, "now is 0s");
		query_free(q);

		query = "select x after 3h ago";
		q = query_parse(query);
		isnt_null(q, "`$s` should be semantically valid BQL", query);
		is_int(q->from, -3 * 3600, "3h ago is -10,800s");
		is_int(q->until, 0, "now is 0s");
		query_free(q);
	}
}
#endif

%{
#include "bql.h"

static void
_mergeb(struct bucket *a, struct bucket *b, struct bucket *c)
{
	a->samples = c->samples ? c->samples : b->samples;
	a->stride  = c->stride  ? c->stride  : b->stride ;
	a->cf      = c->cf      ? c->cf      : b->cf     ;
}
#define mergeb(a,b,c) _mergeb(&(a),&(b),&(c))

static struct {
	char *name;
	int   cf;
} cftab[] = {
	{  "min",       CF_MIN     },
	{  "max",       CF_MAX     },
	{  "sum",       CF_SUM     },
	{  "mean",      CF_MEAN    },
	{  "median",    CF_MEDIAN  },
	{  "stdev",     CF_STDEV   },
	{  "variance",  CF_VAR     },
	{  "delta",     CF_DELTA   },
	{ 0, 0 }
};

static int
cf(const char *name)
{
	int i;
	for (i = 0; cftab[i].name; i++)
		if (strcasecmp(name, cftab[i].name) == 0)
			return cftab[i].cf;

	return 0;
}

#define EXPR_REF   1
#define EXPR_ADD   2
#define EXPR_SUB   3
#define EXPR_MULT  4
#define EXPR_DIV   5
#define EXPR_FUNC  6
#define EXPR_NUM   7

struct qexpr {
	struct qexpr *next;
	int           type;
	void         *a, *b;
};

static struct qcond *
cond(int op, void *a, void *b)
{
	struct qcond *c;

	c = xmalloc(sizeof(*c));
	c->op = op;
	c->a  = a;
	c->b  = b;
	return c;
}

static struct qexpr *
qexpr(int type, void *a, void *b)
{
	struct qexpr *e;

	if (type == EXPR_NUM) {
		/* a is a double*, b is unused.
		   we need to allocate a reference */
		b = xmalloc(sizeof(double));
		*(double *)b = *(double *)a;
		a = b; b = NULL;
	}

	e = xcalloc(1, sizeof(*e));
	e->type = type;
	e->a    = a;
	e->b    = b;
	return e;
}

static void
qexpr_free(struct qexpr *qexpr)
{
	struct qexpr *next_qexpr;

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

		case EXPR_REF:
		case EXPR_NUM:
			free(qexpr->a);
			break;
		}

		next_qexpr = qexpr->next;
		free(qexpr);
		qexpr = next_qexpr;
	}
}

#define NO_GRANULARITY        0
#define BUCKET_GRANULARITY    1
#define AGGREGATE_GRANULARITY 2

static int
granularity(struct qexpr *qx)
{
	int a, b;

	switch (qx->type) {
	default: return NO_GRANULARITY;

	case EXPR_ADD:
	case EXPR_SUB:
	case EXPR_MULT:
	case EXPR_DIV:
		a = granularity(qx->a);
		b = granularity(qx->b);
		if (a != NO_GRANULARITY && b != NO_GRANULARITY && a != b)
			return -1;
		return a == NO_GRANULARITY ? b : a;

	case EXPR_REF:  return BUCKET_GRANULARITY;
	case EXPR_NUM:  return NO_GRANULARITY;
	case EXPR_FUNC: return granularity(qx->b);
	}
}

static int
aggregates(struct qexpr *qx, int seen)
{
	switch (qx->type) {
	default: return 0;

	case EXPR_FUNC:
		if (seen) return -1;
		return aggregates(qx->b, 1);

	case EXPR_ADD:
	case EXPR_SUB:
	case EXPR_MULT:
	case EXPR_DIV:
		if (aggregates(qx->a, seen) < 0) return -1;
		if (aggregates(qx->b, seen) < 0) return -1;
		return 0;
	}
}

static int
verify(struct qexpr *qx)
{
	if (granularity(qx)   < 0) return -1;
	if (aggregates(qx, 0) < 0) return -1;
	return 0;
}

static void
simplify(struct qexpr *qx)
{
	double *a, b; /* for (NUM op NUM) simplification */

	if (!qx) return;
	switch (qx->type) {
	case EXPR_ADD:
	case EXPR_SUB:
	case EXPR_MULT:
	case EXPR_DIV:
		simplify(qx->a);
		simplify(qx->b);

		/* math on two constants; simplify to static computation result */
		if (qx->a && ((struct qexpr *)(qx->a))->type == EXPR_NUM
		 && qx->b && ((struct qexpr *)(qx->b))->type == EXPR_NUM) {

			a = xmalloc(sizeof(double));
			b = *(double *)((struct qexpr *)(qx->b))->a;

			memcpy(a, ((struct qexpr *)(qx->a))->a, sizeof(double));
			switch (qx->type) {
			case EXPR_ADD:  *a += b; break;
			case EXPR_SUB:  *a -= b; break;
			case EXPR_MULT: *a *= b; break;
			case EXPR_DIV:  *a /= b; break; /* FIXME: DIV/0 */
			default: return;
			}

			free(((struct qexpr *)(qx->a))->a); free(qx->a);
			free(((struct qexpr *)(qx->b))->a); free(qx->b);
			qx->type = EXPR_NUM;
			qx->a    = a;
			qx->b    = NULL;
			return;
		}
		break;
	}
}

static int
opcodes(struct qexpr *qx)
{
	switch (qx->type) {
	default:         return 0;

	case EXPR_ADD:
	case EXPR_SUB:
	case EXPR_MULT:
	case EXPR_DIV:   return opcodes(qx->a)
	                      + opcodes(qx->b)
	                      + 1;

	case EXPR_FUNC:  return opcodes(qx->b)
	                      + 1;

	case EXPR_REF:   return 1;
	}
}

static struct qop *
opify(struct qexpr *qx, struct qop *next)
{
	int a, b;

	switch (qx->type) {
	default: return next;

	case EXPR_REF:
		next->code = QOP_PUSH;
		next->data.push.metric = strdup((char *)(qx->a));
		return next+1;

	case EXPR_FUNC:
		next = opify(qx->b, next);
		next->code = QOP_AGGR;
		next->data.aggr.cf = cf((char *)(qx->a));
		return next+1;

	case EXPR_ADD:
	case EXPR_SUB:
	case EXPR_MULT:
	case EXPR_DIV:
		next = opify(qx->a, next);
		next = opify(qx->b, next);
		switch (qx->type) {
		case EXPR_ADD:  next->code = QOP_ADD; break;
		case EXPR_SUB:  next->code = QOP_SUB; break;
		case EXPR_MULT: next->code = QOP_MUL; break;
		case EXPR_DIV:  next->code = QOP_DIV; break;
		}

		a = ((struct qexpr *)(qx->a))->type;
		b = ((struct qexpr *)(qx->b))->type;
		CHECK(!(a == EXPR_NUM && b == EXPR_NUM),
			"asked to opify a( NUM op NUM), which we should never see");
		if (a == EXPR_NUM) {
			/* NUM op METRIC */
			next->code++; /* ADD -> ADDC */
			next->data.imm = *(double *)((struct qexpr *)(qx->a))->a;
		}
		if (b == EXPR_NUM) {
			/* METRIC op NUM */
			next->code++; /* ADD -> ADDC */
			next->data.imm = *(double *)((struct qexpr *)(qx->b))->a;
		}
		return next+1;
	}
}

static struct qfield *
pushf(struct qfield *lst, struct qfield *node)
{
	/* FIXME: this is reversed... */
	node->next = lst;
	return node;
}


static struct qfield *
qfield(struct qexpr *e, char *name)
{
	int n;
	struct qfield *f;

	simplify(e);

	/* auto-alias simple REF fields */
	if (!name && e->type == EXPR_REF)
		name = strdup((char *)(e->a));

	f = xmalloc(sizeof(struct qfield));
	f->name = name;

	if (verify(e) != 0) {
		f->ops = NULL;

	} else {
		n = opcodes(e) + 1; /* append QOP_RETURN */
		f->ops = xcalloc(n, sizeof(struct qop));
		opify(e, f->ops);
		f->ops[n-1].code = QOP_RETURN;
	}

	qexpr_free(e);
	return f;
}

%}

%token T_AFTER
%token T_AGGREGATE
%token T_AGO
%token T_AND
%token T_AS
%token T_BEFORE
%token T_BETWEEN
%token T_BUCKET
%token T_BY
%token T_DAILY
%token T_DAYS
%token T_DOES
%token T_EQ
%token T_EXIST
%token T_HOURLY
%token T_HOURS
%token T_MINUTELY
%token T_MINUTES
%token T_NE
%token T_NOT
%token T_NOW
%token T_OR
%token T_OVER
%token T_PER
%token T_SAMPLES
%token T_SECONDLY
%token T_SECONDS
%token T_SELECT
%token T_USING
%token T_WHERE

%token <number> T_NUMBER
%token <number> T_TIME
%token <text>   T_DQSTRING
%token <text>   T_SQSTRING
%token <text>   T_BAREWORD

%type <number> timespan
%type <number> aggrspan
%type <number> walltime
%type <number> unit

%type <text> tagvalue

%type <range> when_clause
%type <range> when_between
%type <range> when_before
%type <range> when_after

%type <bucket> aggr_clause
%type <bucket> aggr_subclauses
%type <bucket> aggr_subclause

%type <bucket> bucket_clause
%type <bucket> bucket_subclauses
%type <bucket> bucket_subclause
%type <bucket> bucket_cf

%type <qcond> where_clause
%type <qcond> cond

%type <qfield> select_clause
%type <qfield> fields
%type <qfield> field

%type <qexpr>  expr

%type <query> query

%left T_NOT
%left T_EQ T_NE
%left T_AND T_OR

%%

query :                     { $$ = QUERY  = xmalloc(sizeof(struct query)); }
      | query select_clause { $$->select  = $2; }
      | query where_clause  { $$->where   = $2; }
      | query when_clause   { $$->from    = $2.from;
                              $$->until   = $2.until; }
      | query aggr_clause   { mergeb($$->aggr,   $2, $$->aggr);   }
      | query bucket_clause { mergeb($$->bucket, $2, $$->bucket); }
      ;

select_clause: T_SELECT fields { $$ = $2; }
             ;

fields: field            { $$ = $1; }
      | fields ',' field { $$ = pushf($1, $3); }
      ;

field: expr                 { $$ = qfield($1, NULL); }
     | expr T_AS T_BAREWORD { $$ = qfield($1, $3);   }
     ;

expr: T_BAREWORD              { $$ = qexpr(EXPR_REF,  $1, NULL); }
    | T_NUMBER                { $$ = qexpr(EXPR_NUM, &$1, NULL); }
    | '(' expr ')'            { $$ = $2; }
    | expr '+' expr           { $$ = qexpr(EXPR_ADD,  $1, $3); }
    | expr '-' expr           { $$ = qexpr(EXPR_SUB,  $1, $3); }
    | expr '*' expr           { $$ = qexpr(EXPR_MULT, $1, $3); }
    | expr '/' expr           { $$ = qexpr(EXPR_DIV,  $1, $3); }
    | T_BAREWORD '(' expr ')' { $$ = qexpr(EXPR_FUNC, $1, $3); }
    ;

where_clause: T_WHERE cond { $$ = $2; }
            ;

cond: '(' cond ')'    { $$ = $2; }
    | cond T_AND cond { $$ = cond(COND_AND, $1, $3); }
    | cond T_OR  cond { $$ = cond(COND_OR,  $1, $3); }
    | T_NOT cond      { $$ = cond(COND_NOT, $2, NULL); }

    | T_BAREWORD T_EXIST               { $$ = cond(COND_EXIST, $1, NULL); }
    | T_BAREWORD T_DOES T_NOT T_EXIST  { $$ = cond(COND_NOT, cond(COND_EXIST, $1, NULL), NULL); }

    | T_BAREWORD T_EQ tagvalue { $$ = cond(COND_EQ, $1, $3); }
    | T_BAREWORD T_NE tagvalue { $$ = cond(COND_NOT, cond(COND_EQ, $1, $3), NULL); }
    ;

tagvalue: T_DQSTRING { $$ = $1; }
        | T_SQSTRING { $$ = $1; }
        | T_BAREWORD { $$ = $1; }
        ;

aggr_clause: T_AGGREGATE aggr_subclause aggr_subclauses { mergeb($$, $2, $3); }
           ;

aggr_subclauses: { memset(&($$), 0, sizeof($$)); }
               | aggr_subclauses aggr_subclause { mergeb($$, $1, $2); }
               ;

aggr_subclause: timespan                   { $$.stride = (int)$1; }
              | aggrspan                   { $$.stride = (int)$1; }
              | T_BY T_BAREWORD            { $$.cf = cf($2); free($2); }
              | T_USING T_NUMBER T_SAMPLES { $$.samples = $2; }
              ;

timespan: T_NUMBER unit { $$ = $1 * $2; }
        | T_TIME        { $$ = $1; }
        ;

aggrspan: T_DAILY         { $$ = 86400; }
        | T_PER T_DAYS    { $$ = 86400; }
        | T_HOURLY        { $$ = 3600;  }
        | T_PER T_HOURS   { $$ = 3600;  }
        | T_MINUTELY      { $$ = 60;    }
        | T_PER T_MINUTES { $$ = 60;    }
        | T_SECONDLY      { $$ = 1;     }
        | T_PER T_SECONDS { $$ = 1;     }
        ;

unit: T_DAYS     { $$ = 86400; }
    | T_HOURS    { $$ = 3600;  }
    | T_MINUTES  { $$ = 60;    }
    | T_SECONDS  { $$ = 1;     }
    ;

bucket_clause: T_BUCKET bucket_subclause bucket_subclauses { mergeb($$, $2, $3); }
             ;

bucket_subclauses: { memset(&($$), 0, sizeof($$)); }
                 | bucket_subclauses bucket_subclause { mergeb($$, $1, $2); }
                 ;

bucket_subclause: T_USING T_NUMBER T_SAMPLES { $$.samples = $2; }
                | bucket_cf T_OVER timespan  { $$.cf      = $1.cf;
                                               $$.stride  = $3; }
                ;

bucket_cf: T_BAREWORD { $$.cf = cf($1); free($1); }

when_clause: when_after                    { $$.from = $1.from; $$.until = 0; }
           | when_after  T_AND when_before { $$.from = $1.from; $$.until = $3.until; }
           | when_before T_AND when_after  { $$.from = $3.from; $$.until = $1.until; }
           | when_between                  { $$ = $1; }
           ;

when_after: T_AFTER walltime { $$.from = $2; }
          ;

when_before: T_BEFORE walltime { $$.until = $2; }
           ;

when_between: T_BETWEEN walltime T_AND walltime {
                if ($2 > $4) { $$.from = $4; $$.until = $2; }
                else         { $$.from = $2; $$.until = $4; }
              }
            ;

walltime: timespan T_AGO { $$ = -1 * $1; }
        | timespan       { $$ =      $1; }
        | T_NOW          { $$ = 0; }
        ;

%%

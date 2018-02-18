%{
#include "bql.h"

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

	case EXPR_REF:
	case EXPR_FUNC:  return 1;
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

	n = opcodes(e) + 1; /* append QOP_RETURN */
	f->ops = xcalloc(n, sizeof(struct qop));
	opify(e, f->ops);
	f->ops[n-1].code = QOP_RETURN;

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
%token T_PER
%token T_SECONDLY
%token T_SECONDS
%token T_SELECT
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

%type <aggrwin> aggr_clause

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
      | query aggr_clause   { $$->aggr    = $2; }
      | query when_clause   { $$->from    = $2.from;
                              $$->until   = $2.until; }
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

aggr_clause: T_AGGREGATE timespan { $$ = (int)$2; }
           | T_AGGREGATE aggrspan { $$ = (int)$2; }
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

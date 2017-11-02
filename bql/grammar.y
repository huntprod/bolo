%{
#include "bql.h"

static struct query *
query(void)
{
	struct query *q;

	q = calloc(1, sizeof(*q));
	if (!q)
		bail("malloc failed in bql_parse");

	return q;
}

static struct qcond *
cond(int op, void *a, void *b)
{
	struct qcond *c;

	c = malloc(sizeof(*c));
	if (!c)
		bail("malloc failed in bql_parse");
	c->op = op;
	c->a  = a;
	c->b  = b;
	return c;
}

static struct qexpr *
qexpr(int type, void *a, void *b)
{
	struct qexpr *e;

	e = calloc(1, sizeof(*e));
	if (!e)
		bail("malloc failed in bql_parse");
	e->type = type;
	e->a    = a;
	e->b    = b;
	return e;
}

static struct qexpr *
pushex(struct qexpr *lst, struct qexpr *node)
{
	/* FIXME: this is reversed... */
	node->next = lst;
	return node;
}

%}

%token T_AFTER
%token T_AGGREGATE
%token T_AGO
%token T_AND
%token T_AS
%token T_BEFORE
%token T_BETWEEN
%token T_DAYS
%token T_DOES
%token T_EQ
%token T_EXIST
%token T_HOURS
%token T_MINUTES
%token T_NE
%token T_NOT
%token T_NOW
%token T_OR
%token T_SELECT
%token T_SECONDS
%token T_WHERE

%token <number> T_NUMBER
%token <number> T_TIME
%token <text>   T_DQSTRING
%token <text>   T_SQSTRING
%token <text>   T_BAREWORD

%type <number> timespan
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

%type <qexprs> select_clause
%type <qexprs> exprs
%type <qexpr>  expr

%type <query> query

%left T_NOT
%left T_EQ T_NE
%left T_AND T_OR

%%

query :                     { $$ = QUERY = query(); }
      | query select_clause { $$->select = $2; }
      | query where_clause  { $$->where  = $2; }
      | query aggr_clause   { $$->aggr   = $2; }
      | query when_clause   { $$->from   = $2.from;
                              $$->until  = $2.until; }
      ;

select_clause: T_SELECT exprs { $$ = $2; }
             ;

exprs: expr            { $$ = $1; }
     | exprs ',' expr  { $$ = pushex($1, $3); }
     ;

expr: T_BAREWORD              { $$ = qexpr(EXPR_REF,   $1, NULL); }
    | expr T_AS T_BAREWORD    { $$ = qexpr(EXPR_ALIAS, $1, $3); }
    | '(' expr ')'            { $$ = $2; }
    | expr '+' expr           { $$ = qexpr(EXPR_ADD,   $1, $3); }
    | expr '-' expr           { $$ = qexpr(EXPR_SUB,   $1, $3); }
    | expr '*' expr           { $$ = qexpr(EXPR_MULT,  $1, $3); }
    | expr '/' expr           { $$ = qexpr(EXPR_DIV,   $1, $3); }
    | T_BAREWORD '(' expr ')' { $$ = qexpr(EXPR_FUNC,  $1, $3); }
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
           ;

timespan: T_NUMBER unit { $$ = $1 * $2; }
        | T_TIME        { $$ = $1; }
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

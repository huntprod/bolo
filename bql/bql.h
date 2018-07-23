#ifndef BQL_BQL_H
#define BQL_BQL_H

#include "../bolo.h"
#include "public.h"

/* global query value, shared between lexer / parser */
struct bql_query *QUERY;

struct range {
	int from;
	int until;
};

typedef union {
	double  number;
	char   *text;
	int     integer;

	struct {
		int from;
		int until;
	} range;

	struct bql_cons   qcons;
	struct bql_field *qfield;
	struct bql_cond  *qcond;
	struct bql_query *query;

	struct qexpr  *qexpr;
} YYSTYPE;
#define YYSTYPE_IS_DECLARED 1

int yylex(void);
int yyparse(void);
int yyerror(const char *);

#endif

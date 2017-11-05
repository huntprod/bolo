%{
#include "bql.h"
#include "grammar.h"

int yyerror(const char *err)
{
	fprintf(stderr, "lexer: %s\n", err);
	return -1;
}

struct {
	char *name;
	int   token;
} symtab[] = {
	{  "after",      T_AFTER      },
	{  "aggr",       T_AGGREGATE  },
	{  "aggregate",  T_AGGREGATE  },
	{  "ago",        T_AGO        },
	{  "and",        T_AND        },
	{  "as",         T_AS         },
	{  "before",     T_BEFORE     },
	{  "between",    T_BETWEEN    },
	{  "day",        T_DAYS       },
	{  "days",       T_DAYS       },
	{  "does",       T_DOES       },
	{  "exist",      T_EXIST      },
	{  "exists",     T_EXIST      },
	{  "hour",       T_HOURS      },
	{  "hours",      T_HOURS      },
	{  "minute",     T_MINUTES    },
	{  "minutes",    T_MINUTES    },
	{  "not",        T_NOT        },
	{  "now",        T_NOW        },
	{  "or",         T_OR         },
	{  "select",     T_SELECT     },
	{  "second",     T_SECONDS    },
	{  "seconds",    T_SECONDS    },
	{  "where",      T_WHERE      },
	{ 0, 0 },
};
%}

%option noinput
%option nounput

%%
[ \t\n]+    /* ignore whitespace */;

[a-zA-Z][^,|&()!= \t\n]* {
  int i;
  for (i = 0; symtab[i].name; i++)
    if (strcasecmp(symtab[i].name, yytext) == 0)
      return symtab[i].token;

  yylval.text = strdup(yytext);
  return T_BAREWORD;
}
-?[0-9]+(\.[0-9]+)?[dhms] {
  char unit = yytext[yyleng-1];
  yytext[yyleng-1] = '\0';

  yylval.number = strtod(yytext, NULL);
  switch (unit) {
  case 'd': yylval.number *= 86400.0; break;
  case 'h': yylval.number *= 3600.0;  break;
  case 'm': yylval.number *= 60.0;    break;
  }
  return T_TIME;
}
-?[0-9]+(\.[0-9]+)? {
  yylval.number = atoi(yytext); /* FIXME: should be strtold */
  return T_NUMBER;
}
"&&" { return T_AND; }
"||" { return T_OR;  }
"="  { return T_EQ;  }
"!=" { return T_NE;  }
\"(\\.|[^"\\])*\" {
  yylval.text = strdup(yytext+1);
  yylval.text[yyleng - 2] = '\0';
  return T_DQSTRING;
}
\'(\\'|[^'])*\' {
  yylval.text = strdup(yytext+1);
  yylval.text[yyleng - 2] = '\0';
  return T_SQSTRING;
}

. { return yytext[0]; }

%%

int yywrap() {
	return -1;
}

struct query *
bql_parse(const char *q)
{
	int rc;
	YY_BUFFER_STATE buf;

	QUERY = NULL;

	buf = yy_scan_string(q);
	rc = yyparse();
	yy_delete_buffer(buf);

	if (rc == 0)
		return QUERY;

	query_free(QUERY);
	QUERY = NULL;
	return NULL;
}

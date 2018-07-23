%{
#include "bql.h"
#include "public.h"
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
	{  "aggregate",  T_AGGREGATE  },
	{  "aggr",       T_AGGREGATE  },
	{  "ago",        T_AGO        },
	{  "and",        T_AND        },
	{  "as",         T_AS         },
	{  "before",     T_BEFORE     },
	{  "between",    T_BETWEEN    },
	{  "bucket",     T_BUCKET     },
	{  "by",         T_BY         },
	{  "daily",      T_DAILY      },
	{  "days",       T_DAYS       },
	{  "day",        T_DAYS       },
	{  "does",       T_DOES       },
	{  "exists",     T_EXIST      },
	{  "exist",      T_EXIST      },
	{  "hourly",     T_HOURLY     },
	{  "hours",      T_HOURS      },
	{  "hour",       T_HOURS      },
	{  "minutely",   T_MINUTELY   },
	{  "minutes",    T_MINUTES    },
	{  "minute",     T_MINUTES    },
	{  "not",        T_NOT        },
	{  "now",        T_NOW        },
	{  "or",         T_OR         },
	{  "over",       T_OVER       },
	{  "per",        T_PER        },
	{  "samples",    T_SAMPLES    },
	{  "secondly",   T_SECONDLY   },
	{  "seconds",    T_SECONDS    },
	{  "second",     T_SECONDS    },
	{  "select",     T_SELECT     },
	{  "raw",        T_RAW        },
	{  "using",      T_USING      },
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
  case 'd': yylval.number *= 86400.0 * 1000.0; break;
  case 'h': yylval.number *= 3600.0  * 1000.0; break;
  case 'm': yylval.number *= 60.0    * 1000.0; break;
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

static void
free_cond(struct bql_cond *c)
{
	if (!c) return;

	switch (c->op) {
	case COND_AND:
	case COND_OR:  free_cond(c->b);
	case COND_NOT: free_cond(c->a);
	               break;

	case COND_EQ:  free(c->a);
	               free(c->b);
	               break;

	case COND_EXIST: free(c->a);
	                 break;
	}

	free(c);
}

void
bql_free(struct bql_query *q)
{
	int i;
	struct bql_field *f, *_f;

	if (!q) return;
	free_cond(q->where);

	for (f = q->select; f ;) {
		for (i = 0; f->ops && f->ops[i].code != QOP_RETURN; i++) {
			switch (f->ops[i].code) {
			case QOP_PUSH:
				free(f->ops[i].data.push.metric);
			/* FIXME
				for (set = f->ops[i].data.push.set; set; ) {
					_set = set->next;
					free(set);
					set = _set;
				}
			*/
				break;
			}
		}
		free(f->ops);
		free(f->name);
		//free(f->result);

		_f = f->next;
		free(f);
		f = _f;
	}

	free(q->errdat);
	free(q);
}

struct bql_query *
bql_parse(const char *q)
{
	int rc, need, n;
	YY_BUFFER_STATE buf;
	struct bql_field *f;

	QUERY = NULL;

	buf = yy_scan_string(q);
	rc = yyparse();
	yy_delete_buffer(buf);

	if (rc != 0)
		goto fail;

	if (!QUERY->select)
		goto fail;

	QUERY->nfields = n = 0;
	for (f = QUERY->select; f; f = f->next) {
		QUERY->nfields++;
		if (!f->ops)
			goto fail;

		if (!f->name) {
			n++;
			need = snprintf(NULL, 0, "metric_%d", n);
			f->name = xcalloc(need+1, sizeof(char));
			snprintf(f->name, need+1, "metric_%d", n);
		}
	}

	/* fill in default timeframe */
	if (QUERY->until == 0 && QUERY->from == 0)
		QUERY->from = -DEFAULT_QUERY_WINDOW;

	/* fill in default aggregate */
	if (!QUERY->aggregate.cf)      QUERY->aggregate.cf      = DEFAULT_QUERY_CF;
	if (!QUERY->aggregate.samples) QUERY->aggregate.samples = DEFAULT_QUERY_SAMPLES;
	if (!QUERY->aggregate.stride)  QUERY->aggregate.stride  = DEFAULT_BUCKET_STRIDE;

	/* fill in default bucketing parameters */
	if (!QUERY->bucket.cf)      QUERY->bucket.cf      = DEFAULT_QUERY_CF;
	if (!QUERY->bucket.samples) QUERY->bucket.samples = DEFAULT_QUERY_SAMPLES;
	if (!QUERY->bucket.stride)  QUERY->bucket.stride  = DEFAULT_BUCKET_STRIDE;

	/* check the from..until range */
	if (QUERY->until <= QUERY->from)
		goto fail;

	return QUERY;

fail:
	bql_free(QUERY);
	QUERY = NULL;
	return NULL;
}

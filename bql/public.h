#ifndef BQL_PUBLIC_H
#define BQL_PUBLIC_H

#define BQL_COND_EQ    1
#define BQL_COND_AND   2
#define BQL_COND_OR    3
#define BQL_COND_NOT   4
#define BQL_COND_EXIST 5

struct bql_cond {
	int   op;      /* the BQL_COND_* operator type */
	void *a, *b;   /* the lhs / rhs operands */
	void *refs;    /* references, for use by db.o */
};

/* NOTE: it is vital to the correctness of the implementation
         that the *C math operators are exactly one greater
         than their metric-only counterparts. */
#define BQL_OP_PUSH     1
#define BQL_OP_ADD      2
#define BQL_OP_ADDC     3
#define BQL_OP_SUB      4
#define BQL_OP_SUBC     5
#define BQL_OP_MUL      6
#define BQL_OP_MULC     7
#define BQL_OP_DIV      8
#define BQL_OP_DIVC     9
#define BQL_OP_AGGR    10
#define BQL_OP_RETURN 100

struct bql_op {
	int code;
	union {
		double imm;

		struct {
			int    raw;
			char  *metric;
			void  *refs;
		} push;

		struct {
			int cf;
		} aggr;
	} data;
};

struct bql_field {
	struct bql_field *next;  /* for singly-linked list implementations */
	char             *name;  /* the name or alias of the field         */
	struct bql_op    *ops;   /* the qops stream for calcualting values */
};

#define BQL_CF_MIN     1
#define BQL_CF_MAX     2
#define BQL_CF_SUM     3
#define BQL_CF_MEAN    4
#define BQL_CF_MEDIAN  5
#define BQL_CF_STDEV   6
#define BQL_CF_VAR     7
#define BQL_CF_DELTA   8

struct bql_cons {
	int samples;
	int stride;
	int cf;
};

struct bql_query {
	int               nfields;
	struct bql_field *select;
	struct bql_cond  *where;

	int from; /* FIXME: should these be uint64_t (for milliseconds)? */
	int until;

	struct bql_cons bucket;
	struct bql_cons aggregate;

	int   error;
	char *errdat;

	struct list alloc_refs;
};

struct bql_query * bql_parse(const char *q);
void bql_free(struct bql_query *);

#endif

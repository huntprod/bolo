#include "bolo.h"
#include <time.h>

static const char *
cfname(int cf)
{
	static const char * names[] = {
		"(unknown-cf)",
		"min",
		"max",
		"sum",
		"mean",
		"median",
		"stdev",
		"var",
		"delta",
	};
	return names[cf < CF_DELTA ? cf : 0];
}

int
do_parse(int argc, char **argv)
{
	struct query *query;
	char buf[501], datetime[100];
	size_t len;
	int i, j;
	struct tm from, until;
	time_t now;
	struct qfield *f;

	if (argc < 3) {
		fprintf(stderr, "USAGE: bolo parse 'QUERY ...'\n");
		return 1;
	}

	now = time(NULL);
	memset(buf, '-', 500);
	buf[500] = '\0';
	for (i = 2; i < argc; i++) {
		len = strlen(argv[i]);
		if (len < 500) buf[len] = '\0';
		fprintf(stdout, "%s\n%s\n", argv[i], buf);
		if (len < 500) buf[len] = '-';

		query = query_parse(argv[i]);
		if (!query) {
			fprintf(stdout, "  error: BQL query is syntactically incorrect, or semantically invalid\n\n");
			continue;
		}

		fprintf(stdout, "  input is a well-formed BQL query.\n");
		//fprintf(stdout, "  canonical form is\n    '%s'\n", query_stringify(query));
		if (query->aggr.stride) {
			fprintf(stdout, "  aggregate window %ds (%.1fm / %.1fh)\n",
				query->aggr.stride, query->aggr.stride / 60.0, query->aggr.stride / 3600.0);
		} else {
			fprintf(stdout, "  no aggregation will be performed.\n");
		}

		now += query->from;
		if (!localtime_r(&now, &from))
			bail("failed to determine localtime");
		now = now - query->from + query->until;
		if (!localtime_r(&now, &until))
			bail("failed to determine localtime");
		now -= query->until;

		fprintf(stdout, "  data will only be considered if it was submitted\n");
		strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S %z", &from);
		fprintf(stdout, "    between %s (%.1fh ago)\n", datetime, query->from / -3600.0);
		if (query->until == 0) {
			fprintf(stdout, "        and now\n");
		} else {
			strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S %z", &until);
			fprintf(stdout, "        and %s (%.1fh ago)\n", datetime, query->until / -3600.0);
		}
		fprintf(stdout, "\n");

		fprintf(stdout, "  select clause details:\n");
		for (f = query->select; f; f = f->next) {
			fprintf(stdout, "    - %s\n", f->name);
			for (j = 0; ; j++) {
				switch (f->ops[j].code) {
				default:         fprintf(stdout, "        ; unknown op [%#02x]\n", f->ops[j].code); break;
				case QOP_RETURN: fprintf(stdout, "        RETURN\n"); break;
				case QOP_PUSH:   fprintf(stdout, "        PUSH   %s\n", f->ops[j].data.push.metric); break;
				case QOP_ADD:    fprintf(stderr, "        ADD\n"); break;
				case QOP_SUB:    fprintf(stderr, "        SUB\n"); break;
				case QOP_MUL:    fprintf(stderr, "        MUL\n"); break;
				case QOP_DIV:    fprintf(stderr, "        DIV\n"); break;

				case QOP_ADDC:   fprintf(stderr, "        ADDC   %e\n", f->ops[j].data.imm); break;
				case QOP_SUBC:   fprintf(stderr, "        SUBC   %e\n", f->ops[j].data.imm); break;
				case QOP_MULC:   fprintf(stderr, "        MULC   %e\n", f->ops[j].data.imm); break;
				case QOP_DIVC:   fprintf(stderr, "        DIVC   %e\n", f->ops[j].data.imm); break;

				case QOP_AGGR:   fprintf(stdout, "        AGGR   %s\n", cfname(f->ops[j].data.aggr.cf)); break;
				}
				if (f->ops[j].code == QOP_RETURN)
					break;
			}
			fprintf(stdout, "\n");
		}
		// print details about where
	}

	return 0;
}

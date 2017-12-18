#include "bolo.h"
#include <time.h>

int
do_parse(int argc, char **argv)
{
	struct query *query;
	char buf[501], datetime[100];
	size_t len;
	int i;
	struct tm from, until;
	time_t now;

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
		if (query->aggr) {
			fprintf(stdout, "  aggregate window %ds (%.1fm / %.1fh)\n",
				query->aggr, query->aggr / 60.0, query->aggr / 3600.0);
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

		// print details about select
		// print details about where
	}

	return 0;
}

#include <stdio.h>
#include "../bolo.h"

int main(int argc, char **argv)
{
	struct query *q;

	q = bql_parse(argv[1]);
	return q ? 0 : 1;
	//q = bql_parse("SELECT cpu WHERE host = localhost AGGREGATE 1h BETWEEN 4h AGO AND NOW");
	return 0;
}

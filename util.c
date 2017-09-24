#include <stdio.h>
#include <stdlib.h>

void
bail(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(2);
}

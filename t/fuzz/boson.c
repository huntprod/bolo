#include "../../bolo.h"
#include <stdio.h>

int main(int argc, char **argv)
{
	struct boson b;
	struct boson_value v;
	int rc;

	boson_init(&b, 0);
	for (;;) {
		rc = boson_read(&b, &v);
		if (rc == BOSON_EOF || rc == BOSON_ERROR)
			return 0;
	}
}

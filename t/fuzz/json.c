#include "../../bolo.h"
#include <stdio.h>

int main(int argc, char **argv)
{
	struct json b;
	struct json_value v;
	int rc;

	json_init_fd(&b, 0);
	for (;;) {
		rc = json_read(&b, &v);
		if (rc == JSON_EOF || rc == JSON_ERROR)
			return 0;
	}
}

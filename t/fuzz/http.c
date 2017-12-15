#include "../../bolo.h"
#include <stdio.h>

int main(int argc, char **argv)
{
	struct http_conn c;

	memset(&c, 0, sizeof(c));
	http_conn_init(&c, 0);
	for (;;) {
		if (http_conn_read(&c) < 0)
			return 0; /* might have EOF'd early */
		if (http_conn_ready(&c) || http_conn_bad(&c))
			return 0;
	}
}

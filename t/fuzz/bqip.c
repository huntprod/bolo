#include "../../bolo.h"
#include "../../bqip.h"

int main(int argc, char **argv)
{
	struct bqip b;
	memset(&b, 0, sizeof(b));
	bqip_init(&b, 0);

	while (bqip_read(&b) == 1)
		;
}

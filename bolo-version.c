#include "bolo.h"

int
do_version(int argc, char **argv)
{
	fprintf(stdout, "bolo v%d.%d.%d\n", BOLO_VERSION_MAJOR, BOLO_VERSION_MINOR, BOLO_VERSION_POINT);
	return 0;
}

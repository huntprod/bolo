#include "bolo.h"

int
do_help(int argc, char **argv)
{
	printf("bolo v%d.%d.%d\n", BOLO_VERSION_MAJOR, BOLO_VERSION_MINOR, BOLO_VERSION_POINT);
	printf("USAGE: bolo [options] <command> [options...] [args...]\n\n");

	printf("Common Options:\n");
	printf("  -h, --help      Show command-specific help / usage.\n");
	printf("  -v, --version   Print the version of bolo and exit.\n");
	printf("\n");
	printf("\n");
	printf("Environment Variables:\n");
	printf("  BOLO_LOGLEVEL   Control what level of logging bolo does.\n");
	printf("                   - \"error\"   Errors only\n");
	printf("                   - \"warn\"    Warnings and errors\n");
	printf("                   - \"info\"    Informational messages + warnings and errors\n");
	printf("                   - \"debug\"   Like 'info', but also enables debugging\n");
	printf("                                 messages, printed to standard error\n");
	printf("                  Other (incorrect) values will be silently ignored.\n");
	printf("\n");
	printf("  BOLO_DEBUG      If set to a non-empty value, enables debugging messages,\n");
	printf("                  printed to standard error (file descriptor 2).\n");
	printf("                  This also forces BOLO_LOGLEVEL to \"info\".\n");
	printf("\n");
	printf("\n");
	printf("Common Commands:\n");
	printf("  bolo core       Start up the bolo core aggregator.\n");
	printf("  bolo agent      Start up the bolo submission agent.\n");
	printf("  bolo import     Import data into a bolo database.\n");
	printf("  bolo commands   List out all available commands.\n");
	printf("\n");
	return 0;
}

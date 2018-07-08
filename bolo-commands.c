#include "bolo.h"

int
do_commands(int argc, char **argv)
{
	printf("bolo v%d.%d.%d\n", BOLO_VERSION_MAJOR, BOLO_VERSION_MINOR, BOLO_VERSION_POINT);
	printf("\n");
	printf("Available commands:\n");
	printf("  bolo agent        Run the bolo agent process, for submitting measurements.\n");
	printf("  bolo commands     Print this list of all available commands.\n");
	printf("  bolo core         Run the bolo core aggregator, which receives measurements.\n");
	printf("  bolo dbinfo       Print low-level information about a bolo database.\n");
	printf("  bolo help         Print help and usage information.\n");
	printf("  bolo idxinfo      (DEPRECATED) Print low-level information about a bolo index.\n");
	printf("  bolo import       Import time series and their measurements from standard input.\n");
	printf("  bolo init         Initialize a new bolo database.\n");
	printf("  bolo metrics      List all time series contained in a bolo database.\n");
	printf("  bolo parse        Parse a BQL query and print low-level plan/exec information.\n");
	printf("  bolo query        Execute a BQL query against a bolo database and print the results.\n");
	printf("  bolo slabinfo     Print low-level information about a bolo database slab.\n");
	printf("  bolo version      Print the version of the bolo software.\n");
	printf("\n");
	printf("For more detailed help on a particular command,\n");
	printf("use the --help option, i.e.:\n\n");
	printf("  bolo query --help\n");
	printf("\n");
	return 0;
}

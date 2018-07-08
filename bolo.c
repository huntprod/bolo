#include "bolo.h"

#define EXT(x) extern int do_ ## x (int argc, char **argv)
EXT(agent);         /* bolo agent [-c CONFIG] [-l LEVEL] [-D] */
EXT(commands);      /* bolo commands */
EXT(core);          /* bolo core [-c PATH] [-l LEVEL] [-D] */
EXT(dbinfo);        /* bolo dbinfo DATADIR */
EXT(help);          /* bolo help */
EXT(idxinfo);       /* bolo idxinfo DATADIR/idx/INDEXFILE */
EXT(import);        /* bolo import DATADIR <INPUT */
EXT(init);          /* bolo init DATADIR */
EXT(metrics);       /* bolo metrics DATADIR */
EXT(parse);         /* bolo parse 'QUERY' */
EXT(query);         /* bolo query DATADIR 'QUERY' */
EXT(slabinfo);      /* bolo slabinfo DATADIR/slabs/SLABFILE */
EXT(version);       /* bolo [version] */
#undef EXT

/* global */
int SHOW_HELP;

int main(int argc, char **argv)
{
	const char *command;
	int i, log_level;
	char *s;

	SHOW_HELP = 0;
	if (argc < 2) {
		printf("bolo v%d.%d.%d\n", BOLO_VERSION_MAJOR, BOLO_VERSION_MINOR, BOLO_VERSION_POINT);
		printf("\n");
		printf("Bolo is a monitoring and data analytics suite optimized\n");
		printf("for use at scale in both operations and application scenarios.\n");
		printf("\n");
		printf("Getting Started\n");
		printf("\n");
		printf("  bolo --help          Some general usage information.\n");
		printf("  bolo <cmd> --help    Specific help, for the <cmd> command.\n");
		printf("  bolo commands        List all available bolo commands.\n");
		printf("\n");
		printf("Web Resources\n");
		printf("\n");
		printf("  https://github.com/bolo           Source code\n");
		printf("  https://bolo.niftylogic.com       Project page\n");
		printf("  https://bolo.cloud/docs           Documentation\n");
		printf("\n");
		return 0;
	}

	command = NULL;
	command = NULL;
	for (i = 1; i < argc; i++) {
		if (streq(argv[i], "--"))
			break;

		if (!command && argv[i][0] != '-') {
			if (streq(argv[i], "help")) {
				memcpy(argv[i], "-h\0", 3);
				SHOW_HELP = 1;
			} else {
				command = argv[i];
			}
			continue;
		}

		if (streq(argv[i], "-v") || streq(argv[i], "--version"))
			command = "version";

		if (!command && (streq(argv[i], "-h") || streq(argv[i], "--help")))
			SHOW_HELP = 1;
	}

	log_level = LOG_ERRORS;
	if ((s = getenv("BOLO_LOGLEVEL")) != NULL) {
		     if (streq(s, "error")   || streq(s, "ERROR"))  log_level = LOG_ERRORS;
		else if (streq(s, "warning") || streq(s, "WARNING")
		      || streq(s, "warn")    || streq(s, "WARN"))   log_level = LOG_WARNINGS;
		else if (streq(s, "info")    || streq(s, "INFO"))   log_level = LOG_INFO;
		else if (streq(s, "debug")   || streq(s, "DEBUG")) {
			log_level = LOG_INFO;
			debugto(2);
		}
		/* silently ignore incorrect values */
	}
	if ((s = getenv("BOLO_DEBUG")) != NULL) {
		log_level = LOG_INFO;
		debugto(2);
	}
	startlog(argv[0], getpid(), log_level);

	if (!command && SHOW_HELP)
		return do_help(argc, argv);

	#define RUN(c) if (streq(command, #c)) return do_ ## c (argc, argv)
	RUN(agent);
	RUN(commands);
	RUN(core);
	RUN(dbinfo);
	RUN(idxinfo);
	RUN(import);
	RUN(init);
	RUN(metrics);
	RUN(parse);
	RUN(query);
	RUN(slabinfo);
	RUN(version);
	#undef RUN

	return do_help(argc, argv);
}

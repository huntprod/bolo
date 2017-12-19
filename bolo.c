#include "bolo.h"

#define EXT(x) extern int do_ ## x (int argc, char **argv)
EXT(core);          /* bolo core [-c PATH] [-l LEVEL] [-D] */
EXT(dbinfo);        /* bolo dbinfo DATADIR */
EXT(help);          /* bolo help */
EXT(idxinfo);       /* bolo idxinfo DATADIR/idx/INDEXFILE */
EXT(import);        /* bolo import DATADIR <INPUT */
EXT(init);          /* bolo init DATADIR */
EXT(parse);         /* bolo parse 'QUERY' */
EXT(query);         /* bolo query DATADIR 'QUERY' */
EXT(slabinfo);      /* bolo slabinfo DATADIR/slabs/SLABFILE */
EXT(version);       /* bolo [version] */
#undef EXT

int main(int argc, char **argv)
{
	const char *command;
	int log_level;
	char *s;

	if (argc < 2)
		command = "version";
	else
		command = argv[1];

	log_level = LOG_ERRORS;
	if ((s = getenv("BOLO_LOGLEVEL")) != NULL) {
		     if (streq(s, "error")   || streq(s, "ERROR"))  log_level = LOG_ERRORS;
		else if (streq(s, "warning") || streq(s, "WARNING")
		      || streq(s, "warn")    || streq(s, "WARN"))   log_level = LOG_WARNINGS;
		else if (streq(s, "info")    || streq(s, "INFO"))   log_level = LOG_INFO;
		/* silently ignore incorrect values */
	}
	startlog(argv[0], getpid(), log_level);
	if ((s = getenv("BOLO_DEBUG")) != NULL)
		debugto(2);

	if (streq(command, "-v") == 0
	 || streq(command, "--version") == 0)
		command = "version";

	#define RUN(c) if (streq(command, #c)) return do_ ## c (argc, argv)
	RUN(core);
	RUN(dbinfo);
	RUN(idxinfo);
	RUN(import);
	RUN(init);
	RUN(parse);
	RUN(query);
	RUN(slabinfo);
	RUN(version);
	#undef RUN

	return do_help(argc, argv);
}

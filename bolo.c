#include "bolo.h"

#define EXT(x) extern int do_ ## x (int argc, char **argv)
EXT(dbinfo);        /* bolo dbinfo DATADIR */
EXT(core);          /* bolo core [-c PATH] [-l LEVEL] [-D] */
EXT(help);          /* bolo help */
EXT(idxinfo);       /* bolo idxinfo DATADIR/idx/INDEXFILE */
EXT(import);        /* bolo import DATADIR <INPUT */
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

	if (strcmp(command, "-v") == 0
	 || strcmp(command, "--version") == 0)
		command = "version";

	if (strcmp(command, "version") == 0)
		return do_version(argc, argv);

	if (strcmp(command, "import") == 0)
		return do_import(argc, argv);

	if (strcmp(command, "dbinfo") == 0)
		return do_dbinfo(argc, argv);

	if (strcmp(command, "idxinfo") == 0)
		return do_idxinfo(argc, argv);

	if (strcmp(command, "slabinfo") == 0)
		return do_slabinfo(argc, argv);

	if (strcmp(command, "parse") == 0)
		return do_parse(argc, argv);

	if (strcmp(command, "query") == 0)
		return do_query(argc, argv);

	if (strcmp(command, "core") == 0)
		return do_core(argc, argv);

	return do_help(argc, argv);
}

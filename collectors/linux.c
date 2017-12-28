#include "../bolo.h"
#include <ctype.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include <pcre.h>

#define PROC "/proc"
#define DEFAULT_TAGS "tags=none"

static char *tags;
static bolo_msec_t ts = 0;
static char buf[8192];

#define MATCH_ANY   0
#define MATCH_IFACE 1
#define MATCH_MOUNT 2
#define MATCH_DEV   3

typedef struct __matcher {
	int         subject; /* what type of thing to match (a MATCH_* const) */
	char       *pattern; /* raw pattern source string.                    */
	pcre       *regex;   /* compiled pattern to match against.            */
	pcre_extra *extra;   /* additional stuff from pcre_study (perf tweak) */

	struct __matcher *next;
} matcher_t;
static matcher_t *EXCLUDE = NULL;
static matcher_t *INCLUDE = NULL;

#define COLLECT_MEMINFO   (1 << 1)
#define COLLECT_LOADAVG   (1 << 2)
#define COLLECT_STAT      (1 << 3)
#define COLLECT_PROCS     (1 << 4)
#define COLLECT_OPENFILES (1 << 5)
#define COLLECT_MOUNTS    (1 << 6)
#define COLLECT_VMSTAT    (1 << 7)
#define COLLECT_DISKSTATS (1 << 8)
#define COLLECT_NETDEV    (1 << 9)

int collect_meminfo(void);
int collect_loadavg(void);
int collect_stat(void);
int collect_procs(void);
int collect_openfiles(void);
int collect_mounts(void);
int collect_vmstat(void);
int collect_diskstats(void);
int collect_netdev(void);

static int RUNIT  = 0;
static int SKIPIT = 0;
#define should(f)  ((RUNIT & COLLECT_ ## f) && !(SKIPIT & COLLECT_ ## f))
#define RUN(f)  (RUNIT  |= COLLECT_ ## f)
#define SKIP(f) (SKIPIT |= COLLECT_ ## f)

int parse_options(int argc, char **argv);
int matches(int type, const char *name);
int append_matcher(matcher_t **root, const char *flag, const char *value);

int main(int argc, char **argv)
{
	ts = bolo_ms(NULL);
	tags = getenv("BOLO_TAGS");
	if (!tags)
		tags = DEFAULT_TAGS;

	if (parse_options(argc, argv) != 0) {
		fprintf(stderr, "USAGE: %s [-p prefix]\n", argv[0]);
		exit(1);
	}

	int rc = 0;
	if should(MEMINFO)   rc += collect_meminfo();
	if should(LOADAVG)   rc += collect_loadavg();
	if should(STAT)      rc += collect_stat();
	if should(PROCS)     rc += collect_procs();
	if should(OPENFILES) rc += collect_openfiles();
	if should(MOUNTS)    rc += collect_mounts();
	if should(VMSTAT)    rc += collect_vmstat();
	if should(DISKSTATS) rc += collect_diskstats();
	if should(NETDEV)    rc += collect_netdev();
	return rc;
}

int parse_options(int argc, char **argv)
{
	int errors = 0;

	int i;
	for (i = 1; i < argc; ++i) {
		if (streq(argv[i], "-i") || streq(argv[i], "--include")) {
			if (++i >= argc) {
				fprintf(stderr, "Missing required value for --include flag\n");
				return 1;
			}
			if (append_matcher(&INCLUDE, "--include", argv[i]) != 0) {
				return 1;
			}
			continue;
		}

		if (streq(argv[i], "-x") || streq(argv[i], "--exclude")) {
			if (++i >= argc) {
				fprintf(stderr, "Missing required value for --exclude flag\n");
				return 1;
			}
			if (append_matcher(&EXCLUDE, "--exclude", argv[i]) != 0) {
				return 1;
			}
			continue;
		}

		if (streq(argv[i], "-h") || streq(argv[i], "-?") || streq(argv[i], "--help")) {
			fprintf(stdout, "linux (a Bolo collector)\n"
			                "USAGE: linux [flags] [metrics]\n"
			                "\n"
			                "flags:\n"
			                "   -h, --help                 Show this help screen\n"
			                "\n"
			                "   -i, --include type:regex   Only consider things of the given type\n"
			                "                              that match /^regex$/, using PCRE.\n"
			                "                              By default, all things are included.\n"
			                "\n"
			                "   -x, --exclude type:regex   Don't consider things (of the given type)\n"
			                "                              that match /^regex$/, using PCRE.\n"
			                "                              By default, nothing is excluded.\n"
			                "\n"
			                "                              Note: --exclude rules are processed after\n"
			                "                              all --include rules, so you usually want\n"
			                "                              to match liberally first, and exclude\n"
			                "                              conservatively.\n"
			                "\n"
			                "metrics:\n"
			                "\n"
			                "   (no)mem           Memory utilization metrics\n"
			                "   (no)load          System Load Average metrics\n"
			                "   (no)cpu           CPU utilization (aggregate) metrics\n"
			                "   (no)procs         Process creation / context switching metrics\n"
			                "   (no)openfiles     Open File Descriptor metrics\n"
			                "   (no)mounts        Mountpoints and disk space usage metrics\n"
			                "   (no)paging        Virtual Memory paging statistics\n"
			                "   (no)disk          Disk I/O and utilization metrics\n"
			                "   (no)net           Network interface metrics\n"
			                "\n"
			                "   By default, all metrics are collected.  You can suppress specific\n"
			                "   metric sets by prefixing its name with \"no\", without having to\n"
			                "   list out everything you want explicitly.\n"
			                "\n");
			exit(0);
		}

		int good = 0;
		#define KEYWORD(k,n) do { \
			if (streq(argv[i],      k)) {  RUN(n); good = 1; continue; } \
			if (streq(argv[i], "no" k)) { SKIP(n); good = 1; continue; } \
		} while (0)

		KEYWORD("mem",       MEMINFO);
		KEYWORD("load",      LOADAVG);
		KEYWORD("cpu",       STAT);
		KEYWORD("procs",     PROCS);
		KEYWORD("openfiles", OPENFILES);
		KEYWORD("mounts",    MOUNTS);
		KEYWORD("paging",    VMSTAT);
		KEYWORD("disk",      DISKSTATS);
		KEYWORD("net",       NETDEV);

		#undef KEYWORD
		if (good) continue;

		fprintf(stderr, "Unrecognized argument '%s'\n", argv[i]);
		errors++;
	}

	if (RUNIT == 0)
		RUNIT = ~SKIPIT;
	return errors;
}

int matches(int type, const char *name)
{
	matcher_t *m;

	if ((m = INCLUDE) != NULL) {
		while (m) {
			if ((m->subject == MATCH_ANY || m->subject == type)
			 && pcre_exec(m->regex, m->extra, name, strlen(name), 0, 0, NULL, 0) == 0) {
				goto excludes;
			}
			m = m->next;
		}
		return 0; /* not included */
	}

excludes:
	m = EXCLUDE;
	while (m) {
		if ((m->subject == MATCH_ANY || m->subject == type)
		 && pcre_exec(m->regex, m->extra, name, strlen(name), 0, 0, NULL, 0) == 0) {
			return 0; /* excluded */
		}
		m = m->next;
	}

	return 1;
}

int append_matcher(matcher_t **root, const char *flag, const char *value)
{
	matcher_t *m;
	const char *colon, *re_err;
	int re_off;

	m = calloc(1, sizeof(matcher_t));
	if (!m) {
		errnof("unable to allocate memory");
		exit(1);
	}

	m->subject = MATCH_ANY;
	if ((colon = strchr(value, ':')) != NULL) {
		if (strncasecmp(value, "iface:", colon - value) == 0) {
			m->subject = MATCH_IFACE;
		} else if (strncasecmp(value, "dev:", colon - value) == 0) {
			m->subject = MATCH_DEV;
		} else if (strncasecmp(value, "mount:", colon - value) == 0) {
			m->subject = MATCH_MOUNT;
		} else {
			fprintf(stderr, "Invalid type in type:regex specifier for %s flag\n", flag);
			free(m);
			return 1;
		}
		value = colon + 1;
	}

	if (!*value) {
		fprintf(stderr, "Missing regex in type:regex specifier for %s flag\n", flag);
		free(m);
		return 1;
	}

	m->pattern = calloc(1 + strlen(value) + 1 + 1, sizeof(char));
	if (!m->pattern) {
		errnof("unable to allocate memory");
		exit(1);
	}
	m->pattern[0] = '^';
	memcpy(m->pattern+1, value, strlen(value));
	m->pattern[1+strlen(value)] = '$';

	m->regex = pcre_compile(m->pattern, PCRE_ANCHORED, &re_err, &re_off, NULL);
	if (!m->regex) {
		fprintf(stderr, "Bad regex '%s' (error %s) for %s flag\n", m->pattern, flag, re_err);
		free(m->pattern);
		free(m);
		return 1;
	}
	m->extra = pcre_study(m->regex, 0, &re_err);

	if (!*root) {
		*root = m;
	} else {
		while (root && (*root)->next) {
			root = &(*root)->next;
		}
		(*root)->next = m;
	}
	return 0;
}

int collect_meminfo(void)
{
	FILE *io = fopen(PROC "/meminfo", "r");
	if (!io)
		return 1;

	struct {
		uint64_t total;
		uint64_t used;
		uint64_t free;
		uint64_t buffers;
		uint64_t cached;
		uint64_t slab;
	} M = { 0 };
	struct {
		uint64_t total;
		uint64_t used;
		uint64_t free;
		uint64_t cached;
	} S = { 0 };
	uint64_t x;
	ts = bolo_ms(NULL);
	while (fgets(buf, 8192, io) != NULL) {
		/* MemTotal:        6012404 kB\n */
		char *k, *v, *u, *e;

		k = buf; v = strchr(k, ':');
		if (!v || !*v) continue;

		*v++ = '\0';
		while (isspace(*v)) v++;
		u = strchr(v, ' ');
		if (u) {
			*u++ = '\0';
		} else {
			u = strchr(v, '\n');
			if (u) *u = '\0';
			u = NULL;
		}

		x = strtoul(v, &e, 10);
		if (*e) continue;

		if (u && *u == 'k')
			x *= 1024;

		     if (streq(k, "MemTotal"))   M.total   = x;
		else if (streq(k, "MemFree"))    M.free    = x;
		else if (streq(k, "Buffers"))    M.buffers = x;
		else if (streq(k, "Cached"))     M.cached  = x;
		else if (streq(k, "Slab"))       M.slab    = x;

		else if (streq(k, "SwapTotal"))  S.total   = x;
		else if (streq(k, "SwapFree"))   S.free    = x;
		else if (streq(k, "SwapCached")) S.cached  = x;
	}

	M.used = M.total - (M.free + M.buffers + M.cached + M.slab);
	printf("mem.total %s %lu %lu\n",   tags, ts, M.total);
	printf("mem.used %s %lu %lu\n",    tags, ts, M.used);
	printf("mem.free %s %lu %lu\n",    tags, ts, M.free);
	printf("mem.buffers %s %lu %lu\n", tags, ts, M.buffers);
	printf("mem.cached %s %lu %lu\n",  tags, ts, M.cached);
	printf("mem.slab %s %lu %lu\n",    tags, ts, M.slab);

	S.used = S.total - (S.free + S.cached);
	printf("swap.total %s %lu %lu\n",  tags, ts, S.total);
	printf("swap.cached %s %lu %lu\n", tags, ts, S.cached);
	printf("swap.used %s %lu %lu\n",   tags, ts, S.used);
	printf("swap.free %s %lu %lu\n",   tags, ts, S.free);

	fclose(io);
	return 0;
}

int collect_loadavg(void)
{
	FILE *io = fopen(PROC "/loadavg", "r");
	if (!io)
		return 1;

	double load[3];
	uint64_t proc[3];

	ts = bolo_ms(NULL);
	int rc = fscanf(io, "%lf %lf %lf %lu/%lu ",
			&load[0], &load[1], &load[2], &proc[0], &proc[1]);
	fclose(io);
	if (rc < 5)
		return 1;

	if (proc[0])
		proc[0]--; /* don't count us */

	printf("load:1min"        " %s %lu %0.2f\n", tags, ts, load[0]);
	printf("load:5min"        " %s %lu %0.2f\n", tags, ts, load[1]);
	printf("load:15min"       " %s %lu %0.2f\n", tags, ts, load[2]);
	printf("load:runnable"    " %s %lu %lu\n",   tags, ts, proc[0]);
	printf("load:schedulable" " %s %lu %lu\n",   tags, ts, proc[1]);
	return 0;
}

int collect_stat(void)
{
	FILE *io = fopen(PROC "/stat", "r");
	if (!io)
		return 1;

	int cpus = 0;
	ts = bolo_ms(NULL);
	while (fgets(buf, 8192, io) != NULL) {
		char *k, *v, *p;

		k = v = buf;
		while (*v && !isspace(*v)) v++; *v++ = '\0';
		p = strchr(v, '\n'); if (p) *p = '\0';

		if (streq(k, "processes"))
			printf("context.forks %s %lu %s\n", tags, ts, v);
		else if (streq(k, "ctxt"))
			printf("context.switches %s %lu %s\n", tags, ts, v);
		else if (strncmp(k, "cpu", 3) == 0 && isdigit(k[3]))
			cpus++;

		if (streq(k, "cpu")) {
			while (*v && isspace(*v)) v++;
			k = v; while (*k && !isspace(*k)) k++; *k++ = '\0'; printf("cpu.user %s %lu %s\n",       tags, ts, v && *v ? v : "0"); v = k;
			k = v; while (*k && !isspace(*k)) k++; *k++ = '\0'; printf("cpu.nice %s %lu %s\n",       tags, ts, v && *v ? v : "0"); v = k;
			k = v; while (*k && !isspace(*k)) k++; *k++ = '\0'; printf("cpu.system %s %lu %s\n",     tags, ts, v && *v ? v : "0"); v = k;
			k = v; while (*k && !isspace(*k)) k++; *k++ = '\0'; printf("cpu.idle %s %lu %s\n",       tags, ts, v && *v ? v : "0"); v = k;
			k = v; while (*k && !isspace(*k)) k++; *k++ = '\0'; printf("cpu.iowait %s %lu %s\n",     tags, ts, v && *v ? v : "0"); v = k;
			k = v; while (*k && !isspace(*k)) k++; *k++ = '\0'; printf("cpu.irq %s %lu %s\n",        tags, ts, v && *v ? v : "0"); v = k;
			k = v; while (*k && !isspace(*k)) k++; *k++ = '\0'; printf("cpu.softirq %s %lu %s\n",    tags, ts, v && *v ? v : "0"); v = k;
			k = v; while (*k && !isspace(*k)) k++; *k++ = '\0'; printf("cpu.steal %s %lu %s\n",      tags, ts, v && *v ? v : "0"); v = k;
			k = v; while (*k && !isspace(*k)) k++; *k++ = '\0'; printf("cpu.guest %s %lu %s\n",      tags, ts, v && *v ? v : "0"); v = k;
			k = v; while (*k && !isspace(*k)) k++; *k++ = '\0'; printf("cpu.guest-nice %s %lu %s\n", tags, ts, v && *v ? v : "0"); v = k;
		}
	}
	printf("load:cpus %s %lu %i\n", tags, ts, cpus);

	fclose(io);
	return 0;
}

int collect_procs(void)
{
	struct {
		uint16_t running;
		uint16_t sleeping;
		uint16_t zombies;
		uint16_t stopped;
		uint16_t paging;
		uint16_t blocked;
		uint16_t unknown;
	} P = {0};

	int pid;
	struct dirent *dir;
	DIR *d;
	FILE *io;
	char *file, *a;

	d = opendir(PROC);
	if (!d)
		return 1;

	ts = bolo_ms(NULL);
	while ((dir = readdir(d)) != NULL) {
		if (!isdigit(dir->d_name[0])
		 || (pid = atoi(dir->d_name)) < 1)
			continue;

		asprintf(&file, PROC "/%i/stat", pid);
		io = fopen(file, "r");
		free(file);
		if (!io)
			continue;

		if (!fgets(buf, 8192, io)) {
			fclose(io);
			continue;
		}
		fclose(io);

		a = buf;
		/* skip PID */
		while (*a && !isspace(*a)) a++;
		while (*a &&  isspace(*a)) a++;
		/* skip progname */
		while (*a && !isspace(*a)) a++;
		while (*a &&  isspace(*a)) a++;

		switch (*a) {
		case 'R': P.running++;  break;
		case 'S': P.sleeping++; break;
		case 'D': P.blocked++;  break;
		case 'Z': P.zombies++;  break;
		case 'T': P.stopped++;  break;
		case 'W': P.paging++;   break;
		default:  P.unknown++;  break;
		}
	}

	printf("procs.running %s %lu %i\n",  tags, ts, P.running);
	printf("procs.sleeping %s %lu %i\n", tags, ts, P.sleeping);
	printf("procs.blocked %s %lu %i\n",  tags, ts, P.blocked);
	printf("procs.zombies %s %lu %i\n",  tags, ts, P.zombies);
	printf("procs.stopped %s %lu %i\n",  tags, ts, P.stopped);
	printf("procs.paging %s %lu %i\n",   tags, ts, P.paging);
	printf("procs.unknown %s %lu %i\n",  tags, ts, P.unknown);
	return 0;
}

int collect_openfiles(void)
{
	FILE *io = fopen(PROC "/sys/fs/file-nr", "r");
	if (!io)
		return 1;

	ts = bolo_ms(NULL);
	char *a, *b;
	if (!fgets(buf, 8192, io)) {
		fclose(io);
		return 1;
	}

	a = buf;
	/* used file descriptors */
	while (*a &&  isspace(*a)) a++; b = a;
	while (*b && !isspace(*b)) b++; *b++ = '\0';
	printf("openfiles.used %s %lu %s\n", tags, ts, a && *a ? a : "0");

	a = b;
	/* free file descriptors */
	while (*a &&  isspace(*a)) a++; b = a;
	while (*b && !isspace(*b)) b++; *b++ = '\0';
	printf("openfiles.free %s %lu %s\n", tags, ts, a && *a ? a : "0");

	a = b;
	/* max file descriptors */
	while (*a &&  isspace(*a)) a++; b = a;
	while (*b && !isspace(*b)) b++; *b++ = '\0';
	printf("openfiles.max %s %lu %s\n", tags, ts, a && *a ? a : "0");

	return 0;
}

char* resolv_path(char *path)
{
	char  *buf  = malloc(256);
	if (buf == NULL)
		return path;
	ssize_t size = 0;
	char *dev  = strdup(path);

	if ((size = readlink(dev, buf, 256)) == -1)
		return path;
	buf[size] = '\0';

	int begin = 0;
	int cnt   = 1;
	while((begin = strspn(buf, "..")) != 0) {
		buf += begin;
		cnt++;
	}
	int i;
	for (i = 0; i < cnt; i++)
		dev[strlen(dev) - strlen(strrchr(dev, '/'))] = '\0';
	strcat(dev, buf);
	return dev;
}

int collect_mounts(void)
{
	FILE *io = fopen(PROC "/mounts", "r");
	if (!io)
		return 1;

	struct stat st;
	struct statvfs fs;
	struct hash *seen;
	char *a, *b, *c;

	seen = hash_new();
	ts = bolo_ms(NULL);
	while (fgets(buf, 8192, io) != NULL) {
		a = b = buf;
		for (b = buf; *b && !isspace(*b); b++); *b++ = '\0';
		for (c = b;   *c && !isspace(*c); c++); *c++ = '\0';
		char *dev = a, *path = b;

		if (hash_isset(seen, path))
			continue;
		hash_set(seen, path, "1");

		if (lstat(path, &st) != 0
		 || statvfs(path, &fs) != 0
		 || !major(st.st_dev))
			continue;

		if (!matches(MATCH_MOUNT, path))
			continue;
		dev = resolv_path(dev);

		printf("fs.inodes.total %s,path=%s,dev=%s %lu %lu\n", tags, path, dev, ts, fs.f_files);
		printf("fs.inodes.free %s,path=%s,dev=%s %lu %lu\n",  tags, path, dev, ts, fs.f_favail);
		printf("fs.inodes.rfree %s,path=%s,dev=%s %lu %lu\n", tags, path, dev, ts, fs.f_ffree - fs.f_favail);

		printf("fs.bytes.total %s,path=%s,dev=%s %lu %lu\n",  tags, path, dev, ts, fs.f_frsize *  fs.f_blocks);
		printf("fs.bytes.free %s,path=%s,dev=%s %lu %lu\n",   tags, path, dev, ts, fs.f_frsize *  fs.f_bavail);
		printf("fs.bytes.rfree %s,path=%s,dev=%s %lu %lu\n",  tags, path, dev, ts, fs.f_frsize * (fs.f_bfree - fs.f_bavail));
	}

	fclose(io);
	hash_free(seen);
	return 0;
}

int collect_vmstat(void)
{
	FILE *io = fopen(PROC "/vmstat", "r");
	if (!io)
		return 1;

	uint64_t pgsteal = 0;
	uint64_t pgscan_kswapd = 0;
	uint64_t pgscan_direct = 0;
	ts = bolo_ms(NULL);
	while (fgets(buf, 8192, io) != NULL) {
		char name[64];
		uint64_t value;
		int rc = sscanf(buf, "%63s %lu\n", name, &value);
		if (rc < 2)
			continue;

#define VMSTAT_SIMPLE(x,n,v,t) do { \
	if (streq((n), #t)) printf("vm.%s %s %lu %lu\n", #t, tags, ts, (v)); \
} while (0)
		VMSTAT_SIMPLE(VM, name, value, pswpin);
		VMSTAT_SIMPLE(VM, name, value, pswpout);
		VMSTAT_SIMPLE(VM, name, value, pgpgin);
		VMSTAT_SIMPLE(VM, name, value, pgpgout);
		VMSTAT_SIMPLE(VM, name, value, pgfault);
		VMSTAT_SIMPLE(VM, name, value, pgmajfault);
		VMSTAT_SIMPLE(VM, name, value, pgfree);
#undef  VMSTAT_SIMPLE

		if (strncmp(name, "pgsteal_", 8) == 0)        pgsteal       += value;
		if (strncmp(name, "pgscan_kswapd_", 14) == 0) pgscan_kswapd += value;
		if (strncmp(name, "pgscan_direct_", 14) == 0) pgscan_direct += value;
	}
	printf("vm.pgsteal %s %lu %lu\n",       tags, ts, pgsteal);
	printf("vm.pgscan.kswapd %s %lu %lu\n", tags, ts, pgscan_kswapd);
	printf("vm.pgscan.direct %s %lu %lu\n", tags, ts, pgscan_direct);

	fclose(io);
	return 0;
}

/* FIXME: figure out a better way to detect devices */
#define is_device(dev) (strncmp((dev), "loop", 4) != 0 && strncmp((dev), "ram", 3) != 0)

int collect_diskstats(void)
{
	FILE *io = fopen(PROC "/diskstats", "r");
	if (!io)
		return 1;

	uint32_t dev[2];
	uint64_t rd[4], wr[4];
	ts = bolo_ms(NULL);
	while (fgets(buf, 8192, io) != NULL) {
		char name[32];
		int rc = sscanf(buf, "%u %u %31s %lu %lu %lu %lu %lu %lu %lu %lu",
				&dev[0], &dev[1], name,
				&rd[0], &rd[1], &rd[2], &rd[3],
				&wr[0], &wr[1], &wr[2], &wr[3]);
		if (rc != 11)
			continue;
		if (!is_device(name))
			continue;

		if (!matches(MATCH_DEV, name))
			continue;

		printf("diskio.read-iops %s,dev=%s %lu %lu\n",   tags, name, ts, rd[0]);
		printf("diskio.read-miops %s,dev=%s %lu %lu\n",  tags, name, ts, rd[1]);
		printf("diskio.read-bytes %s,dev=%s %lu %lu\n",  tags, name, ts, rd[2] * 512);
		printf("diskio.read-msec %s,dev=%s %lu %lu\n",   tags, name, ts, rd[3]);

		printf("diskio.write-iops %s,dev=%s %lu %lu\n",  tags, name, ts, wr[0]);
		printf("diskio.write-miops %s,dev=%s %lu %lu\n", tags, name, ts, wr[1]);
		printf("diskio.write-bytes %s,dev=%s %lu %lu\n", tags, name, ts, wr[2] * 512);
		printf("diskio.write-msec %s,dev=%s %lu %lu\n",  tags, name, ts, wr[3]);
	}

	fclose(io);
	return 0;
}

int collect_netdev(void)
{
	FILE *io = fopen(PROC "/net/dev", "r");
	if (!io)
		return 1;

	ts = bolo_ms(NULL);
	if (fgets(buf, 8192, io) == NULL
	 || fgets(buf, 8192, io) == NULL) {
		fclose(io);
		return 1;
	}

	struct {
		uint64_t bytes;
		uint64_t packets;
		uint64_t errors;
		uint64_t drops;
		uint64_t overruns;
		uint64_t frames;
		uint64_t compressed;
		uint64_t collisions;
		uint64_t multicast;
		uint64_t carrier;
	} tx, rx;
	memset(&tx, 0, sizeof(tx));
	memset(&rx, 0, sizeof(rx));

	while (fgets(buf, 8192, io) != NULL) {
		char *x = strrchr(buf, ':');
		if (x) *x = ' ';

		char name[32];
		int rc = sscanf(buf, " %31s "
			"%lu %lu %lu %lu %lu %lu %lu %lu "
			"%lu %lu %lu %lu %lu %lu %lu %lu\n",
			name,
			&rx.bytes, &rx.packets, &rx.errors, &rx.drops,
			&rx.overruns, &rx.frames, &rx.compressed, &rx.multicast,
			&tx.bytes, &tx.packets, &tx.errors, &tx.drops,
			&tx.overruns, &tx.collisions, &tx.carrier, &tx.compressed);

		if (rc < 17)
			continue;

		if (!matches(MATCH_IFACE, name))
			continue;

		printf("net.rx.bytes %s,iface=%s %lu %lu\n",      tags, name, ts, rx.bytes);
		printf("net.rx.packets %s,iface=%s %lu %lu\n",    tags, name, ts, rx.packets);
		printf("net.rx.errors %s,iface=%s %lu %lu\n",     tags, name, ts, rx.errors);
		printf("net.rx.drops %s,iface=%s %lu %lu\n",      tags, name, ts, rx.drops);
		printf("net.rx.overruns %s,iface=%s %lu %lu\n",   tags, name, ts, rx.overruns);
		printf("net.rx.compressed %s,iface=%s %lu %lu\n", tags, name, ts, rx.compressed);
		printf("net.rx.frames %s,iface=%s %lu %lu\n",     tags, name, ts, rx.frames);
		printf("net.rx.multicast %s,iface=%s %lu %lu\n",  tags, name, ts, rx.multicast);

		printf("net.tx.bytes %s,iface=%s %lu %lu\n",      tags, name, ts, tx.bytes);
		printf("net.tx.packets %s,iface=%s %lu %lu\n",    tags, name, ts, tx.packets);
		printf("net.tx.errors %s,iface=%s %lu %lu\n",     tags, name, ts, tx.errors);
		printf("net.tx.drops %s,iface=%s %lu %lu\n",      tags, name, ts, tx.drops);
		printf("net.tx.overruns %s,iface=%s %lu %lu\n",   tags, name, ts, tx.overruns);
		printf("net.tx.compressed %s,iface=%s %lu %lu\n", tags, name, ts, tx.compressed);
		printf("net.tx.collisions %s,iface=%s %lu %lu\n", tags, name, ts, tx.collisions);
		printf("net.tx.carrier %s,iface=%s %lu %lu\n",    tags, name, ts, tx.carrier);
	}

	fclose(io);
	return 0;
}

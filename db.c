#include "bolo.h"
#include <ctype.h>
#include <dirent.h>

#define PATH_TO_MAINDB "main.db"

struct db {
	int           rootfd;  /* file descriptor of database root directory */
	struct hash  *main;    /* primary time series (name|tag,tags,... => <block-id>) */

	struct list   idx;
	struct list   slab;
};

struct idx {
	struct list l;         /* list hook for database idxrefs */
	struct btree *btree;   /* balanced B-tree of ts -> slabid */
	uint64_t number;       /* unique identifier for this index */
};

/* Used as a callback for the directory traversal logic.
   Since the ts/slabs directories use the same structure, we
   reuse the traverseal logic in a single s_scandir function,
   and the customization is provided by the fs_handler. */
typedef int(*fs_handler)(struct db *, uint64_t, int);

static int
s_handle_idx(struct db *db, uint64_t id, int fd)
{
	struct idx *idx;

	idx = malloc(sizeof(*idx));
	if (!idx)
		goto fail;

	idx->number = id;
	idx->btree = btree_read(fd);
	if (!idx->btree)
		goto fail;

	push(&db->idx, &idx->l);
	return 0;

fail:
	free(idx);
	close(fd);
	return -1;
}

static int
s_handle_slab(struct db *db, uint64_t id, int fd)
{
	struct tslab *slab;

	slab = malloc(sizeof(*slab));
	if (!slab)
		goto fail;

	slab->number = id;
	if (tslab_map(slab, fd) != 0)
		goto fail;

	push(&db->slab, &slab->l);
	return 0;

fail:
	free(slab);
	close(fd);
	return -1;
}

static int
s_istoplevel(const char *name)
{
	return isxdigit(name[0]) && isxdigit(name[1])
	     && isxdigit(name[2]) && isxdigit(name[3])
	     && name[4] == '.'
	     && isxdigit(name[5]) && isxdigit(name[6])
	     && isxdigit(name[7]) && isxdigit(name[8])
	     && !name[9];
}

static uint64_t
s_xval(char c)
{
	if (c >= 'A' && c <= 'F') return c - 'A';
	if (c >= 'a' && c <= 'f') return c - 'a';
	if (c >= '0' && c <= '9') return c - '0';
	return 0;
}

static uint64_t
s_datfileno(const char *name)
{
	return (s_xval(name[0])  << 60)
	     | (s_xval(name[1])  << 56)
	     | (s_xval(name[2])  << 52)
	     | (s_xval(name[3])  << 48)
	       /* skip the '.' */
	     | (s_xval(name[5])  << 44)
	     | (s_xval(name[6])  << 40)
	     | (s_xval(name[7])  << 36)
	     | (s_xval(name[8])  << 32)
	       /* skip the '.' */
	     | (s_xval(name[10]) << 28)
	     | (s_xval(name[11]) << 24)
	     | (s_xval(name[12]) << 20)
	     | (s_xval(name[13]) << 16)
	       /* skip the '.' */
	     | (s_xval(name[15]) << 12)
	     | (s_xval(name[16]) <<  8)
	     | (s_xval(name[17]) <<  4)
	     | (s_xval(name[18]));
}

static int
s_isdatfile(const char *name, const char *suffix)
{
	if (!(isxdigit(name[0]) && isxdigit(name[1])
	   && isxdigit(name[2]) && isxdigit(name[3])
	   && name[4] == '.'
	   && isxdigit(name[5]) && isxdigit(name[6])
	   && isxdigit(name[7]) && isxdigit(name[8])
	   && name[9] == '.'
	   && isxdigit(name[10]) && isxdigit(name[11])
	   && isxdigit(name[12]) && isxdigit(name[13])
	   && name[14] == '.'
	   && isxdigit(name[15]) && isxdigit(name[16])
	   && isxdigit(name[17]) && isxdigit(name[18])
	   && name[19] == '.'))
		return 0; /* prefix failed */

	/* check the prefix */
	name += 19;
	for (;;) {
		if (!*name && !*suffix) return 1;
		if (*name != *suffix)   return 0;
		name++; suffix++;
	}
}

static int
s_scandir(struct db *db, const char *path, const char *suffix, fs_handler fn)
{
	/* directory structure is as follows:

	   <path>/
	     xxxx.xxxx/
	       xxxx.xxxx.yyyy.yyyy<suffix>

	   Where xxxxxxxxyyyyyyyy is an unsigned 64-bit integer
	   that represents the identity of the file, for use in
	   tracking and linking.
	 */

	DIR *dh1, *dh2;
	int fd;
	uint64_t id;
	struct dirent *e1, *e2;

	assert(db != NULL);
	assert(db->rootfd >= 0);
	assert(path != NULL);
	assert(suffix != NULL);

	dh1 = dh2 = NULL;
	fd = -1;

	fd = openat(db->rootfd, path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		goto fail;
	dh1 = fdopendir(fd);
	fd = -1;
	if (!dh1)
		goto fail;

	while ((e1 = readdir(dh1)) != NULL) {
		if (!s_istoplevel(e1->d_name))
			continue;

		fd = openat(dirfd(dh1), e1->d_name, O_RDONLY | O_DIRECTORY);
		if (fd < 0) {
			errorf("failed to open %s/%s/ for reading: %s (error %d)",
					path, e1->d_name, strerror(errno), errno);
			continue;
		}

		dh2 = fdopendir(fd);
		if (!dh2) {
			errorf("failed to fdopen %s/%s/ for reading: %s (error %d)",
					path, e1->d_name, strerror(errno), errno);
			close(fd);
			continue;
		}

		while ((e2 = readdir(dh2)) != NULL) {
			if (!s_isdatfile(e2->d_name, suffix)) {
				warningf("file %s/%s/%s does not appear to be a data file (name mismatch)",
						path, e1->d_name, e2->d_name);
				continue;
			}

			id = s_datfileno(e2->d_name);
			fd = openat(dirfd(dh2), e2->d_name, O_RDWR);
			if (fd < 0) {
				errorf("failed to open datfile %s/%s/%s: %s (error %d)",
						path, e1->d_name, e2->d_name, strerror(errno), errno);
				continue;
			}

			if (fn(db, id, fd) != 0)
				goto fail;
		}
		closedir(dh2);
		dh2 = NULL;
	}

	closedir(dh1);
	return 0;

fail:
	if (dh1) closedir(dh1);
	if (dh2) closedir(dh2);
	if (fd >= 0) close(fd);
	return -1;
}

#define s_cwd() open(".", O_RDONLY | O_DIRECTORY)

static int
s_dirempty(int dirfd)
{
	int fd;
	DIR *d;
	struct dirent *e;

	d = NULL;
	e = NULL;

	fd = dup(dirfd);
	if (fd < 0)
		goto fail;

	d = fdopendir(fd);
	if (!d)
		goto fail;
	fd = -1; /* closedir will handle this */

	while ((e = readdir(d)) != NULL) {
		if (streq(e->d_name, ".") || streq(e->d_name, ".."))
			continue;

		/* directory not empty */
		goto fail;
	}

	closedir(d);
	return 1;

fail:
	if (d) closedir(d);
	close(fd);
	return 0;
}

struct db *
db_mount(const char *path)
{
	struct db *db;
	int fd, cwd;

	db = NULL;
	fd = cwd = -1;

	cwd = s_cwd();
	if (cwd < 0)
		goto fail;

	fd = openat(cwd, path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		goto fail;

	db = calloc(1, sizeof(struct db));
	if (!db)
		goto fail;

	db->rootfd = fd;

	errno = BOLO_ENOMAINDB;
	fd = openat(db->rootfd, PATH_TO_MAINDB, O_RDONLY);
	if (fd < 0)
		goto fail;

	db->main = hash_read(fd, 0);
	if (!db->main)
		goto fail;
	close(fd);

	/* FIXME: scan the tags/ subdirectory */

	empty(&db->idx);
	if (s_scandir(db, "idx", ".idx", s_handle_idx) != 0)
		goto fail;

	empty(&db->slab);
	if (s_scandir(db, "slabs", ".slab", s_handle_slab) != 0)
		goto fail;

	close(cwd);
	return db;

fail:
	if (cwd >= 0) close(cwd);
	if (fd  >= 0) close(fd);
	if (db) {
		if (db->rootfd <= 0) close(db->rootfd);
		hash_free(db->main);
		free(db);
	}
	return NULL;
}

struct db *
db_init(const char *path)
{
	struct db *db;
	int cwd, fd;

	assert(path != NULL);

	db = NULL;
	fd = cwd = -1;

	cwd = s_cwd();
	if (cwd < 0)
		goto fail;

	fd = openat(cwd, path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		goto fail;

	/* make sure the root dir is empty */
	if (!s_dirempty(fd))
		goto fail;

	db = calloc(1, sizeof(*db));
	if (!db)
		goto fail;

	db->rootfd = fd;
	fd = -1;
	db->main = hash_new(0);
	if (!db->main)
		goto fail;

	fd = openat(db->rootfd, PATH_TO_MAINDB, O_WRONLY|O_CREAT, 0666);
	if (fd < 0)
		goto fail;

	if (hash_write(db->main, fd) != 0)
		goto fail;
	close(fd);
	fd = -1;

	empty(&db->idx);
	empty(&db->slab);

	return db;

fail:
	if (cwd >= 0) close(cwd);
	if (fd  >= 0) close(fd);
	if (db) {
		if (db->rootfd >= 0) close(db->rootfd);
		hash_free(db->main);
		free(db);
	}
	return NULL;
}

static char *
s_tmpcopyof(const char *path)
{
	char *copy, *p;
	int len;

	assert(path != NULL);

	len = asprintf(&copy, "%s..%08x", path, rand());
	if (len < 0)
		return NULL;

	p = strrchr(copy, '/');
	if (p) p++;
	else   p = copy;

	memmove(p+1, p, len - 10 - (p - copy));
	*p = '.';

	return copy;
}

static int
s_tmpcopyat(int dirfd, const char *origpath, int flags, char **copypath)
{
	int fd;

	assert(dirfd >= 0);
	assert(origpath != NULL);
	assert(copypath != NULL);

	*copypath = s_tmpcopyof(origpath);
	if (!*copypath)
		goto fail;

	fd = openat(dirfd, *copypath, flags|O_CREAT, 0666);
	if (fd < 0)
		goto fail;

	return fd;

fail:
	free(*copypath);
	return -1;
}

int
db_sync(struct db *db)
{
	struct tslab *slab;
	struct idx   *idx;
	char *copy;
	int fd;

	fd = -1;
	copy = NULL;

	for_each(slab, &db->slab, l)
		if (tslab_sync(slab) != 0)
			goto fail;

	for_each(idx, &db->idx, l)
		if (btree_write(idx->btree) != 0)
			goto fail;

	fd = s_tmpcopyat(db->rootfd, PATH_TO_MAINDB, O_WRONLY, &copy);
	if (fd < 0)
		goto fail;

	if (hash_write(db->main, fd) != 0)
		goto fail;

	if (renameat(db->rootfd, copy, db->rootfd, PATH_TO_MAINDB) != 0)
		goto fail;

	close(fd);
	free(copy);
	return 0;

fail:
	if (fd >= 0) close(fd);
	free(copy);
	return -1;
}

int
db_unmount(struct db *db)
{
	struct tslab *slab, *tmp_slab;
	struct idx   *idx,  *tmp_idx;
	int ok;

	assert(db != NULL);

	ok = 0;
	for_eachx(slab, tmp_slab, &db->slab, l) {
		if (tslab_unmap(slab) != 0)
			ok = -1;
		free(slab);
	}

	for_eachx(idx, tmp_idx, &db->idx, l) {
		if (btree_close(idx->btree) != 0)
			ok = -1;
		free(idx);
	}

	hash_free(db->main);
	close(db->rootfd);
	free(db);
	return ok;
}

#define BOLO_TS_BLOCK 512 /* FIXME completely arbitrary */
#define s_normts(t) ((t) - ((t) % BOLO_TS_BLOCK))

static int
s_newidx(struct db *db, struct idx **idx, uint64_t *id)
{
	int fd;
	char path[64];

	assert(db  != NULL);
	assert(idx != NULL);
	assert(id  != NULL);

	*idx = NULL;
	fd = -1;
	if (isempty(&db->idx)) *id = (uint64_t)1;
	else                   *id = item(db->idx.prev, struct idx, l)->number + 1;

	snprintf(path, sizeof(path), "idx/%04lx.%04lx/%04lx.%04lx.%04lx.%04lx.idx",
		((*id & 0xffff000000000000ul) >> 48),
		((*id & 0x0000ffff00000000ul) >> 32),
		/* --- */
		((*id & 0xffff000000000000ul) >> 48),
		((*id & 0x0000ffff00000000ul) >> 32),
		((*id & 0x00000000ffff0000ul) >> 16),
		((*id & 0x000000000000fffful)));
	if (mktree(db->rootfd, path, 0777) != 0)
		goto fail;

	fd = openat(db->rootfd, path, O_RDWR|O_CREAT, 0666);
	if (fd < 0)
		goto fail;

	*idx = malloc(sizeof(**idx));
	if (!*idx)
		goto fail;
	(*idx)->number = *id;

	if (!((*idx)->btree = btree_create(fd)))
		goto fail;

	push(&db->idx, &(*idx)->l);
	return 0;

fail:
	if (fd >= 0) close(fd);
	free(*idx);
	return -1;
}

static int
s_newslab(struct db *db, bolo_msec_t ts, uint64_t *id)
{
	int fd;
	struct tslab *slab;
	char path[64];

	assert(db != NULL);
	assert(id != NULL);

	fd = -1;
	if (isempty(&db->slab)) *id = (uint64_t)1;
	else                    *id = item(db->slab.prev, struct tslab, l)->number + 1;

	slab = malloc(sizeof(*slab));
	if (!slab)
		return -1;

	snprintf(path, sizeof(path), "slabs/%04lx.%04lx/%04lx.%04lx.%04lx.%04lx.slab",
		((*id & 0xffff000000000000ul) >> 48),
		((*id & 0x0000ffff00000000ul) >> 32),
		/* --- */
		((*id & 0xffff000000000000ul) >> 48),
		((*id & 0x0000ffff00000000ul) >> 32),
		((*id & 0x00000000ffff0000ul) >> 16),
		((*id & 0x000000000000fffful)));
	if (mktree(db->rootfd, path, 0777) != 0)
		goto fail;

	fd = openat(db->rootfd, path, O_RDWR|O_CREAT, 0666);
	if (fd < 0)
		goto fail;

	if (tslab_init(slab, fd, *id, 1<<19) != 0)
		goto fail;

	slab->number = *id;
	push(&db->slab, &slab->l);
	return 0;

fail:
	if (fd >= 0) close(fd);
	free(slab);
	return -1;
}

static struct tslab *
s_findslab(struct db *db, uint64_t id)
{
	struct tslab *slab;

	assert(db != NULL);

	for_each(slab, &db->slab, l)
		if (slab->number == id)
			return slab;

	return NULL;
}

int
db_insert(struct db *db, const char *name, bolo_msec_t when, bolo_value_t what)
{
	bolo_msec_t slab_ts;
	struct idx *idx;
	struct tslab *slab;
	uint64_t slab_id, idx_id;

	assert(db != NULL);
	assert(db->main != NULL);
	assert(name != NULL);

	if (hash_getp(db->main, &idx, name) != 0) {
		if (s_newidx(db, &idx, &idx_id) != 0)
			return -1;

		if (hash_setp(db->main, name, idx)    != 0
		 || hash_setv(db->main, name, idx_id) != 0)
			return -1;
	}
	assert(idx != NULL);

	slab_ts = s_normts(when);
	slab_id = btree_find(idx->btree, slab_ts);
	if (slab_id == MAX_U64) {
		if (s_newslab(db, slab_ts, &slab_id) != 0)
			return -1;

		if (btree_insert(idx->btree, slab_ts, slab_id) != 0)
			return -1;
	}
	assert(slab_id != MAX_U64);

	slab = s_findslab(db, slab_id);
	if (!slab)
		return -1;

	if (FIXME_log(slab, when, what) != 0)
		return -1;

	/* FIXME: may need to sync */
	return 0;
}

#ifdef TEST
/* LCOV_EXCL_START */
TESTS {
	startlog("test:db", 0, LOG_ERRORS);

	subtest {
		char *copy;

#define test_copy(in,out) do { \
	srand(0x12345678); \
	copy = s_tmpcopyof(in); \
	is_string(copy, (out), "s_tmpcopyof(" #in ") should equal " #out); \
	free(copy); \
} while (0)

		test_copy("file",            ".file.1b4830b4");
		test_copy("dir/file",        "dir/.file.1b4830b4");
		test_copy("rel/ative/path",  "rel/ative/.path.1b4830b4");
		test_copy("/abso/lute/path", "/abso/lute/.path.1b4830b4");

#undef test_copy
	}

	subtest {
		struct db *db;

		if (system("./t/setup/db-init") != 0)
			BAIL_OUT("t/setup/db-init failed!");

		db = db_mount("t/tmp/new");
		is_null(db, "db_mount() should fail with new (empty) new directories");

		db = db_init("t/tmp/old");
		is_null(db, "db_init() should fail with old (existing) data directories");

		db = db_init("t/tmp/new");
		isnt_null(db, "db_init() should succeed with new (empty) new directories");
		isnt_null(db->main, "db->main hash table should exist for a new database");
		ok(isempty(&db->idx), "db->idx list should be empty on a new database");
		ok(isempty(&db->slab), "db->slab list should be empty on a new database");

		ok(db_insert(db, "metric|tags", 1234567890, 4567.89) == 0,
			"db_insert() should succeed on fresh database");

		ok(db_sync(db) == 0,
			"db_sync() should succeed");
		ok(db_unmount(db) == 0,
			"db_unmount() should succeed");


		db = db_mount("t/tmp/new");
		isnt_null(db, "db_mount() should succeed with newly-init'd data directories");

		ok(db_unmount(db) == 0,
			"db_unmount() should succeed");
	}
}
/* LCOV_EXCL_END */
#endif

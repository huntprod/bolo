#include "bolo.h"

#include <ctype.h>
#include <dirent.h>

#define INITIAL_SLAB (uint64_t)(1 << 11)
#define PATH_TO_MAINDB "main.db"

/* Used as a callback for the directory traversal logic.
   Since the ts/slabs directories use the same structure, we
   reuse the traverseal logic in a single s_scandir function,
   and the customization is provided by the fs_handler. */
typedef int(*fs_handler)(struct db *, uint64_t, int);

struct dbkey *
rand_key(size_t len)
{
	struct dbkey *k;

	CHECK(len != 0, "rand_key() asked to generate a random zero-length key");
	CHECK(len <= 4096, "rand_key() asked to generate an impossible large (>4096) key");

	k = xalloc(1, sizeof(*k));
	k->len = len;
	k->key = xalloc(len, sizeof(char));
	if (urand(k->key, k->len) != 0)
		goto fail;

	return k;

fail:
	if (k) free(k->key);
	free(k);
	return NULL;
}

struct dbkey *
read_key(const char *s)
{
	size_t i, len;
	struct dbkey *k;

	CHECK(s != NULL, "read_key() given a NULL string to decode into a key");

	len = strlen(s);
	if (len % 2)
		goto fail;

	k = xalloc(1, sizeof(*k));
	k->len = len/2;
	k->key = xalloc(len, sizeof(char));
	for (i = 0; i < k->len; i++) {
		switch (s[i*2]) {
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				k->key[i] = ((s[i*2] - '0') & 0x0f) << 4;
				break;
			case 'a': case 'b': case 'c':
			case 'd': case 'e': case 'f':
				k->key[i] = ((s[i*2] - 'a' + 10) & 0x0f) << 4;
				break;
			default:
				goto fail;
		}
		switch (s[i*2+1]) {
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				k->key[i] |= (s[i*2+1] - '0') & 0x0f;
				break;
			case 'a': case 'b': case 'c':
			case 'd': case 'e': case 'f':
				k->key[i] |= (s[i*2+1] - 'a' + 10) & 0x0f;
				break;
			default:
				goto fail;
		}
	}
	return k;

fail:
	if (k) free(k->key);
	free(k);
	return NULL;
}

static int
s_handle_idx(struct db *db, uint64_t id, int fd)
{
	int esave;
	struct idx *idx;

	idx = xmalloc(sizeof(*idx));
	idx->number = id;
	idx->btree = btree_read(fd);
	if (!idx->btree)
		goto fail;

	push(&db->idx, &idx->l);
	return 0;

fail:
	esave = errno;
	free(idx);
	close(fd);
	errno = esave;
	return -1;
}

static int
s_handle_slab(struct db *db, uint64_t id, int fd)
{
	int i, esave;
	struct tslab *slab;
	uint64_t max_tblock;

	slab = xmalloc(sizeof(*slab));
	slab->key = db->key;

	slab->number = id;
	if (tslab_map(slab, fd) != 0)
		goto fail;

	push(&db->slab, &slab->l);

	max_tblock = slab->number;
	for (i = 1; i < TBLOCKS_PER_TSLAB; i++) {
		if (!slab->blocks[i].valid)
			break;
		max_tblock++;
	}
	if (max_tblock >= db->next_tblock)
		db->next_tblock = max_tblock + 1;

	return 0;

fail:
	esave = errno;
	free(slab);
	close(fd);
	errno = esave;
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
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
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

	int esave;
	DIR *dh1, *dh2;
	int fd;
	uint64_t id;
	struct dirent *e1, *e2;

	CHECK(db != NULL,      "s_scandir() given a NULL db to operate on");
	CHECK(db->rootfd >= 0, "s_scandir() given an invalid data directory file descriptor to read from");
	CHECK(path != NULL,    "s_scandir() given a NULL path name to scan");
	CHECK(suffix != NULL,  "s_scandir() given a NULL file prefix to scan for");

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
					path, e1->d_name, error(errno), errno);
			continue;
		}

		dh2 = fdopendir(fd);
		if (!dh2) {
			errorf("failed to fdopen %s/%s/ for reading: %s (error %d)",
					path, e1->d_name, error(errno), errno);
			close(fd);
			continue;
		}

		while ((e2 = readdir(dh2)) != NULL) {
			if (e2->d_name[0] == '.')
				continue;

			if (!s_isdatfile(e2->d_name, suffix)) {
				warningf("file %s/%s/%s does not appear to be a data file (name mismatch)",
						path, e1->d_name, e2->d_name);
				continue;
			}

			id = s_datfileno(e2->d_name);
			fd = openat(dirfd(dh2), e2->d_name, O_RDWR);
			if (fd < 0) {
				errorf("failed to open datfile %s/%s/%s: %s (error %d)",
						path, e1->d_name, e2->d_name, error(errno), errno);
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
	esave = errno;
	if (dh1) closedir(dh1);
	if (dh2) closedir(dh2);
	if (fd >= 0) close(fd);
	errno = esave;
	return -1;
}

#define s_cwd() open(".", O_RDONLY | O_DIRECTORY)

static void
s_ensure_dirat(int dirfd, const char *path, mode_t mode)
{
	int fd;

	fd = openat(dirfd, path, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		if (mktree(dirfd, path, mode) != 0
		 || (mkdirat(dirfd, path, mode) != 0 && errno != EEXIST))
			return;

		fd = openat(dirfd, path, O_RDONLY | O_DIRECTORY);
		if (fd < 0)
			return;
	}
	close(fd);
}

static int
s_dirempty(int dirfd)
{
	int esave;
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
	esave = errno;
	if (d) closedir(d);
	close(fd);
	errno = esave;
	return 0;
}

static uint64_t
s_maindb_writer(const char *key, void *_idx, void *_)
{
	CHECK(_idx != NULL, "main.db writer given a NULL time series index to convert");
	return ((struct idx *)_idx)->number;
}

static void
s_setmetric(struct db *db, char *name, struct idx *idx)
{
	struct multidx *tmp, *set, *this;

	if (!name) return;

	set = NULL;
	if (hash_get(db->metrics, &set, name) == 0) {
		/* run through the full list */
		for (tmp = set; tmp; tmp = tmp->next)
			if (tmp->idx == idx)
				return;
	}

	this = xmalloc(sizeof(*this));
	push(&db->multidx, &this->l);
	this->idx  = idx;
	this->next = set;

	if (hash_set(db->metrics, name, this) != 0)
		errorf("failed to track metric '%s' => idx %p: %s", name, idx, error(errno));
}

static void
s_settags(struct db *db, char *name, struct idx *idx)
{
	char *key, *val;
	struct multidx *tmp, *set, *this;

	while (name) {
		name = tags_next(name, &key, &val);

again:
		set = NULL;
		if (hash_get(db->tags, &set, key) == 0) {
			/* run through the full list */
			for (tmp = set; tmp; tmp = tmp->next)
				if (tmp->idx == idx)
					goto next;
		}

		this = xmalloc(sizeof(*this));
		push(&db->multidx, &this->l);
		this->idx  = idx;
		this->next = set;

		if (hash_set(db->tags, key, this) != 0)
			errorf("failed to track tag '%s' => idx %p: %s", key, idx, error(errno));

next:
		if (*(val - 1) == '=')
			continue;
		*(val - 1) = '='; /* clever hack */
		goto again;
	}
}

static void *
s_maindb_reader(const char *key, uint64_t id, void *udata)
{
	struct db *db;
	struct idx *i;
	char *tags, *next;

	CHECK(udata != NULL, "main.db reader given a NULL db pointer to work with");

	db = (struct db *)udata;
	for_each(i, &db->idx, l) {
		if (i->number != id)
			continue;

		/* expand out the tags hash */
		tags = strdup(key);
		insist(tags != NULL, "main.db reader unable to allocate memory during strdup(tags)");

		next = strchr(tags, '|');
		if (next)
			*next++ = '\0';

		s_setmetric(db, tags, i);
		s_settags(db, next, i);
		free(tags);

		return i;
	}

	return NULL;
}

struct db *
db_mount(const char *path, struct dbkey *key)
{
	struct db *db;
	int fd, cwd;
	int esave;

	db = NULL;
	fd = cwd = -1;

	cwd = s_cwd();
	if (cwd < 0)
		goto fail;

	infof("mounting bolo database at %s", path);
	fd = openat(cwd, path, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		if (errno == ENOENT)
			errno = BOLO_ENODBROOT;
		goto fail;
	}

	db = xalloc(1, sizeof(struct db));
	db->rootfd = fd;
	db->key = key;
	if (!db->key)
		goto fail;

	infof("checking for main.db index file at %s/%s", path, PATH_TO_MAINDB);
	fd = openat(db->rootfd, PATH_TO_MAINDB, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			errno = BOLO_ENOMAINDB;
		goto fail;
	}

	/* first, we have to scan the time series indices */
	infof("scanning time series time index files at %s/idx", path);
	empty(&db->idx);
	s_ensure_dirat(db->rootfd, "idx", 0777);
	if (s_scandir(db, "idx", ".idx", s_handle_idx) != 0)
		goto fail;

	infof("mounting main.db index file at %s/%s", path, PATH_TO_MAINDB);
	empty(&db->multidx);

	db->tags = hash_new(0);
	db->metrics = hash_new(0);
	db->main = hash_read(fd, s_maindb_reader, db);
	if (!db->main)
		goto fail;
	close(fd);

	infof("scanning time series slab storage files at %s/slabs", path);
	empty(&db->slab);
	s_ensure_dirat(db->rootfd, "slabs", 0777);
	if (s_scandir(db, "slabs", ".slab", s_handle_slab) != 0)
		goto fail;

	infof("database mounted successfully");
	close(cwd);
	return db;

fail:
	esave = errno;
	if (cwd >= 0) close(cwd);
	if (fd  >= 0) close(fd);
	if (db) {
		if (db->rootfd <= 0) close(db->rootfd);
		hash_free(db->main);
		free(db);
	}
	errno = esave;
	return NULL;
}

struct db *
db_init(const char *path, struct dbkey *key)
{
	struct db *db;
	int cwd, fd;
	int esave;

	CHECK(path != NULL, "db_init() given a NULL path to read from");

	db = NULL;
	fd = cwd = -1;

	cwd = s_cwd();
	if (cwd < 0)
		goto fail;

	/* make sure we have a root directory */
	s_ensure_dirat(cwd, path, 0777);
	fd = openat(cwd, path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		goto fail;

	/* make sure the root dir is empty */
	if (!s_dirempty(fd))
		goto fail;

	db = xalloc(1, sizeof(*db));

	if (!key)
		key = rand_key(DEFAULT_KEY_SIZE);
	if (!key)
		goto fail;
	db->key = key;

	db->rootfd = fd;
	fd = -1;
	db->main    = hash_new(0);
	db->tags    = hash_new(0);
	db->metrics = hash_new(0);

	/* create the main.db index */
	fd = openat(db->rootfd, PATH_TO_MAINDB, O_WRONLY|O_CREAT, 0666);
	if (fd < 0) {
		if (errno == ENOENT)
			errno = BOLO_ENOMAINDB;
		goto fail;
	}
	if (hash_write(db->main, fd, s_maindb_writer, db) != 0)
		goto fail;
	close(fd);
	fd = -1;

	empty(&db->idx);
	empty(&db->multidx);
	empty(&db->slab);
	db->next_tblock = 0x800;

	return db;

fail:
	esave = errno;
	if (cwd >= 0) close(cwd);
	if (fd  >= 0) close(fd);
	if (db) {
		if (db->rootfd >= 0) close(db->rootfd);
		hash_free(db->main);
		hash_free(db->tags);
		hash_free(db->metrics);
		free(db);
	}
	errno = esave;
	return NULL;
}

static char *
s_tmpcopyof(const char *path)
{
	char *copy, *p;
	int len;

	CHECK(path != NULL, "s_tmpcopyof() given a NULL path to make a copy of");

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

	CHECK(dirfd >= 0,       "s_tmpcopyat() given an invalid working-directory file descriptor");
	CHECK(origpath != NULL, "s_tmpcopyat() given a NULL path to copy");
	CHECK(copypath != NULL, "s_tmpcopyat() given a NULL destination pointer for the copied path");

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
	int esave;

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

	if (hash_write(db->main, fd, s_maindb_writer, db) != 0)
		goto fail;

	if (renameat(db->rootfd, copy, db->rootfd, PATH_TO_MAINDB) != 0)
		goto fail;

	close(fd);
	free(copy);
	return 0;

fail:
	esave = errno;
	if (fd >= 0) close(fd);
	free(copy);
	errno = esave;
	return -1;
}

int
db_unmount(struct db *db)
{
	struct tslab   *slab, *tmp_slab;
	struct idx     *idx,  *tmp_idx;
	struct multidx *set,  *tmp_set;
	int ok;

	CHECK(db != NULL, "db_unmount() given a NULL db pointer to unmount");

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

	for_eachx(set, tmp_set, &db->multidx, l) {
		free(set);
	}

	hash_free(db->main);
	hash_free(db->tags);
	hash_free(db->metrics);
	close(db->rootfd);
	free(db);
	return ok;
}

static int
s_newidx(struct db *db, struct idx **idx, uint64_t *id)
{
	int esave;
	int fd;
	char path[64];

	CHECK(db  != NULL, "s_newidx() given a NULL db pointer to work with");
	CHECK(idx != NULL, "s_newidx() given a NULL destination pointer for the new time series index");
	CHECK(id  != NULL, "s_newidx() given a NULL detination pointer for the new time series id number");

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

	*idx = xmalloc(sizeof(**idx));
	(*idx)->number = *id;

	if (!((*idx)->btree = btree_create(fd)))
		goto fail;

	push(&db->idx, &(*idx)->l);
	return 0;

fail:
	esave = errno;
	if (fd >= 0) close(fd);
	free(*idx);
	errno = esave;
	return -1;
}

static struct tslab *
s_findslab(struct db *db, uint64_t id)
{
	struct tslab *slab;

	CHECK(db != NULL, "s_findslab() given a NULL db pointer to work with");

	id = tslab_number(id);
	for_each(slab, &db->slab, l)
		if (slab->number == id)
			return slab;

	errno = BOLO_ENOSLAB;
	return NULL;
}

static struct tslab *
s_newslab(struct db *db, uint64_t id)
{
	int fd, esave;
	char path[64];
	struct tslab *slab;

	CHECK(db != NULL, "s_newslab() given a NULL db pointer to work with");

	slab = xmalloc(sizeof(*slab));
	slab->key = db->key;

	/* formulate a path, relative to db root, for this slab */
	snprintf(path, sizeof(path), "slabs/%04lx.%04lx/%04lx.%04lx.%04lx.%04lx.slab",
		((id & 0xffff000000000000ul) >> 48),
		((id & 0x0000ffff00000000ul) >> 32),
		/* --- */
		((id & 0xffff000000000000ul) >> 48),
		((id & 0x0000ffff00000000ul) >> 32),
		((id & 0x00000000ffff0000ul) >> 16),
		((id & 0x000000000000fffful)));

	/* create the parent directory, if necessary */
	if (mktree(db->rootfd, path, 0777) != 0)
		goto fail;

	/* create the slab file itself */
	fd = openat(db->rootfd, path, O_RDWR|O_CREAT, 0666);
	if (fd < 0)
		goto fail;

	/* initialize the slab file with tslab headers */
	if (tslab_init(slab, fd, tslab_number(id), TBLOCK_SIZE) != 0)
		goto fail;

	/* keep track of the slab */
	push(&db->slab, &slab->l);

	return slab;

fail:
	esave = errno;
	if (fd >= 0) close(fd);
	free(slab);
	errno = esave;
	return NULL;
}

/*
   s_newblock()

   Extend the database to include the next available tblock.
   If a new tslab needs to be allocated to accommodate the
   new block, that happens transparently to the caller.
 */
static struct tblock *
s_newblock(struct db *db, bolo_msec_t ts)
{
	uint64_t id;
	struct tslab *slab;
	struct tblock *block;

	CHECK(db != NULL, "s_newblock() given a NULL db pointer to work with");

	id = db->next_tblock;
	slab = s_findslab(db, tslab_number(id));
	if (!slab)
		slab = s_newslab(db, tslab_number(id));
	if (!slab)
		return NULL;

	block = tslab_tblock(slab, id, ts);
	if (!block)
		return NULL;

	db->next_tblock++;
	CHECK(db->next_tblock >= 0x800, "s_newblock() apparently rolled over to a tblock of < 0x800 (we never thought we'd hit this boundary)");
	return block;
}

static struct tblock *
s_findblock(struct db *db, uint64_t id, bolo_msec_t ts)
{
	struct tslab *slab;

	CHECK(db != NULL, "s_findblock() given a NULL db pointer to work with");

	slab = s_findslab(db, id);
	if (!slab)
		return NULL;

	return tslab_tblock(slab, id, ts);
}

int
db_insert(struct db *db, char *name, bolo_msec_t when, bolo_value_t what)
{
	struct idx *idx;
	struct tblock *block;
	uint64_t idx_id, block_id;
	char *next;

	CHECK(db != NULL,       "db_insert() given a NULL database to insert into");
	CHECK(db->main != NULL, "db_insert() given a database without a main.db hash");
	CHECK(name != NULL,     "db_insert() given a NULL metric|tagset name to insert");

	if (hash_get(db->main, &idx, name) != 0) {
		if (s_newidx(db, &idx, &idx_id) != 0)
			return -1;
		CHECK(idx != NULL, "db_insert() failed to get a valid time series index structure after calling s_newidx()");

		if (hash_set(db->main, name, idx) != 0)
			return -1;
	}
	CHECK(idx != NULL, "db_insert() failed to get a valid time series index structure from the main.db");

	/* find the tblock ID, if we have one */
	if (btree_find(idx->btree, &block_id, when) != 0) {
		infof("allocating a new tblock for '%s' @%lu", name, when);
		block = s_newblock(db, when);
		if (!block)
			return -1;

		infof("inserting block [%08lu] into btree index...", block->number);
		if (btree_insert(idx->btree, when, block->number) != 0)
			return -1;

	} else {
		block = s_findblock(db, block_id, when);

		if (block && (tblock_isfull(block) || !tblock_canhold(block, when))) {
			struct tblock *new_block;

			new_block = s_newblock(db, when);
			if (!new_block)
				return -1;

			tblock_next(block, new_block);
			block = new_block;

			if (btree_insert(idx->btree, when, block->number) != 0)
				return -1;

		} else if (!block) {
			return -1;
		}
	}

	if (tblock_insert(block, when, what) != 0)
		return -1;

	/* ingest the tags */
	next = strchr(name, '|');
	if (next) *next++ = '\0';
	s_setmetric(db, name, idx);
	s_settags(db, next, idx);

	/* FIXME: may need to sync */
	return 0;
}

struct tblock *
db_findblock(struct db *db, uint64_t blkid)
{
	struct tslab *slab;

	if (blkid == 0)
		return NULL;

	for_each(slab, &db->slab, l) {
		if (slab->number == tslab_number(blkid))
			return slab->blocks + tblock_number(blkid);
	}

	return NULL;
}

#ifdef TEST
/* LCOV_EXCL_START */
TESTS {
	struct dbkey *key1, *key2;

	startlog("test:db", 0, LOG_ERRORS);
	key1 = rand_key(DEFAULT_KEY_SIZE);
	key2 = rand_key(DEFAULT_KEY_SIZE+1);
	if (!key1 || !key2)
		BAIL_OUT("failed to generate random testing keys");

	subtest {
		struct dbkey *k;

		k = read_key("0123456789abcdef");
		isnt_null(k, "shoud read_key() properly");

		is_unsigned(k->len, 8, "k[] should by 8 octets long");
		is_unsigned(k->key[0] & 0xff, 0x01, "k[0] is 0x01");
		is_unsigned(k->key[1] & 0xff, 0x23, "k[1] is 0x23");
		is_unsigned(k->key[2] & 0xff, 0x45, "k[2] is 0x45");
		is_unsigned(k->key[3] & 0xff, 0x67, "k[3] is 0x67");
		is_unsigned(k->key[4] & 0xff, 0x89, "k[4] is 0x89");
		is_unsigned(k->key[5] & 0xff, 0xab, "k[5] is 0xab");
		is_unsigned(k->key[6] & 0xff, 0xcd, "k[6] is 0xcd");
		is_unsigned(k->key[7] & 0xff, 0xef, "k[7] is 0xef");

		free(k->key);
		free(k);
	}

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
		char metric[256];


		if (system("./t/setup/db-init") != 0)
			BAIL_OUT("t/setup/db-init failed!");

		db = db_mount("t/tmp/new", key1);
		is_null(db, "db_mount() should fail with new (empty) new directories");

		db = db_init("t/tmp/old", key1);
		is_null(db, "db_init() should fail with old (existing) data directories");

		db = db_init("t/tmp/new", key1);
		isnt_null(db, "db_init() should succeed with new (empty) new directories");
		isnt_null(db->main, "db->main hash table should exist for a new database");
		ok(isempty(&db->idx), "db->idx list should be empty on a new database");
		ok(isempty(&db->slab), "db->slab list should be empty on a new database");
		is_unsigned(tslab_number(db->next_tblock), (1 << 11),
			"first tblock address of a new db should be in tslab 1");
		is_unsigned(tblock_number(db->next_tblock), 0,
			"first tblock address of a new db should be in tblock 0");

		strcpy(metric, "metric|host=localhost,env=test");
		ok(db_insert(db, metric, 1234567890, 4567.89) == 0,
			"db_insert() should succeed on fresh database");

		strcpy(metric, "metric|host=localhost,env=test");
		ok(db_insert(db, metric, 1234567890 + 1, 4567.91) == 0,
			"db_insert() should succeed twice on fresh database");

		strcpy(metric, "metric|host=localhost,env=test");
		ok(db_insert(db, metric, 1234567890ul + (1ul << 33), 4567.91) == 0,
			"db_insert() should succeed after exhausting block capacity (in time)");

		ok(db_sync(db) == 0,
			"db_sync() should succeed");
		ok(db_unmount(db) == 0,
			"db_unmount() should succeed");


		db = db_mount("t/tmp/new", key1);
		isnt_null(db, "db_mount() should succeed with newly-init'd data directories");

		ok(db_unmount(db) == 0,
			"db_unmount() should succeed");
	}

	subtest {
		struct db *db;
		uint64_t ts;
		char metric[256];

		if (system("./t/setup/db-init") != 0)
			BAIL_OUT("t/setup/db-init failed!");

		db = db_init("t/tmp/new", key2);
		for (ts = 0; ts < TCELLS_PER_TBLOCK * 2 + 1; ts++) {
			strcpy(metric, "metric|host=localhost,env=test");
			if (db_insert(db, metric, 10001 + ts, 4567.89) != 0)
				BAIL_OUT("failed to insert into db\n");
		}

		ok(hash_isset(db->metrics, "metric"),      "the 'metric' metric should be set in the metric index");

		ok(hash_isset(db->tags, "host"),           "the 'host' tag should be set in the tag index");
		ok(hash_isset(db->tags, "host=localhost"), "the 'host=localhost' tag should be set in the tag index");
		ok(hash_isset(db->tags, "env"),            "the 'env' tag should be set in the tag index");
		ok(hash_isset(db->tags, "env=test"),       "the 'env=test' tag should be set in the tag index");

		ok(db_sync(db) == 0,
			"db_sync() should succeed");
		ok(db_unmount(db) == 0,
			"db_unmount() should succeed");

		db = db_mount("t/tmp/new", key2);
		isnt_null(db, "mounted t/tmp/new a second time");

		ok(hash_isset(db->metrics, "metric"),      "the 'metric' metric should be set in the metric index (post-read)");

		ok(hash_isset(db->tags, "host"),           "the 'host' tag should be set in the tag index (post-read)");
		ok(hash_isset(db->tags, "host=localhost"), "the 'host=localhost' tag should be set in the tag index (post-read)");
		ok(hash_isset(db->tags, "env"),            "the 'env' tag should be set in the tag index (post-read)");
		ok(hash_isset(db->tags, "env=test"),       "the 'env=test' tag should be set in the tag index (post-read)");

		ok(db_unmount(db) == 0,
			"db_unmount() should succeed");
	}

	free(key1->key); free(key1);
	free(key2->key); free(key2);
}
/* LCOV_EXCL_STOP */
#endif

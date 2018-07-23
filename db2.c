#include "bolo.h"
#include "bql/public.h"

#include <sys/types.h>
#include <dirent.h>
#include <time.h>

#define INDEX_FTYPE "idx"
#define SLAB_FTYPE  "dat"


////////     ///    ////////    ///    //////// //    // ////////  ////////  //////
//     //   // //      //      // //      //     //  //  //     // //       //    //
//     //  //   //     //     //   //     //      ////   //     // //       //
//     // //     //    //    //     //    //       //    ////////  //////    //////
//     // /////////    //    /////////    //       //    //        //             //
//     // //     //    //    //     //    //       //    //        //       //    //
////////  //     //    //    //     //    //       //    //        ////////  //////

#define _SLAB_MAX_ID      0xffffffffffffffffull
#define _BT_MAX_ID        0xffffffffffffffffull
#define _BT_PAGE_SIZE     8192
#define _BT_DEGREE        ((_BT_PAGE_SIZE - 1 - 2 - 8) / 16)
#define _BT_SPLIT_FACTOR  0.9
#define _BT_SLAB_SIZE     (_BT_PAGE_SIZE * 8192)
#define _BT_LEAF          0x80

struct _btree {
	struct list l;

	int used;
	int leaf;

	uint64_t id;

	struct _btree *nodes[_BT_DEGREE+1];
	struct page page;
};
struct _ref {
	struct list     l;
	struct _ref    *next;
	struct _btree  *bt;
};
struct db2 {
	bolo_msec_t now;           /* override the value of "NOW()", allowing queries
	                              in relative time units like "4m ago" */

	int rootfd;                /* data directory root file descriptor.
	                              all files will be accessed relative to this. */


	struct hash *primary;      /* primary time series hash lookup table.
	                              defined as "x|t1=v1,...,tn=vn" => b
	                              where:

	                                x   is the metric name of the series (i.e. "cpu")
	                                tn  is a tag name
	                                vn  is a tag value
	                                b   is a btree root identifier

	                              note: tags are in canonical (sorted) form.

	                              this hash is stored on-disk as $DATADIR/main.db */

	struct hash *refs;         /* auxiliary references hash lookup table.
	                              contains refchains for all partial matches,
	                              including:

	                                x    (no '=' present) bare metric name references
	                                t=   (no value given) tag existence references (t=*)
	                                t=v  (value present)  tag + value references

	                              these are dynamic, maintained mappings, and are not
	                              persisted to disk in any way. */

	struct list slabs;

	uint64_t btmax;
	int btfd;

	uint64_t next_tblock;

	struct {
		struct list refs;      /* all of the reference objects we've allocated. */
		struct list btrees;    /* all of the btree node objects we've allocated. */
	} allocated;
};


////////     ///    //////// //     //  //////
//     //   // //      //    //     // //    //
//     //  //   //     //    //     // //
////////  //     //    //    /////////  //////
//        /////////    //    //     //       //
//        //     //    //    //     // //    //
//        //     //    //    //     //  //////

/*
   Convert a 64-bit ID into a filesystem path.

   The resulting filesystem structure, if all IDs
   are represented, should be manageable by normal
   filesystem utilities (ls, cat, mv, etc.) without
   undue performance delays.
 */
static int id2path(char *buf, size_t len, const char *type, uint64_t id) {
	int n;
	n = snprintf(buf, len,
		"%s/%04lx/%04lx/%04lx/%04lx.%04lx.%04lx.%04lx.%s",
		type,
		/* --- */
		((id & 0xffff000000000000ul) >> 48),
		/* --- */
		((id & 0x0000ffff00000000ul) >> 32),
		/* --- */
		((id & 0x00000000ffff0000ul) >> 16),
		/* --- */
		((id & 0xffff000000000000ul) >> 48),
		((id & 0x0000ffff00000000ul) >> 32),
		((id & 0x00000000ffff0000ul) >> 16),
		((id & 0x000000000000fffful)),
		/* --- */
		type);

	return (n < 0 || n > PATH_MAX) ? -1 : 0;
}


////////  //////// ////////  //////
//     // //       //       //    //
//     // //       //       //
////////  //////   //////    //////
//   //   //       //             //
//    //  //       //       //    //
//     // //////// //        //////

/*
   Free a reference chain link structure.
 */
static void deref(struct _ref *r) {
	CHECK(r != NULL, "deref() given a NULL ref structure to free");

	empty(&r->l);
	free(r);
}

/*
   Create and append a new reference from subj -> bt.

   Allocates a new refchain link pointing at a btree
   node and links it into the existing refchain identified
   by _subj_.  If no existing refchain is found, a new
   one is created, consisting solely of the new ref.

   NOTE: this is a great opportunity for memory
         use optimization, via a _ref free-list
         maintained in the db structure.
 */
static void ref(struct db2 *db, char *subj, struct _btree *bt) {
	struct _ref *r, *extant;

	CHECK(db != NULL,             "ref() given a NULL db structure");
	CHECK(db->refs != NULL, "ref() given a db structure with a NULL references hash");
	CHECK(subj != NULL,           "ref() given a NULL referring subject");

	r = xmalloc(sizeof(*r));
	r->bt =  bt;
	push(&db->allocated.refs, &r->l);

	if (hash_get(db->refs, &extant, subj) == 0)
		r->next = extant;

	hash_set(db->refs, subj, r);
}

static struct _ref * alsoref(struct list *lst, struct _ref *tail, struct _ref *ref) {
	struct _ref *r;

	r = xmalloc(sizeof(*r));
	push(lst, &r->l);

	/* track the btree node */
	r->bt = ref->bt;

	/* insert the new ref at the head of the field data refs */
	r->next = tail;
	return r;
}

static struct _ref * copyrefs(struct list *l, struct _ref *orig) {
	struct _ref *copy;

	copy = NULL;
	for (; orig; orig = orig->next)
		copy = alsoref(l, copy, orig);

	return copy;
}


////////  //        ///////   //////  //    //  //////
//     // //       //     // //    // //   //  //    //
//     // //       //     // //       //  //   //
////////  //       //     // //       /////     //////
//     // //       //     // //       //  //         //
//     // //       //     // //    // //   //  //    //
////////  ////////  ///////   //////  //    //  //////

static struct tslab * find_slab(struct db2 *db, uint64_t id) {
	struct tslab *slab;

	id = id & ~0x7ff;
	for_each(slab, &db->slabs, l)
		if (slab->number == id)
			return slab;

	return NULL;
}

static struct tblock * find_block(struct db2 *db, uint64_t id) {
	struct tslab *slab;

	slab = find_slab(db, id);
	return slab ? slab->blocks + (id & 0x7ff)
	            : NULL;
}

static struct tblock * new_block(struct db2 *db, bolo_msec_t ts) {
	int fd;
	uint64_t id;
	struct tslab *slab;
	struct tblock *blk;
	char path[PATH_MAX];

	id = db->next_tblock;
	slab = find_slab(db, id);
	if (!slab) {
		slab = xmalloc(sizeof(*slab));
		/* FIXME: set the slab key from the db! */

		if (id2path(path, PATH_MAX, SLAB_FTYPE, id) < 0)
			goto fail;

		if (mktree(db->rootfd, path, 0777) != 0)
			goto fail;

		fd = openat(db->rootfd, path, O_RDWR | O_CREAT, 0666);
		if (fd < 0)
			goto fail;

		if (tslab_init(slab, fd, tslab_number(id), TBLOCK_SIZE) != 0)
			goto fail;

		push(&db->slabs, &slab->l);
	}

	blk = tslab_tblock(slab, id, ts);
	if (!blk)
		goto fail;

	db->next_tblock++;
	return blk;

fail:
	if (slab) tslab_unmap(slab);
	return NULL;
}

static int can_hold(struct tblock *blk, bolo_msec_t ts) {
	if (blk->cells == TCELLS_PER_TBLOCK)
		return 0; /* full block */

	if (ts - blk->base >= MAX_U32)
		return 0; /* exceeded block timeframe */

	return 1;
}


////////  //////// ////////  //////// ////////
//     //    //    //     // //       //
//     //    //    //     // //       //
////////     //    ////////  //////   //////
//     //    //    //   //   //       //
//     //    //    //    //  //       //
////////     //    //     // //////// ////////

#define _BT_KEYS 8
#define _BT_VALS (_BT_KEYS + _BT_DEGREE * 8)

#define get_btkey(t,i) (page_read64(&(t)->page, _BT_KEYS + (i) * 8))
#define get_btval(t,i) (page_read64(&(t)->page, _BT_VALS + (i) * 8))

#define set_btkey(t,i,k) (page_write64(&(t)->page, _BT_KEYS + (i) * 8, (k)))
#define set_btval(t,i,v) (page_write64(&(t)->page, _BT_VALS + (i) * 8, (v)))
#define set_btkid(t,i,c) do { set_btval((t),(i),(c)->id); (t)->nodes[(i)] = (c); } while (0)

/*
   Allocate the next btree root node in the sequence.
 */
static struct _btree * next_btree(struct db2 *db) {
	uint64_t id;
	struct _btree *bt;
	off_t offset;

	char path[PATH_MAX];

	id = db->btmax + 1;

	offset = -1;
	if (db->btfd >= 0)
		offset = lseek(db->btfd, 0, SEEK_END);

	if (offset < 0 || offset >= _BT_SLAB_SIZE) {
		/* current btree slab is full; make a new one */

		if (id2path(path, PATH_MAX, INDEX_FTYPE, id) < 0)
			return NULL;

		if (mktree(db->rootfd, path, 0777) != 0)
			return NULL;

		db->btfd = openat(db->rootfd, path, O_RDWR | O_CREAT, 0666);
		if (db->btfd < 0)
			return NULL;

		offset = 0;
	}

	fprintf(stderr, "BTREE INSERT at offset %d\n", offset);

	bt = xmalloc(sizeof(*bt));
	push(&db->allocated.btrees, &bt->l);

	bt->leaf = _BT_LEAF;
	bt->used = 0;
	bt->id = db->btmax = id;

	lseek(db->btfd, _BT_PAGE_SIZE - 1, SEEK_CUR);
	fprintf(stderr, "writing final 0 at offset %d\n", lseek(db->btfd, 0, SEEK_CUR));
	if (write(db->btfd, "\0", 1) != 1)
		return NULL;

	lseek(db->btfd, offset, SEEK_SET);
	fprintf(stderr, "writing btree header at offset %d\n", lseek(db->btfd, 0, SEEK_CUR));
	if (write(db->btfd, "BTREE\x80\x00\x00", 8) != 8)
		return NULL;

	if (page_map(&bt->page, db->btfd, offset, _BT_PAGE_SIZE) != 0)
		return NULL;

	return bt;
}

static int find(struct _btree *bt, uint64_t *v, bolo_msec_t k) {
	int i, lo, hi;

	while (bt) {
		/* handle empty root nodes */
		if (bt->leaf && bt->used == 0) return -1;

		/* find the proper key index */
		lo = -1;
		hi = bt->used;
		while (lo + 1 < hi) {
			i = (lo + hi) / 2;
			if (get_btkey(bt,i) == k) goto done;
			if (get_btkey(bt,i) >  k) hi = i;
			else                      lo = i;
		}
		i = hi;

done:
		if (bt->leaf) {
			if (i == 0 || k == get_btkey(bt,i)) *v = get_btval(bt, i);
			else                                *v = get_btval(bt, i-1);
			return 0;
		}

		bt = bt->nodes[i];
	}
	return -1;
}

static void shift(struct _btree *t, int n) {
	if (t->used - n <= 0)
		return;

	/* slide all keys above [n] one slot to the right */
	memmove((uint8_t *)t->page.data + _BT_KEYS + (n + 1) * 8,
	        (uint8_t *)t->page.data + _BT_KEYS + n * 8,
	        sizeof(bolo_msec_t) * (t->used - n));

	/* slide all values above [n] one slot to the right */
	memmove((uint8_t *)t->page.data + _BT_VALS + (n + 2) * 8,
	        (uint8_t *)t->page.data + _BT_VALS + (n + 1) * 8,
	        sizeof(uint64_t) * (t->used - n));
	memmove(&t->nodes[n + 2],
	        &t->nodes[n + 1],
	        sizeof(struct _btree*) * (t->used - n));
}

static struct _btree * track1(struct db2 *db, struct _btree *t, bolo_msec_t k, uint64_t v, bolo_msec_t *m) {
	int i, mid;
	struct _btree *r;

	CHECK(t != NULL,               "(btree) insert() given a NULL node to insert into");
	CHECK(t->used <= BTREE_DEGREE, "(btree) insert() given a node that was impossibly full");
	/* invariant: Each node in the btree will always have enough
	              free space in it to insert at least one value
	              (either a literal, or a node pointer).

	              Splitting is done later in this function (right
	              before returning) as necessary. */

	i = find(t, NULL, k);

	if (t->leaf) { /* insert into this node */
		if (i < t->used && get_btkey(t,i) == k) {
			set_btval(t,i,v);
			return NULL;
		}

		shift(t, i);
		t->used++;
		set_btkey(t,i,k);
		set_btval(t,i,v);

	} else { /* insert in child */
		if (!t->nodes[i])
			t->nodes[i] = next_btree(db);
		if (!t->nodes[i])
			return NULL;

		r = track1(db, t->nodes[i], k, v, m);
		if (r) {
			shift(t, i);
			t->used++;
			set_btkey(t, i,  *m);
			set_btkid(t, i+1, r);
			return NULL;
		}
	}

	/* split the node now, if it is full, to save complexity */
	if (t->used == BTREE_DEGREE) {
		mid = t->used * BTREE_SPLIT_FACTOR;
		*m = get_btkey(t,mid);

		r = next_btree(db);
		r->leaf = t->leaf;

		/* divide t into [ t . r ] at mid */
		memmove((uint8_t *)r->page.data + _BT_KEYS,
		        (uint8_t *)t->page.data + _BT_KEYS + (mid+1) * 8,
		        sizeof(bolo_msec_t) * r->used);
		memmove((uint8_t *)r->page.data + _BT_VALS,
		        (uint8_t *)t->page.data + _BT_VALS + (mid+1) * 8,
		        sizeof(uint64_t) * (r->used + 1));
		memmove(r->nodes,
		        &t->nodes[mid + 1],
		        sizeof(struct _btree *) * (r->used + 1));

		return r;
	}

	return NULL;
}

static int track(struct db2 *db, struct _btree *bt, bolo_msec_t k, uint64_t v) {
	struct _btree *l, *r;
	bolo_msec_t m;

	r = track1(db, bt, k, v, &m);
	if (r) {
		/* pivot root to the left */
		l = next_btree(db);
		l->used = bt->used;
		l->leaf = bt->leaf;
		memmove(l->nodes, bt->nodes, sizeof(bt->nodes));
		memmove(l->page.data, bt->page.data, BTREE_PAGE_SIZE);

		/* re-initialize root as [ l . m . r ] */
		bt->used = 1;
		bt->leaf = 0;
		set_btkid(bt, 0, l);
		set_btkey(bt, 0, m);
		set_btkid(bt, 1, r);
	}

	return 0;
}

/*
   Find the btree root node for the given ID.

   Returns a pointer to the btree node structure, if found,
   or NULL if not.
 */
static struct _btree * find_btree(struct db2 *db, uint64_t id) {
	struct _btree *bt;

	for_each(bt, &db->allocated.btrees, l)
		if (bt->id == id)
			return bt;
	return NULL;
}


 ///////  //     // //////// ////////  //    //
//     // //     // //       //     //  //  //
//     // //     // //       //     //   ////
//     // //     // //////   ////////     //
//  // // //     // //       //   //      //
//    //  //     // //       //    //     //
 ///// //  ///////  //////// //     //    //

static int plan_cond(struct db2 *db, struct bql_query *q, struct bql_cond *qc, char *buf, size_t len) {
	int n;
	struct _ref *r;

	switch (qc->op) {
	default: return 0;
	case BQL_COND_AND:
	case BQL_COND_OR:     if (plan_cond(db, q, qc->b, buf, len) != 0) return -1;
	case BQL_COND_NOT:    if (plan_cond(db, q, qc->a, buf, len) != 0) return -1;
	                      return 0;

	case BQL_COND_EQ:     n = snprintf(buf, len, "%s=%s", (char *)qc->a, (char *)qc->b);
	                      break;

	case BQL_COND_EXIST:  n = snprintf(buf, len, "%s", (char *)qc->a);
	                      break;
	}

	if (n < 0 || (size_t)n > len)
		return -1;

	qc->refs = NULL;
	if (hash_get(db->refs, &r, buf) == 0)
		qc->refs = copyrefs(&q->alloc_refs, r);
	return 0;
}

static int satisfies(struct bql_cond *qc, struct _btree *bt) {
	struct _ref *ref;

	if (!qc) return 0; /* cannot satisfy the null condition */

	switch (qc->op) {
	default:           return 0; /* does not meet */
	case BQL_COND_AND: return satisfies(qc->a, bt) && satisfies(qc->b, bt);
	case BQL_COND_OR:  return satisfies(qc->a, bt) || satisfies(qc->b, bt);
	case BQL_COND_NOT: return !satisfies(qc->a, bt);

	case BQL_COND_EQ:
	case BQL_COND_EXIST:
		for (ref = qc->refs; ref; ref = ref->next)
			if (ref->bt == bt)
				return 1;
		return 0;
	}
}

static int plan_query(struct db2 *db, struct bql_query *q) {
	int i;
	char buf[8192];
	struct bql_field *f;
	struct _ref *refs;

	if (q->where)
		if (plan_cond(db, q, q->where, buf, sizeof(buf) / sizeof(char)) != 0)
			return -1;

	for (f = q->select; f; f = f->next) {
		for (i = 0; f->ops[i].code != BQL_OP_RETURN; i++) {
			switch (f->ops[i].code) {
			case BQL_OP_PUSH:
				if (hash_get(db->refs, &refs, f->ops[i].data.push.metric) != 0) {
					q->error = QERR_NOSUCHREF;
					q->errdat = strdup(f->ops[i].data.push.metric);
					return -1;
				}

				f->ops[i].data.push.refs = NULL;
				for (; refs; refs = refs->next)
					if (satisfies(q->where, refs->bt))
						f->ops[i].data.push.refs = alsoref(&q->alloc_refs, f->ops[i].data.push.refs, refs);
				break;
			}
		}
	}

	return 0;
}


////////  ////////
//     // //     //
//     // //     //
//     // ////////
//     // //     //
//     // //     //
////////  ////////

/*
   Initialize a new Bolo TSDB data directory.

   This creates the necessary filesystem structures for a
   subsequent call to db2_open() to succeed.

   The new data directory will be empty, with no series or
   measurements in it.

   Returns 0 on success.  On failure, returns -1, and sets
   _errno_ appropriately.
 */
int db2_init(const char *path) {
	int fd = -1;
	DIR *dh = NULL;
	struct dirent *ent = NULL;

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		goto fail;

	dh = fdopendir(fd);
	if (!dh)
		goto fail;
	fd = -1; /* belongs to dh now */

	while ((ent = readdir(dh)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0
		 || strcmp(ent->d_name, "..") == 0)
			continue;

		goto fail;
	}

	fd = openat(dirfd(dh), "main.db", O_CREAT | O_RDWR, 0660);
	if (fd < 0)
		goto fail;

	write(fd, "\0\0", 2);
	/* FIXME: write the HMAC signature */
	close(fd);

	if (mkdirat(dirfd(dh), INDEX_FTYPE, 0770) != 0)
		goto fail;
	if (mkdirat(dirfd(dh), SLAB_FTYPE, 0770) != 0)
		goto fail;

	closedir(dh);
	return 0;

fail:
	if (dh)      closedir(dh);
	if (fd >= 0) close(fd);
	return -1;
}

/*
   Open a Bolo TSDB data directory for reading and writing.

   Interprets _path_ as the root data directory of a Bolo
   TSDB database, and attempts to mount it in-memory.

   Returns a pointer to a new TSDB object (struct db2) on
   success.  On failure, returns NULL, and sets _errno_
   appropriately.
 */
struct db2 * db2_open(const char *path) {
	int fd = -1;
	struct db2 *db = NULL;
	uint64_t id;
	char fpath[PATH_MAX];

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		goto fail;

	db = calloc(1, sizeof(struct db2));
	if (!db)
		goto fail;

	/* keep track of our data directory root file descriptor */
	db->rootfd = fd;

	/* initialize db structure internal state, so that it's always there */
	empty(&db->slabs);
	empty(&db->allocated.refs);
	empty(&db->allocated.btrees);
	db->refs    = hash_new();
	db->primary = hash_new();

	/* read in the index of metric|tags=values -> btree node */
	fd = openat(db->rootfd, INDEX_FTYPE, O_RDONLY | O_DIRECTORY);
	if (fd < 0) goto fail;
	close(fd);

	db->btfd = -1;
	for (id = 0; id < _BT_MAX_ID; id += _BT_PAGE_SIZE) {
		if (id2path(fpath, PATH_MAX, INDEX_FTYPE, id) < 0)
			goto fail;

		fd = openat(db->rootfd, fpath, O_RDWR);
		if (fd < 0)
			break;

		{
			/* each btree node looks like this:

			   [5]   magic number "BTREE"
			   [1]   node-specific flags
			   [2]   # of keys used (<=k)
			   [k]   64-bit keys
			   [k+1] 64-bit values

			   each btree slab file is a series of such nodes,
			   stored sequentially.  there is no header, and
			   no trailer.
			 */

			struct _btree *bt, *p;
			off_t offset;
			ssize_t i;

			db->btmax = id;
			db->btfd = fd;
			offset = lseek(fd, 0, SEEK_END);

			/* map all of the btree nodes, without resolving child nodes */
			for (i = 0; i < offset; i += _BT_PAGE_SIZE) {
				bt = xmalloc(sizeof(*bt));
				push(&db->allocated.btrees, &bt->l);

				lseek(fd, i * _BT_PAGE_SIZE, SEEK_SET);
				fprintf(stderr, "mapping btree page\n");
				if (page_map(&bt->page, fd, i, _BT_PAGE_SIZE) != 0)
					goto fail;

				if (page_read8(&bt->page, 0) != 'B'
				 || page_read8(&bt->page, 1) != 'T'
				 || page_read8(&bt->page, 2) != 'R'
				 || page_read8(&bt->page, 3) != 'E'
				 || page_read8(&bt->page, 4) != 'E') {
					errorf("btree node %lu+%i (%s at offset %i) is corrupt; no BTREE header found", id, i, fpath, i);
					delist(&bt->l);
					if (page_unmap(&bt->page))
						warningf("failed to unmap backing page for btree %p; we might have just leaked a file descriptor.", (void *)bt);
					free(bt);
					continue;
				}

				bt->id = db->btmax++;
				bt->leaf = page_read8 (&bt->page, 5) & _BT_LEAF;
				bt->used = page_read16(&bt->page, 6);
			}

			/* scan through the btree nodes and resolve the child nodes */
			for_each(bt, &db->allocated.btrees, l) {
				if (bt->leaf)
					continue;

				for (i = 0; i < _BT_DEGREE; i++) {
					for_each(p, &db->allocated.btrees, l) {
						if (p->id != get_btkey(bt, i))
							continue;
						set_btkid(bt, i, p);
						break;
					}
				}

				if (i == _BT_DEGREE)
					goto fail; /* corrupt btree. */

			}
		}
		close(fd);
	}

	/* read in the slab files, which contain all of the data */
	fd = openat(db->rootfd, SLAB_FTYPE, O_RDONLY | O_DIRECTORY);
	if (fd < 0) goto fail;
	close(fd);

	for (id = 0; id < _SLAB_MAX_ID; id += TBLOCKS_PER_TSLAB) {
		if (id2path(fpath, PATH_MAX, SLAB_FTYPE, id) < 0)
			goto fail;

		fd = openat(db->rootfd, fpath, O_RDWR);
		if (fd < 0)
			break;

		{
			struct tslab *sl;
			uint64_t blkid;
			int i;

			sl = xmalloc(sizeof(*sl));
			/* FIXME set the key! */

			sl->number = id;
			if (tslab_map(sl, fd) != 0)
				goto fail;

			push(&db->slabs, &sl->l);

			for (blkid = id, i = 1; i < TBLOCKS_PER_TSLAB && sl->blocks[i].valid; i++)
				;
			if (blkid >= db->next_tblock)
				db->next_tblock = blkid;
		}

		close(fd);
	}

	/* read the primary "metric|tag=value,..." index from disk */
	fd = openat(db->rootfd, "main.db", O_RDWR);
	if (fd < 0)
		goto fail;

	{
		/** format of the main.db file is as follows:

		    [ size(name) ][ name ][ btree-id ]*
		          |          |         |
		          |          |         `--- 64 bits (8 octets)
		          |          |              stored in host byte order
		          |          |
		          |          `------------- 1-65534 octets
		          |                         (no null-terminator)
		          |
		          `------------------------ 16 bits (2 octets)
		                                    limits size of series name
		                                    to 64k, which is acceptable.

		    each hash key-value pair is encoded in the above format,
		    and placed sequentially in the main.db file.  at the end
		    of file, the stream is terminated by two NULL (0x0) octets
		    (00000000b), followed by an HMAC-SHA512 sig. (64 octets)
		 */

		char buf[BOLO_BUFSIZ];   /* our read buffer; invariant:
		                            BOLO_BUFSIZ > BOLO_SERIES_NAME_MAXLEN */

		size_t used;             /* how much of buf[] contains actual data? */
		ssize_t nread;           /* how much did we read on the last read op? */

		uint16_t record_len,     /* length of current [len|name|id] record */
		         name_len;       /* length of [name] in the current record */
		uint64_t btid;           /* 64-bit btree id for the current record */

		used = 0;
		for (;;) {
			nread = read(fd, buf + used, BOLO_BUFSIZ - used);
			if (nread <  0) goto fail;
			if (nread == 0) break;

			used += nread;
			if (used < 2) continue;

			name_len = *(uint16_t *)(buf);
			if (name_len == 0) break;
			if (name_len > BOLO_SERIES_NAME_MAXLEN)
				goto fail;

			/* full length of record will be:
			            2   ; 16-bit length of name
			   + name_len   ; the series name itself
			   +        4   ; 64-bit btree id (root)  */
			record_len = 2 + name_len + 4;

			if (record_len < used)
				continue; /* not enough to process */

			btid = *(uint64_t *)(buf + 2 + name_len);
			*(buf + 2 + name_len) = '\0';
			hash_set(db->primary, buf + 2, find_btree(db, btid));
			memmove(buf, buf + record_len, used - record_len);
			used -= record_len;
		}
		/* FIXME: verify HMAC-SHA512 signature */
	}
	close(fd);

	return db;

fail:
	if (fd >= 0)  close(fd);
	if (db)       db2_close(db);
	return NULL;
}

/*
   Close a Bolo TSDB database, freeing up allocated resources.
 */
void db2_close(struct db2 *db) {
	struct _ref *ref, *reftmp;
	struct _btree *bt, *bttmp;
	struct tslab *sl, *sltmp;

	if (!db) return;

	hash_free(db->refs);
	hash_free(db->primary);

	for_eachx(ref, reftmp, &db->allocated.refs, l)
		deref(ref);

	for_eachx(bt, bttmp, &db->allocated.btrees, l) {
		if (page_unmap(&bt->page))
			warningf("failed to unmap backing page for btree %p; we might have just leaked a file descriptor.", (void *)bt);
		free(bt);
	}

	for_eachx(sl, sltmp, &db->slabs, l) {
		if (tslab_unmap(sl) != 0)
			warningf("failed to unmap backing page for slab %p; we might have just leaked a file descriptor.", (void *)sl);
		free(sl);
	}

	close(db->rootfd);
	free(db);
}

/*
   Insert a new measurement, for a given series, into the database.
 */
int db2_insert(struct db2 *db, char *series, bolo_msec_t ts, bolo_value_t v) {
	struct _btree *bt; /* btree root for this metric series */

	char c, *p, *end;  /* for traversing $series in case we have to
	                      generate auxiliary references for new series. */

	uint64_t blkid;    /* the database block id for the insertion op. */
	struct tblock *blk;

	CHECK(db != NULL,     "db2_insert() given a NULL db structure");
	CHECK(series != NULL, "db2_insert() given a NULL series name");

	if (hash_get(db->primary, &bt, series) != 0) {
		/* this is a metric|tags=values instance we have not yet seen... */

		/* allocate a new btree root for this metric series */
		bt = next_btree(db);
		hash_set(db->primary, series, bt);

		/* track all constituent tags in auxiliary lookup tables */
		/* first, the metric name */
		p = series; end = strchr(p, '|');
		if (end == NULL)    /* no '|' in "metric|tag=value,x=y,..." metric name, */
			goto insert;    /* which is kind of weird, but we can't stop now...  */

		*end = '\0';
		ref(db, p, bt);
		*end = '|';
		p = end + 1;

		/* then, track all the constituent tag=value, both as tag
		   existence ("tag=") refs, and as "tag=value" refs. */
		for (;;) {
			/* first, the 'tag=' existence reference */
			end = strchr(p, '=');
			if (end == NULL)   /* no '=' in "tag=value" section. */
				goto insert;   /* definitely weird.  oh well...  */
			end++; c = *end; *end = '\0';
			ref(db, p, bt);
			*end = c;

			/* then, the full 'tag=value' reference */
			end = strchr(end, ',');
			if (!end) { /* the last tag=value in the list */
				ref(db, p, bt);
				break;
			}
			c = *end; *end = '\0';
			ref(db, p, bt);
			*end = c;
			p = end + 1;
		}
	}

insert:
	/* find the block, if we have one */
	if (find(bt, &blkid, ts) == 0) {
		/* use existing block, if it has capacity */
		blk = find_block(db, blkid);
		if (!blk)
			return -1;

		if (!can_hold(blk, ts)) {
			blk = new_block(db, ts);
			if (!blk)
				return -1;
		}

	} else {
		/* allocate new block */
		blk = new_block(db, ts);
		if (!blk)
			return -1;

		if (track(db, bt, ts, blk->number) != 0)
			return -1;
	}

	if (tblock_insert(blk, ts, v) != 0)
		return -1;

	return 0;
}

struct db2_results * db2_query(struct db2 *db, const char *q) {
	struct db2_results *r; /* the final results object to return */
	struct bql_query *bq;  /* parsed representation of the query */
	struct bql_field *f;   /* for iterating over the SELECTed fields */
	struct _ref *ref,      /* for iterating over references */
	            *reftmp;   /* (reftmp used in freeing references) */

	int nth;               /* keeps track of the field index for the linked list. */
	int i, j, k;           /* generic iterators */
	double *set1, *set2;   /* workspaces for assembling series' */
	double *tmp;           /* temporary pointer to set1 / set2, for swaps */
	int len;               /* how long are tmp, set1, and set2 (each) */
	int b2a;               /* how many bucketed values per aggregate value? */

	struct cf *bucket,     /* reservoir-sampled consolidation buffers, */
	          *aggregate;  /* for phase 1 (bucket) and 2 (aggregate) */

	bolo_msec_t now;       /* what time is it? */
	bolo_msec_t start,     /* boundary timestamps for block traversal. */
	            end;       /* these *can* be calculated, but the code  */
	                       /* looks better if we just cache the value. */

	/* nullify allocated resources, so that the fail handler doesn't segfault */
	tmp = set1 = set2 = NULL;
	bucket = aggregate = NULL;

	/* parse and plan the query */
	fprintf(stderr, "# parsing query '%s'\n", q);
	bq = bql_parse(q);
	if (!bq)                     goto fail;
	fprintf(stderr, "# planning query '%s'\n", q);
	if (plan_query(db, bq) != 0) goto fail;

	fprintf(stderr, "# executing query '%s'\n", q);
	now = db->now ? db->now : MS(time(NULL));

	/* set up the results object we're going to return */
	r = xmalloc(sizeof(struct db2_results));
	r->start   = now + bq->from;
	r->stride  = bq->aggregate.stride;

	r->nseries = bq->nfields;
	r->nslots  = (now + bq->until - r->start) / r->stride;

	r->values  = xmalloc(sizeof(bolo_value_t) * r->nslots * r->nseries);
	r->series  = xcalloc(r->nseries, sizeof(char *));

	for (i = 0, f = bq->select; f; i++, f = f->next)
		r->series[i] = strdup(f->name);

	/* set up the reservoir samplers, bucket and aggregate.

	   bucket will be used to sample down a ragged set of
	   raw measurements into a single, aligned composite
	   metric, suitable for pre-aggregation math operations.

	   aggregate will be used when we run an aggregating
	   consolidation function, like MAX(x), and need to combine
	   multiple aligned measurements into a more coarse-grained
	   summary measurement (which will also be aligned).
	 */
	bucket    = cf_new(bq->bucket.cf,    bq->bucket.samples);
	aggregate = cf_new(bq->aggregate.cf, bq->aggregate.samples);

	/* calculate b2a, the factor for consolidating bucketed
	   values into aggregated values. */
	b2a = bq->aggregate.stride / bq->bucket.stride;

	/* allocate our temporary working set arrays, set1 and set2.
	   these have to be large enough to house all of the bucketed
	   measurements for the entire query timeframe.
	 */
	len = (bq->until - bq->from) / bq->bucket.stride;
	set1 = xcalloc(len, sizeof(double));
	set2 = xcalloc(len, sizeof(double));
	tmp = NULL;

	/* process each field in the SELECT clause, in serial.

	   this does mean that we may summarize the same base
	   series multiple times, as in the case of this query:

	     SELECT x, y, x + y AS total ...

	   but the current method is simpler to implement and
	   easier to understand.
	 */
	for (nth = 0, f = bq->select; f; nth++, f = f->next) {
		for (i = 0; ; i++) { /* each qop */
			switch (f->ops[i].code) {
			case BQL_OP_RETURN:
				/* set1 should contain the results of whatever operations
				   have gone before (see the rest of this switch block).

				   since the BQL parser is injecting implicit BQL_OP_AGGR
				   qops into the stream, we must have (r->nslots) values.
				 */
				memcpy(r->values + nth*r->nseries, set1, r->nslots * sizeof(double));
				break;

			case BQL_OP_PUSH:
				/* PUSH qops always occur at the raw -> bucketed level.
				   loop through each bucketed set index (0 .. len) and
				   perform a fresh round of reservoir sampling against
				   all constituent time series.
				 */

				tmp = set1; set1 = set2; set2 = tmp;
				for (j = 0; j < len; j++) {
					start = r->start + j * r->stride;
					end   = start + r->stride;

					cf_reset(bucket);
					for (ref = f->ops[i].data.push.refs; ref; ref = ref->next) {
						struct tblock *blk;
						uint64_t blkid;

						/* if we can't find the btree root for this reference,
						   something is weirdly wrong; fail the whole query. */
						if (find(ref->bt, &blkid, start) != 0)
							goto fail;

						/* traverse the database blocks for this series, and
						   sample the values that lay within (start ... end) */
						blk = find_block(db, blkid);
						while (blk) {
							bolo_msec_t ts;

							for (k = 0; k < blk->cells; k++) {
								ts = tblock_ts(blk, k);
								if (ts >= end) { /* moved past the bucket window... */
									blk = NULL;
									break;

								} else if (ts >= start) { /* in the window... */
									cf_sample(bucket, tblock_value(blk, k));
								}
							}

							if (!blk || blk->next == 0)
								break;

							/* onto the next block, unless we are bailing early */
							blk = find_block(db, blk->next);
						}
					}

					set1[j] = cf_value(bucket);
				}
				break;

			case BQL_OP_AGGR:
				aggregate->type = f->ops[i].data.aggr.cf
				                ? f->ops[i].data.aggr.cf
				                : bq->aggregate.cf;

				/* now that we are at the aggregation point, we can reset
				   len to the number of *aggregated* values in the result. */
				len = r->nslots;

				/* consolidate using the aggregation parameters from the query. */
				for (j = 0; j < len; j++) {
					cf_reset(aggregate);
					for (k = 0; k < b2a; k++)
						cf_sample(aggregate, set1[j * b2a + k]);
					set1[j] = cf_value(aggregate);
				}
				break;

			case BQL_OP_ADD:
				for (j = 0; j < len; j++)
					set1[j] += set2[j];
				break;

			case BQL_OP_ADDC:
				for (j = 0; j < len; j++)
					set1[j] += f->ops[i].data.imm;
				break;

			case BQL_OP_SUB:
				for (j = 0; j < len; j++)
					set1[j] -= set2[j];
				break;

			case BQL_OP_SUBC:
				for (j = 0; j < len; j++)
					set1[j] -= f->ops[i].data.imm;
				break;

			case BQL_OP_MUL:
				for (j = 0; j < len; j++)
					set1[j] *= set2[j];
				break;

			case BQL_OP_MULC:
				for (j = 0; j < len; j++)
					set1[j] *= f->ops[i].data.imm;
				break;

			case BQL_OP_DIV:
				for (j = 0; j < len; j++)
					set1[j] = (set2[j] == 0.0) ? NAN : set1[j] / set2[j];
				break;

			case BQL_OP_DIVC:
				if (f->ops[i].data.imm == 0)
					for (j = 0; j < len; j++)
						set1[j] = NAN;
				else
					for (j = 0; j < len; j++)
						set1[j] /= f->ops[i].data.imm;
				break;
			}

			if (f->ops[i].code == BQL_OP_RETURN)
				break;
		}
	}

	/* free the consolidation buckets */
	cf_free(bucket);
	cf_free(aggregate);

	/* free the working sets */
	free(set1);
	free(set2);

	/* free all references used by the query (cond + fields) */
	for_eachx(ref, reftmp, &bq->alloc_refs, l)
		free(ref);

	/* free whatever structures bql.a allocated */
	bql_free(bq);

	return r;

fail:
	/* free the consolidation buckets */
	if (bucket)    cf_free(bucket);
	if (aggregate) cf_free(aggregate);

	/* free the working sets */
	if (set1) free(set1);
	if (set2) free(set2);

	if (bq) {
		/* free all references used by the query (cond + fields) */
		for_eachx(ref, reftmp, &bq->alloc_refs, l)
			free(ref);

		/* free whatever structures bql.a allocated */
		bql_free(bq);
	}

	return NULL;
}

void db2_free_results(struct db2_results *r) {
	unsigned int i;

	if (!r) return;

	for (i = 0; i < r->nseries; i++)
		free(r->series[i]);
	free(r->series);
	free(r->values);
	free(r);
}

#ifdef TEST
//////// ////////  //////  ////////  //////
   //    //       //    //    //    //    //
   //    //       //          //    //
   //    //////    //////     //     //////
   //    //             //    //          //
   //    //       //    //    //    //    //
   //    ////////  //////     //     //////

/* LCOV_EXCL_START */
static int _dir_exists(const char *path) {
	/* CHECK if _path_ exists and is a directory. */
	struct stat st;
	if (lstat(path, &st) == 0)
		return S_ISDIR(st.st_mode);
	return 0;
}
static int _file_exists(const char *path) {
	/* CHECK if _path_ exists and is a file. */
	struct stat st;
	if (lstat(path, &st) == 0)
		return S_ISREG(st.st_mode);
	return 0;
}
static unsigned int _allocated_btree_nodes(struct db2 *db) {
	int n;
	struct _btree *bt;

	n = 0;
	for_each(bt, &db->allocated.btrees, l)
		n++;

	return n;
}

/* 1531000200 = [Sat Jul 07 2018 21:47:00] */
#define TS MS(1531000200)

TESTS {
	int i;
	struct db2 *db;
	struct db2_results *r;

	startlog("test:db2", 0, LOG_ERRORS);

	subtest { /* db2_init() */
		SETUP("rm -rf t/tmp/ && test ! -d t/tmp/e/no/ent");
		ok(db2_init("t/tmp/e/no/ent") != 0, "db2_init() requires a pre-existing directory");

		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp && touch t/tmp/file && test -f t/tmp/file");
		ok(db2_init("tmp") != 0, "db2_init() should fail if the pre-existing directory is not empty");

		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp");
		ok(db2_init("t/tmp") == 0, "db2_init() should succeed with an empty, pre-existing directory");

		ok(_file_exists("t/tmp/main.db"), "db2_init() should create a new $DATADIR/main.db");
		ok(_dir_exists("t/tmp/idx"),      "db2_init() should create a new $DATADIR/idx/ directory");
		ok(_dir_exists("t/tmp/dat"),      "db2_init() should create a new $DATADIR/dat/ directory");
	}

	subtest { /* db2_open() */
		SETUP("rm -rf t/tmp/ && test ! -d t/tmp/e/no/ent");
		is_null((db = db2_open("t/tmp/e/no/ent")),
		        "db2_open() should fail if $DATADIR does not exist");
		db2_close(db);

		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp");
		is_null((db = db2_open("t/tmp")),
		        "db2_open() should fail if $DATADIR is empty");
		db2_close(db);

		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp && touch t/tmp/file && test -f t/tmp/file");
		is_null((db = db2_open("t/tmp")),
		        "db2_open() should fail if $DATADIR doesn't contain an idx/ subdir");
		db2_close(db);

		/* db2_open without idx/ ... */
		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp");
		if (db2_init("t/tmp") != 0) BAIL("*** unable to db2_init(t/tmp/) ***");
		SETUP("rm -rf t/tmp/idx && test ! -d t/tmp/idx");
		is_null((db = db2_open("t/tmp")),
		          "db2_open() should fail if $DATADIR does not have an idx/ subdir");
		db2_close(db);

		/* db2_open without slab/ ... */
		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp");
		if (db2_init("t/tmp") != 0) BAIL("*** unable to db2_init(t/tmp/) ***");
		SETUP("rm -rf t/tmp/dat");
		is_null((db = db2_open("t/tmp")),
		          "db2_open() should fail if $DATADIR does not have a slab/ subdir");
		db2_close(db);

		/* db2_open without main.db ... */
		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp");
		if (db2_init("t/tmp") != 0) BAIL("*** unable to db2_init(t/tmp/) ***");
		SETUP("rm -rf t/tmp/main.db");
		is_null((db = db2_open("t/tmp")),
		          "db2_open() should fail if $DATADIR does not have a main.db index");
		db2_close(db);

		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp");
		if (db2_init("t/tmp") != 0) BAIL("*** unable to db2_init(t/tmp/) ***");
		isnt_null((db = db2_open("t/tmp")),
		          "db2_open() should succeed if $DATADIR has been properly initialized");
		db2_close(db);
	}

	subtest { /* syntactic validity */
		char metric[1024];
		const char *metrics[] = {
			"a", "b", "c", "x",
			"cpu", "mem", "swap", "disk",
			"mem.used", "mem.free",
			"cpu.total", "cpu.count",
			"one.less", "two.less",
			"what.ever",
			"disk.io.rd@/",
			NULL
		};
		const char *valid[] = {
			"select cpu",
			"SELECT cpu",
			"SElEcT cpu",

			"select a, b",
			"select a, b, c",

			"select cpu, swap",
			"select cpu, swap, disk",

			"select mem.free",
			"select disk.io.rd@/",

			"select cpu where host = localhost",
			"select cpu where some-tag exists",
			"select cpu where some-tag exist",
			"select cpu where not (some-tag exists)",

			"select cpu where some-tag does not exist",
			"select cpu where some-tag does not exists",

			"select cpu where a=b",
			"select cpu where (a = b)",
			"select cpu where a = b and c = d",
			"select cpu where a = b && c = d",
			"select cpu where a = b or c = d",
			"select cpu where a = b || c = d",
			"select cpu where (a = b || c = d) AND e = f",
			"select cpu where (a=b||c=d)&&e=f",
			"select cpu where (( (a = b) || (c = d) ) && (e = f))",

			"select mem aggregate 1h",
			"select mem aggregate 1.5h",
			"select mem aggregate 1 hour",
			"select mem aggregate 1.5 hour",
			"select mem aggregate 42 hours",
			"select mem aggregate -1.5 hours",

			"select x after 4h ago and before now",
			"select x before now and after 4h ago",
			"select x between 4h ago and now",
			"select x between 4h ago and 2h ago",
			"select x between 4 hours ago and 2 hours ago",
			"select x between -4h and -2h",
			"select x between -4 hours and -2 hours",
			"select x after -4 hours and before -2 hours",
			"select x after -4h",
			"select x after 4h ago",

			"select x between 1.5h ago and now",
			"select x between -1.5h and now",

			/* you can re-arrange the select, where, when, and
			   aggregate clauses to your little hearts content. */
			"select x where a=b between 4h ago and now aggregate 10m",
			"select x where a=b aggregate 10m between 4h ago and now",
			"select x between 4h ago and now where a=b aggregate 10m",
			"select x between 4h ago and now aggregate 10m where a=b",
			"select x aggregate 10m where a=b between 4h ago and now",
			"select x aggregate 10m between 4h ago and now where a=b",
			/* ... */
			"where a=b select x between 4h ago and now aggregate 10m",
			"where a=b select x aggregate 10m between 4h ago and now",
			"where a=b between 4h ago and now select x aggregate 10m",
			"where a=b between 4h ago and now aggregate 10m select x",
			"where a=b aggregate 10m select x between 4h ago and now",
			"where a=b aggregate 10m between 4h ago and now select x",
			/* ... */
			"between 4h ago and now select x where a=b aggregate 10m",
			"between 4h ago and now select x aggregate 10m where a=b",
			"between 4h ago and now where a=b select x aggregate 10m",
			"between 4h ago and now where a=b aggregate 10m select x",
			"between 4h ago and now aggregate 10m select x where a=b",
			"between 4h ago and now aggregate 10m where a=b select x",
			/* ... */
			"aggregate 10m select x where a=b between 4h ago and now",
			"aggregate 10m select x between 4h ago and now where a=b",
			"aggregate 10m where a=b select x between 4h ago and now",
			"aggregate 10m where a=b between 4h ago and now select x",
			"aggregate 10m between 4h ago and now select x where a=b",
			"aggregate 10m between 4h ago and now where a=b select x",

			/* math is a thing we can do */
			"select mem.used + mem.free as mem.total",
			"select cpu.total / cpu.count as cpu.each",
			"select one.less + 1 as one",
			"select 1 + one.less as one",
			"select 1 + 1 + two.less as one",
			"select ((1 * 2) + ((3) * 4)) / what.ever as metric",

			/* functions too */
			"select max(mem.used), max(mem.free) aggregate 5m",

			NULL
		};

		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp");
		if (db2_init("t/tmp") != 0)    BAIL("*** unable to db2_init(t/tmp/) ***");
		if (!(db = db2_open("t/tmp"))) BAIL("*** unable to db2_open(t/tmp/) ***");

		for (i = 0; metrics[i]; i++) {
			snprintf(metric, sizeof(metric)/sizeof(char), "%s|tag=value", metrics[i]);
			ok(db2_insert(db, metric, TS, 4.2) == 0, "should be able to seed '%s' metric", metric);
		}

		for (i = 0; valid[i]; i++) {
			r = db2_query(db, valid[i]);
			isnt_null(r, "`%s` should be syntactically valid BQL", valid[i]);
			db2_free_results(r);
		}

		db2_close(db);
	}

	subtest { /* semantic validity */
		char metric[1024];
		const char *metrics[] = {
			"x", "y", "cpu", "bytes.used",
			NULL
		};
		const char *valid[] = {
			"select bytes.used where id = da7fb between 7d ago and now aggregate 15m",

			/* where clause is optional */
			"select bytes.used between 7d ago and now aggregate 15m",

			/* aggregate clause is optional */
			"select bytes.used where id = da7fb between 7d ago and now",

			/* both when and aggregate clauses are optional */
			"select bytes.used where id = da7fb",
			NULL,
		};
		const char *invalid[] = {
			/* select clause is required */
			"where id = blah",
			"aggregate 1h",
			"between 4h ago and now",
			"where id = blah aggregate 1h between 4h ago and now",

			/* cannot run queries from beginning of time */
			"select x before -4h",
			"select x before 4h ago",

			/* cannot run with timeframes that end before they begin */
			"select x before 5h ago and after 4h ago",
			"select x before 4h ago and after 4h ago",
			"select x between 4h ago and 4h ago",

			/* cannot mix aggregate granularities */
			"select median(x) + y",

			/* cannot nest consolidating functions */
			"select median(max(min(cpu)))",

			NULL,
		};

		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp");
		if (db2_init("t/tmp") != 0)    BAIL("*** unable to db2_init(t/tmp/) ***");
		if (!(db = db2_open("t/tmp"))) BAIL("*** unable to db2_open(t/tmp/) ***");

		for (i = 0; metrics[i]; i++) {
			snprintf(metric, sizeof(metric)/sizeof(char), "%s|tag=value", metrics[i]);
			ok(db2_insert(db, metric, TS, 4.2) == 0, "should be able to seed '%s' metric", metric);
		}

		for (i = 0; valid[i]; i++) {
			r = db2_query(db, valid[i]);
			isnt_null(r, "`%s` should be semantically valid BQL", valid[i]);
			db2_free_results(r);
		}
		for (i = 0; invalid[i]; i++) {
			r = db2_query(db, invalid[i]);
			is_null(r, "`%s` should be semantically invalid BQL", invalid[i]);
			db2_free_results(r);
		}

		db2_close(db);
	}

	subtest { /* db2_insert() and db2_query() */
		char series[BOLO_SERIES_NAME_MAXLEN+1];
		const char *query, *pre;
		int first;

		first = 1;
		SETUP("rm -rf t/tmp/ && mkdir -p t/tmp");
		if (db2_init("t/tmp") != 0)    BAIL("*** unable to db2_init(t/tmp/) ***");
		if (!(db = db2_open("t/tmp"))) BAIL("*** unable to db2_open(t/tmp/) ***");

again:
		db->now = TS;
		ctap_prefix(first ? "[new database]" : "[existing database]");

		ok(1, "starting up a round of tests against %s database...",
				first ? "a new" : "an existing");

		if (first) {
			is_unsigned(_allocated_btree_nodes(db), 0,
				"a new database should have no btree nodes");

			strcpy(series, "x|foo=bar,os=linux");
			ok(db2_insert(db, series, TS - 240 * 1000, 123.456) == 0, "db2_insert(x=123.456 @4m ago) should succeed");
			ok(db2_insert(db, series, TS - 180 * 1000, 123.457) == 0, "db2_insert(x=123.457 @3m ago) should succeed");
			ok(db2_insert(db, series, TS - 120 * 1000, 123.448) == 0, "db2_insert(x=123.448 @2m ago) should succeed");
			ok(db2_insert(db, series, TS -  60 * 1000, 123.431) == 0, "db2_insert(x=123.431 @1m ago) should succeed");
			ok(db2_insert(db, series, TS,              123.402) == 0, "db2_insert(x=123.402 @now) should succeed");

			is_unsigned(_allocated_btree_nodes(db), 1,
				"after inserting multiple measurements for the same series, a database should have one btree node");

		} else {
			is_unsigned(_allocated_btree_nodes(db), 3,
				"existing database should have 3 btree nodes");
		}

		/* single time series retrieval */
		query = "SELECT x WHERE foo = 'bar' AND os = 'linux' BETWEEN 4m AGO AND NOW";
		r = db2_query(db, query);
		isnt_null(r, "`%s` should be a valid query, with results", query);
		is_signed(r->nseries, 1, "the query should return one series");
		is_signed(r->nslots, 4, "the query should return four slots");

		is_unsigned(r->start, TS - 240 * 1000, "the query results should start at (now - 240 seconds), in milliseconds");
		is_unsigned(r->stride, DEFAULT_BUCKET_STRIDE, "the query should use the default bucket stride");

		isnt_null(r->series, "the query should return series names");
		is_string(r->series[0], "x", "the query should return one series named 'x'");
		is_within(db2_result_value(r, 0, 0), 123.456, 0.001, "the query should return tuple (0: x=123.456 @4m ago)");
		is_within(db2_result_value(r, 0, 1), 123.457, 0.001, "the query should return tuple (1: x=123.457 @3m ago)");
		is_within(db2_result_value(r, 0, 2), 123.448, 0.001, "the query should return tuple (2: x=123.448 @2m ago)");
		is_within(db2_result_value(r, 0, 3), 123.431, 0.001, "the query should return tuple (3: x=123.431 @1m ago)");
		db2_free_results(r);

		/* time series math, commutative */
		query = "SELECT x + x - 1 AS double_plus_one WHERE foo = 'bar' AND os = 'linux' BETWEEN 4m AGO AND NOW";
		r = db2_query(db, query);
		isnt_null(r, "`%s` should be a valid query, with results", query);
		is_signed(r->nseries, 1, "the query should return one series");
		is_signed(r->nslots, 4, "the query should return four slots");

		is_unsigned(r->start, TS - 240 * 1000, "the query results should start at (now - 240 seconds), in milliseconds");
		is_unsigned(r->stride, DEFAULT_BUCKET_STRIDE, "the query should use the default bucket stride");
		isnt_null(r->series, "the query should return series names");

		is_string(r->series[0], "double_plus_one", "the query should return one series named 'double_plus_one'");
		is_within(db2_result_value(r, 0, 0), 245.912, 0.001, "the query should return tuple (0: x=245.912 @4m ago)");
		is_within(db2_result_value(r, 0, 1), 245.914, 0.001, "the query should return tuple (1: x=245.914 @3m ago)");
		is_within(db2_result_value(r, 0, 2), 245.896, 0.001, "the query should return tuple (2: x=245.896 @2m ago)");
		is_within(db2_result_value(r, 0, 3), 245.862, 0.001, "the query should return tuple (3: x=245.862 @1m ago)");
		db2_free_results(r);

		/* time series math, non-commutative */
		query = "SELECT x - x / 2 WHERE foo = 'bar' AND os = 'linux' BETWEEN 4m AGO AND NOW";
		r = db2_query(db, query);
		isnt_null(r, "`%s` should be a valid query, with results", query);
		is_signed(r->nseries, 1, "the query should return one series");
		is_signed(r->nslots, 4, "the query should return four slots");

		is_unsigned(r->start, TS - 240 * 1000, "the query results should start at (now - 240 seconds), in milliseconds");
		is_unsigned(r->stride, DEFAULT_BUCKET_STRIDE, "the query should use the default bucket stride");

		is_within(db2_result_value(r, 0, 0), 61.728,  0.001, "the query should return tuple (0: x=61.728  @4m ago)");
		is_within(db2_result_value(r, 0, 1), 61.7285, 0.001, "the query should return tuple (1: x=61.7285 @3m ago)");
		is_within(db2_result_value(r, 0, 2), 61.724,  0.001, "the query should return tuple (2: x=61.724  @2m ago)");
		is_within(db2_result_value(r, 0, 3), 61.7155, 0.001, "the query should return tuple (3: x=61.7155 @1m ago)");
		db2_free_results(r);

		if (first) {
			/* insert some more metrics, to test series aggregation in query results */
			strcpy(series, "x|foo=quux,os=linux");
			ok(db2_insert(db, series, TS - 180 * 1000, 0.887) == 0, "db2_insert(x=0.887 @3m ago) should succeed");
			ok(db2_insert(db, series, TS - 120 * 1000, 0.912) == 0, "db2_insert(x=0.912 @2m ago) should succeed");
			ok(db2_insert(db, series, TS -  60 * 1000, 0.322) == 0, "db2_insert(x=0.322 @1m ago) should succeed");
			ok(db2_insert(db, series, TS,              0.152) == 0, "db2_insert(x=0.152 @now) should succeed");

			is_unsigned(_allocated_btree_nodes(db), 2,
				"after inserting multiple measurements for a second series, a database should have two btree nodes");
		}

		/* aggregate time series retrieval */
		query = "SELECT x WHERE os = 'linux' BETWEEN 4m AGO AND NOW";
		r = db2_query(db, query);
		isnt_null(r, "`%s` should be a valid query, with results", query);
		is_within(db2_result_value(r, 0, 0), 123.456, 0.001, "the query should return tuple (0: x=123.456 @4m ago)");
		is_within(db2_result_value(r, 0, 1), 123.457, 0.001, "the query should return tuple (1: x=123.457 @3m ago)");
		is_within(db2_result_value(r, 0, 2), 123.448, 0.001, "the query should return tuple (2: x=123.448 @2m ago)");
		is_within(db2_result_value(r, 0, 3), 123.431, 0.001, "the query should return tuple (3: x=123.431 @1m ago)");
		db2_free_results(r);

		db2_close(db);

		if (first) {
			first = 0;
			if (!(db = db2_open("t/tmp")))
				BAIL("*** unable to db2_open(t/tmp/) with existing data ***");
			goto again;
		}
	}
}
/* LCOV_EXCL_STOP */
#endif

#ifndef BOLO_H
#define BOLO_H

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE /* for asprintf */

#ifdef TEST
#  define _GNU_SOURCE /* to expose syscall() */
#  include <ctap.h>
#  include "t/memfd.h"
#endif

#include "compiler.h"
#include "errno.h"

#include <assert.h>
#include <errno.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/*************************************************  truly global variables  ***/

/* belongs to db.o */
extern const char *ENC_KEY;
extern size_t      ENC_KEY_LEN;

/*******************************************************  common utilities  ***/

#define streq(a,b) (strcmp((a),(b)) == 0)
void bail(const char *msg);
const char * error(int num) RETURNS;

int mktree(int dirfd, const char *path, mode_t mode) RETURNS;

struct list {
	struct list *next;
	struct list *prev;
};

#define item(l,t,m) ((t*)((uint8_t*)(l) - offsetof(t,m)))
#define for_each(v,l,m) \
	for ( v = item((l)->next, typeof(*v), m); \
	     &v->m != (l); v = item(v->m.next, typeof(*v), m))

#define for_eachx(v,t,l,m) \
	for ( v = item((l)->next, typeof(*v), m), t = item(v->m.next, typeof(*v), m); \
	     &v->m != (l); v = t, t = item(t->m.next, typeof(*t), m))

#define empty(l) ((l)->next = (l)->prev = (l))
#define isempty(l) ((l)->next == (l))

size_t len(const struct list *l) RETURNS;
void push(struct list *list, struct list *add);


/****************************************************************  logging  ***/

#define LOG_ERRORS   0
#define LOG_WARNINGS 1
#define LOG_INFO     2

void startlog(const char *bin, pid_t pid, int level);
void logto(int fd);
void errorf(const char *fmt, ...);
void warningf(const char *fmt, ...);
void infof(const char *fmt, ...);


/**************************************************************  debugging  ***/

int  debugto(int fd);
void debugf(const char *fmt, ...);


/*****************************************************************  config  ***/

struct config {
	int log_level;
	char *secret_key;
	unsigned int block_span;
};

int configure(struct config *, int fd) RETURNS;


/****************************************************************  SHA-512  ***/

#define SHA512_DIGEST   64
#define SHA512_BLOCK   128

struct sha512 {
	uint64_t state[8];
	uint64_t bytes[2];
	uint64_t block[16];
};

void sha512_init(struct sha512 *c);
void sha512_feed(struct sha512 *c, const void *buf, size_t len);
void sha512_done(struct sha512 *c);

int sha512_raw(struct sha512 *c, void *digest, size_t len) RETURNS;
int sha512_hex(struct sha512 *c, void *digest, size_t len) RETURNS;

/***********************************************************  HMAC-SHA-512  ***/

struct hmac_sha512 {
	struct sha512 sha;
	char key[128];
};

void hmac_sha512_init(struct hmac_sha512 *c, const char *key, size_t len);
void hmac_sha512_feed(struct hmac_sha512 *c, const void *buf, size_t len);
void hmac_sha512_done(struct hmac_sha512 *c);

int hmac_sha512_raw(struct hmac_sha512 *c, void *hmac, size_t len) RETURNS;
int hmac_sha512_hex(struct hmac_sha512 *c, void *hmac, size_t len) RETURNS;

void hmac_sha512_seal (const char *key, size_t klen, const void *buf, size_t len);
int  hmac_sha512_check(const char *key, size_t klen, const void *buf, size_t len) RETURNS;
#define hmac_seal  hmac_sha512_seal
#define hmac_check hmac_sha512_check


/****************************************************************  hashing  ***/

#define HASH_MANAGED 0x01

struct hash;

struct hash * hash_new(int flags);
void hash_free(struct hash *h);

struct hash * hash_read(int fd, int flags);
int hash_write(struct hash *h, int fd);

int hash_setp(struct hash *h, const char *key, void *value);
int hash_getp(struct hash *h, void *dst, const char *key);

int hash_setv(struct hash *h, const char *key, uint64_t v);
int hash_getv(struct hash *h, uint64_t *dst, const char *key);

/********************************************************************  time ***/

typedef uint64_t bolo_msec_t;
typedef uint32_t bolo_sec_t;

#define INVALID_MS (bolo_msec_t)(-1)
#define INVALID_S  (bolo_sec_t)(-1)

typedef double   bolo_value_t;

bolo_msec_t
bolo_ms(const struct timeval *tv)
RETURNS;

bolo_msec_t
bolo_s(const struct timeval *tv)
RETURNS;

/***********************************************************  bit twiddling ***/

#define MAX_U8  0xff
#define MAX_U16 0xffff
#define MAX_U32 0xffffffff
#define MAX_U64 0xffffffffffffffff

#define read8(b,o)   (*(uint8_t  *)((const uint8_t*)(b)+(o)))
#define read16(b,o)  (*(uint16_t *)((const uint8_t*)(b)+(o)))
#define read32(b,o)  (*(uint32_t *)((const uint8_t*)(b)+(o)))
#define read64(b,o)  (*(uint64_t *)((const uint8_t*)(b)+(o)))
#define read64f(b,o) (*(double   *)((const uint8_t*)(b)+(o)))

static inline void  write8 (void *b, size_t o, uint8_t  v) { memmove((uint8_t *)b+o, &v, 1); }
static inline void write16 (void *b, size_t o, uint16_t v) { memmove((uint8_t *)b+o, &v, 2); }
static inline void write32 (void *b, size_t o, uint32_t v) { memmove((uint8_t *)b+o, &v, 4); }
static inline void write64 (void *b, size_t o, uint64_t v) { memmove((uint8_t *)b+o, &v, 8); }
static inline void write64f(void *b, size_t o, double   v) { memmove((uint8_t *)b+o, &v, 8); }

static inline void writen(void *b, size_t o, const void *x, size_t l)
{ memmove((uint8_t *)b+o, x, l); }

/***********************************************************  mmap'd paging ***/

struct page {
	int     fd;
	void   *data;
	size_t  len;
};


int page_map  (struct page *p, int fd, off_t start, size_t len) RETURNS;
int page_unmap(struct page *p) RETURNS;
int page_sync (struct page *p) RETURNS;

uint8_t  page_read8  (struct page *p, size_t o);
uint16_t page_read16 (struct page *p, size_t o);
uint32_t page_read32 (struct page *p, size_t o);
uint64_t page_read64 (struct page *p, size_t o);
double   page_read64f(struct page *p, size_t o);

void page_write8  (struct page *p, size_t o, uint8_t  v);
void page_write16 (struct page *p, size_t o, uint16_t v);
void page_write32 (struct page *p, size_t o, uint32_t v);
void page_write64 (struct page *p, size_t o, uint64_t v);
void page_write64f(struct page *p, size_t o, double   v);

void page_writen(struct page *p, size_t o, const void *buf, size_t len);
ssize_t page_readn(struct page *p, size_t o, void *buf, size_t len);

/******************************************************************  btree  ***/

/* Almost all systems have 4k or 8k memory pages, a fact
   which can be verified with sysconfig(_SC_PAGESIZE), so
   we will make our btree pages 8k, a multiple of each.
 */
#define BTREE_PAGE_SIZE 8192

/* The degree of a btree governs how many keys each page
   can store.  Since the nodes flank the keys, a btree
   of degree K has K+1 nodes.

   We reserve 1b for flagging this page as a leaf node,
   and 2b (a 16-bit value) to track how many keys are
   actually in use.

   Specifically, this btree implementation stores 64-bit
   keys and 64-bit values, so the degree can be calculated
   as the page size, less 3 octets for header data, less
   another 8 octets (64 bits for the +1 node), divided by the
   composite key+value size (16 octets, or 2x 64-bit values).
 */
#define BTREE_DEGREE ((BTREE_PAGE_SIZE - 1 - 2 - 8) / 16)

/* The btree split factor governs how a btree node is split
   into two pieces to ensure balance.  It ranges (0,1) and
   acts as a percentage.  I.e. a value of 0.5 (50%) nets a
   "classical" btree tuned for random-order insertion.

   Since our btree uses timestamps as its keys, and most
   values will be inserted in-order, we choose a split factor
   higher than 0.5 to bias the balance towards inserting
   "newer" keys.
 */
#define BTREE_SPLIT_FACTOR 0.9

struct btree {
	uint16_t used;  /* how many keys are populated?
	                  (must be strictly <= BTREE_DEGREE */

	int leaf;      /* is this node a leaf node?
	                  (leaf nodes contain immediate data,
	                   non-leaf nodes point to other nodes) */

	uint64_t id;   /* identity of this block, on-disk */

	struct btree *kids[BTREE_DEGREE+1];

	struct page page;
};

struct btree * btree_create(int fd);
struct btree * btree_read(int fd);
int btree_write(struct btree *t);
int btree_close(struct btree *t);

#if 0
void btree_print(struct btree *t);
#endif

int btree_insert(struct btree *t, bolo_msec_t key, uint64_t block_number);

int btree_find(struct btree *t, uint64_t *dst, bolo_msec_t key);

/***************************************************************  database  ***/

/* a SLAB file can be up to 8g in size
   (plus a single page for the header) */
#define TSLAB_MAX_SIZE    (1 << 30)
#define TSLAB_HEADER_SIZE 88

/* a SLAB endian-check magic number, to be
   read/written as a 32-bit unsigned integer.

   translates into BE as [hex 7e d1 32 4c]
               and LE as [hex 4c 32 d1 7e] */
#define TSLAB_ENDIAN_MAGIC 2127639116U

/* a BLOCK in a SLAB is exactly 512k
   with a 24b header and an HMAC-SHA512
   footer, leaving 524,176b for data */
#define TBLOCK_SIZE         (1 << 19)
#define TBLOCK_HEADER_SIZE  24
#define TBLOCK_DATA_SIZE    (TBLOCK_SIZE - TBLOCK_HEADER_SIZE - SHA512_DIGEST)

/* each CELL has a 4b relative timestamp
   (ms), and an 8b IEEE-754 float64 value */
#define TCELL_SIZE       12

#define TBLOCKS_PER_TSLAB (TSLAB_MAX_SIZE  / TBLOCK_SIZE)
#define TCELLS_PER_TBLOCK (TBLOCK_DATA_SIZE / TCELL_SIZE)


/********************************************************  database blocks  ***/

struct tblock {
	int valid;         /* is this block real? */
	int cells;         /* how many cells are in use? */
	bolo_msec_t base;  /* base timestamp (ms) for this block */

	uint64_t number;   /* composite slab / block number,
	                      where bits 0-53 are the slab number
	                      and bits 54-63 are the block number */

	struct page page;  /* backing data page */
};

#define tblock_read8(  b,o) page_read8  (&(b)->page, (o))
#define tblock_read16( b,o) page_read16 (&(b)->page, (o))
#define tblock_read32( b,o) page_read32 (&(b)->page, (o))
#define tblock_read64( b,o) page_read64 (&(b)->page, (o))
#define tblock_read64f(b,o) page_read64f(&(b)->page, (o))

#define tblock_write8(  b,o,v) page_write8  (&(b)->page, (o), (v))
#define tblock_write16( b,o,v) page_write16 (&(b)->page, (o), (v))
#define tblock_write32( b,o,v) page_write32 (&(b)->page, (o), (v))
#define tblock_write64( b,o,v) page_write64 (&(b)->page, (o), (v))
#define tblock_write64f(b,o,v) page_write64f(&(b)->page, (o), (v))

int tblock_map(struct tblock *b, int fd, off_t offset, size_t len) RETURNS;
void tblock_init(struct tblock *b, uint64_t number, bolo_msec_t base);
int tblock_isfull(struct tblock *b) RETURNS;
int tblock_canhold(struct tblock *b, bolo_msec_t when) RETURNS;
int tblock_log(struct tblock *b, bolo_msec_t when, bolo_value_t what) RETURNS;
#define tblock_unmap(b) page_unmap(&(b)->page)
#define tblock_sync(b)  page_sync(&(b)->page)

#define tblock_seal( k,l,b) hmac_seal ((k),(l),(b)->page.data,(b)->page.len)
#define tblock_check(k,l,b) hmac_check((k),(l),(b)->page.data,(b)->page.len)

/*********************************************************  database slabs  ***/

struct tslab {
	struct list l;           /* list hook for database slab refs */

	int fd;                  /* file descriptor of the slab file */
	uint32_t block_size;     /* how big is each data block page? */
	uint64_t number;         /* slab number, with the least significant
	                            11-bits cleared. */

	struct tblock                 /* list of all blocks in this slab.    */
	  blocks[TBLOCKS_PER_TSLAB];  /* present blocks will have .valid = 1 */
};

int tslab_map(struct tslab *s, int fd) RETURNS;
int tslab_unmap(struct tslab *s) RETURNS;
int tslab_sync(struct tslab *s) RETURNS;
int tslab_init(struct tslab *s, int fd, uint64_t number, uint32_t block_size) RETURNS;
int tslab_isfull(struct tslab *s) RETURNS;
int tslab_extend(struct tslab *s, bolo_msec_t base);

int tslab_insert(struct tslab *s, bolo_msec_t when, bolo_value_t what) RETURNS;

/***************************************************************  database  ***/

struct db;

void db_encrypt(const char *key, size_t len);
struct db * db_mount(const char *path) RETURNS;
struct db * db_init(const char *path) RETURNS;
int db_sync(struct db *db) RETURNS;
int db_unmount(struct db *db) RETURNS;
int db_insert(struct db *, const char *name, bolo_msec_t when, bolo_value_t what) RETURNS;

#endif

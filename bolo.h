#ifndef BOLO_H
#define BOLO_H

#include "compiler.h"
#include <sys/types.h>
#include <stdint.h>

/**************************************************************  debugging  ***/

int  debugto(int fd);
void debugf(const char *fmt, ...);

/****************************************************************  SHA-512  ***/

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
int  hmac_sha512_check(const char *key, size_t klen, const void *buf, size_t len)
RETURNS;

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

/********************************************************************  TSDB ***/

struct bolo_tsdb;

struct bolo_tsdb *
bolo_tsdb_create(const char *path)
RETURNS;

int
bolo_tsdb_close(struct bolo_tsdb *db)
RETURNS;

int
bolo_tsdb_log(struct bolo_tsdb *db,
              bolo_msec_t ts,
              bolo_value_t value)
RETURNS;

int
bolo_tsdb_commit(struct bolo_tsdb *db)
RETURNS;

#endif

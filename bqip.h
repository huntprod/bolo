#ifndef BQIP_H
#define BQIP_H

#include "bolo.h"

#define BQIP_BUFSIZ 8192

struct bqip_buf {
	char   data[BQIP_BUFSIZ];
	size_t len;
};

int bqip_buf_read(struct bqip_buf *b, int fd);
size_t bqip_buf_copy(struct bqip_buf *b, const char *s, size_t len);
int bqip_buf_write(struct bqip_buf *b, int fd);
int bqip_buf_skip(struct bqip_buf *b, size_t len);

int bqip_buf_streamout(struct bqip_buf *b, int fd, const char *s, size_t len);

struct bqip {
	int    fd;

	struct bqip_buf rcvbuf;
	struct bqip_buf sndbuf;

	struct {
		size_t  len;
		size_t  dot;
		char   *payload;
	} request;
};

void bqip_init(struct bqip *c, int fd);
void bqip_deinit(struct bqip *c);

int bqip_read(struct bqip *c);

int bqip_send_error(struct bqip *c, const char *e);
int bqip_send_result(struct bqip *c, int nsets);
int bqip_send_set(struct bqip *c, int ntuples, const char *key);
int bqip_send_tuple(struct bqip *c, struct result *r, int first);
int bqip_flush(struct bqip *c);

#endif

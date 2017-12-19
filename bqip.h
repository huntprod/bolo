#ifndef BQIP_H
#define BQIP_H

#include "bolo.h"

#define BQIP_BUFSIZ 8192

/* BQIP packets:

                               Run <query>, which is <n> octets long
   C> Q|<n>|<query>\n
   S> R|field1=t:v,t:v,t:v|field2=t:v,t:v,t:v|...<EOF>

                               Plan <query>, which is <n> octets long
   C> P|<n>|<plan>\n
   S> R|field1|field2|...<EOF>

                               List metrics matching the given filter
   C> M|<n>|<filter>\n
   S> R|cpu:tag=value|mem:tag=value|...<EOF>
 */

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
		char    type;
		size_t  len;
		size_t  dot;
		char   *payload;
	} request;
};

void bqip_init(struct bqip *c, int fd);
void bqip_deinit(struct bqip *c);

int bqip_read(struct bqip *c);

int bqip_sendn(struct bqip *c, const void *buf, size_t len);
int bqip_send0(struct bqip *c, const char *s);
int bqip_send_error(struct bqip *c, const char *e);
int bqip_send_tuple(struct bqip *c, struct result *r);

#endif

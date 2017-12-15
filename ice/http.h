
/*******************************************************************  http  ***/


#define HTTP_GET     1
#define HTTP_POST    2
#define HTTP_PUT     3
#define HTTP_PATCH   4
#define HTTP_DELETE  5
#define HTTP_HEAD    6
#define HTTP_OPTIONS 7

#define HTTP_BODY_SIZE_FD_MIN 8192
struct http_request {
	char  *method;
	char  *uri;
	int    protocol;

	int known_method;
	size_t content_length;

	struct hash *headers;
	struct io   *body;

	int state; /* parser DFA state */

	struct {
		size_t  dot;  /* offset of last successful
		                 iterative parse, (data) */
		char   *data; /* dynamic request buffer  */
		size_t  cap;  /* capacity of raw.data    */
		size_t  len;  /* bytes used in raw.data  */
	} raw;
};

struct http_reply {
	int protocol;
	struct hash *headers;
};

struct http_conn {
	int  fd;
	char buf[8192];
	int  pos;

	struct http_request req;
	struct http_reply   rep;
};

typedef int(*http_handler)(struct http_conn *);
struct http_route {
	char *prefix;
	int   open;

	http_handler handler;
};

struct http_mux {
	int nroutes;
	struct http_route *routes;
};

/* (re-)initialize an HTTP connection handler. */
void http_conn_init(struct http_conn *c, int fd);

int http_conn_read(struct http_conn *c);
int http_conn_bad(struct http_conn *c);
int http_conn_ready(struct http_conn *c);

void http_conn_write(struct http_conn *c, const void *buf, int len);
void http_conn_write0(struct http_conn *c, const char *s);
void http_conn_printf(struct http_conn *c, const char *fmt, ...);
void http_conn_flush(struct http_conn *c);

void http_conn_set_header(struct http_conn *c, const char *header, const char *value);
const char *http_conn_get_header(struct http_conn *c, const char *header);
int http_conn_reply(struct http_conn *c, int status);
int http_conn_replyio(struct http_conn *c, int status, struct io *io);

void http_mux_route(struct http_mux *mux, const char *prefix, http_handler handler);
int http_dispatch(struct http_mux *mux, struct http_conn *c);

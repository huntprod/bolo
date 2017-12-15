
/*****************************************************  data serialization  ***/

#define JSON_NONE          0
#define JSON_OBJECT_START  1
#define JSON_OBJECT_FINISH 2
#define JSON_LIST_START    3
#define JSON_LIST_FINISH   4
#define JSON_KEY           5
#define JSON_STRING        6
#define JSON_INTEGER       7
#define JSON_DECIMAL       8
#define JSON_FLOATP        9
#define JSON_TRUE         10
#define JSON_FALSE        11
#define JSON_NULL         12
#define JSON_ERROR      0xfe
#define JSON_EOF        0xff

#define JSON_STACK_DEPTH 128
struct json {
	struct io *io;

	int    fd;
	char   buf[8192];
	size_t len;
	size_t off;

	char  *strbuf;
	int    strlen;

	int    state[JSON_STACK_DEPTH];
	int    top;
	int    error;
};

struct json_value {
	int type;
	union {
		char  *string;
		long   integer;
		double decimal;
	} data;
};

void json_io(struct json *b, struct io *io);
/* FIXME: I think we can remove the fd / buf readers */
void json_init_fd(struct json *b, int fd);
void json_init_buf(struct json *b, char *buf, size_t len);
int json_read(struct json *b, struct json_value *v);
int json_write(struct json *b, int event, void *data, size_t len);


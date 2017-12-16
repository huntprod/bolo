#include "bolo.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>

int
net_bind(const char *_addr, int backlog)
{
	struct addrinfo hints, *results, *res;
	char *addr, *node, *port, *p;
	int v, fd;

	/* format: [<ipv6>]:<port>   - ipv6
	           <ipv4>:<port>     - ipv4
	           *:<port>          - ALL
	 */

	if (!*_addr)
		goto invalid;

	node = p = addr = strdup(_addr);
	if (!addr)
		return -1;
	if (*p == '*') {
		p++; if (*p != ':') goto invalid;
		node = NULL;
		p++; goto port;
	}
	if (*p == '[') {
		node = ++p;
		while (*p && *p != ']') p++;
		if (!*p) goto invalid;
		*p++ = '\0'; /* NULL-terminate `node` */

		if (*p != ':') goto invalid;
		p++;

	} else {
		while (*p && *p != ':') p++;
		if (!*p) goto invalid;
		*p++ = '\0'; /* NULL-terminate `node` */
	}

port:
	port = p;
	while (isdigit(*p))
		p++;
	if (*p) /* extra junk after port */
		goto invalid;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags    = AI_PASSIVE | AI_NUMERICSERV;
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM; /* only support tcp */
	v = getaddrinfo(node, port, &hints, &results);
	if (v != 0) {
		errno = EINVAL;
		fprintf(stderr, "gai: %s\n", gai_strerror(v));
		goto failed;
	}

	for (res = results; res; res = res->ai_next) {
		fprintf(stderr, "AF %d, SOCK %d, PROTO %d\n", res->ai_family, res->ai_socktype, res->ai_protocol);
		fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd < 0) continue;

		v = 1;
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)) != 0
		 || bind(fd, res->ai_addr, res->ai_addrlen) != 0
		 || listen(fd, backlog) != 0) {
			close(fd);
			continue;
		}

		/* success! */
		freeaddrinfo(results);
		return fd;
	}

	freeaddrinfo(results);
	errno = ENODEV;
	goto failed;

invalid:
	errno = EINVAL;
failed:
	free(addr);
	return -1;
}

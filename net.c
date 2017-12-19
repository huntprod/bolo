#include "bolo.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <sys/epoll.h>

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

	debugf("net: parsing %s", _addr);
	node = p = addr = strdup(_addr);
	if (!addr)
		return -1;
	if (*p == '*') {
		debugf("net: wildcard address detected.");
		p++; if (*p != ':') goto invalid;
		node = NULL;
		p++; goto port;
	}
	if (*p == '[') {
		debugf("net: IPv6 bracketed address detected.");
		node = ++p;
		while (*p && *p != ']') p++;
		if (!*p) goto invalid;
		*p++ = '\0'; /* NULL-terminate `node` */

		if (*p != ':') goto invalid;
		p++;

	} else {
		debugf("net: IPv4 or FQDN address detected.");
		while (*p && *p != ':') p++;
		if (!*p) goto invalid;
		*p++ = '\0'; /* NULL-terminate `node` */
	}

port:
	port = p;
	debugf("net: node is (%s), port is (%s); validating...", node, port);
	while (isdigit(*p))
		p++;
	if (*p) /* extra junk after port */
		goto invalid;

	debugf("net: running getaddrinfo to enumerate bindable interfaces.");
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags    = AI_PASSIVE | AI_NUMERICSERV;
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM; /* only support tcp */
	v = getaddrinfo(node, port, &hints, &results);
	if (v != 0) {
		errno = EINVAL;
		errorf("getaddrinfo errored: %s (error %d)\n", gai_strerror(v), v);
		goto failed;
	}

	errno = ENODEV; /* best guess. */
	for (res = results; res; res = res->ai_next) {
		debugf("net: checking interface {AF %d SOCKET %d PROTO %d}",
				res->ai_family, res->ai_socktype, res->ai_protocol);
		fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd < 0) continue;

		v = 1;
		debugf("net: socket created; setting SO_REUSEADDR, binding and listening.");
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)) != 0
		 || bind(fd, res->ai_addr, res->ai_addrlen) != 0
		 || listen(fd, backlog) != 0) {

			debugf("net: failed to bind and listen: %s (error %d) -- skipping...",
					strerror(errno), errno);
			close(fd);
			continue;
		}

		/* success! */
		debugf("net: fd %d bound and listening on %s", fd, _addr);
		freeaddrinfo(results);
		return fd;
	}

	freeaddrinfo(results);
	goto failed;

invalid:
	errno = EINVAL;
failed:
	free(addr);
	return -1;
}

struct npoll {
	int             fd;
	net_handler_fn  handler;
	void           *udata;
};

struct net_poller {
	int           epfd;
	struct npoll *conns;
	int           maxconns;
};

struct net_poller *
net_poller(int max)
{
	struct net_poller *np;

	np = calloc(1, sizeof(struct net_poller));
	if (!np)
		goto fail;

	np->maxconns = max;
	np->conns = calloc(max, sizeof(struct npoll));
	if (!np->conns)
		goto fail;

	np->epfd = epoll_create1(0);
	if (np->epfd < 0)
		goto fail;

	return np;

fail:
	free(np);
	return NULL;
}

int
net_poll_fd(struct net_poller *np, int fd, net_handler_fn fn, void *udata)
{
	int i;
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));

	for (i = 0; i < np->maxconns; i++) {
		if (np->conns[i].handler) continue;

		ev.events = EPOLLIN;
		ev.data.fd = fd;
		if (epoll_ctl(np->epfd, EPOLL_CTL_ADD, fd, &ev) == -1)
			return -1;

		/* set non blocking */
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

		np->conns[i].fd      = fd;
		np->conns[i].handler = fn;
		np->conns[i].udata   = udata;

		return 0;
	}
	return -1;
}

int
net_poll(struct net_poller *np)
{
	int i, n, rc, nfds, fd;
	struct epoll_event events[NET_POLL_MAX_EVENTS];

	for (;;) {
		nfds = epoll_wait(np->epfd, events, NET_POLL_MAX_EVENTS, -1);
		if (nfds == -1)
			return -1;

		for (n = 0; n < nfds; n++) {
			fd = events[n].data.fd;
			debugf("net: activity on file descriptor %d", fd);

			for (i = 0; i < np->maxconns; i++) {
				if (!np->conns[i].handler) continue;
				if (np->conns[i].fd != fd) continue;

				rc = np->conns[i].handler(fd, np->conns[i].udata);
				if (rc != 0) {
					debugf("net: removing fd %d from epoll watch list", fd);
					if (epoll_ctl(np->epfd, EPOLL_CTL_DEL, fd, &events[n]) != 0)
						errnof("net: unable to remove fd %d from epoll", fd);

					close(fd);
					memset(&np->conns[i], 0, sizeof(np->conns[i]));
				}
				break;
			}

			if (i == np->maxconns) {
				errorf("bogus activity detected on file descriptor %d", fd);
			}
		}
	}
}

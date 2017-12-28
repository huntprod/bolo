#include "bolo.h"
#include <sys/epoll.h>

struct fdpoll_fd {
	int        fd;
	fdpoll_fn  fn;
	void      *udata;
};

struct fdpoll {
	int               epfd;
	struct fdpoll_fd *fds;
	int               maxfds;

	int               timeout;
	fdpoll_fn         on_timeout;
	void             *on_timeout_data;

	fdpoll_fn         on_every;
	void             *on_every_data;
};

struct fdpoll *
fdpoller(int max)
{
	struct fdpoll *fdp;

	fdp = xalloc(1, sizeof(struct fdpoll));
	fdp->timeout = -1; /* no timeout */
	fdp->maxfds = max;
	fdp->fds = xalloc(max, sizeof(struct fdpoll_fd));

	fdp->epfd = epoll_create1(0);
	if (fdp->epfd < 0)
		goto fail;

	return fdp;

fail:
	free(fdp);
	return NULL;
}

int
fdpoll_watch(struct fdpoll *fdp, int fd, int flags, fdpoll_fn fn, void *udata)
{
	int i;
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));

	ev.data.fd = fd;
	ev.events = 0;
	if (flags & FDPOLL_READ)  ev.events |= EPOLLIN;
	if (flags & FDPOLL_WRITE) ev.events |= EPOLLOUT;

	for (i = 0; i < fdp->maxfds; i++) {
		if (fdp->fds[i].fn) continue;
		if (epoll_ctl(fdp->epfd, EPOLL_CTL_ADD, fd, &ev) == -1)
			return -1;

		/* set non blocking */
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

		fdp->fds[i].fd    = fd;
		fdp->fds[i].fn    = fn;
		fdp->fds[i].udata = udata;

		return 0;
	}
	return -1;
}

int
fdpoll_unwatch(struct fdpoll *fdp, int fd)
{
	int i;
	struct epoll_event ev; /* ignored, see epoll_ctl(2) BUGS section */

	for (i = 0; i < fdp->maxfds; i++) {
		if (fdp->fds[i].fd != fd) continue;

		debugf("fdpoll: removing fd %d from epoll watch list", fd);
		if (epoll_ctl(fdp->epfd, EPOLL_CTL_DEL, fd, &ev) != 0)
			errnof("fdpoll: unable to remove fd %d from epoll", fd);
		memset(&fdp->fds[i], 0, sizeof(fdp->fds[i]));
		return 0;
	}

	errno = ENOENT;
	return -1;
}

void
fdpoll_timeout(struct fdpoll *fdp, int timeout_ms, fdpoll_fn fn, void *udata)
{
	fdp->timeout = timeout_ms;
	fdp->on_timeout      = fn;
	fdp->on_timeout_data = udata;
}

void
fdpoll_every(struct fdpoll *fdp, fdpoll_fn fn, void *udata)
{
	fdp->on_every      = fn;
	fdp->on_every_data = udata;
}

int
fdpoll(struct fdpoll *fdp)
{
	int i, n, rc, nfds, fd;
	struct epoll_event events[FDPOLL_MAX_EVENTS];

	for (;;) {
		nfds = epoll_wait(fdp->epfd, events, FDPOLL_MAX_EVENTS, fdp->timeout);
		if (nfds == -1)
			return -1;

		if (nfds == 0 && fdp->on_timeout)
			fdp->on_timeout(-1, fdp->on_timeout_data);

		for (n = 0; n < nfds; n++) {
			fd = events[n].data.fd;
			debugf("fdpoll: activity on file descriptor %d", fd);

			for (i = 0; i < fdp->maxfds; i++) {
				if (!fdp->fds[i].fn)       continue;
				if ( fdp->fds[i].fd != fd) continue;

				rc = fdp->fds[i].fn(fd, fdp->fds[i].udata);
				if (rc != 0) {
					debugf("fdpoll: removing fd %d from epoll watch list", fd);
					if (epoll_ctl(fdp->epfd, EPOLL_CTL_DEL, fd, &events[n]) != 0)
						errnof("fdpoll: unable to remove fd %d from epoll", fd);

					close(fd);
					memset(&fdp->fds[i], 0, sizeof(fdp->fds[i]));
				}
				break;
			}

			if (i == fdp->maxfds) {
				errorf("bogus activity detected on file descriptor %d", fd);
			}
		}

		if (nfds > 0 && fdp->on_every)
			fdp->on_every(-1, fdp->on_every_data);
	}
}

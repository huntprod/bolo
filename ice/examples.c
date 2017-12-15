
#define MAX_EVENTS 100
#define MAX_CONNECTIONS 1024
static int
handle_v1_auth(struct http_conn *c)
{
	struct json b;

	if (c->req.known_method == HTTP_POST) {
		json_io(&b, c->req.body);

		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else if (c->req.known_method == HTTP_DELETE) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else {
		http_conn_set_header(c, "Content-Length", "20");
		http_conn_reply(c, 405);
		http_conn_write0(c, "method not allowed\r\n");
	}
	return 0;
}
static int
handle_v1_nav(struct http_conn *c)
{
	if (c->req.known_method == HTTP_GET) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else if (c->req.known_method == HTTP_PUT) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else {
		http_conn_set_header(c, "Content-Length", "20");
		http_conn_reply(c, 405);
		http_conn_write0(c, "method not allowed\r\n");
	}
	return 0;
}
static int
handle_v1_boards(struct http_conn *c)
{
	if (c->req.known_method == HTTP_GET) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else if (c->req.known_method == HTTP_POST) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else {
		http_conn_set_header(c, "Content-Length", "20");
		http_conn_reply(c, 405);
		http_conn_write0(c, "method not allowed\r\n");
	}
	return 0;
}
static int
handle_v1_board(struct http_conn *c)
{
	if (c->req.known_method == HTTP_GET) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "NOT implemented\r\n");

	} else if (c->req.known_method == HTTP_PUT) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else if (c->req.known_method == HTTP_PATCH) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else if (c->req.known_method == HTTP_DELETE) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else {
		http_conn_set_header(c, "Content-Length", "20");
		http_conn_reply(c, 405);
		http_conn_write0(c, "method not allowed\r\n");
	}
	return 0;
}
static int
handle_v1_query(struct http_conn *c)
{
	if (c->req.known_method == HTTP_GET) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else if (c->req.known_method == HTTP_POST) {
		http_conn_set_header(c, "Content-Length", "17");
		http_conn_reply(c, 501);
		http_conn_write0(c, "not implemented\r\n");

	} else {
		http_conn_set_header(c, "Content-Length", "20");
		http_conn_reply(c, 405);
		http_conn_write0(c, "method not allowed\r\n");
	}
	return 0;
}
static int
do_api(int argc, char **argv)
{
	int i, v, rc, epfd, n, nfds, listenfd, sockfd, flags;
	struct sockaddr_in ipv4;
	struct epoll_event ev, events[MAX_EVENTS];
	struct http_conn *conns;
	int nconns = 0;
	struct http_mux mux;

	memset(&ev, 0, sizeof(ev));
	memset(&mux, 0, sizeof(mux));

	http_mux_route(&mux, "/v1/auth",     handle_v1_auth);
	http_mux_route(&mux, "/v1/nav",      handle_v1_nav);
	http_mux_route(&mux, "/v1/boards",   handle_v1_boards);
	http_mux_route(&mux, "/v1/boards/*", handle_v1_board);
	http_mux_route(&mux, "/v1/query",    handle_v1_query);
	//http_mux_route(&mux, "/v1/query/*", handle_v1_board);

	conns = calloc(MAX_CONNECTIONS, sizeof(*conns));
	if (!conns)
		bail("calloc(conns) failed");
	for (i = 0; i < MAX_CONNECTIONS; i++)
		conns[i].fd = -1;

	epfd = epoll_create1(0);
	if (epfd < 0)
		bail("epoll_create failed");

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0)
		bail("socket failed");

	printf("S: setting SO_REUSEADDR socket option.\n");
	v = 1;
	rc = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
	if (rc < 0)
		bail("so_reusaddr failed");

	printf("S: binding to *:8082 (IPv4)\n");
	ipv4.sin_family = AF_INET;
	ipv4.sin_addr.s_addr = INADDR_ANY;
	ipv4.sin_port = htons(8082);
	rc = bind(listenfd, (struct sockaddr *)(&ipv4), sizeof(ipv4));
	if (rc < 0)
		bail("bind failed");

	printf("S: listening for inbound connections, backlog 64.\n");
	rc = listen(listenfd, 64);
	if (rc < 0)
		bail("listen failed");

	ev.events = EPOLLIN;
	ev.data.fd = listenfd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) == -1)
		bail("epoll_ctl failed");

	printf("S: entering main loop.\n");
	for (;;) {
		nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
		if (nfds == -1)
			bail("epoll_wait failed");

		for (n = 0; n < nfds; ++n) {
			if (events[n].data.fd == listenfd) {
				sockfd = accept(listenfd, NULL, NULL);
				printf("S: accepted new inbound connection.\n");

				if (nconns > MAX_CONNECTIONS) {
					close(sockfd);
					continue;
				}

				nconns++;
				for (i = 0; i < MAX_CONNECTIONS; i++) {
					if (conns[i].fd < 0) {
						http_conn_init(&conns[i], sockfd);
						break;
					}
				}

				flags = fcntl(sockfd, F_GETFL, 0);
				flags |= O_NONBLOCK;
				if (fcntl(sockfd, F_SETFL, flags) != 0)
					bail("F_SETFL failed");

				ev.events = EPOLLIN;
				ev.data.fd = sockfd;
				if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
					fprintf(stderr, "failed to add socket %d to epoll: %s (error %d)\n",
							sockfd, strerror(errno), errno);
					close(sockfd);
				}
				printf("S: epolling new inbound connection.\n");

			} else {
				printf("S: activity detected on file descriptor %d\n", events[n].data.fd);
				for (i = 0; i < MAX_CONNECTIONS; i++) {
					if (events[n].data.fd == conns[i].fd) {
						if (http_conn_read(&conns[i]) != 0) {
							fprintf(stderr, "fatal error reading HTTP data from socket %d; aborting connection.\n",
									conns[i].fd);
							close(conns[i].fd);
							conns[i].fd = -1;
							break;
						}
						if (http_conn_ready(&conns[i])) {
							if (http_dispatch(&mux, &conns[i]) != 0)
								printf("S: ERROR\n");
							printf("S: closing connection.\n");
						}
						break;
					}
				}
				if (i == MAX_CONNECTIONS) {
					fprintf(stderr, "activity on unrecognized file descriptor %d; ignoring.\n",
							events[n].data.fd);
				}
			}
		}
	}

	close(listenfd);
	return 0;
}


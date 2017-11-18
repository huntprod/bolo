#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(int argc, char **argv)
{
	int n = 1, delay = 100, done;
	char *p, buf[8192];
	ssize_t nread, len;

	if (argc > 3) {
		fprintf(stderr, "usage: input | slow [n [delay]] | output\n");
		return 1;
	}

	if (argc >= 2) {
		for (n = 0, p = argv[1]; *p; p++) {
			if (*p >= '0' && *p <= '9') {
				n = n * 10 + (*p - '0');
			} else {
				fprintf(stderr, "invalid number '%s'\n", argv[1]);
				return 1;
			}
		}
	}
	if (argc >= 3) {
		for (delay = 0, p = argv[2]; *p; p++) {
			if (*p >= '0' && *p <= '9') {
				delay = delay * 10 + (*p - '0');
			} else {
				fprintf(stderr, "invalid number '%s'\n", argv[2]);
				return 1;
			}
		}
	}

	len = nread = 0;
	done = 0;
	while (!done) {
		while (len < n) {
			nread = read(0, buf + len, 8192 - len);
			if (nread < 0) {
				fprintf(stderr, "failed to read: %s (errno %d)\n", strerror(errno), errno);
				return 1;
			}
			len += nread;
			if (nread == 0) {
				done = 1;
				break;
			}
		}
		while (len >= n) {
			write(1, buf, n);
			memmove(buf, buf + n, len - n);
			len -= n;
			usleep(delay * 1000);
		}
	}
	if (len > 0)
		write(1, buf, len);
	close(0);
	close(1);
	return 0;
}

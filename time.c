#include "bolo.h"
#include <stdlib.h>
#include <sys/time.h>

bolo_msec_t
bolo_ms(const struct timeval *tv)
{
	struct timeval now;

	if (!tv) {
		if (gettimeofday(&now, NULL) != 0)
			return INVALID_MS;
		tv = &now;
	}

	return tv->tv_sec  * 1000
	     + tv->tv_usec / 1000;
}

bolo_msec_t
bolo_s(const struct timeval *tv)
{
	struct timeval now;

	if (!tv) {
		if (gettimeofday(&now, NULL) != 0)
			return INVALID_S;
		tv = &now;
	}

	return tv->tv_sec;
}

#ifdef TEST
#include <stdio.h>

int main(int argc, char **argv)
{
	bolo_msec_t ms, want_ms;
	bolo_sec_t  s,  want_s;
	struct timeval tv;

	tv.tv_sec = 400; tv.tv_usec = 0;
	ms = bolo_ms(&tv); want_ms = 400000;
	if (ms != want_ms) {
		fprintf(stderr, "time: bolo_ms({400,0})\n"
		                "  was %lu\n"
		                "  not %lu\n", ms, want_ms);
		return 1;
	}
	s = bolo_s(&tv); want_s = 400;
	if (s != want_s) {
		fprintf(stderr, "time: bolo_s({400,0})\n"
		                "  was %u\n"
		                "  not %u\n", s, want_s);
		return 1;
	}

	tv.tv_sec = 400, tv.tv_usec = 700 * 1000;
	ms = bolo_ms(&tv); want_ms = 400700;
	if (ms != want_ms) {
		fprintf(stderr, "time: bolo_ms({400,700_000})\n"
		                "  was %lu\n"
		                "  not %lu\n", ms, want_ms);
		return 1;
	}
	s = bolo_s(&tv); want_s = 400;
	if (s != want_s) {
		fprintf(stderr, "time: bolo_s({400,700_000})\n"
		                "  was %u\n"
		                "  not %u\n", s, want_s);
		return 1;
	}

	ms = bolo_ms(NULL);
	if (ms == INVALID_MS) {
		fprintf(stderr, "time: bolo_ms(NULL) failed\n");
		return 1;
	}
	s = bolo_s(NULL);
	if (s == INVALID_S) {
		fprintf(stderr, "time: bolo_s(NULL) failed\n");
		return 1;
	}

	fprintf(stderr, "time: ok\n");
	return 0;
}

#endif

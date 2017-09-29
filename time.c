#include "bolo.h"
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
/* LCOV_EXCL_START */
TESTS {
	struct timeval tv;

#define is_time(s,ms,real_ms,msg) do { \
	tv.tv_sec = (s); tv.tv_usec = (ms) * 1000; \
	is_unsigned(bolo_ms(&tv),(real_ms),msg ": bolo_ms()"); \
	is_unsigned(bolo_s (&tv),(s),      msg ": bolo_s()"); \
} while (0)

	is_time(400, 0,   400000, "400s 0ms");
	is_time(400, 700, 400700, "400s 700ms");

	ok(bolo_ms(NULL) != INVALID_MS, "bolo_ms(NULL) returns now");
	ok(bolo_s(NULL)  != INVALID_S,  "bolo_s(NULL) returns now");
}
/* LCOV_EXCL_END */
#endif

#include "bolo.h"

/* to avoid compiler warnings... */
void bits_unused() {} /* LCOV_EXCL_LINE */
/* ... this translation unit is only really
       useful if -DTEST is passed, to test
       the read* / write* macros... */

#ifdef TEST
/* LCOV_EXCL_START */
TESTS {
	char buf[32];

	write8(buf, 0, 0x41);
	is_unsigned(read8(buf, 0), 0x41, "write8() / read8()");

	write16(buf, 0, 0x4242);
	is_unsigned(read16(buf, 0), 0x4242, "write16() / read16()");

	write32(buf, 0, 0x43434343);
	is_unsigned(read32(buf, 0), 0x43434343, "write32() / read32()");

	write64(buf, 0, 0x4545454545454545ul);
	is_unsigned(read64(buf, 0), 0x4545454545454545ul, "write64() / read64()");

	write64f(buf, 0, 12345.6789);
	ok(read64f(buf, 0) == 12345.6789, "write64f() / read64f()");

	writen(buf, 0, "Hello!", 6);
	ok(memcmp(buf, "Hello!", 6) == 0, "writen() / direct memory access");
}
/* LCOV_EXCL_STOP */
#endif

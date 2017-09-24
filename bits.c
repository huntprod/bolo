#include "bolo.h"

/* to avoid compiler warnings... */
void bits_unused() {}
/* ... this translation unit is only really
       useful if -DTEST is passed, to test
       the read* / write* macros... */

#ifdef TEST
#include <stdio.h>
int main(int argc, char **argv)
{
	char buf[32];

#define test(n,v) do {\
	write##n(buf, 0, (v)); \
	if (read##n(buf, 0) != (v)) { \
		fprintf(stderr, "write" #n "(buf,0," #v ") != read" #n "(buf,0)\n" \
		                "  was %1$ld (%1$02lx)\n" \
		                "  not %2$ld (%2$02lx)\n", \
		                (unsigned long)read##n(buf, 0), \
		                (unsigned long)(v)); \
		return 0; \
	} \
} while (0)

	test(8, 0x41);
	test(16, 0x4242);
	test(32, 0x43434343);
	test(64, 0x4545454545454545ul);

	printf("bits: ok\n");
	return 0;
}
#endif

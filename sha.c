#include "bolo.h"

#ifdef DEBUG
static void hexit(const char *msg, const void *buf, size_t start, size_t len)
{
	char *pre = strdup(msg);
	char  sep = ':';
again:
	fprintf(stderr, "%s%c ", pre, sep);
	if (msg) {
		memset(pre, ' ', strlen(msg));
		msg = NULL; sep = ' ';
	}
	while (start < len) {
		fprintf(stderr, "%02X", ((const unsigned char *)buf)[start++]);
		if (start % 32 == 0 && start < len) {
			fprintf(stderr, "\n");
			goto again;
		}
	}
	fprintf(stderr, "\n\n");
}
#else
#define hexit(m,b,s,l)
#endif

#define  SHR(n,x) ((x) >> (n))                       /* 3.2.3 */
#define ROTR(n,x) ((x) >> (n) | ((x) << (64 - n)))   /* 3.2.4 */

#define Ch(x,y,z) ( ((x) & (y)) ^ (~(x) & (z)) )                     /* 4.8  */
#define Maj(x,y,z) ( ((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)) )       /* 4.9  */
#define SIGMA0(x) ( ROTR(28, (x)) ^ ROTR(34, (x)) ^ ROTR(39, (x)) )  /* 4.10 */
#define SIGMA1(x) ( ROTR(14, (x)) ^ ROTR(18, (x)) ^ ROTR(41, (x)) )  /* 4.11 */
#define sigma0(x) ( ROTR( 1, (x)) ^ ROTR( 8, (x)) ^  SHR( 7, (x)) )  /* 4.12 */
#define sigma1(x) ( ROTR(19, (x)) ^ ROTR(61, (x)) ^  SHR( 6, (x)) )  /* 4.13 */

/* K0 - K79 */
static uint64_t K[80] = {
	0x428a2f98d728ae22ul, 0x7137449123ef65cdul, 0xb5c0fbcfec4d3b2ful, 0xe9b5dba58189dbbcul,
	0x3956c25bf348b538ul, 0x59f111f1b605d019ul, 0x923f82a4af194f9bul, 0xab1c5ed5da6d8118ul,
	0xd807aa98a3030242ul, 0x12835b0145706fbeul, 0x243185be4ee4b28cul, 0x550c7dc3d5ffb4e2ul,
	0x72be5d74f27b896ful, 0x80deb1fe3b1696b1ul, 0x9bdc06a725c71235ul, 0xc19bf174cf692694ul,
	0xe49b69c19ef14ad2ul, 0xefbe4786384f25e3ul, 0x0fc19dc68b8cd5b5ul, 0x240ca1cc77ac9c65ul,
	0x2de92c6f592b0275ul, 0x4a7484aa6ea6e483ul, 0x5cb0a9dcbd41fbd4ul, 0x76f988da831153b5ul,
	0x983e5152ee66dfabul, 0xa831c66d2db43210ul, 0xb00327c898fb213ful, 0xbf597fc7beef0ee4ul,
	0xc6e00bf33da88fc2ul, 0xd5a79147930aa725ul, 0x06ca6351e003826ful, 0x142929670a0e6e70ul,
	0x27b70a8546d22ffcul, 0x2e1b21385c26c926ul, 0x4d2c6dfc5ac42aedul, 0x53380d139d95b3dful,
	0x650a73548baf63deul, 0x766a0abb3c77b2a8ul, 0x81c2c92e47edaee6ul, 0x92722c851482353bul,
	0xa2bfe8a14cf10364ul, 0xa81a664bbc423001ul, 0xc24b8b70d0f89791ul, 0xc76c51a30654be30ul,
	0xd192e819d6ef5218ul, 0xd69906245565a910ul, 0xf40e35855771202aul, 0x106aa07032bbd1b8ul,
	0x19a4c116b8d2d0c8ul, 0x1e376c085141ab53ul, 0x2748774cdf8eeb99ul, 0x34b0bcb5e19b48a8ul,
	0x391c0cb3c5c95a63ul, 0x4ed8aa4ae3418acbul, 0x5b9cca4f7763e373ul, 0x682e6ff3d6b2b8a3ul,
	0x748f82ee5defb2fcul, 0x78a5636f43172f60ul, 0x84c87814a1f0ab72ul, 0x8cc702081a6439ecul,
	0x90befffa23631e28ul, 0xa4506cebde82bde9ul, 0xbef9a3f7b2c67915ul, 0xc67178f2e372532bul,
	0xca273eceea26619cul, 0xd186b8c721c0c207ul, 0xeada7dd6cde0eb1eul, 0xf57d4f7fee6ed178ul,
	0x06f067aa72176fbaul, 0x0a637dc5a2c898a6ul, 0x113f9804bef90daeul, 0x1b710b35131c471bul,
	0x28db77f523047d84ul, 0x32caab7b40c72493ul, 0x3c9ebe0a15c9bebcul, 0x431d67c49c100d4cul,
	0x4cc5d4becb3e42b6ul, 0x597f299cfc657e2aul, 0x5fcb6fab3ad6faecul, 0x6c44198c4a475817ul,
};

/* H0 */
static uint64_t H0[8] = {
	0x6a09e667f3bcc908ul, 0xbb67ae8584caa73bul, 0x3c6ef372fe94f82bul, 0xa54ff53a5f1d36f1ul,
	0x510e527fade682d1ul, 0x9b05688c2b3e6c1ful, 0x1f83d9abfb41bd6bul, 0x5be0cd19137e2179ul,
};

/* hex conversion */
static char HEX[16] = "0123456789abcdef";

/* bit twiddling */
#if BYTE_ORDER == LITTLE_ENDIAN
#  define swap64(dst,src) do { \
	uint64_t __tmp = (src); \
	__tmp = (__tmp >> 32) | (__tmp << 32); \
	__tmp = ((__tmp & 0xff00ff00ff00ff00ul) >>  8) | ((__tmp & 0x00ff00ff00ff00fful) <<  8); \
	(dst) = ((__tmp & 0xffff0000ffff0000ul) >> 16) | ((__tmp & 0x0000ffff0000fffful) << 16); \
} while (0)
#else
#  define swap64(v)
#endif

/* big-endian 64-bit conversion routine */
#define u64be(b,i,n) do { \
	((b)[8 * (i) + 0] = (uint8_t)((n) >> 56)); \
	((b)[8 * (i) + 1] = (uint8_t)((n) >> 48)); \
	((b)[8 * (i) + 2] = (uint8_t)((n) >> 40)); \
	((b)[8 * (i) + 3] = (uint8_t)((n) >> 32)); \
	((b)[8 * (i) + 4] = (uint8_t)((n) >> 24)); \
	((b)[8 * (i) + 5] = (uint8_t)((n) >> 16)); \
	((b)[8 * (i) + 6] = (uint8_t)((n) >>  8)); \
	((b)[8 * (i) + 7] = (uint8_t)((n)      )); \
} while (0)

void
sha512_init(struct sha512 *c)
{
	CHECK(c != NULL, "sha512_init() given a NULL sha512 context to initialize");

	memset(c, 0, sizeof(*c));
	memcpy(c->state, H0, SHA512_DIGEST);

#ifdef DEBUG
	printf("Initial hash value:\n");
	printf("H[0] = %016lX\n", c->state[0]);
	printf("H[1] = %016lX\n", c->state[1]);
	printf("H[2] = %016lX\n", c->state[2]);
	printf("H[3] = %016lX\n", c->state[3]);
	printf("H[4] = %016lX\n", c->state[4]);
	printf("H[5] = %016lX\n", c->state[5]);
	printf("H[6] = %016lX\n", c->state[6]);
	printf("H[7] = %016lX\n", c->state[7]);
	printf("\n");
#endif
}

static void
sha512_transform(struct sha512 *x)
{
	uint64_t W[80], t1, t2;
	uint64_t a, b, c, d,
	         e, f, g, h;
	int t;

	/* prepare the W[t] message schedule */
	for (t = 0;  t < 16; t++) {
		W[t] = x->block[t];
		swap64(W[t], W[t]);
	}
	for (t = 16; t < 80; t++) {
		W[t] = sigma1(W[t -  2]) + W[t -  7]
		     + sigma0(W[t - 15]) + W[t - 16];
	}

#ifdef DEBUG
	printf("Block Contents:\n");
	printf("W[0]  = %016lX\n", W[0]);
	printf("W[1]  = %016lX\n", W[1]);
	printf("W[2]  = %016lX\n", W[2]);
	printf("W[3]  = %016lX\n", W[3]);
	printf("W[4]  = %016lX\n", W[4]);
	printf("W[5]  = %016lX\n", W[5]);
	printf("W[6]  = %016lX\n", W[6]);
	printf("W[7]  = %016lX\n", W[7]);
	printf("W[8]  = %016lX\n", W[8]);
	printf("W[9]  = %016lX\n", W[9]);
	printf("W[10] = %016lX\n", W[10]);
	printf("W[11] = %016lX\n", W[11]);
	printf("W[12] = %016lX\n", W[12]);
	printf("W[13] = %016lX\n", W[13]);
	printf("W[14] = %016lX\n", W[14]);
	printf("W[15] = %016lX\n", W[15]);
	printf("\n");
#endif

	/* initialize the working vars with the last hash value */
	a = x->state[0]; b = x->state[1]; c = x->state[2]; d = x->state[3];
	e = x->state[4]; f = x->state[5]; g = x->state[6]; h = x->state[7];

#ifdef DEBUG
#define dump(t,a,b,c,d,e,f,g,h) do { \
	if ((t) == 0) \
		printf("      %7s  %16s  %16s  %16s\n", "A/E", "B/F", "C/G", "D/H"); \
	printf("t=%2d  %016lX  %016lX  %016lX  %016lX\n", t, a, b, c, d); \
	printf( "      %016lX  %016lX  %016lX  %016lX\n",    e, f, g, h); \
} while (0)
#else
#define dump(t,a,b,c,d,e,f,g,h)
#endif
#define transform(a,b,c,d,e,f,g,h) do { \
	t1 = h + SIGMA1(e) + Ch(e,f,g) + K[t] + W[t]; \
	t2 =     SIGMA0(a) + Maj(a,b,c); \
	d += t1; h = t1 + t2; \
	dump(t,h,a,b,c,d,e,f,g); \
} while (0)
	for (t = 0; t < 80; ) {
		transform(a, b, c, d, e, f, g, h); t++;
		transform(h, a, b, c, d, e, f, g); t++;
		transform(g, h, a, b, c, d, e, f); t++;
		transform(f, g, h, a, b, c, d, e); t++;
		transform(e, f, g, h, a, b, c, d); t++;
		transform(d, e, f, g, h, a, b, c); t++;
		transform(c, d, e, f, g, h, a, b); t++;
		transform(b, c, d, e, f, g, h, a); t++;
	}

#ifdef DEBUG
	printf("\n");
	printf("H[0] = %016lX + %016lX = %016lX\n", x->state[0], a, x->state[0] + a);
	printf("H[1] = %016lX + %016lX = %016lX\n", x->state[1], b, x->state[1] + b);
	printf("H[2] = %016lX + %016lX = %016lX\n", x->state[2], c, x->state[2] + c);
	printf("H[3] = %016lX + %016lX = %016lX\n", x->state[3], d, x->state[3] + d);
	printf("H[4] = %016lX + %016lX = %016lX\n", x->state[4], e, x->state[4] + e);
	printf("H[5] = %016lX + %016lX = %016lX\n", x->state[5], f, x->state[5] + f);
	printf("H[6] = %016lX + %016lX = %016lX\n", x->state[6], g, x->state[6] + g);
	printf("H[7] = %016lX + %016lX = %016lX\n", x->state[7], h, x->state[7] + h);
#endif

	/* compute this round's hash values */
	x->state[0] += a; x->state[1] += b; x->state[2] += c; x->state[3] += d;
	x->state[4] += e; x->state[5] += f; x->state[6] += g; x->state[7] += h;
}

void
sha512_feed(struct sha512 *c, const void *buf, size_t len)
{
	size_t used, avail;

	if (len == 0)
		return;

	/* do we have data from a previous feeding? */
	used  = c->bytes[0] % SHA512_BLOCK;
	avail = SHA512_BLOCK - used;

	c->bytes[0] += len;
	if (c->bytes[0] < len)
		c->bytes[1]++; /* rollover */

	while (len >= avail) {
		memcpy((uint8_t *)c->block + used, buf, avail);
		sha512_transform(c);
		buf = (uint8_t *)buf + avail;
		len -= avail;

		used  = 0;
		avail = SHA512_BLOCK;
	}

	if (len) /* store remainder for next feed/done call */
		memcpy((uint8_t *)c->block, buf, len);
}

void
sha512_done(struct sha512 *c)
{
	size_t used, avail;
	uint64_t hi, lo;

	/* where do we start the padding? */
	used  = c->bytes[0] % SHA512_BLOCK;
	avail = SHA512_BLOCK - used;


	memset((uint8_t *)c->block + used,    0, avail);
	memset((uint8_t *)c->block + used, 0x80, 1);

	/* if we have 112 bytes or more in the block, we
	   must extend the padding into a second block */
	if (used >= 112) {
		sha512_transform(c);
		memset(c->block,  0, SHA512_BLOCK);
	}

	hi = (c->bytes[1] << 3) | (c->bytes[0] >> 61);
	lo = (c->bytes[0] << 3);

	swap64(c->block[14], hi);
	swap64(c->block[15], lo);

	sha512_transform(c);
}

int
sha512_raw(struct sha512 *c, void *d, size_t len)
{
	CHECK(c != NULL, "sha512_raw() given a NULL sha512 context to query");

	errno = EINVAL;
	if (!d || len < SHA512_DIGEST)
		return -1;

#define u64(i) u64be((uint8_t*)d,(i),c->state[(i)])
	u64(0); u64(1); u64(2); u64(3);
	u64(4); u64(5); u64(6); u64(7);
#undef u64

	return 0;
}

int
sha512_hex(struct sha512 *c, void *d, size_t len)
{
	CHECK(c != NULL, "sha512_hex() given a NULL sha512 context to query");

	errno = EINVAL;
	if (!d || len < 2 * SHA512_DIGEST)
		return -1;

#define x64(i) do {\
	(((uint8_t*)d)[16 * (i) +  0] = HEX[(uint8_t)(c->state[(i)] >> 60) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) +  1] = HEX[(uint8_t)(c->state[(i)] >> 56) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) +  2] = HEX[(uint8_t)(c->state[(i)] >> 52) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) +  3] = HEX[(uint8_t)(c->state[(i)] >> 48) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) +  4] = HEX[(uint8_t)(c->state[(i)] >> 44) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) +  5] = HEX[(uint8_t)(c->state[(i)] >> 40) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) +  6] = HEX[(uint8_t)(c->state[(i)] >> 36) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) +  7] = HEX[(uint8_t)(c->state[(i)] >> 32) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) +  8] = HEX[(uint8_t)(c->state[(i)] >> 28) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) +  9] = HEX[(uint8_t)(c->state[(i)] >> 24) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) + 10] = HEX[(uint8_t)(c->state[(i)] >> 20) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) + 11] = HEX[(uint8_t)(c->state[(i)] >> 16) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) + 12] = HEX[(uint8_t)(c->state[(i)] >> 12) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) + 13] = HEX[(uint8_t)(c->state[(i)] >>  8) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) + 14] = HEX[(uint8_t)(c->state[(i)] >>  4) & 0x0f]); \
	(((uint8_t*)d)[16 * (i) + 15] = HEX[(uint8_t)(c->state[(i)]      ) & 0x0f]); \
} while (0)
	x64(0); x64(1); x64(2); x64(3);
	x64(4); x64(5); x64(6); x64(7);
#undef x64

	return 0;
}

#define HMAC_SHA512_IPAD 0x36
#define HMAC_SHA512_OPAD 0x5c

void
hmac_sha512_init(struct hmac_sha512 *c, const char *k, size_t len)
{
	char ipad[SHA512_BLOCK];
	int i;

	CHECK(c != NULL, "hmac_sha512_init() given a NULL hmac-sha512 context to initialize");
	CHECK(k != NULL, "hmac_sha512_init() given a NULL encryption key to seal with");
	CHECK(len > 0,   "hmac_sha512_init() given an invalid encryption key length");

	memset(c->key, 0, SHA512_BLOCK);
	if (len > SHA512_BLOCK) {
		sha512_init(&c->sha);
		sha512_feed(&c->sha, k, len);
		sha512_done(&c->sha);
		if (sha512_raw(&c->sha, c->key, SHA512_DIGEST))
			memset(c->key, 0, SHA512_DIGEST);
		len = SHA512_DIGEST; /* debugging only */
	} else {
		memcpy(c->key, k, len);
	}

	hexit("key", c->key, 0, len);

	/* K ^ ipad */
	for (i = 0; i < SHA512_BLOCK; i++)
		ipad[i] = c->key[i] ^ HMAC_SHA512_IPAD;

	hexit("ipad", ipad, 0, SHA512_BLOCK);

	sha512_init(&c->sha);
	sha512_feed(&c->sha, ipad, SHA512_BLOCK);

	hexit("H(K^ipad) h", (const uint8_t *)c->sha.state, 0, SHA512_DIGEST);
	hexit("H(K^ipad) b", (const uint8_t *)c->sha.block, 0, SHA512_BLOCK);
}

void
hmac_sha512_feed(struct hmac_sha512 *c, const void *buf, size_t len)
{
	sha512_feed(&c->sha, buf, len);
	hexit("update H(K^ipad . m) h", (const uint8_t *)c->sha.state, 0, SHA512_DIGEST);
	hexit("update H(K^ipad . m) b", (const uint8_t *)c->sha.block, 0, SHA512_BLOCK);
}

void
hmac_sha512_done(struct hmac_sha512 *c)
{
	uint8_t digest[SHA512_DIGEST];
	char opad[SHA512_BLOCK];
	int i;

	/* snag a copy of the inner hash digest */
	sha512_done(&c->sha);
	if (sha512_raw(&c->sha, digest, SHA512_DIGEST) != 0)
		memset(digest, 0, SHA512_DIGEST);

	hexit("final H(K^ipad . m) h", (const uint8_t *)c->sha.state, 0, SHA512_DIGEST);
	hexit("final H(K^ipad . m) b", (const uint8_t *)c->sha.block, 0, SHA512_BLOCK);

	/* K^opad */
	for (i = 0; i < SHA512_BLOCK; i++)
		opad[i] = c->key[i] ^ HMAC_SHA512_OPAD;

	hexit("final opad", opad, 0, SHA512_BLOCK);

	/* calculate the outer digest */
	sha512_init(&c->sha);
	sha512_feed(&c->sha, opad,   SHA512_BLOCK);
	hexit("final H(K^opad) h", (const uint8_t *)c->sha.state, 0, SHA512_DIGEST);
	hexit("final H(K^opad) b", (const uint8_t *)c->sha.block, 0, SHA512_BLOCK);
	sha512_feed(&c->sha, digest, SHA512_DIGEST);
	hexit("final H(K^opad . <digest>) h", (const uint8_t *)c->sha.state, 0, SHA512_DIGEST);
	hexit("final H(K^opad . <digest>) b", (const uint8_t *)c->sha.block, 0, SHA512_BLOCK);
	sha512_done(&c->sha);

	if (sha512_raw(&c->sha, digest, SHA512_DIGEST) != 0)
		memset(digest, 0, SHA512_DIGEST);
	hexit("FINAL H", digest, 0, SHA512_DIGEST);
}

int
hmac_sha512_raw(struct hmac_sha512 *c, void *hmac, size_t len)
{
	return sha512_raw(&c->sha, hmac, len);
}

int
hmac_sha512_hex(struct hmac_sha512 *c, void *hmac, size_t len)
{
	return sha512_hex(&c->sha, hmac, len);
}

void
hmac_sha512_seal(const char *key, size_t key_len, const void *buf, size_t len)
{
	struct hmac_sha512 c;
	uint8_t zeros[64];

	CHECK(key != NULL, "hmac_sha512_seal() given a NULL encryption key to seal with");
	CHECK(key_len > 0, "hmac_sha512_seal() given an invalid encryption key length");
	CHECK(buf != NULL, "hmac_sha512_seal() given a NULL buffer to seal");
	CHECK(len > 64,    "hmac_sha512_seal() given an invalid buffer length");

	memset(zeros, 0, 64);
	hmac_sha512_init(&c, key, key_len);
	hmac_sha512_feed(&c, (uint8_t *)buf, len - 64);
	hmac_sha512_feed(&c, zeros, 64);
	hmac_sha512_done(&c);

	if (hmac_sha512_raw(&c, (uint8_t *)buf + len - 64, 64) != 0)
		memset((uint8_t *)buf + len - 64, 0, 64);
}

int
hmac_sha512_check(const char *key, size_t key_len, const void *buf, size_t len)
{
	struct hmac_sha512 c;
	uint8_t zeros[64];
	uint8_t digest[64];

	CHECK(key != NULL, "hmac_sha512_check() given a NULL encryption key to validate the seal with");
	CHECK(key_len > 0, "hmac_sha512_check() given an invalid encryption key length");
	CHECK(buf != NULL, "hmac_sha512_check() given a NULL buffer to check");
	CHECK(len > 64,    "hmac_sha512_check() given an invalid buffer length");

	memset(zeros, 0, 64);
	hmac_sha512_init(&c, key, key_len);
	hmac_sha512_feed(&c, (uint8_t *)buf, len - 64);
	hmac_sha512_feed(&c, zeros, 64);
	hmac_sha512_done(&c);

	if (hmac_sha512_raw(&c, digest, 64) != 0)
		memset(digest, 0, 64);

	return memcmp(digest, (uint8_t *)buf + len - 64, 64);
}

#ifdef TEST
/* LCOV_EXCL_START */
TESTS {
	subtest {
		struct sha512 c;
		char hex[2 * SHA512_DIGEST + 1];

		sha512_init(&c);
		sha512_feed(&c, "abc", 3);
		sha512_done(&c);

		if (sha512_hex(&c, hex, 2 * SHA512_DIGEST) != 0)
			BAIL_OUT("sha512_hex() failed");

		hex[2 * SHA512_DIGEST] = '\0';
		is_string(hex, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
		               "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
		  "sha512('abc', 3)");
	}

	subtest {
		struct sha512 c;
		char hex[2 * SHA512_DIGEST + 1];

		sha512_init(&c);
		sha512_feed(&c, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
		                "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 112);
		sha512_done(&c);

		if (sha512_hex(&c, hex, 2 * SHA512_DIGEST) != 0)
			BAIL_OUT("sha512_hex() failed");

		hex[2 * SHA512_DIGEST] = '\0';
		is_string(hex, "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
		               "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909",
		  "sha512('abcd...qrstu', 112)");
	}

	subtest {
		struct hmac_sha512 c;
		char key[20];
		char hex[2 * SHA512_DIGEST + 1];

		memset(key, 0x0b, 20);
		hmac_sha512_init(&c, key, 20);
		hmac_sha512_feed(&c, "Hi There", 8);
		hmac_sha512_done(&c);

		if (hmac_sha512_hex(&c, hex, 2 * SHA512_DIGEST))
			BAIL_OUT("sha512_hex() failed");

		hex[2 * SHA512_DIGEST] = '\0';
		is_string(hex, "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
		               "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854",
		  "RFC-4231 HMAC-SHA512 Test Case 1");
	}

	subtest {
		struct hmac_sha512 c;
		char hex[2 * SHA512_DIGEST + 1];

		hmac_sha512_init(&c, "Jefe", 4);
		hmac_sha512_feed(&c, "what do ya want for nothing?", 28);
		hmac_sha512_done(&c);

		if (hmac_sha512_hex(&c, hex, 2 * SHA512_DIGEST))
			BAIL_OUT("sha512_hex() failed");

		hex[2 * SHA512_DIGEST] = '\0';
		is_string(hex, "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
		               "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737",
		  "RFC-4231 HMAC-SHA512 Test Case 2");
	}

	subtest {
		struct hmac_sha512 c;
		char key[20];
		char dat[50];
		char hex[2 * SHA512_DIGEST + 1];

		memset(key, 0xaa, 20);
		memset(dat, 0xdd, sizeof(dat));
		hmac_sha512_init(&c, key, sizeof(key));
		hmac_sha512_feed(&c, dat, sizeof(dat));
		hmac_sha512_done(&c);

		if (hmac_sha512_hex(&c, hex, 2 * SHA512_DIGEST))
			BAIL_OUT("sha512_hex() failed");

		hex[2 * SHA512_DIGEST] = '\0';
		is_string(hex, "fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39"
		               "bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb",
		  "RFC-4231 HMAC-SHA512 Test Case 3");
	}

	subtest {
		struct hmac_sha512 c;
		char key[25];
		char dat[50];
		char hex[2 * SHA512_DIGEST + 1];
		unsigned int i;

		for (i = 0; i < sizeof(key); i++) key[i] = i+1;
		memset(dat, 0xcd, sizeof(dat));
		hmac_sha512_init(&c, key, sizeof(key));
		hmac_sha512_feed(&c, dat, sizeof(dat));
		hmac_sha512_done(&c);

		if (hmac_sha512_hex(&c, hex, 2 * SHA512_DIGEST))
			BAIL_OUT("sha512_hex() failed");

		hex[2 * SHA512_DIGEST] = '\0';
		is_string(hex, "b0ba465637458c6990e5a8c5f61d4af7e576d97ff94b872de76f8050361ee3db"
		               "a91ca5c11aa25eb4d679275cc5788063a5f19741120c4f2de2adebeb10a298dd",
		  "RFC-4231 HMAC-SHA512 Test Case 4");
	}

	subtest {
		char box[4 + SHA512_DIGEST];

		memset(box, 0, sizeof(box));
		memcpy(box, "test", 4);

		ok(hmac_sha512_check("key1", 4, box, sizeof(box)) != 0,
			"hmac_sha512_check should fail on unsealed crypto box");

		hmac_sha512_seal("key1", 4, box, sizeof(box));
		ok(hmac_sha512_check("key1", 4, box, sizeof(box)) == 0,
			"hmac_sha512_check should succeed on sealed crypto box");

		ok(hmac_sha512_check("BAD KEY", 7, box, sizeof(box)) != 0,
			"hmac_sha512_check should fail on sealed crypto box with incorrect key");

		box[60] = box[59];
		ok(hmac_sha512_check("key1", 4, box, sizeof(box)) != 0,
			"hmac_sha512_check should fail on sealed crypto box that has been tampered with");
	}

	subtest {
		char box[4 + SHA512_DIGEST];
		char key[1024];

		memset(key, 0x41, sizeof(key));
		memset(box, 0,    sizeof(box));
		memcpy(box, "test", 4);

		hmac_sha512_seal(key, sizeof(key), box, sizeof(box));
		ok(hmac_sha512_check(key, sizeof(key), box, sizeof(box)) == 0,
			"hmac_sha512_seal with large keys (> SHA-512 block size) should succeed");
	}
}
/* LCOV_EXCL_STOP */
#endif

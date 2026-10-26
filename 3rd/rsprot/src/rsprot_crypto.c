#include "rsprot_crypto.h"

#include <string.h>

/* --- ISAAC (IsaacRandom.kt) -------------------------------------------------- */

static void
isaac_scramble(RsprotIsaac *s)
{
	s->c++;
	s->b += s->c;
	for (int i = 0; i < 256; i++) {
		uint32_t x = s->mem[i];
		uint32_t y;

		switch (i & 3) {
		case 0: s->a ^= s->a << 13; break;
		case 1: s->a ^= s->a >> 6; break;
		case 2: s->a ^= s->a << 2; break;
		default: s->a ^= s->a >> 16; break;
		}
		s->a += s->mem[(i + 128) & 0xff];
		s->mem[i] = y = s->mem[(x >> 2) & 0xff] + s->a + s->b;
		s->rsl[i] = s->b = s->mem[((y >> 8) >> 2) & 0xff] + x;
	}
}

#define ISAAC_MIX(a, b, c, d, e, f, g, h) \
	do {                                  \
		a ^= b << 11; d += a; b += c;     \
		b ^= c >> 2;  e += b; c += d;     \
		c ^= d << 8;  f += c; d += e;     \
		d ^= e >> 16; g += d; e += f;     \
		e ^= f << 10; h += e; f += g;     \
		f ^= g >> 4;  a += f; g += h;     \
		g ^= h << 8;  b += g; h += a;     \
		h ^= a >> 9;  c += h; a += b;     \
	} while (0)

static void
isaac_init(RsprotIsaac *s)
{
	uint32_t a, b, c, d, e, f, g, h;

	a = b = c = d = e = f = g = h = 0x9e3779b9u;
	for (int i = 0; i < 4; i++)
		ISAAC_MIX(a, b, c, d, e, f, g, h);
	for (int pass = 0; pass < 2; pass++) {
		/* The seed (rsl) folds in first, then a second pass over mem. */
		const uint32_t *in = pass == 0 ? s->rsl : s->mem;

		for (int i = 0; i < 256; i += 8) {
			a += in[i];
			b += in[i + 1];
			c += in[i + 2];
			d += in[i + 3];
			e += in[i + 4];
			f += in[i + 5];
			g += in[i + 6];
			h += in[i + 7];
			ISAAC_MIX(a, b, c, d, e, f, g, h);
			s->mem[i] = a;
			s->mem[i + 1] = b;
			s->mem[i + 2] = c;
			s->mem[i + 3] = d;
			s->mem[i + 4] = e;
			s->mem[i + 5] = f;
			s->mem[i + 6] = g;
			s->mem[i + 7] = h;
		}
	}
	isaac_scramble(s);
	s->count = 256;
}

const int32_t RSPROT_XTEA_KEY_ZERO[4] = { 0, 0, 0, 0 };

void
rsprot_cipher_init_nop(RsprotCipher *c)
{
	memset(c, 0, sizeof(*c));
	c->kind = RSPROT_CIPHER_NOP;
}

bool
rsprot_cipher_init_isaac(RsprotCipher *c, const int32_t seed[4])
{
	memset(c, 0, sizeof(*c));
	for (int i = 0; i < 4; i++)
		c->isaac.rsl[i] = (uint32_t)seed[i];
	isaac_init(&c->isaac);
	c->kind = RSPROT_CIPHER_ISAAC;
	return true;
}

void
rsprot_cipher_destroy(RsprotCipher *c)
{
	if (!c)
		return;
	rsprot_cipher_init_nop(c);
}

int32_t
rsprot_cipher_next(RsprotCipher *c)
{
	RsprotIsaac *s = &c->isaac;

	if (c->kind != RSPROT_CIPHER_ISAAC)
		return 0; /* NopStreamCipher.nextInt() */
	if (s->count-- == 0) {
		isaac_scramble(s);
		s->count = 255;
	}
	return (int32_t)s->rsl[s->count];
}

void
rsprot_cipher_pair_destroy(RsprotCipherPair *p)
{
	if (!p)
		return;
	rsprot_cipher_destroy(&p->encode);
	rsprot_cipher_destroy(&p->decode);
}

/* --- XTEA --------------------------------------------------------------------- */

#define XTEA_DELTA 0x9E3779B9u
#define XTEA_ROUNDS 32

static uint32_t
be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void
put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

void
rsprot_xtea_decrypt(void *data, int32_t len, const int32_t key[4])
{
	uint8_t *p = data;
	const uint32_t *k = (const uint32_t *)key;

	for (int32_t block = 0; block + 8 <= len; block += 8) {
		uint32_t v0 = be32(p + block);
		uint32_t v1 = be32(p + block + 4);
		uint32_t sum = XTEA_DELTA * XTEA_ROUNDS;

		for (int i = 0; i < XTEA_ROUNDS; i++) {
			v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]);
			sum -= XTEA_DELTA;
			v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]);
		}
		put_be32(p + block, v0);
		put_be32(p + block + 4, v1);
	}
}

void
rsprot_xtea_encrypt(void *data, int32_t len, const int32_t key[4])
{
	uint8_t *p = data;
	const uint32_t *k = (const uint32_t *)key;

	for (int32_t block = 0; block + 8 <= len; block += 8) {
		uint32_t v0 = be32(p + block);
		uint32_t v1 = be32(p + block + 4);
		uint32_t sum = 0;

		/* The inverse of decrypt: the sum runs up from 0, each half added
		 * where decrypt subtracts. */
		for (int i = 0; i < XTEA_ROUNDS; i++) {
			v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]);
			sum += XTEA_DELTA;
			v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]);
		}
		put_be32(p + block, v0);
		put_be32(p + block + 4, v1);
	}
}

/* --- RSA: modPow over 32-bit limbs, least significant first ------------------- */

#define LIMBS RSPROT_RSA_LIMBS

static int
hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Hex into limbs; returns the significant limb count, or -1. */
static int32_t
from_hex(uint32_t out[LIMBS], const char *hex)
{
	size_t n = strlen(hex);
	int32_t used = 0;

	memset(out, 0, sizeof(uint32_t) * LIMBS);
	if (n == 0 || n > (size_t)LIMBS * 8)
		return -1;
	for (size_t i = 0; i < n; i++) {
		size_t bit = 4 * (n - 1 - i);
		int d = hex_digit(hex[i]);

		if (d < 0)
			return -1;
		out[bit / 32] |= (uint32_t)d << (bit % 32);
	}
	for (int32_t i = LIMBS - 1; i >= 0; i--)
		if (out[i]) {
			used = i + 1;
			break;
		}
	return used;
}

/* -n^-1 mod 2^32, for an odd n (Newton's iteration). */
static uint32_t
mont_inverse(uint32_t n0)
{
	uint32_t x = n0;

	for (int i = 0; i < 5; i++)
		x *= 2 - n0 * x;
	return (uint32_t)0 - x;
}

/* r = a * b * R^-1 mod n (CIOS), every operand `len` limbs; a and b < R, b < n. */
static void
mont_mul(uint32_t *r, const uint32_t *a, const uint32_t *b, const uint32_t *n, int32_t len,
         uint32_t n0inv)
{
	uint32_t t[LIMBS + 2];

	memset(t, 0, sizeof(uint32_t) * (size_t)(len + 2));
	for (int32_t i = 0; i < len; i++) {
		uint64_t carry = 0;
		uint32_t m;

		for (int32_t j = 0; j < len; j++) {
			uint64_t v = (uint64_t)a[j] * b[i] + t[j] + carry;

			t[j] = (uint32_t)v;
			carry = v >> 32;
		}
		carry += t[len];
		t[len] = (uint32_t)carry;
		t[len + 1] = (uint32_t)(carry >> 32);

		m = t[0] * n0inv;
		carry = ((uint64_t)m * n[0] + t[0]) >> 32;
		for (int32_t j = 1; j < len; j++) {
			uint64_t v = (uint64_t)m * n[j] + t[j] + carry;

			t[j - 1] = (uint32_t)v;
			carry = v >> 32;
		}
		carry += t[len];
		t[len - 1] = (uint32_t)carry;
		t[len] = t[len + 1] + (uint32_t)(carry >> 32);
	}
	/* t < 2n: one subtraction at most. */
	{
		int ge = t[len] != 0;

		if (!ge) {
			ge = 1;
			for (int32_t j = len - 1; j >= 0; j--)
				if (t[j] != n[j]) {
					ge = t[j] > n[j];
					break;
				}
		}
		if (ge) {
			int64_t borrow = 0;

			for (int32_t j = 0; j < len; j++) {
				int64_t v = (int64_t)t[j] - n[j] - borrow;

				t[j] = (uint32_t)v;
				borrow = v < 0;
			}
		}
	}
	memcpy(r, t, sizeof(uint32_t) * (size_t)len);
}

/* R^2 mod n, by doubling 1 2 * 32 * len times. */
static void
r_squared(uint32_t *r2, const uint32_t *n, int32_t len)
{
	memset(r2, 0, sizeof(uint32_t) * (size_t)len);
	r2[0] = 1;
	for (int32_t k = 0; k < 2 * 32 * len; k++) {
		uint32_t top = r2[len - 1] >> 31;
		int ge = top != 0;

		for (int32_t j = len - 1; j > 0; j--)
			r2[j] = (r2[j] << 1) | (r2[j - 1] >> 31);
		r2[0] <<= 1;
		if (!ge) {
			ge = 1;
			for (int32_t j = len - 1; j >= 0; j--)
				if (r2[j] != n[j]) {
					ge = r2[j] > n[j];
					break;
				}
		}
		if (ge) {
			int64_t borrow = 0;

			for (int32_t j = 0; j < len; j++) {
				int64_t v = (int64_t)r2[j] - n[j] - borrow;

				r2[j] = (uint32_t)v;
				borrow = v < 0;
			}
		}
	}
}

bool
rsprot_rsa_init(RsprotRsaKey *key, const char *exponent_hex, const char *modulus_hex)
{
	memset(key, 0, sizeof(*key));
	key->exponent_limbs = from_hex(key->exponent, exponent_hex);
	key->modulus_limbs = from_hex(key->modulus, modulus_hex);
	return key->exponent_limbs > 0 && key->modulus_limbs > 0 && (key->modulus[0] & 1) != 0;
}

int32_t
rsprot_rsa_decrypt(const RsprotRsaKey *key, const void *in, int32_t len, void *out, int32_t out_cap)
{
	const uint8_t *src = in;
	uint8_t *dst = out;
	int32_t n_len = key->modulus_limbs;
	uint32_t base[LIMBS] = { 0 };
	uint32_t one[LIMBS] = { 0 };
	uint32_t r2[LIMBS];
	uint32_t acc[LIMBS];
	uint32_t x[LIMBS];
	uint32_t n0inv;
	int32_t bytes;
	int started = 0;

	if (len < 0 || len > n_len * 4)
		return -1;
	for (int32_t i = 0; i < len; i++) {
		int32_t bit = 8 * (len - 1 - i);

		base[bit / 32] |= (uint32_t)src[i] << (bit % 32);
	}
	n0inv = mont_inverse(key->modulus[0]);
	r_squared(r2, key->modulus, n_len);
	mont_mul(x, base, r2, key->modulus, n_len, n0inv); /* base in Montgomery form */
	one[0] = 1;
	mont_mul(acc, one, r2, key->modulus, n_len, n0inv); /* 1 in Montgomery form */
	for (int32_t i = key->exponent_limbs * 32 - 1; i >= 0; i--) {
		if (started)
			mont_mul(acc, acc, acc, key->modulus, n_len, n0inv);
		if ((key->exponent[i / 32] >> (i % 32)) & 1) {
			mont_mul(acc, acc, x, key->modulus, n_len, n0inv);
			started = 1;
		}
	}
	mont_mul(acc, acc, one, key->modulus, n_len, n0inv); /* out of Montgomery form */

	bytes = n_len * 4;
	while (bytes > 0 && ((acc[(bytes - 1) / 4] >> (8 * ((bytes - 1) % 4))) & 0xff) == 0)
		bytes--;
	if (bytes > out_cap)
		return -1;
	for (int32_t i = 0; i < bytes; i++) {
		int32_t b = bytes - 1 - i;

		dst[i] = (uint8_t)(acc[b / 4] >> (8 * (b % 4)));
	}
	return bytes;
}

void
rsprot_rsa_destroy(RsprotRsaKey *key)
{
	if (key)
		memset(key, 0, sizeof(*key));
}

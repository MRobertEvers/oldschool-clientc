/* Adapter smoke tests: rsprot_crypto over the repo's existing ISAAC/XTEA/RSA,
 * and a from-scratch port of the Huffman/Base37 compression module. */

#include "rsprot.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg)                                                                                              \
	do {                                                                                                            \
		if (!(cond)) {                                                                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                                  \
			g_failures++;                                                                                          \
		}                                                                                                          \
	} while (0)

#define CHECK_EQ_I(a, b, msg)                                                                                          \
	do {                                                                                                            \
		long long _a = (long long)(a), _b = (long long)(b);                                                        \
		if (_a != _b) {                                                                                             \
			printf("FAIL %s:%d: %s (got %lld, want %lld)\n", __FILE__, __LINE__, msg, _a, _b);                     \
			g_failures++;                                                                                          \
		}                                                                                                          \
	} while (0)

static void test_isaac_pair_symmetry(void)
{
	/* A server's encode cipher and the client's decode cipher are seeded
	 * with the SAME array (RSProt hands one IntArray to both sides of the
	 * pair in mirror configurations elsewhere); here we just check that two
	 * ISAAC ciphers seeded identically produce identical streams -- the
	 * property every opcode-obfuscation scheme in this protocol depends on. */
	int32_t seed[4] = { 1, 2, 3, 4 };
	RsprotCipher a, b;
	int i;

	CHECK(rsprot_cipher_init_isaac(&a, seed), "isaac init a");
	CHECK(rsprot_cipher_init_isaac(&b, seed), "isaac init b");
	for (i = 0; i < 50; i++)
		CHECK_EQ_I(rsprot_cipher_next(&a), rsprot_cipher_next(&b), "isaac streams match");

	rsprot_cipher_destroy(&a);
	rsprot_cipher_destroy(&b);
}

static void test_nop_cipher(void)
{
	RsprotCipher c;
	rsprot_cipher_init_nop(&c);
	CHECK_EQ_I(rsprot_cipher_next(&c), 0, "nop cipher always 0");
	CHECK_EQ_I(rsprot_cipher_next(&c), 0, "nop cipher always 0 (2)");
}

static void test_xtea_roundtrip(void)
{
	int32_t key[4] = { 0x11223344, 0x55667788, (int32_t)0x99AABBCC, (int32_t)0xDDEEFF00 };
	char data[16] = "0123456789abcde";
	char orig[16];

	memcpy(orig, data, sizeof(data));
	rsprot_xtea_encrypt(data, sizeof(data), key);
	CHECK(memcmp(data, orig, sizeof(data)) != 0, "xtea actually changes bytes");
	rsprot_xtea_decrypt(data, sizeof(data), key);
	CHECK(memcmp(data, orig, sizeof(data)) == 0, "xtea decrypt undoes encrypt");
}

static void test_rsa_roundtrip(void)
{
	/* A small toy keypair: p=61, q=53 -> n=3233, standard textbook-RSA
	 * example (e=17, d=2753). Not a security-relevant key, just a known
	 * modPow fixture with a hand-verifiable answer: 65^17 mod 3233 = 2790. */
	RsprotRsaKey key;
	uint8_t in[1] = { 65 };
	uint8_t out[8] = { 0 };
	int32_t n;

	CHECK(rsprot_rsa_init(&key, "11", "ca1"), "rsa init"); /* 17 hex=11, 3233 hex=ca1 */
	n = rsprot_rsa_decrypt(&key, in, 1, out, sizeof(out));
	CHECK(n > 0, "rsa produced output");
	if (n == 2) {
		CHECK_EQ_I((out[0] << 8) | out[1], 2790, "65^17 mod 3233 == 2790");
	} else if (n == 1) {
		CHECK_EQ_I(out[0], 2790, "unexpected single-byte result");
	}
}

static void test_base37(void)
{
	char out[16];
	int32_t len;
	int64_t encoded = rsprot_base37_encode("Zezima");

	CHECK(encoded != 0, "base37 encode nonzero");
	len = rsprot_base37_decode(encoded, out, sizeof(out));
	CHECK(len > 0, "base37 decode length");
	CHECK(strcmp(out, "zezima") == 0, "base37 round trip lowercases");

	len = rsprot_base37_decode_with_case(encoded, out, sizeof(out));
	CHECK(len > 0, "base37 decodeWithCase length");
	CHECK(out[0] == 'Z', "base37 decodeWithCase capitalizes first letter");

	CHECK_EQ_I(rsprot_base37_encode(""), 0, "base37 empty encodes to 0");
	{
		char zero_out[4];
		len = rsprot_base37_decode(0, zero_out, sizeof(zero_out));
		CHECK(len == 0 && zero_out[0] == '\0', "base37 decode of 0 is empty string");
	}
}

static void test_base37_underscore(void)
{
	char out[24];
	int64_t encoded = rsprot_base37_encode("iron_man");
	int32_t len = rsprot_base37_decode_with_case(encoded, out, sizeof(out));
	CHECK(len > 0, "underscore name decodes");
	/* decodeWithCase: 'i' capitalized, '_' -> NBSP (0xA0), 'm' capitalized. */
	CHECK((uint8_t)out[0] == 'I', "iron_man decode: leading cap");
	CHECK((uint8_t)out[4] == 0xA0, "iron_man decode: underscore -> nbsp");
	CHECK((uint8_t)out[5] == 'M', "iron_man decode: cap after nbsp");
}

/* A synthetic but valid Huffman table: bit-length 1 for 'a', 2 for 'b' and
 * 'c', 3 for 'd' -- a proper prefix code (Kraft sum = 1/2+1/4+1/4+1/8 > 1 is
 * NOT valid; use lengths that sum to <= 1: 1,2,3,3 -> 1/2+1/4+1/8+1/8 = 1). */
static void test_huffman_roundtrip(void)
{
	uint8_t table[256];
	RsprotHuffman h;
	uint8_t mem[256];
	RsprotBuf buf;
	char out[64];
	int32_t n;

	memset(table, 0, sizeof(table));
	table[(uint8_t)'a'] = 1;
	table[(uint8_t)'b'] = 2;
	table[(uint8_t)'c'] = 3;
	table[(uint8_t)'d'] = 3;

	CHECK(rsprot_huffman_create(&h, table), "huffman create");

	rsprot_buf_wrap(&buf, mem, sizeof(mem));
	rsprot_huffman_encode(&h, &buf, "abcdabcd");
	CHECK(rsprot_buf_ok(&buf), "huffman encode ok");

	rsprot_buf_wrap_read(&buf, mem, buf.wpos);
	n = rsprot_huffman_decode(&h, &buf, out, sizeof(out));
	CHECK_EQ_I(n, 8, "huffman decode length");
	CHECK(memcmp(out, "abcdabcd", 8) == 0, "huffman round trip content");

	rsprot_huffman_destroy(&h);
}

int main(void)
{
	test_isaac_pair_symmetry();
	test_nop_cipher();
	test_xtea_roundtrip();
	test_rsa_roundtrip();
	test_base37();
	test_base37_underscore();
	test_huffman_roundtrip();

	if (g_failures) {
		printf("%d failures\n", g_failures);
		return 1;
	}
	printf("all rsprot crypto/compression tests passed\n");
	return 0;
}

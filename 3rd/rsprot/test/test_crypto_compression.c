/* rsprot_crypto: ISAAC, XTEA and RSA,
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

static void test_rsa_1024(void)
{
	/* A 1024-bit key and block, the result checked against Python's pow(). */
	static const char d_hex[] = "6d3d1e32a6d775e9cf177b49e884534649a211d3b0e4a577880831bb74dcb0e16aa7a946a787152e236fcef5b6041b59e92c59e31f2cefe0cf555b70ea3b79768c1832a2112b17a3560352318b4814a69bcad7779d84557fff97de66411a49f5db71fd80cf0ed42e3bfd264db60dff23daaffc8aefbe34dad6f2c85e34bed5a1";
	static const char n_hex[] = "737693699a02fe0a3a1af660a0fb469cd8e90ab58303f0b96bba0435bbf563548e2211fe9540f92280a4d489e367463afa3aa41fe0e2c7ec0bea932cd42bae5cf7a25442c53dc1d819741952c779e2e07264db8817030f90aee54cdee37ce25e6a74b3e3b947c25e57d49242697060d21e43898f1d83c1c2b1cdec657049c4f7";
	static const uint8_t in[128] = { 0x08, 0xbe, 0xea, 0x27, 0xb5, 0x67, 0x5d, 0x43, 0x62, 0x61, 0xf7, 0x64, 0xaa, 0xb6, 0x7c, 0x8f, 0x6f, 0xa4, 0xb8, 0x50, 0x0a, 0xc1, 0xfe, 0x0a, 0x7b, 0xc5, 0x89, 0x68, 0x51, 0x4f, 0xd0, 0x9d, 0xdb, 0xaa, 0xe5, 0xe8, 0x89, 0x31, 0xa1, 0xc5, 0x6a, 0x85, 0xf9, 0x79, 0x4d, 0x3b, 0xd0, 0x98, 0xee, 0xbb, 0xb2, 0x7c, 0x02, 0x5a, 0x06, 0x8a, 0x13, 0x88, 0x86, 0xe6, 0xa3, 0x2a, 0xfc, 0xc9, 0x0c, 0xc4, 0x4b, 0xfe, 0xce, 0x09, 0x56, 0x69, 0xef, 0x3d, 0x84, 0x1d, 0x28, 0xc9, 0xa4, 0x46, 0x4c, 0x82, 0x41, 0x87, 0xa7, 0xc9, 0xc0, 0x27, 0x75, 0x9e, 0x25, 0xf3, 0xbf, 0xc8, 0xa1, 0x95, 0xb0, 0xbd, 0x85, 0xad, 0x3a, 0x3d, 0xa8, 0xd3, 0x4e, 0x1f, 0x63, 0x7d, 0x38, 0x2d, 0xf1, 0x21, 0xef, 0xa8, 0x10, 0x3a, 0x20, 0x20, 0x29, 0xb5, 0xd9, 0xd4, 0xea, 0xfb, 0x84, 0xb6, 0xba, 0xfc };
	static const uint8_t want[125] = { 0x3b, 0x7c, 0x55, 0x80, 0xf8, 0x4f, 0xde, 0x62, 0xc0, 0x4d, 0x5d, 0x57, 0x5e, 0x5f, 0xd9, 0x17, 0x85, 0x6a, 0x1e, 0x44, 0xa1, 0xbd, 0x45, 0x9b, 0x6f, 0xfa, 0x65, 0x7e, 0x71, 0xc8, 0x69, 0x38, 0x8b, 0xe8, 0x7b, 0xbc, 0x1f, 0xcc, 0xe4, 0xf5, 0x1b, 0xb9, 0x96, 0x28, 0xbc, 0x0b, 0x7e, 0x54, 0x20, 0x48, 0x33, 0x25, 0x33, 0x6e, 0x5f, 0xf1, 0xc0, 0x76, 0x18, 0x23, 0x73, 0x74, 0x59, 0x9d, 0x70, 0xfd, 0x6d, 0xff, 0x22, 0x46, 0xa1, 0x27, 0xfa, 0x02, 0x36, 0xbf, 0xf5, 0x18, 0xb8, 0x61, 0xf9, 0x9f, 0x24, 0x5d, 0xbc, 0x31, 0xf9, 0xc7, 0x5c, 0xe8, 0xac, 0x6c, 0x7b, 0xb0, 0xf3, 0x10, 0x27, 0x98, 0xc5, 0x64, 0xff, 0x14, 0xcd, 0xeb, 0xd2, 0x24, 0xdf, 0xab, 0x23, 0x2f, 0x37, 0xad, 0xf9, 0x21, 0x89, 0x0a, 0x39, 0xd8, 0x19, 0x78, 0x1d, 0xba, 0x2a, 0x70, 0xe1 };
	RsprotRsaKey key;
	uint8_t out[160];
	int32_t n;

	CHECK(rsprot_rsa_init(&key, d_hex, n_hex), "rsa 1024 init");
	n = rsprot_rsa_decrypt(&key, in, sizeof(in), out, sizeof(out));
	CHECK_EQ_I(n, (int32_t)sizeof(want), "rsa 1024 length");
	CHECK(memcmp(out, want, sizeof(want)) == 0, "rsa 1024 matches modPow");
	rsprot_rsa_destroy(&key);
}

int main(void)
{
	test_isaac_pair_symmetry();
	test_nop_cipher();
	test_xtea_roundtrip();
	test_rsa_roundtrip();
	test_rsa_1024();
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

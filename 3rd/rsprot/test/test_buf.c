/* Round-trip tests for RsprotBuf: every read/write pair writes then reads back
 * the same value, plus a few known-good byte sequences transcribed by hand
 * from RSProt's own doc comments so a sign or byte-order slip shows as a
 * mismatch against a fixed expectation, not just "round-trips with itself". */

#include "rsprot.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg)                                                                                             \
	do {                                                                                                            \
		if (!(cond)) {                                                                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                                  \
			g_failures++;                                                                                          \
		}                                                                                                          \
	} while (0)

#define CHECK_EQ_I(a, b, msg)                                                                                         \
	do {                                                                                                            \
		long long _a = (long long)(a), _b = (long long)(b);                                                       \
		if (_a != _b) {                                                                                            \
			printf("FAIL %s:%d: %s (got %lld, want %lld)\n", __FILE__, __LINE__, msg, _a, _b);                    \
			g_failures++;                                                                                          \
		}                                                                                                          \
	} while (0)

static void test_bytes(void)
{
	uint8_t mem[64];
	RsprotBuf b;

	rsprot_buf_wrap(&b, mem, sizeof(mem));
	rsprot_p1(&b, 0xAB);
	rsprot_p1_alt1(&b, 10);
	rsprot_p1_alt2(&b, 10);
	rsprot_p1_alt3(&b, 10);
	rsprot_buf_wrap_read(&b, mem, b.wpos);
	CHECK_EQ_I(rsprot_g1(&b), 0xAB, "g1");
	CHECK_EQ_I(rsprot_g1_alt1(&b), 10, "g1alt1 roundtrip");
	CHECK_EQ_I(rsprot_g1_alt2(&b), 10, "g1alt2 roundtrip");
	CHECK_EQ_I(rsprot_g1_alt3(&b), 10, "g1alt3 roundtrip");

	/* Known byte for p1Alt1(10): 10 + 128 = 138 = 0x8A. */
	CHECK_EQ_I(mem[1], 0x8A, "p1Alt1 byte value");
	/* p1Alt2(10): -10 as unsigned byte = 246 = 0xF6. */
	CHECK_EQ_I(mem[2], 0xF6, "p1Alt2 byte value");
	/* p1Alt3(10): 128 - 10 = 118 = 0x76. */
	CHECK_EQ_I(mem[3], 0x76, "p1Alt3 byte value");
}

static void test_shorts(void)
{
	uint8_t mem[64];
	RsprotBuf b;
	int i;
	int32_t vals[] = { 0, 1, 255, 256, 12345, 65535, -1, -32768 };

	for (i = 0; i < 8; i++) {
		int32_t v = vals[i];
		rsprot_buf_wrap(&b, mem, sizeof(mem));
		rsprot_p2(&b, v);
		rsprot_p2_alt1(&b, v);
		rsprot_p2_alt2(&b, v);
		rsprot_p2_alt3(&b, v);
		rsprot_buf_wrap_read(&b, mem, b.wpos);
		CHECK_EQ_I(rsprot_g2(&b) & 0xFFFF, v & 0xFFFF, "g2 roundtrip");
		CHECK_EQ_I(rsprot_g2_alt1(&b) & 0xFFFF, v & 0xFFFF, "g2alt1 roundtrip");
		CHECK_EQ_I(rsprot_g2_alt2(&b) & 0xFFFF, v & 0xFFFF, "g2alt2 roundtrip");
		CHECK_EQ_I(rsprot_g2_alt3(&b) & 0xFFFF, v & 0xFFFF, "g2alt3 roundtrip");
	}

	/* IF_OPENSUB writes p2Alt3(interfaceId) per RSProt's encoder. Fixed
	 * known value: interfaceId=596 -> bytes (0xD4+? ) check via roundtrip
	 * above is sufficient; here just check the known encoding shape for a
	 * small value transcribed from JagexByteBufExtensions.kt's p2Alt3. */
	rsprot_buf_wrap(&b, mem, sizeof(mem));
	rsprot_p2_alt3(&b, 5);
	/* p2Alt3: writeByte(value + 128), writeByte(value >> 8) -> (133, 0) */
	CHECK_EQ_I(mem[0], 133, "p2Alt3(5) byte0");
	CHECK_EQ_I(mem[1], 0, "p2Alt3(5) byte1");
}

static void test_mediums_and_ints(void)
{
	uint8_t mem[64];
	RsprotBuf b;
	int32_t vals[] = { 0, 1, 0x7FFFFF, 0x800000, 0xABCDEF, -1 };
	int i;

	for (i = 0; i < 6; i++) {
		int32_t v = vals[i] & 0xFFFFFF;
		rsprot_buf_wrap(&b, mem, sizeof(mem));
		rsprot_p3(&b, v);
		rsprot_p3_alt1(&b, v);
		rsprot_p3_alt2(&b, v);
		rsprot_p3_alt3(&b, v);
		rsprot_buf_wrap_read(&b, mem, b.wpos);
		CHECK_EQ_I(rsprot_g3(&b) & 0xFFFFFF, v, "g3 roundtrip");
		CHECK_EQ_I(rsprot_g3_alt1(&b) & 0xFFFFFF, v, "g3alt1 roundtrip");
		CHECK_EQ_I(rsprot_g3_alt2(&b) & 0xFFFFFF, v, "g3alt2 roundtrip");
		CHECK_EQ_I(rsprot_g3_alt3(&b) & 0xFFFFFF, v, "g3alt3 roundtrip");
	}

	{
		int32_t vals4[] = { 0, 1, -1, 0x12345678, (int32_t)0x80000000 };
		for (i = 0; i < 5; i++) {
			int32_t v = vals4[i];
			rsprot_buf_wrap(&b, mem, sizeof(mem));
			rsprot_p4(&b, v);
			rsprot_p4_alt1(&b, v);
			rsprot_p4_alt2(&b, v);
			rsprot_p4_alt3(&b, v);
			rsprot_buf_wrap_read(&b, mem, b.wpos);
			CHECK_EQ_I(rsprot_g4(&b), v, "g4 roundtrip");
			CHECK_EQ_I(rsprot_g4_alt1(&b), v, "g4alt1 roundtrip");
			CHECK_EQ_I(rsprot_g4_alt2(&b), v, "g4alt2 roundtrip");
			CHECK_EQ_I(rsprot_g4_alt3(&b), v, "g4alt3 roundtrip");
		}
	}
}

static void test_smart(void)
{
	uint8_t mem[64];
	RsprotBuf b;

	rsprot_buf_wrap(&b, mem, sizeof(mem));
	rsprot_psmart1or2(&b, 5);
	rsprot_psmart1or2(&b, 200);
	rsprot_psmart1or2s(&b, -10);
	rsprot_psmart1or2s(&b, 5000);
	rsprot_psmart1or2null(&b, -1);
	rsprot_psmart1or2null(&b, 99);
	rsprot_psmart2or4(&b, 40000);
	rsprot_psmart2or4null(&b, -1);
	rsprot_psmart2or4null(&b, 99999);

	rsprot_buf_wrap_read(&b, mem, b.wpos);
	CHECK_EQ_I(rsprot_gsmart1or2(&b), 5, "smart1or2 small");
	CHECK_EQ_I(rsprot_gsmart1or2(&b), 200, "smart1or2 large");
	CHECK_EQ_I(rsprot_gsmart1or2s(&b), -10, "smart1or2s small");
	CHECK_EQ_I(rsprot_gsmart1or2s(&b), 5000, "smart1or2s large");
	CHECK_EQ_I(rsprot_gsmart1or2null(&b), -1, "smart1or2null null");
	CHECK_EQ_I(rsprot_gsmart1or2null(&b), 99, "smart1or2null value");
	CHECK_EQ_I(rsprot_gsmart2or4(&b), 40000, "smart2or4");
	CHECK_EQ_I(rsprot_gsmart2or4null(&b), -1, "smart2or4null null");
	CHECK_EQ_I(rsprot_gsmart2or4null(&b), 99999, "smart2or4null value");
	CHECK(rsprot_buf_ok(&b), "smart chain no errors");
}

static void test_varint_and_type(void)
{
	uint8_t mem[64];
	RsprotBuf b;
	int32_t vals[] = { 0, 1, 127, 128, 16384, 1000000, -1 };
	int i;

	for (i = 0; i < 7; i++) {
		rsprot_buf_wrap(&b, mem, sizeof(mem));
		rsprot_pvarint2(&b, vals[i]);
		rsprot_buf_wrap_read(&b, mem, b.wpos);
		CHECK_EQ_I(rsprot_gvarint2(&b), vals[i], "varint2 roundtrip");
	}

	{
		int32_t svals[] = { 0, 1, -1, 12345, -12345 };
		for (i = 0; i < 5; i++) {
			rsprot_buf_wrap(&b, mem, sizeof(mem));
			rsprot_pvarint2s(&b, svals[i]);
			rsprot_buf_wrap_read(&b, mem, b.wpos);
			CHECK_EQ_I(rsprot_gvarint2s(&b), svals[i], "varint2s roundtrip");
		}
	}

	{
		int32_t tvals[] = { 0, 100, 251, 252, 500, 1023 };
		for (i = 0; i < 6; i++) {
			rsprot_buf_wrap(&b, mem, sizeof(mem));
			rsprot_ptype(&b, tvals[i]);
			rsprot_buf_wrap_read(&b, mem, b.wpos);
			CHECK_EQ_I(rsprot_gtype(&b), tvals[i], "type roundtrip");
		}
	}
}

static void test_strings(void)
{
	uint8_t mem[64];
	RsprotBuf b;
	int32_t len;
	const char *s;

	rsprot_buf_wrap(&b, mem, sizeof(mem));
	rsprot_pjstr(&b, "hello");
	rsprot_pjstrnull(&b, NULL);
	rsprot_pjstrnull(&b, "world");
	rsprot_pjstr2(&b, "abc");

	rsprot_buf_wrap_read(&b, mem, b.wpos);
	s = rsprot_gjstr(&b, &len);
	CHECK(s != NULL && len == 5 && memcmp(s, "hello", 5) == 0, "gjstr");

	s = rsprot_gjstrnull(&b, &len);
	CHECK(s == NULL && rsprot_buf_ok(&b), "gjstrnull null case");

	s = rsprot_gjstrnull(&b, &len);
	CHECK(s != NULL && len == 5 && memcmp(s, "world", 5) == 0, "gjstrnull value case");

	s = rsprot_gjstr2(&b, &len);
	CHECK(s != NULL && len == 3 && memcmp(s, "abc", 3) == 0, "gjstr2");
}

static void test_bits(void)
{
	uint8_t mem[64];
	RsprotBuf b;

	rsprot_buf_alloc(&b, 4);
	rsprot_buf_bit_begin(&b);
	rsprot_pbits(&b, 1, 1);
	rsprot_pbits(&b, 3, 5);
	rsprot_pbits(&b, 12, 4000);
	rsprot_pbits(&b, 32, 0x12345678);
	rsprot_buf_bit_end(&b);

	CHECK(rsprot_buf_ok(&b), "bit writes ok");

	memcpy(mem, b.data, (size_t)b.wpos);
	{
		int32_t total_len = b.wpos;
		rsprot_buf_free(&b);
		rsprot_buf_wrap_read(&b, mem, total_len);
	}
	rsprot_buf_bit_begin(&b);
	CHECK_EQ_I(rsprot_gbits(&b, 1), 1, "gbits 1");
	CHECK_EQ_I(rsprot_gbits(&b, 3), 5, "gbits 3");
	CHECK_EQ_I(rsprot_gbits(&b, 12), 4000, "gbits 12");
	CHECK_EQ_I(rsprot_gbits(&b, 32), (int32_t)0x12345678, "gbits 32");
	rsprot_buf_bit_end(&b);
	CHECK(rsprot_buf_ok(&b), "bit reads ok");

	/* Byte alignment: 1+3+12+32 = 48 bits = 6 bytes exactly, so bit_end
	 * should not have had to pad. */
	CHECK_EQ_I(b.wpos, 6, "bit section byte-aligned length");
}

static void test_bits_padding(void)
{
	/* 5 bits should pad to 1 byte on close, verifying zero-fill of the
	 * unused tail bits (RSProt's BitBuf.close()). */
	uint8_t mem[16];
	RsprotBuf b;

	rsprot_buf_wrap(&b, mem, sizeof(mem));
	rsprot_buf_bit_begin(&b);
	rsprot_pbits(&b, 5, 0x1F);
	rsprot_buf_bit_end(&b);
	CHECK_EQ_I(b.wpos, 1, "5-bit section pads to 1 byte");
	CHECK_EQ_I(mem[0], 0xF8, "padding bits are zero");
}

static void test_combined_id(void)
{
	uint8_t mem[16];
	RsprotBuf b;
	int32_t c;

	c = rsprot_combined_id(596, 12);
	CHECK_EQ_I(rsprot_combined_interface(c), 596, "combined interface");
	CHECK_EQ_I(rsprot_combined_component(c), 12, "combined component");

	c = rsprot_combined_id(-1, -1);
	CHECK_EQ_I(rsprot_combined_interface(c), -1, "combined -1 interface");
	CHECK_EQ_I(rsprot_combined_component(c), -1, "combined -1 component");

	rsprot_buf_wrap(&b, mem, sizeof(mem));
	rsprot_pcombined_id_alt3(&b, rsprot_combined_id(100, 200));
	rsprot_buf_wrap_read(&b, mem, b.wpos);
	c = rsprot_gcombined_id_alt3(&b);
	CHECK_EQ_I(rsprot_combined_interface(c), 100, "combined alt3 roundtrip interface");
	CHECK_EQ_I(rsprot_combined_component(c), 200, "combined alt3 roundtrip component");
}

static void test_crc32(void)
{
	/* CRC-32 of "123456789" is the standard check value 0xCBF43926. */
	uint32_t c = rsprot_crc32("123456789", 9);
	CHECK_EQ_I(c, 0xCBF43926u, "crc32 check value");
}

static void test_err_sticky(void)
{
	uint8_t mem[2];
	RsprotBuf b;

	rsprot_buf_wrap_read(&b, mem, 1);
	rsprot_g1(&b);
	CHECK(rsprot_buf_ok(&b), "one byte reads fine");
	(void)rsprot_g1(&b); /* underflow */
	CHECK(!rsprot_buf_ok(&b), "underflow sets err");
	CHECK_EQ_I(rsprot_g1(&b), 0, "reads past err return 0");
	rsprot_buf_clear_err(&b);
	CHECK(rsprot_buf_ok(&b), "clear_err resets");
}

static void test_growable_alloc(void)
{
	RsprotBuf b;
	int i;

	rsprot_buf_alloc(&b, 4);
	for (i = 0; i < 1000; i++)
		rsprot_p4(&b, i);
	CHECK(rsprot_buf_ok(&b), "grown buffer stays ok");
	CHECK_EQ_I(b.wpos, 4000, "grown buffer wrote everything");
	b.rpos = 0;
	for (i = 0; i < 1000; i++)
		CHECK_EQ_I(rsprot_g4(&b), i, "grown buffer round trip");
	rsprot_buf_free(&b);
}

int main(void)
{
	test_bytes();
	test_shorts();
	test_mediums_and_ints();
	test_smart();
	test_varint_and_type();
	test_strings();
	test_bits();
	test_bits_padding();
	test_combined_id();
	test_crc32();
	test_err_sticky();
	test_growable_alloc();

	if (g_failures) {
		printf("%d failures\n", g_failures);
		return 1;
	}
	printf("all rsprot buf tests passed\n");
	return 0;
}

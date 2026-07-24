/*
 * Write-primitive tests: CRCs, XTEA, gzip and bzip2.
 *
 * The bzip2 encoder is the risky one — it is our own implementation, not
 * vendored — so it is checked against this tree's independent bunzip decoder over
 * inputs chosen to exercise each stage of the pipeline (RLE1 runs, single-symbol
 * alphabets, multi-table selection). Interoperability with the reference `bzip2`
 * binary is covered separately by test_bzip_interop.sh, which this test cannot do
 * itself without shelling out.
 */

#include "rscache_test.h"

#include <rscache.h>
#include <xteas.h>

#include <stdlib.h>

static void
test_crc32(void)
{
    RSCACHE_TEST_GROUP("crc32 (reflected, ISO-HDLC)");

    /* Standard published vectors for CRC-32/ISO-HDLC. */
    RSCACHE_CHECK_EQ(RSCache_Crc32Buffer((const uint8_t*)"", 0), 0x00000000u);
    RSCACHE_CHECK_EQ(RSCache_Crc32Buffer((const uint8_t*)"a", 1), 0xE8B7BE43u);
    RSCACHE_CHECK_EQ(RSCache_Crc32Buffer((const uint8_t*)"abc", 3), 0x352441C2u);
    RSCACHE_CHECK_EQ(RSCache_Crc32Buffer((const uint8_t*)"123456789", 9), 0xCBF43926u);

    /* Incremental must equal one-shot, since reference tables are built by
     * streaming over an archive. */
    const char* text = "the quick brown fox jumps over the lazy dog";
    size_t len = strlen(text);
    uint32_t one_shot = RSCache_Crc32Buffer((const uint8_t*)text, len);
    uint32_t incremental = 0;
    for( size_t i = 0; i < len; i++ )
        incremental = RSCache_Crc32(incremental, (const uint8_t*)text + i, 1);
    RSCACHE_CHECK_EQ(incremental, one_shot);
}

static void
test_crc32_bzip2(void)
{
    RSCACHE_TEST_GROUP("crc32 (unreflected, bzip2 variant)");

    /* Published vector for CRC-32/BZIP2 over "123456789". */
    uint32_t crc = RSCache_Crc32Bzip2(0xFFFFFFFFu, (const uint8_t*)"123456789", 9);
    RSCACHE_CHECK_EQ(~crc, 0xFC891918u);

    /* The two variants must not agree — if they did, one of the tables is wrong
     * and reference-table CRCs would silently be the bzip2 flavour. */
    uint32_t reflected = RSCache_Crc32Buffer((const uint8_t*)"123456789", 9);
    RSCACHE_CHECK(reflected != ~crc);
}

static void
test_xtea(void)
{
    RSCACHE_TEST_GROUP("xtea encrypt/decrypt");

    int32_t key[4] = { 0x11223344, 0x55667788, (int32_t)0x99aabbcc, (int32_t)0xddeeff00 };

    /* Exact multiple of the 8-byte block. */
    {
        char original[16];
        for( int i = 0; i < 16; i++ )
            original[i] = (char)(i * 7 + 1);

        char work[16];
        memcpy(work, original, sizeof(work));

        xteas_encrypt(work, sizeof(work), key);
        RSCACHE_CHECK(memcmp(work, original, sizeof(work)) != 0);

        xteas_decrypt(work, sizeof(work), key);
        RSCACHE_CHECK_BYTES_EQ(work, original, 16);
    }

    /* A trailing partial block must pass through untouched, and the round trip
     * must still reproduce the input — map squares are not block-aligned. */
    {
        char original[21];
        for( int i = 0; i < 21; i++ )
            original[i] = (char)(200 - i * 3);

        char work[21];
        memcpy(work, original, sizeof(work));

        xteas_encrypt(work, sizeof(work), key);
        /* Bytes 16..20 are the partial block. */
        RSCACHE_CHECK_BYTES_EQ(work + 16, original + 16, 5);

        xteas_decrypt(work, sizeof(work), key);
        RSCACHE_CHECK_BYTES_EQ(work, original, 21);
    }

    /* A different key must not decrypt. */
    {
        char work[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        int32_t other[4] = { 1, 2, 3, 4 };
        xteas_encrypt(work, sizeof(work), key);
        xteas_decrypt(work, sizeof(work), other);
        static const char expected[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        bool same = memcmp(work, expected, 8) == 0;
        RSCACHE_CHECK(!same);
    }
}

static void
check_gzip_roundtrip(
    const uint8_t* data,
    int length,
    const char* label)
{
    rscache_test_group = label;

    uint32_t bound = RSCache_CompressionGzipCompressBound(length);
    uint8_t* packed = malloc(bound);
    RSCACHE_CHECK(packed != NULL);
    if( !packed )
        return;

    uint32_t packed_size = RSCache_CompressionGzipCompress(packed, (int)bound, data, length);
    RSCACHE_CHECK(packed_size > 0);

    /* The decompressor identifies the stream by its magic, so the framing has to
     * be right, not just the deflate body. */
    RSCACHE_CHECK_EQ(packed[0], 0x1f);
    RSCACHE_CHECK_EQ(packed[1], 0x8b);

    /* The footer's ISIZE is what RSCache_CompressionGzipUncompressedSize reads. */
    RSCACHE_CHECK_EQ(RSCache_CompressionGzipUncompressedSize(packed, (int)packed_size),
                     (uint32_t)length);

    uint8_t* restored = malloc(length > 0 ? (size_t)length : 1);
    RSCACHE_CHECK(restored != NULL);
    if( restored )
    {
        uint32_t restored_size =
            RSCache_CompressionGzipDecompress(restored, length, packed, (int)packed_size, 0);
        RSCACHE_CHECK_EQ(restored_size, (uint32_t)length);
        if( length > 0 )
            RSCACHE_CHECK_BYTES_EQ(restored, data, length);
        free(restored);
    }

    free(packed);
}

static void
check_bzip_roundtrip(
    const uint8_t* data,
    int length,
    const char* label)
{
    rscache_test_group = label;

    uint32_t bound = RSCache_CompressionBzipCompressBound(length);
    uint8_t* packed = malloc(bound);
    RSCACHE_CHECK(packed != NULL);
    if( !packed )
        return;

    uint32_t packed_size = RSCache_CompressionBzipCompress(packed, (int)bound, data, length);
    RSCACHE_CHECK(packed_size > 0);

    if( packed_size > 0 )
    {
        /* The magic must be absent: the client prepends "BZh1" itself, so a
         * payload that still carried it would decompress to garbage. */
        bool starts_with_magic = packed_size >= 4 && packed[0] == 'B' && packed[1] == 'Z' &&
                                 packed[2] == 'h' && packed[3] == '1';
        RSCACHE_CHECK(!starts_with_magic);

        uint8_t* restored = malloc((size_t)length + 64);
        RSCACHE_CHECK(restored != NULL);
        if( restored )
        {
            memset(restored, 0xAA, (size_t)length + 64);
            RSCache_CompressionBzipDecompress(restored, length, packed, (int)packed_size);
            RSCACHE_CHECK_BYTES_EQ(restored, data, length);
            free(restored);
        }
    }

    free(packed);
}

static void
test_gzip(void)
{
    RSCACHE_TEST_GROUP("gzip");

    static const uint8_t text[] = "The quick brown fox jumps over the lazy dog. "
                                  "The quick brown fox jumps over the lazy dog.";
    check_gzip_roundtrip(text, (int)sizeof(text) - 1, "gzip: repetitive text");

    /* Incompressible: deflate must still frame correctly when it cannot win. */
    uint8_t noise[4096];
    uint32_t state = 12345u;
    for( size_t i = 0; i < sizeof(noise); i++ )
    {
        state = state * 1103515245u + 12345u;
        noise[i] = (uint8_t)(state >> 16);
    }
    check_gzip_roundtrip(noise, (int)sizeof(noise), "gzip: pseudorandom");

    /* Long single-byte run. */
    uint8_t zeros[8192];
    memset(zeros, 0, sizeof(zeros));
    check_gzip_roundtrip(zeros, (int)sizeof(zeros), "gzip: all zeros");

    /* Single byte. */
    static const uint8_t one[] = { 0x42 };
    check_gzip_roundtrip(one, 1, "gzip: one byte");
}

static void
test_bzip(void)
{
    RSCACHE_TEST_GROUP("bzip2");

    /* Short, mixed alphabet. */
    static const uint8_t text[] = "The quick brown fox jumps over the lazy dog.";
    check_bzip_roundtrip(text, (int)sizeof(text) - 1, "bzip2: short text");

    /* Repetitive, so RLE1 fires and the MTF stream is mostly zero runs. */
    {
        uint8_t repeated[3000];
        for( size_t i = 0; i < sizeof(repeated); i++ )
            repeated[i] = (uint8_t)("abcabcabcdd"[i % 11]);
        check_bzip_roundtrip(repeated, (int)sizeof(repeated), "bzip2: repetitive");
    }

    /* Single distinct byte over a long span: exercises RLE1's 255+4 cap and the
     * smallest possible alphabet (one literal plus the run codes). */
    {
        uint8_t same[5000];
        memset(same, 'Z', sizeof(same));
        check_bzip_roundtrip(same, (int)sizeof(same), "bzip2: single symbol");
    }

    /* Exactly four identical bytes: the RLE1 boundary where a length byte starts
     * being emitted, and the classic off-by-one in that transform. */
    {
        static const uint8_t four[] = { 'a', 'a', 'a', 'a' };
        check_bzip_roundtrip(four, 4, "bzip2: four identical");
        static const uint8_t three[] = { 'a', 'a', 'a' };
        check_bzip_roundtrip(three, 3, "bzip2: three identical");
        static const uint8_t five[] = { 'a', 'a', 'a', 'a', 'a' };
        check_bzip_roundtrip(five, 5, "bzip2: five identical");
    }

    /* Runs longer than one length byte can express, forcing RLE1 to split. */
    {
        uint8_t long_run[1000];
        memset(long_run, 'q', sizeof(long_run));
        check_bzip_roundtrip(long_run, (int)sizeof(long_run), "bzip2: 1000-byte run");
    }

    /* Pseudorandom: full 256-symbol alphabet, worst case for the sort and enough
     * volume to select more than two Huffman tables. */
    {
        uint8_t noise[20000];
        uint32_t state = 987654321u;
        for( size_t i = 0; i < sizeof(noise); i++ )
        {
            state = state * 1103515245u + 12345u;
            noise[i] = (uint8_t)(state >> 16);
        }
        check_bzip_roundtrip(noise, (int)sizeof(noise), "bzip2: pseudorandom 20k");
    }

    /* All 256 byte values present exactly once. */
    {
        uint8_t every[256];
        for( int i = 0; i < 256; i++ )
            every[i] = (uint8_t)i;
        check_bzip_roundtrip(every, 256, "bzip2: every byte value");
    }

    /* One byte. */
    {
        static const uint8_t one[] = { 0x7f };
        check_bzip_roundtrip(one, 1, "bzip2: one byte");
    }

    /* Capacity refusal rather than overrun. */
    {
        rscache_test_group = "bzip2: tight buffer";
        uint8_t tiny[4];
        static const uint8_t payload[] = "some data that will not fit in four bytes";
        uint32_t written = RSCache_CompressionBzipCompress(
            tiny, (int)sizeof(tiny), payload, (int)sizeof(payload) - 1);
        RSCACHE_CHECK_EQ(written, 0);
    }
}

static void
check_whirlpool(
    const char* message,
    const char* expected_hex)
{
    uint8_t digest[RSCACHE_WHIRLPOOL_DIGEST_SIZE];
    RSCache_Whirlpool((const uint8_t*)message, strlen(message), digest);

    char hex[RSCACHE_WHIRLPOOL_DIGEST_SIZE * 2 + 1];
    for( int i = 0; i < RSCACHE_WHIRLPOOL_DIGEST_SIZE; i++ )
        snprintf(hex + i * 2, 3, "%02X", digest[i]);

    RSCACHE_CHECK_STR_EQ(hex, expected_hex);
}

static void
test_whirlpool(void)
{
    RSCACHE_TEST_GROUP("whirlpool");

    /* Published vectors from the Whirlpool specification. Every table in the
     * implementation is derived rather than transcribed, so these vectors are
     * what certify the derivation — an error in the S-box mini-boxes, the
     * circulant coefficients or the round constants changes the digest
     * completely. */
    check_whirlpool(
        "",
        "19FA61D75522A4669B44E39C1D2E1726C530232130D407F89AFEE0964997F7A7"
        "3E83BE698B288FEBCF88E3E03C4F0757EA8964E59B63D93708B138CC42A66EB3");
    check_whirlpool(
        "abc",
        "4E2448A4C6F486BB16B6562C73B4020BF3043E3A731BCE721AE1B303D97E6D4C"
        "7181EEBDB6C57E277D0E34957114CBD6C797FC9D95D8B582D225292076D4EEF5");
    check_whirlpool(
        "The quick brown fox jumps over the lazy dog",
        "B97DE512E91E3828B40D2B0FDCE9CEB3C4A71F9BEA8D88E75C4FA854DF36725F"
        "D2B52EB6544EDCACD6F8BEDDFEA403CB55AE31F03AD62A5EF54E42EE82C3FB35");

    /* A message that forces the length field into a second block: 56 bytes
     * leaves no room for the 32-byte length after the 0x80 marker. */
    {
        char long_message[57];
        memset(long_message, 'a', sizeof(long_message) - 1);
        long_message[sizeof(long_message) - 1] = '\0';

        uint8_t digest[RSCACHE_WHIRLPOOL_DIGEST_SIZE];
        RSCache_Whirlpool((const uint8_t*)long_message, strlen(long_message), digest);

        /* Not a published vector, so assert the property that matters: a
         * two-block padding path must still produce a digest distinct from the
         * one-block case, i.e. it must not silently drop the tail. */
        uint8_t shorter[RSCACHE_WHIRLPOOL_DIGEST_SIZE];
        RSCache_Whirlpool((const uint8_t*)long_message, strlen(long_message) - 1, shorter);
        RSCACHE_CHECK(memcmp(digest, shorter, sizeof(digest)) != 0);
    }
}

int
main(void)
{
    printf("compression / checksum tests\n");
    test_crc32();
    test_crc32_bzip2();
    test_xtea();
    test_gzip();
    test_bzip();
    test_whirlpool();
    return rscache_test_report("compression");
}

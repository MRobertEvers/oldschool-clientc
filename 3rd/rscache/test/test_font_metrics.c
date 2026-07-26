/*
 * Dat2 font-metrics blob decode: OldSchool 257-byte (V1) vs RS2 263-byte (V2).
 */

#include "rscache_test.h"

#include <rscache.h>
#include <string.h>

static void
test_decode_v1_osrs_layout(void)
{
    RSCACHE_TEST_GROUP("V1 (257-byte OldSchool) layout");

    uint8_t blob[RSCACHE_DAT2_FONT_METRICS_V1_SIZE];
    memset(blob, 0, sizeof(blob));
    blob[(unsigned char)'A'] = 7;
    blob[(unsigned char)'B'] = 8;
    blob[(unsigned char)' '] = 4;
    blob[(unsigned char)'i'] = 3;
    blob[256] = 12;

    struct RSCache_Dat2FontMetrics m;
    RSCACHE_CHECK(RSCache_Dat2FontMetricsDecodeCodec(
        blob, (int)sizeof(blob), RSCACHE_CODEC_FONT_METRICS_V1, &m));
    RSCACHE_CHECK_EQ(m.advance[(unsigned char)'A'], 7);
    RSCACHE_CHECK_EQ(m.advance[(unsigned char)'B'], 8);
    RSCACHE_CHECK_EQ(m.advance[(unsigned char)' '], 4);
    RSCACHE_CHECK_EQ(m.advance[(unsigned char)'i'], 3);
    RSCACHE_CHECK_EQ(m.ascent, 12);
}

static void
test_decode_v2_rs2_layout(void)
{
    RSCACHE_TEST_GROUP("V2 (263-byte RS2) layout");

    uint8_t blob[RSCACHE_DAT2_FONT_METRICS_V2_SIZE];
    memset(blob, 0, sizeof(blob));
    /* Reserved lead-in; advances start at offset 2. */
    blob[0] = 0;
    blob[1] = 0;
    blob[2 + (unsigned char)'A'] = 9;
    blob[2 + (unsigned char)'B'] = 8;
    blob[2 + (unsigned char)' '] = 4;
    blob[2 + (unsigned char)'i'] = 4;
    /* Mislead: put wrong values at the V1 offsets so a V1 read would fail. */
    blob[(unsigned char)'B'] = 14;
    blob[(unsigned char)'i'] = 8;
    blob[(unsigned char)' '] = 0;
    blob[256] = 8;
    blob[258] = 12;
    blob[259] = 10;
    blob[260] = 6;
    blob[261] = 12;
    blob[262] = 4;

    struct RSCache_Dat2FontMetrics m;
    RSCACHE_CHECK(RSCache_Dat2FontMetricsDecodeCodec(
        blob, (int)sizeof(blob), RSCACHE_CODEC_FONT_METRICS_V2, &m));
    RSCACHE_CHECK_EQ(m.advance[(unsigned char)'A'], 9);
    RSCACHE_CHECK_EQ(m.advance[(unsigned char)'B'], 8);
    RSCACHE_CHECK_EQ(m.advance[(unsigned char)' '], 4);
    RSCACHE_CHECK_EQ(m.advance[(unsigned char)'i'], 4);
    RSCACHE_CHECK_EQ(m.ascent, 12);
}

static void
test_decode_truncated_blob(void)
{
    RSCACHE_TEST_GROUP("truncated blob returns false");

    uint8_t short_v1[100];
    uint8_t short_v2[200];
    memset(short_v1, 1, sizeof(short_v1));
    memset(short_v2, 1, sizeof(short_v2));

    struct RSCache_Dat2FontMetrics m;
    RSCACHE_CHECK(!RSCache_Dat2FontMetricsDecodeCodec(
        short_v1, (int)sizeof(short_v1), RSCACHE_CODEC_FONT_METRICS_V1, &m));
    RSCACHE_CHECK(!RSCache_Dat2FontMetricsDecodeCodec(
        short_v2, (int)sizeof(short_v2), RSCACHE_CODEC_FONT_METRICS_V2, &m));
}

static void
test_decode_via_profile(void)
{
    RSCACHE_TEST_GROUP("DecodeProfile picks codec from identity");

    uint8_t v2[RSCACHE_DAT2_FONT_METRICS_V2_SIZE];
    memset(v2, 0, sizeof(v2));
    v2[2 + (unsigned char)'W'] = 12;
    v2[258] = 12;

    struct RSCache rs643 = RSCache_ProfileForIdentity(
        RSCACHE_GAME_RS2, RSCACHE_EPOCH_DAT2, 643, RSCACHE_QUIRK_NONE);
    struct RSCache_Dat2FontMetrics m;
    RSCACHE_CHECK(RSCache_Dat2FontMetricsDecodeProfile(&rs643, v2, (int)sizeof(v2), &m));
    RSCACHE_CHECK_EQ(m.advance[(unsigned char)'W'], 12);
    RSCACHE_CHECK_EQ(m.ascent, 12);

    uint8_t v1[RSCACHE_DAT2_FONT_METRICS_V1_SIZE];
    memset(v1, 0, sizeof(v1));
    v1[(unsigned char)'W'] = 12;
    v1[256] = 12;

    struct RSCache osrs = RSCache_ProfileForIdentity(
        RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 230, RSCACHE_QUIRK_NONE);
    RSCACHE_CHECK(RSCache_Dat2FontMetricsDecodeProfile(&osrs, v1, (int)sizeof(v1), &m));
    RSCACHE_CHECK_EQ(m.advance[(unsigned char)'W'], 12);
    RSCACHE_CHECK_EQ(m.ascent, 12);
}

int
main(void)
{
    printf("font metrics decode tests\n");
    test_decode_v1_osrs_layout();
    test_decode_v2_rs2_layout();
    test_decode_truncated_blob();
    test_decode_via_profile();
    return rscache_test_report("font_metrics");
}

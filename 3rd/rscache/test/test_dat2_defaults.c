#include "rscache_test.h"

#include "datatypes/dat2_defaults.h"

#include <stdint.h>
#include <string.h>

/*
 * The defaults table (OldSchool idx17, RS2 idx28). See docs/CACHE_INDEX_16_17.md.
 *
 * Both records below are the real bytes, not constructions. The osrs239 one is
 * all 83 bytes of idx17 group 3; the RS2 one is the first 47 of idx28 group 3,
 * which is exactly as far as the decoder gets before it has to decline.
 */

/* cache.osrs239, idx17 group 3, complete. */
static const uint8_t k_osrs239_group3[] = {
    /* opcode 1: a 24-bit value the client reads and discards */
    0x01, 0x00, 0x01, 0x39,
    /* opcode 2: eleven bigsmart sprite ids */
    0x02,
    0x00, 0xa9, /* compass          169 */
    0x01, 0xa8, /* mapedge          424 */
    0x01, 0x3d, /* mapscene         317 */
    0x01, 0xb7, /* headicons_pk     439 */
    0x01, 0xb8, /* headicons_prayer 440 */
    0x01, 0xb9, /* headicons_hint   441 */
    0x01, 0xa6, /* mapmarker        422 */
    0x01, 0x2b, /* cross            299 */
    0x01, 0x2c, /* mapdots          300 */
    0x01, 0x3c, /* scrollbar        316 */
    0x01, 0xa7, /* mod_icons        423 */
    /* opcode 3: three 5-stop 24-bit ramps */
    0x03,
    0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    /* opcode 5: two int32 model ids */
    0x05, 0x00, 0x00, 0xe0, 0x22, 0x00, 0x00, 0xe0, 0x23,
    /* terminator */
    0x00,
};

/* cache.rs727_preeoc, idx28 group 3, first 47 bytes. */
static const uint8_t k_rs727_group3_head[] = {
    0x03, 0x06, 0x01, 0x00, 0x00, 0xff, 0xec, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x3c, 0x00,
    0x00, 0x00, 0x50, 0x02, 0x80, 0x00, 0xb7, 0x98, 0x05, 0x00, 0x02, 0xe8,
    0x06, 0x00, 0x03, 0x8a, 0x07, 0x1a, 0x8e, 0x00, 0x19, 0x1a, 0x8e,
};

static void
test_osrs239_record(void)
{
    struct RSCache_Dat2Defaults rec;
    static const int want_ids[RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT] = {
        169, 424, 317, 439, 440, 441, 422, 299, 300, 316, 423,
    };

    RSCACHE_TEST_GROUP("osrs239 idx17 group 3 decodes and re-encodes exactly");

    RSCACHE_CHECK(RSCache_Dat2DefaultsDecode(
        k_osrs239_group3, (int)sizeof(k_osrs239_group3), &rec) == 1);

    /* Every byte consumed, or the terminator was data. */
    RSCACHE_CHECK_EQ(rec.consumed, (int)sizeof(k_osrs239_group3));

    RSCACHE_CHECK_EQ(rec.opcode_count, 4);
    RSCACHE_CHECK_EQ(rec.opcode_order[0], 1);
    RSCACHE_CHECK_EQ(rec.opcode_order[1], 2);
    RSCACHE_CHECK_EQ(rec.opcode_order[2], 3);
    RSCACHE_CHECK_EQ(rec.opcode_order[3], 5);

    RSCACHE_CHECK_EQ(rec.sprite_opcode, 2);
    for( int i = 0; i < RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT; i++ )
        RSCACHE_CHECK_EQ(rec.sprite_ids[i], want_ids[i]);

    /* The whole point of the table, and the reason this test exists: the compass
     * id is data, not something compiled into a client. */
    RSCACHE_CHECK_EQ(rec.sprite_ids[0], 169);
    RSCACHE_CHECK(strcmp(RSCache_Dat2DefaultsSpriteSlotNames[0], "compass") == 0);

    RSCACHE_CHECK_EQ(rec.has_legacy_value, 1);
    RSCACHE_CHECK_EQ(rec.legacy_value, 0x000139);

    RSCACHE_CHECK_EQ(rec.has_ramps, 1);
    RSCACHE_CHECK_EQ(rec.ramps[0][1], 0xff0000);
    RSCACHE_CHECK_EQ(rec.ramps[1][1], 0x00ff00);
    RSCACHE_CHECK_EQ(rec.ramps[2][1], 0x0000ff);

    RSCACHE_CHECK_EQ(rec.has_models, 1);
    RSCACHE_CHECK_EQ(rec.model_ids[0], 57378);
    RSCACHE_CHECK_EQ(rec.model_ids[1], 57379);

    RSCACHE_CHECK(RSCache_Dat2DefaultsRoundTrips(
        &rec, k_osrs239_group3, (int)sizeof(k_osrs239_group3)) == 1);
}

static void
test_rs727_declines(void)
{
    struct RSCache_Dat2Defaults rec;

    RSCACHE_TEST_GROUP("RS2 idx28 group 3 is a different schema and is declined");

    /*
     * It opens on opcode 3, which this record does have — so the decoder gets 45
     * bytes in before the disagreement shows, and the byte it lands on is 142.
     * Declining there is the whole safety property: a decoder that carried on
     * would write a misread RS2 record into an OldSchool-shaped struct and
     * cachepack would believe it.
     */
    RSCACHE_CHECK(RSCache_Dat2DefaultsDecode(
        k_rs727_group3_head, (int)sizeof(k_rs727_group3_head), &rec) == 0);
}

static void
test_trailing_bytes_decline(void)
{
    struct RSCache_Dat2Defaults rec;
    uint8_t padded[sizeof(k_osrs239_group3) + 1];

    RSCACHE_TEST_GROUP("a record with bytes after the terminator is declined");

    memcpy(padded, k_osrs239_group3, sizeof(k_osrs239_group3));
    padded[sizeof(k_osrs239_group3)] = 0x77;
    RSCACHE_CHECK(RSCache_Dat2DefaultsDecode(padded, (int)sizeof(padded), &rec) == 0);
}

static void
test_colours(void)
{
    struct RSCache_Dat2DefaultsColours col;
    /* idx17 group 1 file 3615: one flat colour. */
    static const uint8_t flat[] = { 0x63, 0x8f, 0xe6 };
    /* file 15939: two stops, 32 apart. */
    static const uint8_t pair[] = { 0xec, 0xd2, 0xb4, 0x20, 0x23, 0x72, 0x24 };
    /* file 5012's first four stops: red pulsing to pink. */
    static const uint8_t pulse[] = { 0xff, 0x00, 0x00, 0x07, 0xf6, 0xa1, 0xfd,
                                     0x01, 0xff, 0x00, 0x00, 0x07, 0xf6, 0xa1, 0xfd };
    /* 4n-1 is the only legal size, so 4 bytes cannot be this record. */
    static const uint8_t bad[] = { 0x00, 0x01, 0x02, 0x03 };

    RSCACHE_TEST_GROUP("group 1 colour records decode and re-encode exactly");

    RSCACHE_CHECK(RSCache_Dat2DefaultsColoursDecode(flat, (int)sizeof(flat), &col) == 1);
    RSCACHE_CHECK_EQ(col.stop_count, 1);
    RSCACHE_CHECK_EQ(col.colours[0], 0x638fe6);
    RSCACHE_CHECK(RSCache_Dat2DefaultsColoursRoundTrips(&col, flat, (int)sizeof(flat)) == 1);

    RSCACHE_CHECK(RSCache_Dat2DefaultsColoursDecode(pair, (int)sizeof(pair), &col) == 1);
    RSCACHE_CHECK_EQ(col.stop_count, 2);
    RSCACHE_CHECK_EQ(col.colours[0], 0xecd2b4);
    RSCACHE_CHECK_EQ(col.intervals[0], 32);
    RSCACHE_CHECK_EQ(col.colours[1], 0x237224);
    RSCACHE_CHECK(RSCache_Dat2DefaultsColoursRoundTrips(&col, pair, (int)sizeof(pair)) == 1);

    RSCACHE_CHECK(RSCache_Dat2DefaultsColoursDecode(pulse, (int)sizeof(pulse), &col) == 1);
    RSCACHE_CHECK_EQ(col.stop_count, 4);
    RSCACHE_CHECK_EQ(col.intervals[0], 7);
    RSCACHE_CHECK_EQ(col.intervals[1], 1);
    RSCACHE_CHECK(RSCache_Dat2DefaultsColoursRoundTrips(&col, pulse, (int)sizeof(pulse)) == 1);

    RSCACHE_CHECK(RSCache_Dat2DefaultsColoursDecode(bad, (int)sizeof(bad), &col) == 0);
}

int
main(void)
{
    printf("dat2 defaults tests\n");
    test_osrs239_record();
    test_rs727_declines();
    test_trailing_bytes_decline();
    test_colours();
    RSCACHE_CHECK(rscache_test_checks > 0);
    return rscache_test_report("dat2 defaults");
}

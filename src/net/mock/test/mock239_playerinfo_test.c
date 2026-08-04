/*
 * PLAYER_INFO v5 round-trip.
 *
 * The decoder below is a transcription of RSProt's own reference client
 * (osrs-239-desktop/src/test/.../info/PlayerInfoClient.kt) — the same file the
 * encoder was written against, but read in the opposite direction and written
 * independently of it. That is what makes this a test rather than a restatement:
 * it walks the four bit sections the way a client does, tracks the same
 * `skipped` counter, and asserts the same two things the client asserts —
 * that every section ends with `skipped == 0`, and that the extended-info
 * blocks it was promised are the ones that arrive.
 *
 * What it cannot check is whether the FORMAT is right; only a real client can
 * say that. What it does check is that the encoder is self-consistent with the
 * one written statement of the format, which is where off-by-ones in the skip
 * runs and misaligned bit sections live — and those are the failures that
 * produce a stream decoding into the wrong players rather than an error.
 *
 * THE LIMIT OF THAT, demonstrated: this test passed while the encoder wrote an
 * ABSOLUTE coordinate into a field the client adds as a DELTA. A round-trip
 * proves a value survives the bits; it cannot prove the value meant the right
 * thing, because both ends of the round trip are this file's own reading. Only
 * a real client caught it, as a world that built and then went black once the
 * player had been displaced off the loaded scene.
 *
 * Nothing in this repo was masking it. The C client REFUSES PLAYER_INFO at
 * revision 239 (osrs239_parse.c returns -1 rather than decode a moved layout),
 * and the classic rev-230 decoder is a different codec — its local-player op 3
 * is `2 bits level, 7 bits scene x, 7 bits scene z`, an absolute placement
 * inside the scene, not a 30-bit delta. There is no shared reader in which one
 * semantics could paper over the other.
 *
 *     make -C src test-mock239-playerinfo
 */

#include "net/mock/mock239_playerinfo.h"

#include <rsareabuf.h>

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, ...)                                                                   \
    do                                                                                     \
    {                                                                                      \
        if( !(cond) )                                                                      \
        {                                                                                  \
            fprintf(stderr, "  FAIL ");                                                    \
            fprintf(stderr, __VA_ARGS__);                                                  \
            fprintf(stderr, "\n");                                                         \
            g_failures++;                                                                  \
        }                                                                                  \
        else                                                                               \
        {                                                                                  \
            fprintf(stderr, "  ok   ");                                                    \
            fprintf(stderr, __VA_ARGS__);                                                  \
            fprintf(stderr, "\n");                                                         \
        }                                                                                  \
    } while( 0 )

#define SLOTS MOCK239_PLAYER_SLOTS
#define LOCAL_INDEX 42

/* readStationary: 2-bit type selecting the run width. */
static int
read_stationary(struct RSAreaBuf* buf)
{
    int type = rsab_gbit(buf, 2);
    switch( type )
    {
    case 0:
        return 0;
    case 1:
        return rsab_gbit(buf, 5);
    case 2:
        return rsab_gbit(buf, 8);
    default:
        return rsab_gbit(buf, 11);
    }
}

struct Decoded
{
    int local_index;
    int32_t init_coord;
    int init_low_res_entries;

    int hi_res_seen;
    int hi_res_extended;
    int32_t hi_res_coord;
    int hi_res_teleported;

    int low_res_skipped;   /* players covered by section 4 */
    int section_underrun;  /* a section ended with skipped != 0 */

    int ext_flag;
    int ext_appearance_len;
    uint8_t ext_appearance[256];
};

static void
decode_init(struct RSAreaBuf* buf, struct Decoded* out)
{
    rsab_bits(buf);
    out->init_coord = rsab_gbit(buf, 30);
    out->init_low_res_entries = 0;
    for( int idx = 1; idx < SLOTS; idx++ )
    {
        if( idx == out->local_index )
            continue;
        (void)rsab_gbit(buf, 18);
        out->init_low_res_entries++;
    }
    rsab_bytes(buf);
}

/**
 * One section of the bit stream.
 *
 * `entries` is how many players the client would iterate here. Returns the
 * number it consumed; a section that ends mid-run sets `section_underrun`,
 * which is precisely the client-side exception this test exists to prevent.
 */
static int
decode_section(
    struct RSAreaBuf* buf,
    int entries,
    struct Decoded* out,
    int high_resolution)
{
    int skipped = 0;
    int consumed = 0;

    rsab_bits(buf);
    for( int i = 0; i < entries; i++ )
    {
        consumed++;
        if( skipped > 0 )
        {
            skipped--;
            continue;
        }
        if( rsab_gbit(buf, 1) == 0 )
        {
            skipped = read_stationary(buf);
            continue;
        }
        if( high_resolution )
        {
            int extended = rsab_gbit(buf, 1);
            int opcode = rsab_gbit(buf, 2);
            out->hi_res_seen++;
            out->hi_res_extended = extended;
            if( opcode == 3 )
            {
                int far = rsab_gbit(buf, 1);
                out->hi_res_teleported = 1;
                out->hi_res_coord = far ? rsab_gbit(buf, 30) : rsab_gbit(buf, 12);
            }
            else if( opcode == 0 )
            {
                out->hi_res_teleported = 0;
            }
        }
    }
    if( skipped != 0 )
        out->section_underrun = 1;
    rsab_bytes(buf);
    return consumed;
}

/**
 * The same walk, but reading the crowd from section 3 -- which is where the
 * client looks from the second tick onward, once it has set a cycle bit on
 * everyone it skipped. Its own function rather than a flag, so the two orders
 * are visibly different things.
 */
static void
decode_tick_late(struct RSAreaBuf* buf, struct Decoded* out, int expect_extended)
{
    int const low_res_count = SLOTS - 1 - 1;

    decode_section(buf, 1, out, 1);
    decode_section(buf, 0, out, 1);
    out->low_res_skipped = decode_section(buf, low_res_count, out, 0);
    decode_section(buf, 0, out, 0);

    if( expect_extended )
    {
        out->ext_flag = rsab_g1(buf);
        out->ext_appearance_len = (128 - rsab_g1(buf)) & 0xff;
        for( int i = 0; i < out->ext_appearance_len && i < (int)sizeof(out->ext_appearance);
             i++ )
            out->ext_appearance[i] = (uint8_t)((rsab_g1(buf) - 128) & 0xff);
    }
}

static void
decode_tick(struct RSAreaBuf* buf, struct Decoded* out, int expect_extended)
{
    int const low_res_count = SLOTS - 1 - 1;

    /* Section 1: high resolution, active. One entry — the local player. */
    decode_section(buf, 1, out, 1);
    /* Sections 2 and 3 are empty but still open a reader each. */
    decode_section(buf, 0, out, 1);
    decode_section(buf, 0, out, 0);
    /* Section 4: low resolution, active. */
    out->low_res_skipped = decode_section(buf, low_res_count, out, 0);

    if( expect_extended )
    {
        out->ext_flag = rsab_g1(buf);
        out->ext_appearance_len = (128 - rsab_g1(buf)) & 0xff;
        for( int i = 0; i < out->ext_appearance_len && i < (int)sizeof(out->ext_appearance);
             i++ )
            out->ext_appearance[i] = (uint8_t)((rsab_g1(buf) - 128) & 0xff);
    }
}

int
main(void)
{
    static uint8_t storage[16 * 1024];
    struct RSAreaBuf buf;
    struct Decoded got;
    /* A coord the packing cannot accidentally reproduce: distinct level, x
     * and z, none of them zero and none a multiple of the others. */
    int32_t const coord = (1 << 28) | (3222 << 14) | 3218;
    uint8_t appearance[64];

    for( int i = 0; i < (int)sizeof(appearance); i++ )
        appearance[i] = (uint8_t)(i * 7 + 1);

    fprintf(stderr, "mock239-playerinfo: init block\n");
    memset(&got, 0, sizeof(got));
    got.local_index = LOCAL_INDEX;
    rsab_wrap(&buf, storage, sizeof(storage));
    mock239_playerinfo_write_init(&buf, LOCAL_INDEX, coord);
    {
        size_t written = rsab_len(&buf);
        /* 30 bits + 2046 * 18 bits = 36858 bits = 4608 bytes (rounded up). */
        CHECK(written == 4608, "init block is %zu bytes (30 + 2046*18 bits)", written);
        rsab_wrap(&buf, storage, written);
        decode_init(&buf, &got);
        CHECK(got.init_coord == coord, "init coord round-trips (%d)", got.init_coord);
        CHECK(got.init_low_res_entries == SLOTS - 2,
              "init carries %d low-resolution entries (2047 slots minus local)",
              got.init_low_res_entries);
    }

    fprintf(stderr, "mock239-playerinfo: tick with teleport + appearance\n");
    memset(&got, 0, sizeof(got));
    got.local_index = LOCAL_INDEX;
    rsab_wrap(&buf, storage, sizeof(storage));
    mock239_playerinfo_write(&buf, LOCAL_INDEX, coord, 0, appearance,
                             sizeof(appearance));
    {
        size_t written = rsab_len(&buf);
        rsab_wrap(&buf, storage, written);
        decode_tick(&buf, &got, 1);
        CHECK(!got.section_underrun, "no bit section ends mid-skip-run");
        CHECK(got.hi_res_seen == 1, "exactly one high-resolution update (%d)",
              got.hi_res_seen);
        CHECK(got.hi_res_extended == 1, "the high-resolution update flags extended info");
        CHECK(got.hi_res_teleported == 1, "the position is stated as a delta");
        CHECK(got.hi_res_coord == coord, "coord delta round-trips (%d)",
              got.hi_res_coord);
        CHECK(got.low_res_skipped == SLOTS - 2,
              "section 4 covers all %d low-resolution players", got.low_res_skipped);
        CHECK(got.ext_flag == 0x20, "extended-info flag is APPEARANCE (0x%02x)",
              got.ext_flag);
        CHECK(got.ext_appearance_len == (int)sizeof(appearance),
              "appearance length round-trips (%d)", got.ext_appearance_len);
        CHECK(memcmp(got.ext_appearance, appearance, sizeof(appearance)) == 0,
              "appearance bytes round-trip through the +128 obfuscation");
    }

    fprintf(stderr, "mock239-playerinfo: tick with no extended info\n");
    memset(&got, 0, sizeof(got));
    got.local_index = LOCAL_INDEX;
    rsab_wrap(&buf, storage, sizeof(storage));
    mock239_playerinfo_write(&buf, LOCAL_INDEX, coord, 0, NULL, 0);
    {
        size_t written = rsab_len(&buf);
        rsab_wrap(&buf, storage, written);
        decode_tick(&buf, &got, 0);
        CHECK(!got.section_underrun, "no bit section ends mid-skip-run");
        CHECK(got.hi_res_extended == 0, "no extended-info bit when nothing is attached");
        CHECK(got.low_res_skipped == SLOTS - 2, "section 4 still covers every low-res slot");
    }

    fprintf(stderr, "mock239-playerinfo: a later tick (crowd moves to section 3)\n");
    memset(&got, 0, sizeof(got));
    got.local_index = LOCAL_INDEX;
    rsab_wrap(&buf, storage, sizeof(storage));
    mock239_playerinfo_write(&buf, LOCAL_INDEX, coord, 1, appearance,
                             sizeof(appearance));
    {
        size_t written = rsab_len(&buf);
        rsab_wrap(&buf, storage, written);
        decode_tick_late(&buf, &got, 1);
        CHECK(!got.section_underrun, "no bit section ends mid-skip-run");
        CHECK(got.hi_res_seen == 1, "still exactly one high-resolution update");
        CHECK(got.low_res_skipped == SLOTS - 2,
              "section 3 now covers all %d low-resolution players",
              got.low_res_skipped);
        CHECK(got.ext_flag == 0x20, "appearance still follows the bit sections");
    }

    if( g_failures )
        fprintf(stderr, "mock239-playerinfo: %d failure(s)\n", g_failures);
    else
        fprintf(stderr, "mock239-playerinfo: all tests passed\n");
    return g_failures ? 1 : 0;
}

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
#include "net/mock/mock239_appearance.h"

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

static void
check_appearance_headicon_index(void)
{
    CHECK(mock239_appearance_headicon_index(0) == -1, "empty icon mask writes -1");
    CHECK(mock239_appearance_headicon_index(1) == 0, "first prayer bit writes index 0");
    CHECK(mock239_appearance_headicon_index(1 << 5) == 5, "prayer bit 5 writes index 5");
    CHECK(
        mock239_appearance_headicon_index((1 << 2) | (1 << 6)) == 2,
        "one-index wire form deterministically chooses the first active bit");
}

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
    int hi_res_opcode;
    int hi_res_direction;

    int low_res_skipped;   /* players covered by section 4 */
    int section_underrun;  /* a section ended with skipped != 0 */

    int ext_flag;
    int ext_hit_count;
    int ext_hit_type;
    int ext_hit_value;
    int ext_hit_delay;
    int ext_hit_slots;
    int ext_temp_move_speed;
    int ext_appearance_len;
    uint8_t ext_appearance[256];
};

static int
read_smart1or2(struct RSAreaBuf* buf)
{
    int first = rsab_g1(buf);

    if( first < 128 )
        return first;
    return ((first << 8) | rsab_g1(buf)) - 32768;
}

static void
decode_extended(struct RSAreaBuf* buf, struct Decoded* out)
{
    int flag = rsab_g1(buf);

    if( flag & 0x8 )
        flag |= rsab_g1(buf) << 8;
    if( flag & 0x800 )
        flag |= rsab_g1(buf) << 16;
    out->ext_flag = flag;

    /* Golden client field order: hitmarks precede traversal and appearance. */
    if( flag & 0x40000 )
    {
        out->ext_hit_count = (128 - rsab_g1(buf)) & 0xff;
        if( out->ext_hit_count > 0 )
        {
            out->ext_hit_type = read_smart1or2(buf);
            out->ext_hit_value = read_smart1or2(buf);
            out->ext_hit_delay = read_smart1or2(buf);
            out->ext_hit_slots = read_smart1or2(buf);
        }
    }
    if( flag & 0x1000 )
        out->ext_temp_move_speed = (int)(int8_t)rsab_g1(buf);
    if( flag & 0x20 )
    {
        out->ext_appearance_len = (128 - rsab_g1(buf)) & 0xff;
        for( int i = 0;
             i < out->ext_appearance_len && i < (int)sizeof(out->ext_appearance);
             i++ )
            out->ext_appearance[i] = (uint8_t)((rsab_g1(buf) - 128) & 0xff);
    }
}

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
            out->hi_res_opcode = opcode;
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
            else if( opcode == 1 )
            {
                out->hi_res_direction = rsab_gbit(buf, 3);
            }
            else if( opcode == 2 )
            {
                out->hi_res_direction = rsab_gbit(buf, 4);
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
        decode_extended(buf, out);
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
        decode_extended(buf, out);
}

int
main(void)
{
    check_appearance_headicon_index();
    static uint8_t storage[16 * 1024];
    struct RSAreaBuf buf;
    struct Decoded got;
    /* A coord the packing cannot accidentally reproduce: distinct level, x
     * and z, none of them zero and none a multiple of the others. */
    int32_t const coord = (1 << 28) | (3222 << 14) | 3218;
    uint8_t appearance[64];

    for( int i = 0; i < (int)sizeof(appearance); i++ )
        appearance[i] = (uint8_t)(i * 7 + 1);

    fprintf(stderr, "mock239-playerinfo: npc tail sentinel threshold\n");
    /* The live failure packet had 13 high-resolution NPCs ending at bit 45,
     * one byte of extended info and a writer-added sentinel: 3 + 8 + 16 = 27
     * bits. The golden client requires 28 before it even reads an index. */
    CHECK(!mock239_npcinfo_tail_needs_sentinel(45, 1),
          "bit 45 + one-byte mask omits the unreachable sentinel (11 < 28 bits)");
    CHECK(mock239_npcinfo_tail_needs_sentinel(45, 4),
          "bit 45 + four-byte mask terminates before the decoder can read it as an add");
    CHECK(!mock239_npcinfo_tail_needs_sentinel(48, 3),
          "a byte-aligned three-byte tail remains below the 28-bit guard");
    CHECK(mock239_npcinfo_tail_needs_sentinel(48, 4),
          "a byte-aligned four-byte tail reaches the 28-bit guard");

    /* Literal layouts from RSProt's revision-239 Face encoder.  These do not
     * round-trip through another implementation: the first fixture proves the
     * PlayerFacingEncoder p1Alt2 header and Loc payload, the second proves the
     * NpcFaceEncoder p1Alt1 header and Entity payload. */
    fprintf(stderr, "mock239-playerinfo: literal player/npc face blocks\n");
    {
        struct Mock239Face face;
        static uint8_t const player_loc[] = { 0xf8, 0x8c, 0x96, 0x8c, 0x92, 0x11 };
        static uint8_t const npc_player[] = { 0x80, 0x02, 0x00, 0x2a, 0x00 };

        mock239_face_init(&face);
        face.kind = MOCK239_FACE_LOC;
        face.x = 3222;
        face.z = 3218;
        rsab_wrap(&buf, storage, sizeof(storage));
        mock239_face_write_player(&buf, &face);
        CHECK(rsab_len(&buf) == sizeof(player_loc) &&
                  memcmp(storage, player_loc, sizeof(player_loc)) == 0,
              "player Face.Loc is literal p1Alt2 + smart coords + packed size");

        mock239_face_init(&face);
        face.kind = MOCK239_FACE_ENTITY;
        face.entity_type = MOCK239_FACE_PLAYER;
        face.entity_index = 42;
        rsab_wrap(&buf, storage, sizeof(storage));
        mock239_face_write_npc(&buf, &face);
        CHECK(rsab_len(&buf) == sizeof(npc_player) &&
                  memcmp(storage, npc_player, sizeof(npc_player)) == 0,
              "npc Face.Entity is literal p1Alt1 + type/index/fallback");
    }

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
    mock239_playerinfo_write(&buf, LOCAL_INDEX, MOCK239_PLAYER_TELEPORT, coord, 0, appearance,
                             sizeof(appearance), NULL);
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
    mock239_playerinfo_write(&buf, LOCAL_INDEX, MOCK239_PLAYER_NOMOVE, 0, 0, NULL, 0, NULL);
    {
        size_t written = rsab_len(&buf);
        rsab_wrap(&buf, storage, written);
        decode_tick(&buf, &got, 0);
        CHECK(!got.section_underrun, "no bit section ends mid-skip-run");
        CHECK(got.hi_res_seen == 0, "a stationary player with no ext is skipped");
        CHECK(got.hi_res_extended == 0, "no extended-info bit when nothing is attached");
        CHECK(got.low_res_skipped == SLOTS - 2, "section 4 still covers every low-res slot");
    }

    fprintf(stderr, "mock239-playerinfo: walk and run opcodes\n");
    memset(&got, 0, sizeof(got));
    got.local_index = LOCAL_INDEX;
    rsab_wrap(&buf, storage, sizeof(storage));
    mock239_playerinfo_write(&buf, LOCAL_INDEX, MOCK239_PLAYER_WALK, 7, 0, NULL, 0, NULL);
    {
        size_t written = rsab_len(&buf);
        rsab_wrap(&buf, storage, written);
        decode_tick(&buf, &got, 0);
        CHECK(!got.section_underrun, "walk leaves every section aligned");
        CHECK(got.hi_res_opcode == 1 && got.hi_res_direction == 7,
              "walk opcode/direction survive (op=%d dir=%d)", got.hi_res_opcode,
              got.hi_res_direction);
    }

    fprintf(stderr, "mock239-playerinfo: drawable hitmark block\n");
    memset(&got, 0, sizeof(got));
    got.local_index = LOCAL_INDEX;
    rsab_wrap(&buf, storage, sizeof(storage));
    {
        struct Mock239PlayerExt ext;

        memset(&ext, 0, sizeof(ext));
        ext.has_hit = 1;
        ext.hit_type = 28;
        ext.hit_value = 73;
        mock239_playerinfo_write(
            &buf, LOCAL_INDEX, MOCK239_PLAYER_NOMOVE, 0, 0, NULL, 0, &ext);
    }
    {
        size_t written = rsab_len(&buf);
        rsab_wrap(&buf, storage, written);
        decode_tick(&buf, &got, 1);
        CHECK(!got.section_underrun, "hitmark leaves every section aligned");
        CHECK(got.ext_flag == 0x40808,
              "hitmark extended flag carries both continuation bytes (0x%x)",
              got.ext_flag);
        CHECK(got.ext_hit_count == 1, "one hitmark is encoded (%d)", got.ext_hit_count);
        CHECK(got.ext_hit_type == 28 && got.ext_hit_value == 73,
              "hitmark type/value survive (%d/%d)", got.ext_hit_type,
              got.ext_hit_value);
        CHECK(got.ext_hit_delay == 0, "hitmark lands immediately (delay %d)",
              got.ext_hit_delay);
        CHECK(got.ext_hit_slots == 4,
              "golden Actor.method3560 receives four drawable slots, got %d",
              got.ext_hit_slots);
    }
    memset(&got, 0, sizeof(got));
    got.local_index = LOCAL_INDEX;
    rsab_wrap(&buf, storage, sizeof(storage));
    {
        struct Mock239PlayerExt ext;

        memset(&ext, 0, sizeof(ext));
        ext.has_temp_move_speed = 1;
        ext.temp_move_speed = 2;
        mock239_playerinfo_write(
            &buf, LOCAL_INDEX, MOCK239_PLAYER_RUN, 8, 0, NULL, 0, &ext);
    }
    {
        size_t written = rsab_len(&buf);
        rsab_wrap(&buf, storage, written);
        decode_tick(&buf, &got, 1);
        CHECK(!got.section_underrun, "run leaves every section aligned");
        CHECK(got.hi_res_opcode == 2 && got.hi_res_direction == 8,
              "run opcode/direction survive (op=%d dir=%d)", got.hi_res_opcode,
              got.hi_res_direction);
        CHECK(got.hi_res_extended == 1, "run stages a traversal override");
        CHECK(got.ext_flag == 0x1008,
              "run extended flag is TEMP_MOVE_SPEED plus continuation (0x%x)",
              got.ext_flag);
        CHECK(got.ext_temp_move_speed == 2,
              "run traversal value is 2 (%d)", got.ext_temp_move_speed);
    }

    fprintf(stderr, "mock239-playerinfo: a later tick (crowd moves to section 3)\n");
    memset(&got, 0, sizeof(got));
    got.local_index = LOCAL_INDEX;
    rsab_wrap(&buf, storage, sizeof(storage));
    mock239_playerinfo_write(&buf, LOCAL_INDEX, MOCK239_PLAYER_NOMOVE, 0, 1, appearance,
                             sizeof(appearance), NULL);
    {
        size_t written = rsab_len(&buf);
        rsab_wrap(&buf, storage, written);
        decode_tick_late(&buf, &got, 1);
        CHECK(!got.section_underrun, "no bit section ends mid-skip-run");
        CHECK(got.hi_res_seen == 1, "still exactly one high-resolution update");
        CHECK(got.hi_res_opcode == 0, "stationary extended info uses NOMOVE, not teleport");
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

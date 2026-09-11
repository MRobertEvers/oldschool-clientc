/*
 * The hitsplat multi-var selector: settings 5 and 279, as state.
 *
 * ## Why this is a test and not a screenshot
 *
 * Both settings are INVERTED rows, and both of the ways to get one wrong draw a
 * splat:
 *
 *   - reading the row the plain way tints your OWN damage and leaves everyone
 *     else's plain — the feature working exactly backwards;
 *   - resolving against the stream ids WITHOUT the fallback appended shifts the
 *     bound by one, so "setting off" falls through to the fallback and draws the
 *     tinted leaf anyway.
 *
 * Neither shows up as an error, an empty screen, or a missing sprite. A shot of
 * a fight with tinting on and a shot with it off differ by one splat colour on
 * one hit, which is also what "the server never sent a hit from someone else"
 * looks like. So the resolution is pinned here instead.
 *
 * ## The records are the cache's own
 *
 * The byte arrays below are records 16, 17, 28, 29 and 43 of cache.osrs239's
 * group 32, reproduced from `OSRS-Content/osrs239-content/configs/all.hitsplat`
 * — which `cachepack verify --types hitsplat` reports as 83/83 byte-exact
 * against the cache. The decode assertions restate that table, so a decoder
 * change that silently re-reads a field fails here rather than in a fight.
 *
 * To re-derive them:
 *   3rd/rscache/tools/cachepack/cachepack unpack --cache cache.osrs239 \
 *       --rev osrs239 --src /tmp/hs --types hitsplat
 *   grep -A4 'variantop' /tmp/hs/configs/all.hitsplat
 */

#include "game/rs_hitsplat.h"

#include "datatypes/dat2_config_hitsplat.h"
#include "varp/varp_manager.h"
#include "world/entity_pathing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, msg)                                                                      \
    do                                                                                        \
    {                                                                                         \
        g_checks++;                                                                           \
        if( !(cond) )                                                                         \
        {                                                                                     \
            g_failures++;                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));                   \
        }                                                                                     \
    } while( 0 )

#define CHECK_EQ(got, want, msg)                                                              \
    do                                                                                        \
    {                                                                                         \
        int got_ = (got);                                                                     \
        int want_ = (want);                                                                   \
        g_checks++;                                                                           \
        if( got_ != want_ )                                                                   \
        {                                                                                     \
            g_failures++;                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s (got %d, want %d)\n", __FILE__, __LINE__, (msg),  \
                    got_, want_);                                                             \
        }                                                                                     \
    } while( 0 )

/* --- the cache's own bytes ------------------------------------------------ */

/* 16 `hitsplat_damage_me`: duration 50, selector varbit 10236 -> [28, 28] fb 28 */
static const uint8_t REC_16[] = { 0x09, 0x00, 0x32, 0x12, 0x27, 0xfc, 0xff, 0xff,
                                  0x00, 0x1c, 0x01, 0x00, 0x1c, 0x00, 0x1c, 0x00 };
/* 17 `hitsplat_damage_other`: duration 50, selector varbit 10236 -> [29, 28] fb 29 */
static const uint8_t REC_17[] = { 0x09, 0x00, 0x32, 0x12, 0x27, 0xfc, 0xff, 0xff,
                                  0x00, 0x1d, 0x01, 0x00, 0x1d, 0x00, 0x1c, 0x00 };
/* 43 `hitsplat_damage_max_me`: duration 50, selector varbit 14196 -> [48, 28] fb 48 */
static const uint8_t REC_43[] = { 0x09, 0x00, 0x32, 0x12, 0x37, 0x74, 0xff, 0xff,
                                  0x00, 0x30, 0x01, 0x00, 0x30, 0x00, 0x1c, 0x00 };
/* 28 `hitsplat_damage`: the plain red leaf, sprite 1359 */
static const uint8_t REC_28[] = { 0x08, 0x00, 0x25, 0x31, 0x00, 0x05, 0x05, 0x4f,
                                  0x09, 0x00, 0x32, 0x0d, 0x00, 0x01, 0x00 };
/* 29: the tinted leaf, sprite 1631 */
static const uint8_t REC_29[] = { 0x08, 0x00, 0x25, 0x31, 0x00, 0x05, 0x06, 0x5f,
                                  0x09, 0x00, 0x32, 0x0d, 0x00, 0x01, 0x00 };

#define HITSPLAT_TINT_DISABLED 10236
#define HITSPLAT_MAXHIT_DISABLED 14196

/*
 * Three synthetic ids past the real ones, for outcomes cache.osrs239 happens not
 * to exercise. All three are legal records, not malformed ones.
 *
 * ID_DISTINCT is the one that earns its place. **Every selector in
 * cache.osrs239 has `fallback == ids[0]`**, which makes the array's bound
 * unfalsifiable on real data: resolving against `count` and resolving against
 * `count - 1` give the same answer for every real record and every in-range
 * value, and differ only when the var reads past the list. So a record whose
 * fallback matches nothing in its stream is the only way to pin the bound from
 * both sides.
 */
#define ID_WIDE 60     /* a 3-bit varbit, so a value CAN land out of range */
#define ID_HIDE 61     /* a selector whose "on" entry is -1: draw no splat */
#define ID_DISTINCT 62 /* fallback shares no value with the stream ids */

#define TYPE_COUNT 64

/* A three-bit varbit, so a value of 5 against a two-entry list is reachable. */
#define VARBIT_WIDE 900
/* Its own single-bit varbit, so ID_HIDE can be driven independently. */
#define VARBIT_HIDE 901

#define VARP_SETTINGS 1425
#define VARP_SPARE 1426

static struct RS_Hitsplats g_hitsplats;
static struct VarPManager g_varps;

/** Decode one record into the tables the loader builds, exactly as
 *  `task_dat2_hitsplat_load.c` builds them — including the fallback appended
 *  after the stream ids, which is the whole of the off-by-one this guards. */
static void
install(
    int id,
    const uint8_t* bytes,
    int size,
    int* sprite_ids,
    int* durations,
    int* slot_policies,
    struct RS_HitsplatVariants* variants)
{
    struct RSCache_Dat2ConfigHitsplat entry;

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigHitsplatDecodeInplace(&entry, bytes, size,
                                            RSCACHE_CONFIG_HITSPLAT_DECODE_OSRS);
    CHECK_EQ(entry._consumed, size, "record consumed exactly");

    sprite_ids[id] = entry.sprite_id;
    durations[id] = entry.duration;
    slot_policies[id] = entry.slot_policy;
    if( entry.variant_count > 0 )
    {
        int n = entry.variant_count + 1;
        int* ids = malloc((size_t)n * sizeof(*ids));

        for( int v = 0; v < entry.variant_count; v++ )
            ids[v] = entry.variants[v];
        ids[n - 1] = entry.variant_fallback;
        variants[id].varbit = entry.variant_varbit;
        variants[id].varp = entry.variant_varp;
        variants[id].ids = ids;
        variants[id].count = n;
    }
}

/** A selector built by hand rather than decoded, for the two shapes the cache
 *  has no record of. `fallback` is appended the same way the loader appends it. */
static void
install_synthetic(
    int id,
    int varbit,
    const int* stream,
    int stream_count,
    int fallback,
    int* sprite_ids,
    int* durations,
    int* slot_policies,
    struct RS_HitsplatVariants* variants)
{
    int n = stream_count + 1;
    int* ids = malloc((size_t)n * sizeof(*ids));

    for( int i = 0; i < stream_count; i++ )
        ids[i] = stream[i];
    ids[n - 1] = fallback;
    sprite_ids[id] = -1;
    durations[id] = 70;
    slot_policies[id] = -1;
    variants[id].varbit = varbit;
    variants[id].varp = -1;
    variants[id].ids = ids;
    variants[id].count = n;
}

static void
setup(void)
{
    int* sprite_ids = malloc(TYPE_COUNT * sizeof(int));
    int* durations = malloc(TYPE_COUNT * sizeof(int));
    int* slot_policies = malloc(TYPE_COUNT * sizeof(int));
    struct RS_HitsplatVariants* variants = calloc(TYPE_COUNT, sizeof(*variants));

    for( int i = 0; i < TYPE_COUNT; i++ )
    {
        sprite_ids[i] = -1;
        durations[i] = WORLD_HITMARK_DEFAULT_DURATION;
        slot_policies[i] = WORLD_HITMARK_POLICY_DISCARD;
    }

    install(16, REC_16, (int)sizeof(REC_16), sprite_ids, durations, slot_policies, variants);
    install(17, REC_17, (int)sizeof(REC_17), sprite_ids, durations, slot_policies, variants);
    install(43, REC_43, (int)sizeof(REC_43), sprite_ids, durations, slot_policies, variants);
    install(28, REC_28, (int)sizeof(REC_28), sprite_ids, durations, slot_policies, variants);
    install(29, REC_29, (int)sizeof(REC_29), sprite_ids, durations, slot_policies, variants);

    {
        static const int wide[] = { 28, 29 };
        static const int hide[] = { -1, 28 };
        static const int distinct[] = { 16, 17, 43 };
        install_synthetic(ID_WIDE, VARBIT_WIDE, wide, 2, 28, sprite_ids, durations,
                          slot_policies, variants);
        install_synthetic(ID_HIDE, VARBIT_HIDE, hide, 2, 28, sprite_ids, durations,
                          slot_policies, variants);
        install_synthetic(ID_DISTINCT, VARBIT_WIDE, distinct, 3, 29, sprite_ids, durations,
                          slot_policies, variants);
    }

    RS_Hitsplats_Init(&g_hitsplats);
    CHECK(RS_Hitsplats_SetTypes(&g_hitsplats, sprite_ids, durations, slot_policies, variants,
                                TYPE_COUNT),
          "SetTypes");

    {
        /* Two real varbits and two synthetic ones. 10236 and 14196 are the rows'
         * own — bit 16 and bit 20 of varp 1425 `ironman_var_1`, which is where
         * cache.osrs239 puts them, and putting BOTH on one varp is not a
         * simplification: it is why an unmirrored settings write is fragile,
         * since one server update to 1425 moves all of them at once. */
        static struct VarBitType types[HITSPLAT_MAXHIT_DISABLED + 1];
        types[HITSPLAT_TINT_DISABLED].basevar = VARP_SETTINGS;
        types[HITSPLAT_TINT_DISABLED].startbit = 16;
        types[HITSPLAT_TINT_DISABLED].endbit = 16;
        types[HITSPLAT_MAXHIT_DISABLED].basevar = VARP_SETTINGS;
        types[HITSPLAT_MAXHIT_DISABLED].startbit = 20;
        types[HITSPLAT_MAXHIT_DISABLED].endbit = 20;
        types[VARBIT_WIDE].basevar = VARP_SPARE;
        types[VARBIT_WIDE].startbit = 0;
        types[VARBIT_WIDE].endbit = 2;
        types[VARBIT_HIDE].basevar = VARP_SPARE;
        types[VARBIT_HIDE].startbit = 8;
        types[VARBIT_HIDE].endbit = 8;

        static struct VarPType varps[VARP_SPARE + 1];

        VarPManager_Init(&g_varps);
        /*
         * The varp table FIRST, and it is not a formality. `SetVarbitOptimistic`
         * returns silently when the varbit's base varp is past `varp_count`, so
         * a manager with varbit types and no varp types accepts every write and
         * keeps every value at 0 — under which half the assertions below pass by
         * accident, because 0 is also the "feature on" state of an inverted row.
         * That is exactly the failure `TORIRS_SIM_VARBIT` prints "base varp -1"
         * for, and it is why the varbit-write assertions here check BOTH values.
         */
        CHECK(VarPManager_SetVarpTypes(&g_varps, varps, VARP_SPARE + 1), "SetVarpTypes");
        CHECK(VarPManager_SetVarbitTypes(&g_varps, types, HITSPLAT_MAXHIT_DISABLED + 1),
              "SetVarbitTypes");
        /* Proof the writes land at all, before anything is concluded from one. */
        VarPManager_SetVarbitOptimistic(&g_varps, HITSPLAT_TINT_DISABLED, 1);
        CHECK_EQ(VarPManager_GetVarbit(&g_varps, HITSPLAT_TINT_DISABLED), 1,
                 "a varbit write reads back");
        VarPManager_SetVarbitOptimistic(&g_varps, HITSPLAT_TINT_DISABLED, 0);
        CHECK_EQ(VarPManager_GetVarbit(&g_varps, HITSPLAT_TINT_DISABLED), 0,
                 "and reads back the other way");
    }
}

static void
teardown(void)
{
    RS_Hitsplats_Free(&g_hitsplats);
    VarPManager_Free(&g_varps);
}

static int
resolve(int type)
{
    return RS_Hitsplats_ResolveType(&g_hitsplats, &g_varps, type);
}

static int
sprite_of(int type)
{
    int resolved = resolve(type);

    return resolved < 0 ? -1 : RS_Hitsplats_SpriteFor(&g_hitsplats, resolved);
}

int
main(void)
{
    setup();

    /* --- the records decode to the table `all.hitsplat` states -------------- */
    CHECK_EQ(g_hitsplats.variants[17].varbit, HITSPLAT_TINT_DISABLED, "17 keys on 10236");
    CHECK_EQ(g_hitsplats.variants[17].varp, -1, "17 names no varp");
    CHECK_EQ(g_hitsplats.variants[17].count, 3, "17 is 2 stream ids plus the fallback");
    CHECK_EQ(g_hitsplats.variants[17].ids[0], 29, "17 value 0 -> tinted 29");
    CHECK_EQ(g_hitsplats.variants[17].ids[1], 28, "17 value 1 -> plain 28");
    CHECK_EQ(g_hitsplats.variants[17].ids[2], 29, "17 fallback is 29");
    CHECK_EQ(g_hitsplats.variants[43].varbit, HITSPLAT_MAXHIT_DISABLED, "43 keys on 14196");
    CHECK_EQ(g_hitsplats.sprite_ids[28], 1359, "leaf 28 is sprite 1359");
    CHECK_EQ(g_hitsplats.sprite_ids[29], 1631, "leaf 29 is sprite 1631");
    CHECK_EQ(g_hitsplats.sprite_ids[17], -1, "a selector carries no sprite of its own");

    /* --- setting 5, both ways ---------------------------------------------- *
     * The row is INVERTED: varbit 0 is "Hitsplat tinting ON". Reading it the
     * plain way swaps these two lines and nothing else changes on screen. */
    VarPManager_SetVarbitOptimistic(&g_varps, HITSPLAT_TINT_DISABLED, 0);
    CHECK_EQ(resolve(17), 29, "tinting on: someone else's damage is the tinted leaf");
    CHECK_EQ(sprite_of(17), 1631, "... which draws sprite 1631");
    CHECK_EQ(resolve(16), 28, "tinting on: your OWN damage is never tinted");
    CHECK_EQ(sprite_of(16), 1359, "... which draws sprite 1359");

    VarPManager_SetVarbitOptimistic(&g_varps, HITSPLAT_TINT_DISABLED, 1);
    CHECK_EQ(resolve(17), 28, "tinting off: someone else's damage is the plain leaf");
    CHECK_EQ(sprite_of(17), 1359, "... which draws sprite 1359");
    CHECK_EQ(resolve(16), 28, "tinting off: your own damage is unchanged");

    /* --- setting 279, both ways -------------------------------------------- */
    VarPManager_SetVarbitOptimistic(&g_varps, HITSPLAT_MAXHIT_DISABLED, 0);
    CHECK_EQ(resolve(43), 48, "max-hit splats on: the max leaf");
    VarPManager_SetVarbitOptimistic(&g_varps, HITSPLAT_MAXHIT_DISABLED, 1);
    CHECK_EQ(resolve(43), 28, "max-hit splats off: falls back to ordinary damage");

    /* The two rows share varp 1425 and must not move each other. */
    VarPManager_SetVarbitOptimistic(&g_varps, HITSPLAT_TINT_DISABLED, 0);
    CHECK_EQ(resolve(43), 28, "toggling tinting does not switch max-hit splats back on");
    CHECK_EQ(resolve(17), 29, "... and tinting is still on");

    /* --- a leaf resolves to itself ----------------------------------------- */
    CHECK_EQ(resolve(28), 28, "an ordinary record is its own answer");
    CHECK_EQ(resolve(29), 29, "an ordinary record is its own answer");

    /* --- out of range takes the LAST entry, not entry[count-1] of the stream *
     * This is the off-by-one: with the fallback left off the array, a value of
     * 5 would index nothing and the bound would make value 1 the fallback. */
    VarPManager_SetVarbitOptimistic(&g_varps, VARBIT_WIDE, 5);
    CHECK_EQ(resolve(ID_WIDE), 28, "a var past the list takes the fallback");
    VarPManager_SetVarbitOptimistic(&g_varps, VARBIT_WIDE, 1);
    CHECK_EQ(resolve(ID_WIDE), 29, "a var inside the list takes its entry");
    VarPManager_SetVarbitOptimistic(&g_varps, VARBIT_WIDE, 0);
    CHECK_EQ(resolve(ID_WIDE), 28, "a var inside the list takes its entry");

    /* The bound, from both sides, on the only record shape that can show it.
     * ids = [16, 17, 43] then fallback 29, so every value 0..2 has an answer of
     * its own and only 3+ may reach 29. Resolving against `count - 1` moves the
     * cliff to 2 and answers 29 for a value that names a real entry. */
    VarPManager_SetVarbitOptimistic(&g_varps, VARBIT_WIDE, 0);
    CHECK_EQ(resolve(ID_DISTINCT), 16, "value 0 -> first entry");
    VarPManager_SetVarbitOptimistic(&g_varps, VARBIT_WIDE, 1);
    CHECK_EQ(resolve(ID_DISTINCT), 17, "value 1 -> second entry");
    VarPManager_SetVarbitOptimistic(&g_varps, VARBIT_WIDE, 2);
    CHECK_EQ(resolve(ID_DISTINCT), 43, "value 2 -> LAST stream entry, not the fallback");
    VarPManager_SetVarbitOptimistic(&g_varps, VARBIT_WIDE, 3);
    CHECK_EQ(resolve(ID_DISTINCT), 29, "value 3 -> the fallback");
    VarPManager_SetVarbitOptimistic(&g_varps, VARBIT_WIDE, 7);
    CHECK_EQ(resolve(ID_DISTINCT), 29, "any value past the list -> the fallback");

    /* --- a resolved -1 is "draw nothing", not "fall back" ------------------ */
    VarPManager_SetVarbitOptimistic(&g_varps, VARBIT_HIDE, 0);
    CHECK_EQ(resolve(ID_HIDE), -1, "a -1 entry hides the splat");
    CHECK_EQ(sprite_of(ID_HIDE), -1, "... and the caller draws nothing");
    VarPManager_SetVarbitOptimistic(&g_varps, VARBIT_HIDE, 1);
    CHECK_EQ(resolve(ID_HIDE), 28, "the other value still resolves normally");

    /* --- duration and slot policy come from the WIRE type ------------------ *
     * The reference reads both off the unresolved record. They happen to agree
     * here (both 50), so the assertion that matters is that asking the SELECTOR
     * answers at all rather than falling to the 70 default — which is what
     * reading them off a resolved id would do for a type with no record. */
    CHECK_EQ(RS_Hitsplats_DurationFor(&g_hitsplats, 17), 50, "selector 17 carries duration 50");
    CHECK_EQ(RS_Hitsplats_DurationFor(&g_hitsplats, 43), 50, "selector 43 carries duration 50");
    CHECK_EQ(RS_Hitsplats_DurationFor(&g_hitsplats, ID_WIDE), 70,
             "a record with no opcode 9 keeps the reference default");

    /* --- no var state at all resolves to the fallback ----------------------- */
    CHECK_EQ(RS_Hitsplats_ResolveType(&g_hitsplats, NULL, 17), 29,
             "no varps: the fallback, not a dropped splat");
    CHECK_EQ(RS_Hitsplats_ResolveType(&g_hitsplats, NULL, 28), 28, "no varps: a leaf is itself");

    /* --- a table with no selectors at all is the identity ------------------ *
     * The dat1 path and any pre-OldSchool cache. */
    {
        struct RS_Hitsplats plain;
        int* sprites = malloc(4 * sizeof(int));

        for( int i = 0; i < 4; i++ )
            sprites[i] = 100 + i;
        RS_Hitsplats_Init(&plain);
        CHECK(RS_Hitsplats_SetTypes(&plain, sprites, NULL, NULL, NULL, 4), "SetTypes no variants");
        CHECK_EQ(RS_Hitsplats_ResolveType(&plain, &g_varps, 2), 2, "no selector table: identity");
        CHECK_EQ(RS_Hitsplats_ResolveType(&plain, &g_varps, 99), 99, "out of range: identity");
        RS_Hitsplats_Free(&plain);
    }

    teardown();

    printf("rs_hitsplat_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

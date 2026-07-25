/*
 * Cache-profile and revision-registry tests.
 *
 * The point of these is regression cover for the ambiguity the profile layer was
 * built to remove: one number ("revision") that meant a game revision in some
 * decoders, a small-integer archive counter in others, and a unix timestamp in
 * modern caches. Each datatype below is checked at both sides of its era gate,
 * through both the profile path and the retained archive-revision path.
 */

#include "rscache_test.h"

#include <rscache.h>

static void
test_profile_zero(void)
{
    RSCACHE_TEST_GROUP("ProfileZero defaults");

    struct RSCache cache = RSCache_ProfileZero();
    RSCACHE_CHECK_EQ(cache.game, RSCACHE_GAME_OLDSCHOOL);
    RSCACHE_CHECK_EQ(cache.container, RSCACHE_CONTAINER_DAT2);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_OSRS);
    RSCACHE_CHECK_EQ(cache.version, RSCACHE_REVISION_UNKNOWN);
    RSCACHE_CHECK_EQ(cache.quirks, 0);

    /* Every slot must start neutral, so adding a datatype does not silently
     * inherit a stale codec pin or revision from another. */
    bool all_neutral = true;
    for( int i = 0; i < RSCACHE_TYPE_COUNT; i++ )
    {
        if( cache.group_revision[i] != RSCACHE_GROUP_REVISION_UNKNOWN ||
            cache.codec[i] != RSCACHE_CODEC_AUTO )
        {
            all_neutral = false;
            break;
        }
    }
    RSCACHE_CHECK(all_neutral);
    RSCACHE_CHECK(!RSCache_IsDat1(&cache));
    RSCACHE_CHECK(!RSCache_HasQuirk(&cache, RSCACHE_QUIRK_KRONOS));

    /* A NULL profile must be tolerated everywhere: callers that have not been
     * migrated yet pass one. */
    RSCACHE_CHECK(!RSCache_IsDat1(NULL));
    RSCACHE_CHECK(!RSCache_HasQuirk(NULL, RSCACHE_QUIRK_KRONOS));
    RSCACHE_CHECK_EQ(RSCache_GroupRevision(NULL, RSCACHE_TYPE_LOC),
                     RSCACHE_GROUP_REVISION_UNKNOWN);
}

static void
test_revision_at_least(void)
{
    RSCACHE_TEST_GROUP("RevisionAtLeast precedence");

    /* A declared game revision wins outright and ignores archive revisions. */
    {
        struct RSCache cache = RSCache_ProfileZero();
        cache.version = 230;
        /* An archive revision that would say "old" must not override it. */
        RSCache_ProfileSetGroupRevision(&cache, RSCACHE_TYPE_LOC, 1);
        RSCACHE_CHECK(RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_LOC, 220, 2000, false));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_LOC, 237, 2000, true));
    }

    /* Without one, the group's archive revision decides. */
    {
        struct RSCache cache = RSCache_ProfileZero();
        RSCache_ProfileSetGroupRevision(&cache, RSCACHE_TYPE_LOC, 1767694604);
        RSCACHE_CHECK(RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_LOC, 220, 2000, false));

        RSCache_ProfileSetGroupRevision(&cache, RSCACHE_TYPE_LOC, 997);
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_LOC, 220, 2000, false));
    }

    /* Group revisions are independent. The npc, seq and loc groups share table 2
     * but carry unrelated counters — conflating them was the original bug. */
    {
        struct RSCache cache = RSCache_ProfileZero();
        RSCache_ProfileSetGroupRevision(&cache, RSCACHE_TYPE_NPC, 1500);
        RSCache_ProfileSetGroupRevision(&cache, RSCACHE_TYPE_SEQUENCE, 1000);
        RSCACHE_CHECK(RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_NPC, 210, 1493, false));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_SEQUENCE, 220, 1142, false));
    }

    /* With neither, the caller's stated default is returned verbatim — both
     * ways round, so an unidentified cache is a deliberate choice per datatype. */
    {
        struct RSCache cache = RSCache_ProfileZero();
        RSCACHE_CHECK(RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_OBJ, 220, 2000, true));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_OBJ, 220, 2000, false));
        /* A group revision with no threshold to compare against is also unknown. */
        RSCache_ProfileSetGroupRevision(&cache, RSCACHE_TYPE_OBJ, 5000);
        RSCACHE_CHECK(RSCache_RevisionAtLeastOsrs(
            &cache, RSCACHE_TYPE_OBJ, 220, RSCACHE_GROUP_REVISION_UNKNOWN, true));
    }

    RSCACHE_TEST_GROUP("a dat1 revision never satisfies an OldSchool threshold");
    /*
     * The two numbering lineages overlap and mean different things: dat1 runs to 254 in
     * a 2004-era sequence, OldSchool restarted from 1 and is now in the 230s. Comparing
     * across them would make a dat1 cache look newer than every OSRS gate in the
     * library and switch its decoders to layouts that postdate it by two decades.
     *
     * This is not hypothetical — it is what the engine wiring would have done the
     * moment it began passing real profiles to the Flags functions.
     */
    {
        struct RSCache cache;
        RSCACHE_CHECK(RSCache_ProfileByName("lc254", &cache));
        RSCACHE_CHECK(cache.version == 254);
        RSCACHE_CHECK(RSCache_IsDat1(&cache));

        /* Every threshold the library actually gates on. */
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_LOC, 220, 2000, false));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_NPC, 210, 1493, false));
        RSCACHE_CHECK(
            !RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_SEQUENCE, 226, 1268, false));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_TEXTURE, 220, 2000, false));

        /* And the flags a decoder actually receives: the dat1 shape, nothing modern. */
        RSCACHE_CHECK(RSCache_Dat2ConfigLocFlags(&cache) == RSCACHE_CONFIG_LOC_DECODE_DAT);

        /* The 643 branch is likewise its own lineage, not a later OSRS. */
        struct RSCache rs643;
        if( RSCache_ProfileByName("rs643", &rs643) )
        {
            RSCACHE_CHECK(
                !RSCache_RevisionAtLeastOsrs(&rs643, RSCACHE_TYPE_LOC, 220, 2000, false));
        }
    }
}

static void
test_registry(void)
{
    RSCACHE_TEST_GROUP("revision registry");

    struct RSCache cache;

    RSCACHE_CHECK(RSCache_ProfileByName("lc254", &cache));
    RSCACHE_CHECK_EQ(cache.container, RSCACHE_CONTAINER_DAT1);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_DAT1_CLASSIC);
    RSCACHE_CHECK_EQ(cache.version, 254);
    RSCACHE_CHECK(RSCache_IsDat1(&cache));

    RSCACHE_CHECK(RSCache_ProfileByName("osrs230", &cache));
    RSCACHE_CHECK_EQ(cache.container, RSCACHE_CONTAINER_DAT2);
    RSCACHE_CHECK_EQ(cache.version, 230);
    RSCACHE_CHECK_EQ(cache.codec[RSCACHE_TYPE_SEQUENCE], RSCACHE_CODEC_SEQUENCE_V3);

    /* xrsps233 and osrs233 are the same cache; they differ only in transport. */
    struct RSCache xrsps;
    RSCACHE_CHECK(RSCache_ProfileByName("xrsps233", &xrsps));
    RSCACHE_CHECK(RSCache_ProfileByName("osrs233", &cache));
    RSCACHE_CHECK_EQ(xrsps.version, cache.version);
    RSCACHE_CHECK_EQ(xrsps.container, cache.container);

    /* Kronos is the only source of the quirk, and it is reachable by both names. */
    RSCACHE_CHECK(RSCache_ProfileByName("kronos", &cache));
    RSCACHE_CHECK(RSCache_HasQuirk(&cache, RSCACHE_QUIRK_KRONOS));
    RSCACHE_CHECK_EQ(cache.version, 184);
    RSCACHE_CHECK(RSCache_ProfileByName("osrs184", &cache));
    RSCACHE_CHECK(RSCache_HasQuirk(&cache, RSCACHE_QUIRK_KRONOS));

    /* No other profile carries it. */
    RSCACHE_CHECK(RSCache_ProfileByName("osrs230", &cache));
    RSCACHE_CHECK(!RSCache_HasQuirk(&cache, RSCACHE_QUIRK_KRONOS));

    /* 643 states its epoch and deliberately leaves the revision unknown: it is
     * not on the OSRS revision line, so comparing it to 220 would be nonsense. */
    RSCACHE_CHECK(RSCache_ProfileByName("643", &cache));
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_643);
    RSCACHE_CHECK_EQ(cache.game, RSCACHE_GAME_RS2);
    RSCACHE_CHECK_EQ(cache.version, RSCACHE_REVISION_UNKNOWN);

    RSCACHE_CHECK(!RSCache_ProfileByName("nope", &cache));
    RSCACHE_CHECK(!RSCache_ProfileByName(NULL, &cache));
}

static void
test_container_revision_lookup(void)
{
    RSCACHE_TEST_GROUP("ProfileForContainerRevision");

    /* Exact hits. */
    struct RSCache cache = RSCache_ProfileForContainerRevision(RSCACHE_CONTAINER_DAT2, 230);
    RSCACHE_CHECK_EQ(cache.version, 230);
    RSCACHE_CHECK_EQ(cache.codec[RSCACHE_TYPE_SEQUENCE], RSCACHE_CODEC_SEQUENCE_V3);

    cache = RSCache_ProfileForContainerRevision(RSCACHE_CONTAINER_DAT1, 254);
    RSCACHE_CHECK_EQ(cache.version, 254);
    RSCACHE_CHECK(RSCache_IsDat1(&cache));

    /* An undeclared revision must still get the right container and epoch —
     * those are what would corrupt a decode outright — and must carry the
     * caller's revision through so era gates still work. */
    cache = RSCache_ProfileForContainerRevision(RSCACHE_CONTAINER_DAT2, 235);
    RSCACHE_CHECK_EQ(cache.container, RSCACHE_CONTAINER_DAT2);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_OSRS);
    RSCACHE_CHECK_EQ(cache.version, 235);
    /* Codec pins were verified against 233, not 235, so they must not carry. */
    RSCACHE_CHECK_EQ(cache.codec[RSCACHE_TYPE_SEQUENCE], RSCACHE_CODEC_AUTO);

    cache = RSCache_ProfileForContainerRevision(RSCACHE_CONTAINER_DAT1, 250);
    RSCACHE_CHECK(RSCache_IsDat1(&cache));
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_DAT1_CLASSIC);
    RSCACHE_CHECK_EQ(cache.version, 250);

    /* Older than anything declared: still the right container and epoch. */
    cache = RSCache_ProfileForContainerRevision(RSCACHE_CONTAINER_DAT2, 100);
    RSCACHE_CHECK_EQ(cache.container, RSCACHE_CONTAINER_DAT2);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_OSRS);
    RSCACHE_CHECK_EQ(cache.version, 100);

    /* Newer than anything declared inherits from the newest below it. */
    cache = RSCache_ProfileForContainerRevision(RSCACHE_CONTAINER_DAT2, 999);
    RSCACHE_CHECK_EQ(cache.version, 999);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_OSRS);
}

static void
test_sequence_codec_selection(void)
{
    RSCACHE_TEST_GROUP("sequence codec selection");

    /* By game revision. */
    struct RSCache cache = RSCache_ProfileZero();
    cache.version = 184;
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&cache), RSCACHE_CODEC_SEQUENCE_V1);
    cache.version = 222;
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&cache), RSCACHE_CODEC_SEQUENCE_V2);
    cache.version = 230;
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&cache), RSCACHE_CODEC_SEQUENCE_V3);
    /* Exactly on each boundary. */
    cache.version = 220;
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&cache), RSCACHE_CODEC_SEQUENCE_V2);
    cache.version = 226;
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&cache), RSCACHE_CODEC_SEQUENCE_V3);

    /* An explicit pin overrides the derivation, which is what makes a revision
     * module's declaration authoritative. */
    cache.version = 184;
    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&cache), RSCACHE_CODEC_SEQUENCE_V3);

    /* By archive revision, reproducing the reference client's ladder. This is
     * the path a cache with no declared revision takes. */
    struct RSCache by_archive = RSCache_ProfileZero();
    RSCache_ProfileSetGroupRevision(&by_archive, RSCACHE_TYPE_SEQUENCE, 1000);
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&by_archive),
                     RSCACHE_CODEC_SEQUENCE_V1);
    /* 1141 is the last v1 revision; 1142 is the first v2. */
    RSCache_ProfileSetGroupRevision(&by_archive, RSCACHE_TYPE_SEQUENCE,
                                    RSCACHE_SEQUENCE_ARCHIVE_REV_220);
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&by_archive),
                     RSCACHE_CODEC_SEQUENCE_V1);
    RSCache_ProfileSetGroupRevision(&by_archive, RSCACHE_TYPE_SEQUENCE,
                                    RSCACHE_SEQUENCE_ARCHIVE_REV_220 + 1);
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&by_archive),
                     RSCACHE_CODEC_SEQUENCE_V2);
    /* 1268 is the last v2; 1269 is the first v3. */
    RSCache_ProfileSetGroupRevision(&by_archive, RSCACHE_TYPE_SEQUENCE,
                                    RSCACHE_SEQUENCE_ARCHIVE_REV_226);
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&by_archive),
                     RSCACHE_CODEC_SEQUENCE_V2);
    RSCache_ProfileSetGroupRevision(&by_archive, RSCACHE_TYPE_SEQUENCE,
                                    RSCACHE_SEQUENCE_ARCHIVE_REV_226 + 1);
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&by_archive),
                     RSCACHE_CODEC_SEQUENCE_V3);

    /* A modern timestamp revision lands on v3 — the same answer the old
     * `revision <= 1268` comparison reached, but now for a stated reason rather
     * than because a timestamp happens to exceed every small-integer threshold. */
    RSCache_ProfileSetGroupRevision(&by_archive, RSCACHE_TYPE_SEQUENCE, 1767694604);
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&by_archive),
                     RSCACHE_CODEC_SEQUENCE_V3);

    /* Nothing known: newest, matching what every shipped dat2 cache needs. */
    struct RSCache unknown = RSCache_ProfileZero();
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(&unknown),
                     RSCACHE_CODEC_SEQUENCE_V3);
    RSCACHE_CHECK_EQ(RSCache_Dat2ConfigSequenceCodecVersion(NULL), RSCACHE_CODEC_SEQUENCE_V3);
}

static void
test_loc_flags(void)
{
    RSCACHE_TEST_GROUP("loc flags");

    /* dat1 selects the newline-terminated string path. */
    struct RSCache dat1 = RSCache_ProfileDat1Lc254();
    int flags = RSCache_Dat2ConfigLocFlags(&dat1);
    RSCACHE_CHECK(flags & RSCACHE_CONFIG_LOC_DECODE_DAT);
    RSCACHE_CHECK(!(flags & RSCACHE_CONFIG_LOC_DECODE_KRONOS));

    /* 230 gets the >= 220 payload shapes. */
    struct RSCache osrs230 = RSCache_ProfileDat2Osrs230();
    flags = RSCache_Dat2ConfigLocFlags(&osrs230);
    RSCACHE_CHECK(!(flags & RSCACHE_CONFIG_LOC_DECODE_DAT));
    RSCACHE_CHECK(flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220);
    RSCACHE_CHECK(!(flags & RSCACHE_CONFIG_LOC_DECODE_KRONOS));

    /* Kronos at 184: pre-220 payloads *and* the quirk. Before the profile layer
     * this flag had no possible source — it was defined, branched on, and never
     * set by any caller. */
    struct RSCache kronos = RSCache_ProfileDat2Osrs184Kronos();
    flags = RSCache_Dat2ConfigLocFlags(&kronos);
    RSCACHE_CHECK(flags & RSCACHE_CONFIG_LOC_DECODE_KRONOS);
    RSCACHE_CHECK(!(flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220));

    /* No profile sets LARGE_MODEL_IDS: a scan of the jan2026 cache showed
     * big-smart model ids misaligning 15k files. */
    RSCACHE_CHECK(!(RSCache_Dat2ConfigLocFlags(&osrs230) &
                    RSCACHE_CONFIG_LOC_DECODE_LARGE_MODEL_IDS));

    /* The retained archive-revision entry point still behaves as it did: a
     * modern timestamp yields the >= 220 shapes, a small integer does not. */
    RSCACHE_CHECK(RSCache_Dat2ConfigLocFlagsForRevision(1767694604) &
                  RSCACHE_CONFIG_LOC_DECODE_OSRS_220);
    RSCACHE_CHECK(!(RSCache_Dat2ConfigLocFlagsForRevision(997) &
                    RSCACHE_CONFIG_LOC_DECODE_OSRS_220));
    /* ...and cannot produce the flags only a profile knows about. */
    RSCACHE_CHECK(!(RSCache_Dat2ConfigLocFlagsForRevision(1767694604) &
                    RSCACHE_CONFIG_LOC_DECODE_KRONOS));
    RSCACHE_CHECK(!(RSCache_Dat2ConfigLocFlagsForRevision(1767694604) &
                    RSCACHE_CONFIG_LOC_DECODE_DAT));
}

static void
test_npc_flags(void)
{
    RSCACHE_TEST_GROUP("npc flags");

    /* 230 >= 210: head-icon bitfield. */
    struct RSCache osrs230 = RSCache_ProfileDat2Osrs230();
    RSCACHE_CHECK(RSCache_Dat2ConfigNpcFlags(&osrs230) &
                  RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS);

    /* 184 < 210: the single u16 sprite index. This is the branch the hardcoded
     * `rev210_head_icons = true` made unreachable — any pre-210 cache decoded
     * opcode 102 with the wrong shape and misaligned everything after it. */
    struct RSCache kronos = RSCache_ProfileDat2Osrs184Kronos();
    RSCACHE_CHECK(!(RSCache_Dat2ConfigNpcFlags(&kronos) &
                    RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS));

    /* Boundary. */
    struct RSCache cache = RSCache_ProfileZero();
    cache.version = 209;
    RSCACHE_CHECK(!(RSCache_Dat2ConfigNpcFlags(&cache) &
                    RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS));
    cache.version = 210;
    RSCACHE_CHECK(RSCache_Dat2ConfigNpcFlags(&cache) &
                  RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS);

    /* By archive revision, per RuneLite's NpcLoader gate. */
    struct RSCache by_archive = RSCache_ProfileZero();
    RSCache_ProfileSetGroupRevision(&by_archive, RSCACHE_TYPE_NPC,
                                    RSCACHE_NPC_ARCHIVE_REV_210 - 1);
    RSCACHE_CHECK(!(RSCache_Dat2ConfigNpcFlags(&by_archive) &
                    RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS));
    RSCache_ProfileSetGroupRevision(&by_archive, RSCACHE_TYPE_NPC,
                                    RSCACHE_NPC_ARCHIVE_REV_210);
    RSCACHE_CHECK(RSCache_Dat2ConfigNpcFlags(&by_archive) &
                  RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS);

    /* Unidentified keeps the modern shape — exactly what the old hardcoded
     * `true` produced, so nothing regresses for a cache we cannot identify. */
    struct RSCache unknown = RSCache_ProfileZero();
    RSCACHE_CHECK(RSCache_Dat2ConfigNpcFlags(&unknown) &
                  RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS);
    RSCACHE_CHECK(RSCache_Dat2ConfigNpcFlags(NULL) &
                  RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS);
}

static void
test_texture_and_map_flags(void)
{
    RSCACHE_TEST_GROUP("texture / map flags");

    struct RSCache osrs233 = RSCache_ProfileDat2Osrs233();
    RSCACHE_CHECK_EQ(RSCache_Dat2TextureCodecVersion(&osrs233), RSCACHE_CODEC_TEXTURE_V2);

    struct RSCache kronos = RSCache_ProfileDat2Osrs184Kronos();
    RSCACHE_CHECK_EQ(RSCache_Dat2TextureCodecVersion(&kronos), RSCACHE_CODEC_TEXTURE_V1);

    /* Unidentified keeps the full record, and a timestamp archive revision still
     * selects the simplified one. */
    struct RSCache unknown = RSCache_ProfileZero();
    RSCACHE_CHECK_EQ(RSCache_Dat2TextureCodecVersion(&unknown), RSCACHE_CODEC_TEXTURE_V1);
    RSCache_ProfileSetGroupRevision(&unknown, RSCACHE_TYPE_TEXTURE, 1767694604);
    RSCACHE_CHECK_EQ(RSCache_Dat2TextureCodecVersion(&unknown), RSCACHE_CODEC_TEXTURE_V2);

    /*
     * Terrain widths follow the ERA, not the container: u8 until OldSchool 209,
     * u16 from there. dat1 and OSRS are the easy ends.
     */
    struct RSCache dat1 = RSCache_ProfileDat1Lc254();
    RSCACHE_CHECK_EQ(RSCache_MapTerrainFlags(&dat1), RSCACHE_MAP_TERRAIN_DECODE_U8);
    RSCACHE_CHECK_EQ(RSCache_MapTerrainFlags(&osrs233), RSCACHE_MAP_TERRAIN_DECODE_U16);

    /*
     * 643 is the case that matters, and the one a container test got wrong: it is
     * dat2, so "dat2 means u16" gave it the wide layout, and its revision number
     * (643) clears every OSRS threshold while belonging to a different lineage
     * entirely. Both readings must lose to the epoch.
     *
     * Getting this wrong is not a clean failure. The u16 attribute read swallows
     * the next tile's opcode, the loop only breaks on 0 or 1, so the square
     * resynchronises into garbage: 120 of 15,376 tiles kept an underlay or overlay
     * instead of ~4,200, and the world rendered as void. See B12/D20.
     */
    struct RSCache rs643 = RSCache_ProfileDat2Rs643();
    RSCACHE_CHECK_EQ(RSCache_MapTerrainFlags(&rs643), RSCACHE_MAP_TERRAIN_DECODE_U8);

    /* An OSRS cache from before the widening is narrow too — the threshold is a
     * real revision test, not a proxy for "is this OldSchool". */
    struct RSCache osrs_old = RSCache_ProfileZero();
    osrs_old.epoch = RSCACHE_EPOCH_OSRS;
    osrs_old.version = 184;
    RSCACHE_CHECK_EQ(RSCache_MapTerrainFlags(&osrs_old), RSCACHE_MAP_TERRAIN_DECODE_U8);

    osrs_old.version = 209;
    RSCACHE_CHECK_EQ(RSCache_MapTerrainFlags(&osrs_old), RSCACHE_MAP_TERRAIN_DECODE_U16);
}

static void
test_component_rev(void)
{
    RSCACHE_TEST_GROUP("component decode rev");

    /* The 643 layout is only reachable through the epoch: its index revisions
     * share OSRS's numeric range, so no threshold can separate them. Before the
     * profile existed, RSCache_Dat2ComponentDecodeRev643 had no caller at all. */
    struct RSCache rs643 = RSCache_ProfileDat2Rs643();
    struct RSCache_Dat2ComponentDecodeRev rev = RSCache_Dat2ComponentDecodeRevFromProfile(
        &rs643, RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN);
    RSCACHE_CHECK_EQ(rev.era, RSCACHE_DAT2_COMPONENT_DECODE_ERA_643);

    /* An OSRS cache with the *same* index revision picks the other family. */
    struct RSCache osrs = RSCache_ProfileDat2Osrs230();
    rev = RSCache_Dat2ComponentDecodeRevFromProfile(&osrs, 1193);
    RSCACHE_CHECK_EQ(rev.era, RSCACHE_DAT2_COMPONENT_DECODE_ERA_OSRS);
    RSCACHE_CHECK_EQ(rev.index_revision, 1193);
    RSCACHE_CHECK_EQ(rev.game_revision, 230);

    /* 239 is past the 237 model-id widening; 230 is not. Both are answered by
     * the game revision, without needing a timestamped interfaces table. */
    struct RSCache osrs239 = RSCache_ProfileDat2Osrs239();
    rev = RSCache_Dat2ComponentDecodeRevFromProfile(
        &osrs239, RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN);
    RSCACHE_CHECK_EQ(rev.game_revision, 239);

    /* The legacy constructors still default game_revision to unknown, so they
     * keep falling back to the index-revision threshold. */
    struct RSCache_Dat2ComponentDecodeRev legacy = RSCache_Dat2ComponentDecodeRevOsrs(1193);
    RSCACHE_CHECK_EQ(legacy.game_revision, RSCACHE_REVISION_UNKNOWN);
    RSCACHE_CHECK_EQ(legacy.era, RSCACHE_DAT2_COMPONENT_DECODE_ERA_OSRS);
    struct RSCache_Dat2ComponentDecodeRev legacy643 = RSCache_Dat2ComponentDecodeRev643();
    RSCACHE_CHECK_EQ(legacy643.game_revision, RSCACHE_REVISION_UNKNOWN);
    RSCACHE_CHECK_EQ(legacy643.era, RSCACHE_DAT2_COMPONENT_DECODE_ERA_643);
}

int
main(void)
{
    printf("cache profile tests\n");
    test_profile_zero();
    test_revision_at_least();
    test_registry();
    test_container_revision_lookup();
    test_sequence_codec_selection();
    test_loc_flags();
    test_npc_flags();
    test_texture_and_map_flags();
    test_component_rev();
    return rscache_test_report("profile");
}

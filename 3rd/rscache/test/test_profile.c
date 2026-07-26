/*
 * Cache-profile and identity-model tests.
 *
 * Regression cover for the ambiguity the profile layer removes: revision numbers
 * from independent lineages (dat1 RS2, dat2 RS2, OldSchool) must not be compared
 * across games, and client-build quirks must travel with a stated identity.
 */

#include "rscache_test.h"

#include <rscache.h>

static void
test_profile_zero(void)
{
    RSCACHE_TEST_GROUP("ProfileZero is UNSET");

    struct RSCache cache = RSCache_ProfileZero();
    RSCACHE_CHECK_EQ(cache.game, RSCACHE_GAME_UNSET);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_UNSET);
    RSCACHE_CHECK_EQ(cache.revision, RSCACHE_REVISION_UNKNOWN);
    RSCACHE_CHECK_EQ(cache.quirks, 0u);
    RSCACHE_CHECK(!RSCache_ProfileIsIdentified(&cache));

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
    RSCACHE_CHECK(!RSCache_IsOsrs(&cache));
    RSCACHE_CHECK(!RSCache_IsRs2Dat2(&cache));
    RSCACHE_CHECK(!RSCache_HasQuirk(&cache, RSCACHE_QUIRK_KRONOS));

    RSCACHE_CHECK(!RSCache_IsDat1(NULL));
    RSCACHE_CHECK(!RSCache_HasQuirk(NULL, RSCACHE_QUIRK_KRONOS));
}

static void
test_named_profiles(void)
{
    RSCACHE_TEST_GROUP("named profile identity");

    struct RSCache cache;

    RSCACHE_CHECK(RSCache_ProfileByName("lc254", &cache));
    RSCACHE_CHECK_EQ(cache.game, RSCACHE_GAME_RS2);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_DAT1);
    RSCACHE_CHECK_EQ(cache.revision, 254);
    RSCACHE_CHECK_EQ(cache.quirks, 0u);
    RSCACHE_CHECK(RSCache_IsDat1(&cache));

    RSCACHE_CHECK(RSCache_ProfileByName("osrs230", &cache));
    RSCACHE_CHECK_EQ(cache.game, RSCACHE_GAME_OLDSCHOOL);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_DAT2);
    RSCACHE_CHECK_EQ(cache.revision, 230);
    RSCACHE_CHECK_EQ(cache.quirks, 0u);
    RSCACHE_CHECK(RSCache_IsOsrs(&cache));

    RSCACHE_CHECK(RSCache_ProfileByName("643", &cache));
    RSCACHE_CHECK_EQ(cache.game, RSCACHE_GAME_RS2);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_DAT2);
    RSCACHE_CHECK_EQ(cache.revision, 643);
    RSCACHE_CHECK_EQ(cache.quirks, 0u);
    RSCACHE_CHECK(RSCache_IsRs2Dat2(&cache));

    RSCACHE_CHECK(RSCache_ProfileByName("kronos", &cache));
    RSCACHE_CHECK_EQ(cache.game, RSCACHE_GAME_OLDSCHOOL);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_DAT2);
    RSCACHE_CHECK_EQ(cache.revision, 184);
    RSCACHE_CHECK(RSCache_HasQuirk(&cache, RSCACHE_QUIRK_KRONOS));

    RSCACHE_CHECK(!RSCache_ProfileByName("nope", &cache));
    RSCACHE_CHECK(!RSCache_ProfileByName(NULL, &cache));
}

static void
test_profile_for_identity(void)
{
    RSCACHE_TEST_GROUP("ProfileForIdentity");

    /* Exact (game, epoch, revision) borrows codec pins only. */
    struct RSCache cache = RSCache_ProfileForIdentity(
        RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 230, RSCACHE_QUIRK_NONE);
    RSCACHE_CHECK_EQ(cache.game, RSCACHE_GAME_OLDSCHOOL);
    RSCACHE_CHECK_EQ(cache.epoch, RSCACHE_EPOCH_DAT2);
    RSCACHE_CHECK_EQ(cache.revision, 230);
    RSCACHE_CHECK_EQ(cache.codec[RSCACHE_TYPE_SEQUENCE], RSCACHE_CODEC_SEQUENCE_V3);

    /* Caller-stated quirks are not overwritten by the registry row. */
    cache = RSCache_ProfileForIdentity(
        RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 184, RSCACHE_QUIRK_KRONOS);
    RSCACHE_CHECK(RSCache_HasQuirk(&cache, RSCACHE_QUIRK_KRONOS));

    /* No exact match: identity verbatim, codecs stay AUTO. */
    cache = RSCache_ProfileForIdentity(
        RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 235, RSCACHE_QUIRK_NONE);
    RSCACHE_CHECK_EQ(cache.revision, 235);
    RSCACHE_CHECK_EQ(cache.codec[RSCACHE_TYPE_SEQUENCE], RSCACHE_CODEC_AUTO);

    /* RS2 dat2 exact match borrows loc codec pin. */
    cache = RSCache_ProfileForIdentity(RSCACHE_GAME_RS2, RSCACHE_EPOCH_DAT2, 643, 0);
    RSCACHE_CHECK_EQ(cache.codec[RSCACHE_TYPE_LOC], RSCACHE_CODEC_LOC_RS2);
}

static void
test_name_parsers(void)
{
    RSCACHE_TEST_GROUP("identity name parsers");

    RSCACHE_CHECK_EQ(RSCache_GameFromName("oldschool"), RSCACHE_GAME_OLDSCHOOL);
    RSCACHE_CHECK_EQ(RSCache_GameFromName("rs2"), RSCACHE_GAME_RS2);
    RSCACHE_CHECK_EQ(RSCache_GameFromName("nope"), RSCACHE_GAME_UNSET);
    RSCACHE_CHECK_EQ(RSCache_GameFromName(NULL), RSCACHE_GAME_UNSET);
    RSCACHE_CHECK(strcmp(RSCache_GameName(RSCACHE_GAME_OLDSCHOOL), "oldschool") == 0);
    RSCACHE_CHECK(strcmp(RSCache_GameName(RSCACHE_GAME_RS2), "rs2") == 0);
    RSCACHE_CHECK(strcmp(RSCache_GameName(RSCACHE_GAME_UNSET), "unset") == 0);

    RSCACHE_CHECK_EQ(RSCache_EpochFromName("dat1"), RSCACHE_EPOCH_DAT1);
    RSCACHE_CHECK_EQ(RSCache_EpochFromName("dat2"), RSCACHE_EPOCH_DAT2);
    RSCACHE_CHECK_EQ(RSCache_EpochFromName("js5"), RSCACHE_EPOCH_UNSET);
    RSCACHE_CHECK(strcmp(RSCache_EpochName(RSCACHE_EPOCH_DAT1), "dat1") == 0);
    RSCACHE_CHECK(strcmp(RSCache_EpochName(RSCACHE_EPOCH_DAT2), "dat2") == 0);

    uint32_t quirks = 0;
    RSCACHE_CHECK(RSCache_QuirksFromList("none", &quirks));
    RSCACHE_CHECK_EQ(quirks, 0u);
    RSCACHE_CHECK(RSCache_QuirksFromList("kronos", &quirks));
    RSCACHE_CHECK_EQ(quirks, RSCACHE_QUIRK_KRONOS);
    RSCACHE_CHECK(RSCache_QuirksFromList("kronos,none", &quirks));
    RSCACHE_CHECK_EQ(quirks, RSCACHE_QUIRK_KRONOS);
    RSCACHE_CHECK(!RSCache_QuirksFromList("wat", &quirks));

    char buf[32];
    RSCache_QuirksName(0, buf, sizeof(buf));
    RSCACHE_CHECK(strcmp(buf, "none") == 0);
    RSCache_QuirksName(RSCACHE_QUIRK_KRONOS, buf, sizeof(buf));
    RSCACHE_CHECK(strcmp(buf, "kronos") == 0);
}

static void
test_revision_at_least_osrs(void)
{
    RSCACHE_TEST_GROUP("RevisionAtLeastOsrs (OldSchool only)");

    /* UNSET identity never takes the game-revision path; with default false it
     * clears every threshold (default true is the caller's explicit choice). */
    {
        struct RSCache unset = RSCache_ProfileZero();
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(
            &unset, RSCACHE_TYPE_LOC, 1, RSCACHE_GROUP_REVISION_UNKNOWN, false));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastRs2(
            &unset, RSCACHE_TYPE_TEXTURE, 1, RSCACHE_GROUP_REVISION_UNKNOWN, false));
    }

    /* Declared OldSchool game revision wins. */
    {
        struct RSCache cache = RSCache_ProfileZero();
        cache.game = RSCACHE_GAME_OLDSCHOOL;
        cache.epoch = RSCACHE_EPOCH_DAT2;
        cache.revision = 230;
        RSCache_ProfileSetGroupRevision(&cache, RSCACHE_TYPE_LOC, 1);
        RSCACHE_CHECK(RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_LOC, 220, 2000, false));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_LOC, 237, 2000, true));
    }

    /* RS2 profiles never satisfy OldSchool thresholds. */
    {
        struct RSCache lc254;
        RSCACHE_CHECK(RSCache_ProfileByName("lc254", &lc254));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&lc254, RSCACHE_TYPE_LOC, 220, 2000, false));
        RSCACHE_CHECK(
            !RSCache_RevisionAtLeastOsrs(&lc254, RSCACHE_TYPE_SEQUENCE, 226, 1268, false));

        struct RSCache rs643;
        RSCACHE_CHECK(RSCache_ProfileByName("643", &rs643));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&rs643, RSCACHE_TYPE_LOC, 220, 2000, false));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastOsrs(&rs643, RSCACHE_TYPE_NPC, 210, 1493, false));
    }

    /* Archive revision fallback when game revision unknown. */
    {
        struct RSCache cache = RSCache_ProfileZero();
        cache.game = RSCACHE_GAME_OLDSCHOOL;
        cache.epoch = RSCACHE_EPOCH_DAT2;
        RSCache_ProfileSetGroupRevision(&cache, RSCACHE_TYPE_LOC, 1767694604);
        RSCACHE_CHECK(RSCache_RevisionAtLeastOsrs(&cache, RSCACHE_TYPE_LOC, 220, 2000, false));
    }
}

static void
test_revision_at_least_rs2(void)
{
    RSCACHE_TEST_GROUP("RevisionAtLeastRs2 (RS2 only)");

    {
        struct RSCache rs643;
        RSCACHE_CHECK(RSCache_ProfileByName("643", &rs643));
        RSCACHE_CHECK(RSCache_RevisionAtLeastRs2(&rs643, RSCACHE_TYPE_TEXTURE, 474, -1, false));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastRs2(&rs643, RSCACHE_TYPE_TEXTURE, 700, -1, false));
    }

    /* OldSchool never satisfies RS2 thresholds when the default is false. */
    {
        struct RSCache osrs230;
        RSCACHE_CHECK(RSCache_ProfileByName("osrs230", &osrs230));
        RSCACHE_CHECK(!RSCache_RevisionAtLeastRs2(
            &osrs230, RSCACHE_TYPE_TEXTURE, 474, RSCACHE_GROUP_REVISION_UNKNOWN, false));
    }

    /* dat1 RS2 can use RS2 thresholds. */
    {
        struct RSCache lc254;
        RSCACHE_CHECK(RSCache_ProfileByName("lc254", &lc254));
        RSCACHE_CHECK(RSCache_RevisionAtLeastRs2(&lc254, RSCACHE_TYPE_TEXTURE, 200, -1, false));
    }
}

static void
test_map_locs_encrypted(void)
{
    RSCACHE_TEST_GROUP("MapLocsEncrypted");

    struct RSCache cache;

    RSCACHE_CHECK(RSCache_ProfileByName("osrs239", &cache));
    RSCACHE_CHECK(!RSCache_MapLocsEncrypted(&cache));

    RSCACHE_CHECK(RSCache_ProfileByName("osrs230", &cache));
    RSCACHE_CHECK(RSCache_MapLocsEncrypted(&cache));

    RSCACHE_CHECK(RSCache_ProfileByName("osrs233", &cache));
    RSCACHE_CHECK(RSCache_MapLocsEncrypted(&cache));

    RSCACHE_CHECK(RSCache_ProfileByName("643", &cache));
    RSCACHE_CHECK(RSCache_MapLocsEncrypted(&cache));

    RSCACHE_CHECK(RSCache_ProfileByName("kronos", &cache));
    RSCACHE_CHECK(RSCache_MapLocsEncrypted(&cache));

    RSCACHE_CHECK(RSCache_ProfileByName("lc254", &cache));
    RSCACHE_CHECK(RSCache_MapLocsEncrypted(&cache));
}

static void
test_loc_flags(void)
{
    RSCACHE_TEST_GROUP("loc flags via profile + group revision");

    struct RSCache dat1 = RSCache_ProfileForIdentity(
        RSCACHE_GAME_RS2, RSCACHE_EPOCH_DAT1, 254, RSCACHE_QUIRK_NONE);
    int flags = RSCache_Dat2ConfigLocFlags(&dat1);
    RSCACHE_CHECK(flags & RSCACHE_CONFIG_LOC_DECODE_DAT);
    RSCACHE_CHECK(!(flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220));

    struct RSCache osrs230 = RSCache_ProfileForIdentity(
        RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 230, RSCACHE_QUIRK_NONE);
    flags = RSCache_Dat2ConfigLocFlags(&osrs230);
    RSCACHE_CHECK(!(flags & RSCACHE_CONFIG_LOC_DECODE_DAT));
    RSCACHE_CHECK(flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220);
    RSCACHE_CHECK(!(flags & RSCACHE_CONFIG_LOC_DECODE_KRONOS));

    struct RSCache kronos = RSCache_ProfileForIdentity(
        RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 184, RSCACHE_QUIRK_KRONOS);
    flags = RSCache_Dat2ConfigLocFlags(&kronos);
    RSCACHE_CHECK(flags & RSCACHE_CONFIG_LOC_DECODE_KRONOS);
    RSCACHE_CHECK(!(flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220));

    struct RSCache rs643 = RSCache_ProfileForIdentity(
        RSCACHE_GAME_RS2, RSCACHE_EPOCH_DAT2, 643, RSCACHE_QUIRK_NONE);
    flags = RSCache_Dat2ConfigLocFlags(&rs643);
    RSCACHE_CHECK(flags & RSCACHE_CONFIG_LOC_DECODE_RS2);

    /* Archive revision path through ProfileForIdentity + SetGroupRevision. */
    struct RSCache by_archive = RSCache_ProfileForIdentity(
        RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, RSCACHE_REVISION_UNKNOWN, 0);
    RSCache_ProfileSetGroupRevision(&by_archive, RSCACHE_TYPE_LOC, 1767694604);
    RSCACHE_CHECK(RSCache_Dat2ConfigLocFlags(&by_archive) & RSCACHE_CONFIG_LOC_DECODE_OSRS_220);

    RSCache_ProfileSetGroupRevision(&by_archive, RSCACHE_TYPE_LOC, 997);
    RSCACHE_CHECK(!(RSCache_Dat2ConfigLocFlags(&by_archive) & RSCACHE_CONFIG_LOC_DECODE_OSRS_220));
}

int
main(void)
{
    printf("cache profile tests\n");
    test_profile_zero();
    test_named_profiles();
    test_profile_for_identity();
    test_name_parsers();
    test_revision_at_least_osrs();
    test_revision_at_least_rs2();
    test_map_locs_encrypted();
    test_loc_flags();
    return rscache_test_report("profile");
}

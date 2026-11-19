/*
 * Feature behaviour resolved beyond the table (features/feature_facts.h).
 *
 * Lighting comes from the table, each stated override replaces its one value,
 * an unstated override changes nothing, and re-reading the table drops the
 * overrides. The mouseover line belongs to the cache only for "cache"; empty
 * and "client" are the client; any other value is refused and leaves the
 * client drawing it. And a table holds an era once one is copied into it,
 * not while it is still zeroed.
 *
 * Run: make -C src test-feature-facts
 */

#include "features/feature_facts.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_lighting(void)
{
    struct ToriRS_FeatureTable era;
    struct ToriRS_FeatureFacts facts;

    memset(&era, 0, sizeof(era));
    memset(&facts, 0, sizeof(facts));
    era.npc_light_uses_type_ambient_contrast = 1;
    era.player_head_light_ambient = 128;

    ToriRS_FeatureFacts_ResolveLight(&facts, &era, 0, 0, 0, 0);
    TEST_ASSERT(ToriRS_FeatureFacts_NpcTypeAmbientContrast(&facts) == 1, "no override: the era's npc");
    TEST_ASSERT(ToriRS_FeatureFacts_PlayerHeadAmbient(&facts) == 128, "no override: the era's head");

    ToriRS_FeatureFacts_ResolveLight(&facts, &era, 1, 0, 0, 77);
    TEST_ASSERT(ToriRS_FeatureFacts_NpcTypeAmbientContrast(&facts) == 0, "a stated npc override wins");
    TEST_ASSERT(
        ToriRS_FeatureFacts_PlayerHeadAmbient(&facts) == 128, "an unstated head override is ignored");

    ToriRS_FeatureFacts_ResolveLight(&facts, &era, 0, 0, 1, 0);
    TEST_ASSERT(ToriRS_FeatureFacts_NpcTypeAmbientContrast(&facts) == 1, "npc back to the era");
    TEST_ASSERT(ToriRS_FeatureFacts_PlayerHeadAmbient(&facts) == 0, "a stated head override of 0 wins");
    TEST_ASSERT(era.player_head_light_ambient == 128, "the table is not written");

    era.npc_light_uses_type_ambient_contrast = 0;
    era.player_head_light_ambient = 96;
    ToriRS_FeatureFacts_LightFromTable(&facts, &era);
    TEST_ASSERT(ToriRS_FeatureFacts_NpcTypeAmbientContrast(&facts) == 0, "re-read: the table's npc");
    TEST_ASSERT(ToriRS_FeatureFacts_PlayerHeadAmbient(&facts) == 96, "re-read: the override is gone");
}

static void
test_mouseover_text(void)
{
    struct ToriRS_FeatureFacts facts;

    memset(&facts, 0, sizeof(facts));
    TEST_ASSERT(!ToriRS_FeatureFacts_MouseoverByCache(&facts), "zeroed: the client draws it");

    TEST_ASSERT(ToriRS_FeatureFacts_SetMouseoverText(&facts, "cache"), "cache is a value");
    TEST_ASSERT(ToriRS_FeatureFacts_MouseoverByCache(&facts), "cache: the cache draws it");

    TEST_ASSERT(ToriRS_FeatureFacts_SetMouseoverText(&facts, "client"), "client is a value");
    TEST_ASSERT(!ToriRS_FeatureFacts_MouseoverByCache(&facts), "client: the client draws it");

    ToriRS_FeatureFacts_SetMouseoverText(&facts, "cache");
    TEST_ASSERT(ToriRS_FeatureFacts_SetMouseoverText(&facts, ""), "unstated is a value");
    TEST_ASSERT(!ToriRS_FeatureFacts_MouseoverByCache(&facts), "unstated: the client draws it");

    ToriRS_FeatureFacts_SetMouseoverText(&facts, "cache");
    TEST_ASSERT(!ToriRS_FeatureFacts_SetMouseoverText(&facts, "Cache"), "anything else is refused");
    TEST_ASSERT(!ToriRS_FeatureFacts_MouseoverByCache(&facts), "refused: the client draws it");
}

static void
test_resolved(void)
{
    struct ToriRS_FeatureTable table;

    printf("TEST: a table holds an era once one is copied in\n");
    memset(&table, 0, sizeof(table));
    TEST_ASSERT(!ToriRS_Features_Resolved(&table), "a zeroed table holds no era");
    table = *ToriRS_Features_LostCity();
    TEST_ASSERT(ToriRS_Features_Resolved(&table), "a copy of lostcity does");
    table = *ToriRS_Features_OSRS();
    TEST_ASSERT(ToriRS_Features_Resolved(&table), "a copy of osrs does");
    table = *ToriRS_Features_ServerRouted();
    TEST_ASSERT(ToriRS_Features_Resolved(&table), "a copy of server_routed does");
}

int
main(void)
{
    test_lighting();
    test_mouseover_text();
    test_resolved();
    if( g_failures )
    {
        printf("feature_facts_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("feature_facts_test: ok\n");
    return 0;
}

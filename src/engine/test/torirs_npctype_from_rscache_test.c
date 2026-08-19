/*
 * NpcType.multiNpc (dat2 opcode 106): the decoder's `varbit_id`/`varp_index`/
 * `configs` fields must land on ToriRS_Npctype's `transform_*` fields
 * unmodified, since App_NpctypeResolveMultiId (app.c) feeds them straight to
 * VarPManager_ResolveTransform, the same function a loc's transform table
 * uses. `configs` already carries VarPManager_ResolveTransform's own -1
 * sentinel for a hidden entry, so this is a pass-through, not a translation --
 * the thing worth pinning is that it isn't dropped, which it was before this
 * struct had anywhere to put it.
 */
#include "engine/torirs_npctype_from_rscache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_dat2_carries_multinpc_fields(void)
{
    struct RSCache_Dat2ConfigNpc src;
    int configs[4] = {100, 101, -1, -1};
    struct ToriRS_Npctype* npctype;

    memset(&src, 0, sizeof(src));
    src.varbit_id = 42;
    src.varp_index = -1;
    src.configs = configs;
    src.configs_count = 4;

    npctype = ToriRS_NpctypeFromRSCacheDat2(999, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(npctype->transform_varbit == 42, "varbit_id carried");
    TEST_ASSERT(npctype->transform_varp == -1, "varp_index carried");
    TEST_ASSERT(npctype->transform_count == 4, "configs_count carried");
    TEST_ASSERT(npctype->transforms != NULL, "transforms array allocated");
    TEST_ASSERT(
        npctype->transforms[0] == 100 && npctype->transforms[1] == 101 &&
            npctype->transforms[2] == -1 && npctype->transforms[3] == -1,
        "transforms array contents match configs verbatim");

    /* Not ToriRS_NpctypeFree: it pulls in the whole torirs_types.c
     * component-hook dependency chain for a struct this test built by hand
     * and with none of those fields set. Free directly. */
    free(npctype->transforms);
    free(npctype);
}

static void
test_dat2_no_multinpc_leaves_transform_count_zero(void)
{
    struct RSCache_Dat2ConfigNpc src;
    struct ToriRS_Npctype* npctype;

    memset(&src, 0, sizeof(src));
    src.varbit_id = -1;
    src.varp_index = -1;

    npctype = ToriRS_NpctypeFromRSCacheDat2(1, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(npctype->transform_count == 0, "no opcode 106 -> no transform table");
    TEST_ASSERT(npctype->transforms == NULL, "no opcode 106 -> no transforms array");

    /* Not ToriRS_NpctypeFree: it pulls in the whole torirs_types.c
     * component-hook dependency chain for a struct this test built by hand
     * and with none of those fields set. Free directly. */
    free(npctype->transforms);
    free(npctype);
}

static void
test_dat1_has_no_multinpc(void)
{
    struct RSCache_Dat1ConfigNpc src;
    struct ToriRS_Npctype* npctype;

    memset(&src, 0, sizeof(src));

    npctype = ToriRS_NpctypeFromRSCacheDat1(1, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(npctype->transform_varbit == -1, "dat1 states no varbit switch");
    TEST_ASSERT(npctype->transform_varp == -1, "dat1 states no varp switch");
    TEST_ASSERT(npctype->transform_count == 0, "dat1 carries no multiNpc opcode");

    /* Not ToriRS_NpctypeFree: it pulls in the whole torirs_types.c
     * component-hook dependency chain for a struct this test built by hand
     * and with none of those fields set. Free directly. */
    free(npctype->transforms);
    free(npctype);
}

/*
 * The movement animation SET, not just the walk pair.
 *
 * `dat2_config_npc.c` has always decoded opcodes 15/16 (turn on the spot),
 * 114/115 (run) and 116/117 (crawl); the converter dropped every one of them
 * and `ToriRS_Npctype` carried a comment claiming this revision had no such
 * fields. It is the walk pair that proves the claim was about dat1: the same
 * record states both, and only half of it arrived.
 *
 * Pinned here rather than in the world, because the consumers are already
 * tested -- World_UpdateMoverMovementAndAnimation prefers runanim at speed and
 * World_EntityFace prefers turnanim, both exercised by every player -- so the
 * only thing that was ever missing is this hop.
 */
static void
test_dat2_carries_the_movement_animation_set(void)
{
    struct RSCache_Dat2ConfigNpc src;
    struct ToriRS_Npctype* npctype;

    memset(&src, 0, sizeof(src));
    src.walking_animation = 819;
    src.idle_rotate_left_animation = 820;
    src.idle_rotate_right_animation = 821;
    src.run_animation = 824;
    src.run_rotate180_animation = 825;
    src.run_rotate_left_animation = 826;
    src.run_rotate_right_animation = 827;
    src.crawl_animation = 828;
    src.crawl_rotate180_animation = 829;
    src.crawl_rotate_left_animation = 830;
    src.crawl_rotate_right_animation = 831;

    npctype = ToriRS_NpctypeFromRSCacheDat2(998, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(npctype->walkanim == 819, "walk animation carried");
    TEST_ASSERT(npctype->turnanim_l == 820, "opcode 15 turn-left carried");
    TEST_ASSERT(npctype->turnanim_r == 821, "opcode 16 turn-right carried");
    TEST_ASSERT(npctype->runanim == 824, "opcode 114 run carried");
    TEST_ASSERT(npctype->runanim_b == 825, "run-180 carried");
    TEST_ASSERT(npctype->runanim_l == 826, "run-left carried");
    TEST_ASSERT(npctype->runanim_r == 827, "run-right carried");
    TEST_ASSERT(npctype->crawlanim == 828, "opcode 116 crawl carried");
    TEST_ASSERT(npctype->crawlanim_b == 829, "crawl-180 carried");
    TEST_ASSERT(npctype->crawlanim_l == 830, "crawl-left carried");
    TEST_ASSERT(npctype->crawlanim_r == 831, "crawl-right carried");

    /* An absent opcode is -1, not 0: 0 is a real sequence id and the mover's
     * `!= -1` fallbacks would bind it. */
    memset(&src, 0, sizeof(src));
    npctype = ToriRS_NpctypeFromRSCacheDat2(997, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(npctype->runanim == -1, "absent run animation reads -1, not 0");
    TEST_ASSERT(npctype->turnanim_l == -1, "absent turn animation reads -1, not 0");
    TEST_ASSERT(npctype->crawlanim == -1, "absent crawl animation reads -1, not 0");
}

/*
 * NpcType opcode 130: restart the idle when an action animation finishes. Same
 * story -- decoded since rev 236, read by nothing, and it is what
 * World_StepEntityAnimation gates its idle reset on.
 */
static void
test_dat2_carries_idle_anim_restart(void)
{
    struct RSCache_Dat2ConfigNpc src;
    struct ToriRS_Npctype* npctype;

    memset(&src, 0, sizeof(src));
    src.idle_anim_restart = true;
    npctype = ToriRS_NpctypeFromRSCacheDat2(996, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(npctype->idle_anim_restart, "opcode 130 carried");

    memset(&src, 0, sizeof(src));
    npctype = ToriRS_NpctypeFromRSCacheDat2(995, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(!npctype->idle_anim_restart, "absent opcode 130 stays false");
}

int
main(void)
{
    g_failures = 0;
    test_dat2_carries_multinpc_fields();
    test_dat2_carries_the_movement_animation_set();
    test_dat2_carries_idle_anim_restart();
    test_dat2_no_multinpc_leaves_transform_count_zero();
    test_dat1_has_no_multinpc();
    return g_failures ? 1 : 0;
}

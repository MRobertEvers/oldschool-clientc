/*
 * NpcType.multiNpc (dat2 opcode 106): the decoder's `varbit_id`/`varp_index`/
 * `configs` fields must land on ToriRS_Npctype's `transform_*` fields
 * unmodified, since App_NpctypeResolveMultiId (app.c) feeds them straight to
 * VarPManager_ResolveTransform, the same function a loc's transform table
 * uses. `configs` already carries VarPManager_ResolveTransform's own -1
 * sentinel for a hidden entry, so this is a pass-through, not a translation --
 * the thing worth pinning is that it isn't dropped, which it was before this
 * struct had anywhere to put it.
 *
 * And then the other half of what multiNpc means: the entity's own facts --
 * size, turn speed, stance set -- are the shell's, the record the wire named
 * (ToriRS_NpctypeEntityFacts, below).
 */
#include "engine/torirs_npctype_from_rscache.h"
#include "engine/torirs_types.h"

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

    ToriRS_NpctypeFree(npctype);
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

    ToriRS_NpctypeFree(npctype);
}

/*
 * The examine string, npc config opcode 3.
 *
 * No pristine dat2 record states one -- OldSchool retired the opcode in 2006
 * and sends npc examine from the server -- so the converter used to drop the
 * field on the floor and every npc examined as "It's a <name>.". This tree's
 * content pack authors examine text into the npc archive under that opcode,
 * exactly as it already did for a loc, and the hop from the decoded record to
 * ToriRS_Npctype is the half that was missing.
 */
static void
test_dat2_carries_the_examine_string(void)
{
    struct RSCache_Dat2ConfigNpc src;
    struct ToriRS_Npctype* npctype;
    char desc[] = "A dangerous cave-dwelling creature.";

    memset(&src, 0, sizeof(src));
    src.desc = desc;

    npctype = ToriRS_NpctypeFromRSCacheDat2(1, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(
        strcmp(npctype->desc, "A dangerous cave-dwelling creature.") == 0,
        "opcode 3 lands on ToriRS_Npctype.desc");

    ToriRS_NpctypeFree(npctype);
}

/* A record that states none leaves the field empty, which is what makes the
 * Examine handler's "It's a <name>." fallback reachable. */
static void
test_dat2_without_examine_leaves_desc_empty(void)
{
    struct RSCache_Dat2ConfigNpc src;
    struct ToriRS_Npctype* npctype;

    memset(&src, 0, sizeof(src));

    npctype = ToriRS_NpctypeFromRSCacheDat2(1, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(npctype->desc[0] == '\0', "no opcode 3 -> empty desc");

    ToriRS_NpctypeFree(npctype);
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
    TEST_ASSERT(!npctype->no_rotation_flag, "dat1 has no opcode 109");
    ToriRS_NpctypeFree(npctype);

    /* Opcode 17's third and fourth ids cross here, once: Client-TS swaps the
     * pair onto the entity, and doing it in the conversion gives walkanim_r one
     * meaning in both eras (the step toward a higher yaw). */
    memset(&src, 0, sizeof(src));
    src.walkanim_r = 822; /* opcode 17's third id */
    src.walkanim_l = 823; /* opcode 17's fourth id */
    npctype = ToriRS_NpctypeFromRSCacheDat1(2, &src);
    TEST_ASSERT(npctype->walkanim_r == 823, "dat1: the fourth id is the right step");
    TEST_ASSERT(npctype->walkanim_l == 822, "dat1: the third id is the left step");

    ToriRS_NpctypeFree(npctype);
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
    src.rotate_left_animation = 822;
    src.rotate_right_animation = 823;
    src.rotation_flag = true;
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
    /* No crossing on dat2: rev-239 plays opcode 17's fourth id (field1485)
     * for the step toward a higher yaw, which is walkanim_r. */
    TEST_ASSERT(npctype->walkanim_l == 822, "dat2: rotate-left is the left step");
    TEST_ASSERT(npctype->walkanim_r == 823, "dat2: rotate-right is the right step");
    TEST_ASSERT(!npctype->no_rotation_flag, "no opcode 109: the flag stays clear");
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
    /* A zeroed decoder struct reads as "opcode 109 seen" (rotation_flag false). */
    TEST_ASSERT(npctype->no_rotation_flag, "opcode 109 reaches the npc type");
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

/*
 * The rung/shell rule: the entity's size, turn speed and stance set are the
 * SHELL's -- the record the wire named -- and never the rung's
 * (deob Statics.method4320 reads them off class86.field1237, the
 * untransformed type). The rung draws the body and names the ops.
 *
 * `verzik_initial_base` states no size and no readyanim under a shell that
 * states both; the Corsair Curse's sick crew are rungs standing in
 * `human_ready` under shells sitting up in bed at size 2; the Recipe for
 * Disaster council's frozen shells state no readyanim at all, which is what
 * freezes them. Each case below is one of those.
 */

static int g_facts_failures;

#define FACTS_CHECK(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));                                 \
            g_facts_failures++;                                                                    \
        }                                                                                          \
    } while( 0 )

/* Every animation field absent, size absent. */
static void
facts_blank(struct ToriRS_Npctype* type)
{
    memset(type, 0, sizeof(*type));
    type->size = 0;
    type->readyanim = -1;
    type->walkanim = -1;
    type->walkanim_b = -1;
    type->walkanim_l = -1;
    type->walkanim_r = -1;
    type->turnanim_l = -1;
    type->runanim = -1;
    type->runanim_b = -1;
    type->runanim_l = -1;
    type->runanim_r = -1;
}

static void
test_npc_entity_facts(void)
{
    struct ToriRS_Npctype rung;
    struct ToriRS_Npctype shell;
    struct ToriRS_NpcEntityFacts facts;

    printf("TEST: multinpc rung/shell entity facts\n");

    /* A record that states everything, with no shell at all: every field
     * lands in its own slot -- distinct values, so a swapped pair shows. */
    facts_blank(&rung);
    rung.size = 3;
    rung.readyanim = 100;
    rung.walkanim = 101;
    rung.walkanim_b = 102;
    rung.walkanim_l = 103;
    rung.walkanim_r = 104;
    rung.turnanim_l = 105;
    rung.runanim = 106;
    rung.runanim_b = 107;
    rung.runanim_l = 108;
    rung.runanim_r = 109;
    rung.turn_speed = 17;
    ToriRS_NpctypeEntityFacts(&rung, NULL, &facts);
    FACTS_CHECK(facts.size == 3, "size did not come from the record");
    FACTS_CHECK(facts.readyanim == 100, "readyanim");
    FACTS_CHECK(facts.walkanim == 101, "walkanim");
    FACTS_CHECK(facts.walkanim_b == 102, "walkanim_b");
    FACTS_CHECK(facts.walkanim_l == 103, "walkanim_l");
    FACTS_CHECK(facts.walkanim_r == 104, "walkanim_r");
    /* The one renamed field: the entity carries one turn animation and the
     * record has a left and a right. It is the LEFT one. */
    FACTS_CHECK(facts.turnanim == 105, "turnanim did not come from turnanim_l");
    FACTS_CHECK(facts.runanim == 106, "runanim");
    FACTS_CHECK(facts.runanim_b == 107, "runanim_b");
    FACTS_CHECK(facts.runanim_l == 108, "runanim_l");
    FACTS_CHECK(facts.runanim_r == 109, "runanim_r");
    FACTS_CHECK(facts.turn_speed == 17, "turn_speed");

    /* Verzik: a rung that states nothing under a shell that states size and
     * idle. */
    facts_blank(&rung);
    facts_blank(&shell);
    shell.size = 5;
    shell.readyanim = 7700;
    ToriRS_NpctypeEntityFacts(&rung, &shell, &facts);
    FACTS_CHECK(facts.size == 5, "a size-less rung did not take its shell's size");
    FACTS_CHECK(facts.readyanim == 7700, "an animation-less rung did not take its shell's idle");

    /* The sick crew: a rung that states its own stance, size and turn speed
     * under a shell that states others. The shell wins every one. */
    facts_blank(&rung);
    rung.size = 1;
    rung.readyanim = 808; /* human_ready */
    rung.walkanim = 819;
    rung.turn_speed = 32;
    facts_blank(&shell);
    shell.size = 2;
    shell.readyanim = 6000; /* dream_cyrisus_sitting_up */
    shell.walkanim = 6000;
    shell.turn_speed = 0;
    ToriRS_NpctypeEntityFacts(&rung, &shell, &facts);
    FACTS_CHECK(facts.readyanim == 6000, "the rung's readyanim beat its shell's");
    FACTS_CHECK(facts.walkanim == 6000, "the rung's walkanim beat its shell's");
    FACTS_CHECK(facts.size == 2, "the rung's size beat its shell's");
    FACTS_CHECK(facts.turn_speed == 0, "the rung's turn speed beat its shell's");

    /* The frozen council: the shell states no idle, the rung does. The npc
     * has none -- the absence is the shell's answer, not a gap to fill. */
    facts_blank(&rung);
    rung.readyanim = 4242;
    facts_blank(&shell);
    ToriRS_NpctypeEntityFacts(&rung, &shell, &facts);
    FACTS_CHECK(facts.readyanim == -1, "a shell with no idle took the rung's");
    FACTS_CHECK(facts.size == 1, "a blank shell did not settle on size 1");

    /* No shell at all: a blank record comes back blank. */
    facts_blank(&rung);
    ToriRS_NpctypeEntityFacts(&rung, NULL, &facts);
    FACTS_CHECK(facts.size == 1, "a shell-less blank record did not default to size 1");
    FACTS_CHECK(facts.readyanim == -1, "a shell-less blank record grew a readyanim");
    FACTS_CHECK(facts.walkanim == -1, "a shell-less blank record grew a walkanim");
    FACTS_CHECK(facts.runanim_r == -1, "a shell-less blank record grew a run animation");

    /* A record read against ITSELF is the same answer -- the drawn id and the
     * wire id are the same for an ordinary npc. */
    facts_blank(&rung);
    rung.size = 0;
    ToriRS_NpctypeEntityFacts(&rung, &rung, &facts);
    FACTS_CHECK(facts.size == 1, "a record read against itself changed");
    FACTS_CHECK(facts.readyanim == -1, "a record read against itself grew an animation");

    /* Every field comes from its own slot of the shell. */
    facts_blank(&rung);
    rung.walkanim = 55;
    facts_blank(&shell);
    shell.readyanim = 200;
    shell.walkanim = 201;
    shell.walkanim_b = 202;
    shell.walkanim_l = 203;
    shell.walkanim_r = 204;
    shell.turnanim_l = 205;
    shell.runanim = 206;
    shell.runanim_b = 207;
    shell.runanim_l = 208;
    shell.runanim_r = 209;
    ToriRS_NpctypeEntityFacts(&rung, &shell, &facts);
    FACTS_CHECK(facts.readyanim == 200, "readyanim");
    FACTS_CHECK(facts.walkanim == 201, "walkanim");
    FACTS_CHECK(facts.walkanim_b == 202, "walkanim_b");
    FACTS_CHECK(facts.walkanim_l == 203, "walkanim_l");
    FACTS_CHECK(facts.walkanim_r == 204, "walkanim_r");
    FACTS_CHECK(facts.turnanim == 205, "turnanim did not come from the shell's left turn");
    FACTS_CHECK(facts.runanim == 206, "runanim");
    FACTS_CHECK(facts.runanim_b == 207, "runanim_b");
    FACTS_CHECK(facts.runanim_l == 208, "runanim_l");
    FACTS_CHECK(facts.runanim_r == 209, "runanim_r");
}

int
main(void)
{
    g_failures = 0;
    test_dat2_carries_multinpc_fields();
    test_dat2_carries_the_movement_animation_set();
    test_dat2_carries_idle_anim_restart();
    test_dat2_carries_the_examine_string();
    test_dat2_without_examine_leaves_desc_empty();
    test_dat2_no_multinpc_leaves_transform_count_zero();
    test_dat1_has_no_multinpc();
    test_npc_entity_facts();
    return (g_failures || g_facts_failures) ? 1 : 0;
}

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
 * And then the other half of what multiNpc means: a rung record is a DELTA off
 * its shell, so the entity's own facts are the rung's where it states them and
 * the shell's where it does not (ToriRS_NpctypeEntityFacts, below).
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

    free(npctype->transforms);
    free(npctype);
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

/*
 * The rung/shell gap-fill.
 *
 * A multinpc is one shell record plus a rung per state, and this client
 * resolves the rung before it spawns anything -- so without this the whole
 * entity is built out of a record that was never authored to stand alone.
 * `verzik_initial_base` states a name, a model, a chathead, two ops and
 * nothing else; its shell carries size 5 and a readyanim. Read straight off
 * the rung, Verzik is size 1 -- which puts her draw origin two tiles
 * south-west of her own dais -- and readyanim -1, so nothing ever binds and
 * she sits in the model's bind pose. One record's absent fields, two bugs that
 * look unrelated.
 *
 * Which is why the cases here are about ABSENCE, and about the one asymmetry:
 * size's "absent" is 0 or 1 while every animation's is -1, because size has no
 * sentinel and 1 is both the default and a real answer.
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

    /* A record that states everything, with no shell at all. Nothing is
     * filled, and every field lands in its own slot -- the values are
     * distinct so a pair that swapped would show. */
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

    /* Verzik: a rung that states nothing but its name, under a shell that
     * states the two fields whose absence broke her. */
    facts_blank(&rung);
    facts_blank(&shell);
    shell.size = 5;
    shell.readyanim = 7700;
    ToriRS_NpctypeEntityFacts(&rung, &shell, &facts);
    FACTS_CHECK(facts.size == 5, "a size-less rung did not take its shell's size");
    FACTS_CHECK(facts.readyanim == 7700, "an animation-less rung did not take its shell's idle");

    /* A rung that states the field itself keeps it -- including when it
     * disagrees with the shell, which 49 records in this cache do. This is the
     * half a "the shell wins" rewrite gets wrong while every gap still fills. */
    facts_blank(&rung);
    rung.readyanim = 42;
    rung.size = 2;
    facts_blank(&shell);
    shell.readyanim = 7700;
    shell.size = 5;
    ToriRS_NpctypeEntityFacts(&rung, &shell, &facts);
    FACTS_CHECK(facts.readyanim == 42, "the shell overrode a readyanim the rung stated");
    FACTS_CHECK(facts.size == 2, "the shell overrode a size the rung stated");

    /*
     * Size's absence is not an animation's. There is no sentinel: the record
     * default is 0 and 1 is a real, common answer, so "absent" has to mean
     * "not bigger than one" -- and the fill only ever grows. A shell of size 1
     * under a rung of size 1 must not move, and neither must a rung of size 3
     * under a shell of size 1.
     */
    facts_blank(&rung);
    facts_blank(&shell);
    shell.size = 5;
    ToriRS_NpctypeEntityFacts(&rung, &shell, &facts);
    FACTS_CHECK(facts.size == 5, "a size-0 rung did not take the shell's size");

    rung.size = 1;
    ToriRS_NpctypeEntityFacts(&rung, &shell, &facts);
    FACTS_CHECK(facts.size == 5, "a size-1 rung did not take the shell's size");

    facts_blank(&rung);
    rung.size = 3;
    facts_blank(&shell);
    shell.size = 1;
    ToriRS_NpctypeEntityFacts(&rung, &shell, &facts);
    FACTS_CHECK(facts.size == 3, "a shell of size 1 shrank a rung that states a size");

    facts_blank(&rung);
    facts_blank(&shell);
    ToriRS_NpctypeEntityFacts(&rung, &shell, &facts);
    FACTS_CHECK(facts.size == 1, "two size-less records did not settle on 1");

    /*
     * No shell at all. Every npc that is not a multinpc arrives this way, and
     * a record with nothing in it must come back with nothing in it rather
     * than reaching for a record that is not there.
     */
    facts_blank(&rung);
    ToriRS_NpctypeEntityFacts(&rung, NULL, &facts);
    FACTS_CHECK(facts.size == 1, "a shell-less blank record did not default to size 1");
    FACTS_CHECK(facts.readyanim == -1, "a shell-less blank record grew a readyanim");
    FACTS_CHECK(facts.walkanim == -1, "a shell-less blank record grew a walkanim");
    FACTS_CHECK(facts.runanim_r == -1, "a shell-less blank record grew a run animation");

    /* A record read against ITSELF is the same answer -- the drawn id and the
     * wire id are the same for an ordinary npc, and the caller does not have
     * to notice. */
    facts_blank(&rung);
    rung.size = 0;
    ToriRS_NpctypeEntityFacts(&rung, &rung, &facts);
    FACTS_CHECK(facts.size == 1, "a record read against itself changed");
    FACTS_CHECK(facts.readyanim == -1, "a record read against itself grew an animation");

    /*
     * Every animation fills independently. A rung that states one and a shell
     * that states all of them: the stated one survives and the other nine come
     * from the shell, each into its own slot. A fill that reads the wrong
     * source field shows up here as one value in two places.
     */
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
    FACTS_CHECK(facts.walkanim == 55, "the rung's own walkanim was overwritten");
    /* And the same shell against a rung that states nothing, so the one field
     * held back above is filled here too -- otherwise walkanim is the one slot
     * whose fill is never exercised. */
    {
        struct ToriRS_NpcEntityFacts filled;
        facts_blank(&rung);
        ToriRS_NpctypeEntityFacts(&rung, &shell, &filled);
        FACTS_CHECK(filled.walkanim == 201, "walkanim did not fill");
    }
    FACTS_CHECK(facts.readyanim == 200, "readyanim did not fill");
    FACTS_CHECK(facts.walkanim_b == 202, "walkanim_b did not fill");
    FACTS_CHECK(facts.walkanim_l == 203, "walkanim_l did not fill");
    FACTS_CHECK(facts.walkanim_r == 204, "walkanim_r did not fill");
    FACTS_CHECK(facts.turnanim == 205, "turnanim did not fill from the shell's left turn");
    FACTS_CHECK(facts.runanim == 206, "runanim did not fill");
    FACTS_CHECK(facts.runanim_b == 207, "runanim_b did not fill");
    FACTS_CHECK(facts.runanim_l == 208, "runanim_l did not fill");
    FACTS_CHECK(facts.runanim_r == 209, "runanim_r did not fill");
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

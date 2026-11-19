/*
 * The interface-model stores (ui/uitree_if_models.h).
 *
 * Heads: one entry per component, a new one with no animation and an update
 * that keeps the one it had, pending again after every set, growth past the
 * first allocation, and a removal that moves the last entry into the hole.
 * Player models: a claim that takes the first free slot and its scene id
 * before it grows, a freed slot that keeps its scene id, an animation set on
 * the slot that holds the component. Forgetting a component, a checkpoint
 * restore of both tables, and the local figure's changed-since-built test.
 *
 * Run: make -C src test-if-models
 */

#include "ui/uitree_if_models.h"

#include <stdio.h>
#include <stdlib.h>
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

#define SCENE_BASE 0x60000000

static void
test_head_set_and_update(void)
{
    struct UIIfModels m;
    int i;

    memset(&m, 0, sizeof(m));
    TEST_ASSERT(UIIfModels_HeadIndex(&m, 100) == -1, "an empty store has no head");

    i = UIIfModels_SetHead(&m, 100, UI_IFHEAD_NPC, 7, 0, 3);
    TEST_ASSERT(i == 0 && m.head_count == 1, "the first head is entry 0");
    TEST_ASSERT(m.heads[0].anim_id == -1, "a new head has no animation");
    TEST_ASSERT(m.heads[0].kind == UI_IFHEAD_NPC && m.heads[0].npc_id == 7, "the head is stored");
    TEST_ASSERT(m.heads[0].close_serial == 3 && m.heads[0].applied_gen == 0, "pending, stamped");

    TEST_ASSERT(UIIfModels_SetHeadAnim(&m, 100, 55), "the head takes an animation");
    m.heads[0].applied_gen = 9;
    TEST_ASSERT(!UIIfModels_SetHeadAnim(&m, 101, 56), "a component with no head takes none");

    m.heads[0].applied_gen = 9;
    i = UIIfModels_SetHead(&m, 100, UI_IFHEAD_OBJ, 4151, 250, 4);
    TEST_ASSERT(i == 0 && m.head_count == 1, "a second set on the component replaces its entry");
    TEST_ASSERT(m.heads[0].anim_id == 55, "an update keeps the animation");
    TEST_ASSERT(m.heads[0].kind == UI_IFHEAD_OBJ && m.heads[0].npc_id == 4151, "and takes the new head");
    TEST_ASSERT(m.heads[0].zoom == 250 && m.heads[0].close_serial == 4, "with its zoom and serial");
    TEST_ASSERT(m.heads[0].applied_gen == 0, "and is pending again");

    /* A6: a head that could not composite is remembered as missed; a new set
     * or its load task's end makes it worth attempting again. */
    m.heads[0].missed = 1;
    UIIfModels_ForgetMiss(&m, 100);
    TEST_ASSERT(m.heads[0].missed == 0, "the load task's end forgets the miss");
    UIIfModels_ForgetMiss(&m, 999);
    m.heads[0].missed = 1;
    (void)UIIfModels_SetHead(&m, 100, UI_IFHEAD_OBJ, 4152, 250, 4);
    TEST_ASSERT(m.heads[0].missed == 0, "a new set forgets the miss");

    UIIfModels_Free(&m);
    TEST_ASSERT(m.heads == NULL && m.head_count == 0 && m.head_cap == 0, "free empties the store");
}

static void
test_head_growth_and_removal(void)
{
    struct UIIfModels m;

    memset(&m, 0, sizeof(m));
    for( int c = 0; c < 40; c++ )
        (void)UIIfModels_SetHead(&m, 1000 + c, UI_IFHEAD_MODEL, c, 0, 0);
    TEST_ASSERT(m.head_count == 40, "forty components, forty entries");
    TEST_ASSERT(m.head_cap >= 40, "the table grew");
    TEST_ASSERT(UIIfModels_HeadIndex(&m, 1039) == 39, "the last one is found");

    UIIfModels_RemoveHeadAt(&m, 3);
    TEST_ASSERT(m.head_count == 39, "a removal drops one entry");
    TEST_ASSERT(m.heads[3].com_id == 1039, "the last entry moves into the hole");
    TEST_ASSERT(UIIfModels_HeadIndex(&m, 1003) == -1, "the removed one is gone");

    UIIfModels_Free(&m);
}

static void
test_player_model_slots(void)
{
    struct UIIfModels m;
    struct UIIfPlayerModel* pm;

    memset(&m, 0, sizeof(m));
    pm = UIIfModels_ClaimPlayerModel(&m, 200, SCENE_BASE, 5);
    TEST_ASSERT(pm == &m.player_models[0], "the first claim is slot 0");
    TEST_ASSERT(pm->scene_id == SCENE_BASE && pm->anim_id == -1, "its scene id is the base");
    TEST_ASSERT(pm->close_serial == 5 && !pm->built && pm->applied_gen == 0, "and it is empty");
    pm->built = 1;
    pm->slots[0] = 99;

    pm = UIIfModels_ClaimPlayerModel(&m, 201, SCENE_BASE, 5);
    TEST_ASSERT(pm->scene_id == SCENE_BASE + 1 && m.player_model_count == 2, "a second claim grows");

    UIIfModels_FreePlayerModelAt(&m, 0);
    TEST_ASSERT(m.player_models[0].com_id == -1 && !m.player_models[0].built, "a freed slot holds nothing");
    TEST_ASSERT(m.player_models[0].scene_id == SCENE_BASE, "and keeps its scene id");
    TEST_ASSERT(UIIfModels_PlayerModelIndex(&m, 200) == -1, "the freed component has no slot");

    pm = UIIfModels_ClaimPlayerModel(&m, 202, SCENE_BASE, 6);
    TEST_ASSERT(pm == &m.player_models[0] && m.player_model_count == 2, "a claim takes the free slot first");
    TEST_ASSERT(pm->scene_id == SCENE_BASE, "with the scene id it kept");
    TEST_ASSERT(pm->slots[0] == 0 && pm->close_serial == 6, "and the slot comes back zeroed");

    TEST_ASSERT(UIIfModels_SetPlayerModelAnim(&m, 201, 808), "the slot holding 201 takes an animation");
    TEST_ASSERT(m.player_models[1].anim_id == 808, "on the slot that holds it");
    TEST_ASSERT(!UIIfModels_SetPlayerModelAnim(&m, 203, 1), "a component with no slot takes none");

    for( int c = 0; c < 20; c++ )
        (void)UIIfModels_ClaimPlayerModel(&m, 300 + c, SCENE_BASE, 0);
    TEST_ASSERT(m.player_model_count == 22, "claims past the first allocation grow the table");
    TEST_ASSERT(m.player_models[21].scene_id == SCENE_BASE + 21, "each new slot's scene id is its index");

    UIIfModels_Free(&m);
}

static void
test_forget(void)
{
    struct UIIfModels m;

    memset(&m, 0, sizeof(m));
    (void)UIIfModels_SetHead(&m, 10, UI_IFHEAD_MODEL, 1, 0, 0);
    (void)UIIfModels_SetHead(&m, 11, UI_IFHEAD_MODEL, 2, 0, 0);
    (void)UIIfModels_ClaimPlayerModel(&m, 11, SCENE_BASE, 0);
    (void)UIIfModels_ClaimPlayerModel(&m, 12, SCENE_BASE, 0);

    UIIfModels_Forget(&m, 11);
    TEST_ASSERT(m.head_count == 1 && m.heads[0].com_id == 10, "the component's head goes");
    TEST_ASSERT(m.player_model_count == 2, "its player-model slot stays");
    TEST_ASSERT(m.player_models[0].com_id == -1, "freed");
    TEST_ASSERT(m.player_models[1].com_id == 12, "and the other component keeps its own");

    UIIfModels_Forget(&m, 99);
    TEST_ASSERT(m.head_count == 1 && m.player_models[1].com_id == 12, "forgetting a stranger changes nothing");
    UIIfModels_Free(&m);
}

static void
test_restore(void)
{
    struct UIIfModels m;
    struct UIIfHead heads[2];
    struct UIIfPlayerModel models[1];

    memset(&m, 0, sizeof(m));
    memset(heads, 0, sizeof(heads));
    memset(models, 0, sizeof(models));
    heads[0].com_id = 5;
    heads[1].com_id = 6;
    models[0].com_id = 7;
    models[0].scene_id = SCENE_BASE;
    models[0].built = 1;

    (void)UIIfModels_ClaimPlayerModel(&m, 40, SCENE_BASE, 0);
    (void)UIIfModels_ClaimPlayerModel(&m, 41, SCENE_BASE, 0);
    m.player_models[1].built = 1;
    (void)UIIfModels_SetHead(&m, 50, UI_IFHEAD_NPC, 1, 0, 0);

    UIIfModels_RestoreHeads(&m, heads, 2);
    TEST_ASSERT(m.head_count == 2 && m.heads[1].com_id == 6, "the checkpoint's heads come back");
    UIIfModels_RestoreHeads(&m, NULL, 0);
    TEST_ASSERT(m.head_count == 0, "an empty checkpoint empties the heads");

    UIIfModels_RestorePlayerModels(&m, models, 1);
    TEST_ASSERT(m.player_models[0].com_id == 7 && m.player_models[0].built, "the checkpoint's slot comes back");
    TEST_ASSERT(m.player_model_count == 2, "the slot past it is kept");
    TEST_ASSERT(m.player_models[1].com_id == -1 && !m.player_models[1].built, "freed");
    TEST_ASSERT(m.player_models[1].scene_id == SCENE_BASE + 1, "with its scene id");
    UIIfModels_Free(&m);

    memset(&m, 0, sizeof(m));
    UIIfModels_RestorePlayerModels(&m, models, 1);
    TEST_ASSERT(m.player_model_count == 1 && m.player_model_cap >= 1, "a restore grows an empty table");
    UIIfModels_Free(&m);
}

static void
test_local_player(void)
{
    struct UIIfModels m;
    int slots[12] = { 1, 2, 3 };
    int colors[5] = { 4, 5 };

    memset(&m, 0, sizeof(m));
    TEST_ASSERT(UIIfModels_LocalPlayerChanged(&m, slots, colors, 0), "never built is a change");
    UIIfModels_LocalPlayerBuilt(&m, slots, colors, 0);
    TEST_ASSERT(m.local_player.built, "built");
    TEST_ASSERT(!UIIfModels_LocalPlayerChanged(&m, slots, colors, 0), "the same appearance is not");
    TEST_ASSERT(UIIfModels_LocalPlayerChanged(&m, slots, colors, 1), "a gender change is");
    slots[11] = 9;
    TEST_ASSERT(UIIfModels_LocalPlayerChanged(&m, slots, colors, 0), "a slot change is");
    slots[11] = 0;
    colors[4] = 9;
    TEST_ASSERT(UIIfModels_LocalPlayerChanged(&m, slots, colors, 0), "a colour change is");
}

int
main(void)
{
    test_head_set_and_update();
    test_head_growth_and_removal();
    test_player_model_slots();
    test_forget();
    test_restore();
    test_local_player();
    if( g_failures )
    {
        printf("if_models: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("if_models: all passed\n");
    return 0;
}

/*
 * Stack-menu expansion for player picks (Client-TS addViewportOptions):
 * a tile-centred player pick re-emits co-located NPCs/players; the local
 * player never gets OPPLAYER rows.
 */
#include "game/rs_minimenu_build.h"
#include "game/rs_minimenu_world.h"
#include "revconfig/revconfig.h"
#include "test_harness.h"
#include "ui/uitree_minimenu.h"
#include "world.h"
#include "world_pickset.h"

#include <stdio.h>
#include <string.h>

int g_failures;

static int
menu_has_substr(struct UIMinimenu const* menu, char const* needle)
{
    for( int i = 0; i < menu->option_count; i++ )
        if( strstr(menu->options[i].text, needle) )
            return 1;
    return 0;
}

static int
menu_player_row_count(struct UIMinimenu const* menu)
{
    int n = 0;
    for( int i = 0; i < menu->option_count; i++ )
        if( menu->options[i].pick.kind == UI_MINIMENU_PICK_PLAYER )
            n++;
    return n;
}

static int
menu_npc_row_count(struct UIMinimenu const* menu)
{
    int n = 0;
    for( int i = 0; i < menu->option_count; i++ )
        if( menu->options[i].pick.kind == UI_MINIMENU_PICK_NPC )
            n++;
    return n;
}

static void
test_local_player_pick_expands_stacked_npcs(void)
{
    printf("TEST: local player pick expands stacked NPCs\n");

    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct World* world = World_TestMakeReady(104);
    world->local_pid = 7;

    int lp = World_PlayerSpawn(world, 100, 0, 25, 25, idle);
    struct WorldEntity_Player* local = World_EntityPoolGet(&world->entities.player, lp);
    local->server_pid = 7;
    snprintf(local->name, sizeof(local->name), "You");

    int n0 = World_NpcSpawn(world, 101, 500, 0, 25, 25, 1, idle);
    struct WorldEntity_NPC* a = World_EntityPoolGet(&world->entities.npc, n0);
    snprintf(a->name, sizeof(a->name), "GoblinA");
    snprintf(a->actions[0].name, sizeof(a->actions[0].name), "Talk-to");

    int n1 = World_NpcSpawn(world, 102, 501, 0, 25, 25, 1, idle);
    struct WorldEntity_NPC* b = World_EntityPoolGet(&world->entities.npc, n1);
    snprintf(b->name, sizeof(b->name), "GoblinB");
    snprintf(b->actions[0].name, sizeof(b->actions[0].name), "Talk-to");

    struct World_PickSet picks;
    World_PickSetReset(&picks);
    World_PickSetAdd(&picks, 100, WORLD_PICK_PLAYER, 25, 25, 0);

    char player_ops[5][40];
    int player_ops_primary[5] = { 0 };
    memset(player_ops, 0, sizeof(player_ops));
    snprintf(player_ops[0], sizeof(player_ops[0]), "Follow");

    struct RS_MinimenuBuildCtx ctx = {
        .selection = { .mode = RS_MINIMENU_SELECT_NONE },
        .player_ops = (char const(*)[40])player_ops,
        .player_ops_primary = player_ops_primary,
        .world = world,
        .world_pickset = &picks,
        .click_in_world = true,
    };
    struct UIMinimenu menu;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);

    TEST_ASSERT(menu_has_substr(&menu, "GoblinA"), "stacked GoblinA options present");
    TEST_ASSERT(menu_has_substr(&menu, "GoblinB"), "stacked GoblinB options present");
    TEST_ASSERT(menu_npc_row_count(&menu) >= 2, "at least one row per stacked NPC");
    TEST_ASSERT(menu_player_row_count(&menu) == 0, "local player emits no OPPLAYER rows");
    TEST_ASSERT(!menu_has_substr(&menu, "Follow"), "Follow not shown for local");

    World_Free(world);
}

static void
test_other_player_stack_rows(void)
{
    printf("TEST: other players on stack get rows; local does not\n");

    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct World* world = World_TestMakeReady(104);
    world->local_pid = 7;

    int lp = World_PlayerSpawn(world, 200, 0, 30, 30, idle);
    struct WorldEntity_Player* local = World_EntityPoolGet(&world->entities.player, lp);
    local->server_pid = 7;
    snprintf(local->name, sizeof(local->name), "You");

    int op = World_PlayerSpawn(world, 201, 0, 30, 30, idle);
    struct WorldEntity_Player* other = World_EntityPoolGet(&world->entities.player, op);
    other->server_pid = 8;
    other->combat_level = 10;
    snprintf(other->name, sizeof(other->name), "Bob");

    struct World_PickSet picks;
    World_PickSetReset(&picks);
    /* Local wins draw — pick is local; expansion still lists Bob. */
    World_PickSetAdd(&picks, 200, WORLD_PICK_PLAYER, 30, 30, 0);

    char player_ops[5][40];
    int player_ops_primary[5] = { 1, 0, 0, 0, 0 };
    memset(player_ops, 0, sizeof(player_ops));
    snprintf(player_ops[0], sizeof(player_ops[0]), "Follow");
    snprintf(player_ops[1], sizeof(player_ops[1]), "Trade with");

    struct RS_MinimenuBuildCtx ctx = {
        .selection = { .mode = RS_MINIMENU_SELECT_NONE },
        .player_ops = (char const(*)[40])player_ops,
        .player_ops_primary = player_ops_primary,
        .world = world,
        .world_pickset = &picks,
        .click_in_world = true,
    };
    struct UIMinimenu menu;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);

    TEST_ASSERT(menu_has_substr(&menu, "Bob"), "other player Bob listed");
    TEST_ASSERT(menu_has_substr(&menu, "Follow"), "Follow for Bob");
    TEST_ASSERT(menu_player_row_count(&menu) >= 1, "at least one player row for Bob");
    for( int i = 0; i < menu.option_count; i++ )
    {
        if( menu.options[i].pick.kind != UI_MINIMENU_PICK_PLAYER )
            continue;
        TEST_ASSERT(
            menu.options[i].pick.secondary_id == 8, "player pick pid is Bob (8), not local");
    }

    World_Free(world);
}

static int
menu_obj_row_count(struct UIMinimenu const* menu)
{
    int n = 0;
    for( int i = 0; i < menu->option_count; i++ )
        if( menu->options[i].pick.kind == UI_MINIMENU_PICK_OBJ )
            n++;
    return n;
}

static void
test_local_player_pick_expands_ground_items(void)
{
    printf("TEST: local player pick expands ground items on tile\n");

    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct World* world = World_TestMakeReady(104);
    world->local_pid = 7;
    char actions[5][32] = { { 0 } };

    int lp = World_PlayerSpawn(world, 400, 0, 22, 22, idle);
    struct WorldEntity_Player* local = World_EntityPoolGet(&world->entities.player, lp);
    local->server_pid = 7;

    World_ObjStackAdd(world, 401, 22, 22, 0, 995, 1, "Coins", actions);
    World_ObjStackAdd(world, 402, 22, 22, 0, 526, 1, "Bones", actions);

    struct World_PickSet picks;
    World_PickSetReset(&picks);
    World_PickSetAdd(&picks, 400, WORLD_PICK_PLAYER, 22, 22, 0);

    struct RS_MinimenuBuildCtx ctx = {
        .selection = { .mode = RS_MINIMENU_SELECT_NONE },
        .world = world,
        .world_pickset = &picks,
        .click_in_world = true,
    };
    struct UIMinimenu menu;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);

    TEST_ASSERT(menu_has_substr(&menu, "Coins"), "Coins on tile appear");
    TEST_ASSERT(menu_has_substr(&menu, "Bones"), "Bones on tile appear");
    TEST_ASSERT(menu_has_substr(&menu, "Take"), "Take default for empty op2");
    TEST_ASSERT(menu_obj_row_count(&menu) >= 2, "at least one row family per item");

    World_Free(world);
}

static void
test_obj_pick_expands_siblings(void)
{
    printf("TEST: obj pick expands sibling ground items on tile\n");

    struct World* world = World_TestMakeReady(104);
    char actions[5][32] = { { 0 } };
    World_ObjStackAdd(world, 501, 15, 15, 0, 995, 5, "Coins", actions);
    World_ObjStackAdd(world, 502, 15, 15, 0, 526, 1, "Bones", actions);

    struct World_PickSet picks;
    World_PickSetReset(&picks);
    World_PickSetAdd(&picks, 501, WORLD_PICK_OBJSTACK, 15, 15, 0);

    struct RS_MinimenuBuildCtx ctx = {
        .selection = { .mode = RS_MINIMENU_SELECT_NONE },
        .world = world,
        .world_pickset = &picks,
        .click_in_world = true,
    };
    struct UIMinimenu menu;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);

    TEST_ASSERT(menu_has_substr(&menu, "Coins"), "picked Coins listed");
    TEST_ASSERT(menu_has_substr(&menu, "Bones"), "sibling Bones listed");

    World_Free(world);
}

static void
test_local_alone_no_player_ops(void)
{
    printf("TEST: local alone yields no player rows\n");

    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct World* world = World_TestMakeReady(104);
    world->local_pid = 7;

    int lp = World_PlayerSpawn(world, 300, 0, 40, 40, idle);
    struct WorldEntity_Player* local = World_EntityPoolGet(&world->entities.player, lp);
    local->server_pid = 7;
    snprintf(local->name, sizeof(local->name), "You");

    struct World_PickSet picks;
    World_PickSetReset(&picks);
    World_PickSetAdd(&picks, 300, WORLD_PICK_PLAYER, 40, 40, 0);

    char player_ops[5][40];
    int player_ops_primary[5] = { 0 };
    memset(player_ops, 0, sizeof(player_ops));
    snprintf(player_ops[0], sizeof(player_ops[0]), "Follow");

    struct RS_MinimenuBuildCtx ctx = {
        .selection = { .mode = RS_MINIMENU_SELECT_NONE },
        .player_ops = (char const(*)[40])player_ops,
        .player_ops_primary = player_ops_primary,
        .world = world,
        .world_pickset = &picks,
        .click_in_world = true,
    };
    struct UIMinimenu menu;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);

    TEST_ASSERT(menu_player_row_count(&menu) == 0, "solo local: zero player rows");
    TEST_ASSERT(!menu_has_substr(&menu, "Follow"), "solo local: no Follow");

    World_Free(world);
}

static void
test_player_get_by_element_id(void)
{
    printf("TEST: World_PlayerGetByElementId\n");

    struct World* world = World_TestMakeReady(64);
    int pi = World_PlayerSpawn(world, 42, 0, 5, 5, World_TestDefaultIdle());
    struct WorldEntity_Player* p = World_EntityPoolGet(&world->entities.player, pi);
    p->server_pid = 3;

    TEST_ASSERT(World_PlayerGetByElementId(world, 42) == p, "hit");
    TEST_ASSERT(World_PlayerGetByElementId(world, 99) == NULL, "miss");
    World_Free(world);
}

int
main(void)
{
    test_player_get_by_element_id();
    test_local_player_pick_expands_stacked_npcs();
    test_other_player_stack_rows();
    test_local_player_pick_expands_ground_items();
    test_obj_pick_expands_siblings();
    test_local_alone_no_player_ops();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}

/*
 * Stack-menu expansion for player picks (Client-TS addViewportOptions):
 * a tile-centred player pick re-emits co-located NPCs/players; the local
 * player never gets OPPLAYER rows.
 */
#include "game/rs_attack_option.h"
#include "game/rs_minimenu_build.h"
#include "game/rs_minimenu_world.h"
#include "engine/torirs_objtype_from_rscache.h"
#include "revconfig/revconfig.h"
#include "test_harness.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_minimenu.h"
#include "world.h"
#include "world_pickset.h"

#include <stdio.h>
#include <string.h>

int g_failures;

static void
test_widget_target_priority_default(void)
{
    struct UITree* tree = UITree_New(1);
    struct UITreeNodeSpec spec = { 0 };
    int32_t index;

    spec.type = UIELEM_RS_LAYER;
    spec.component_id = 1;
    index = UITree_Push(tree, -1, &spec);
    TEST_ASSERT(index >= 0, "widget target-priority fixture pushed");
    if( index >= 0 )
        TEST_ASSERT(
            tree->components[index].target_priority == 4,
            "rev239 widgets default target priority to op slot 4");
    UITree_Free(tree);
}

static int
menu_has_substr(struct UIMinimenu const* menu, char const* needle)
{
    for( int i = 0; i < menu->option_count; i++ )
        if( strstr(menu->options[i].text, needle) )
            return 1;
    return 0;
}

struct TestEvents
{
    int component_id;
    int mask;
};

static int
test_events_for_component(void* user, int component_id, int sub_id)
{
    struct TestEvents const* events = (struct TestEvents const*)user;
    (void)sub_id;
    return events && component_id == events->component_id ? events->mask : 0;
}

static int
menu_action_count(struct UIMinimenu const* menu, int action)
{
    int count = 0;
    for( int i = 0; i < menu->option_count; i++ )
        count += menu->options[i].action == action;
    return count;
}

static int
menu_index_of(struct UIMinimenu const* menu, char const* needle)
{
    for( int i = 0; i < menu->option_count; i++ )
        if( strstr(menu->options[i].text, needle) )
            return i;
    return -1;
}

static void
test_if3_continue_uses_resume(void)
{
    struct UITree* tree = UITree_New(4);
    struct UITreeNodeSpec spec = { 0 };
    struct UITreeBehavior behavior = { .click_mask = 1 };
    struct TestEvents events = { .component_id = 1, .mask = 1 };
    struct RS_MinimenuBuildCtx ctx = {
        .tree = tree,
        .events_for_component = test_events_for_component,
        .events_user = &events,
    };
    struct UIMinimenu menu;

    spec.type = UIELEM_RS_TEXT;
    spec.component_id = events.component_id;
    spec.width = 120;
    spec.height = 20;
    spec.behavior = &behavior;
    spec.u.rs_text.text = "Prompt";
    TEST_ASSERT(UITree_Push(tree, -1, &spec) >= 0, "continue fixture pushed");
    UITree_LayoutResolve(tree, 0, 0, 200, 100);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);

    TEST_ASSERT(
        menu_action_count(&menu, REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON) == 1,
        "IF3 event bit 0 builds action 30 / RESUME_PAUSEBUTTON");
    TEST_ASSERT(
        menu_action_count(&menu, REVCONFIG_MINIMENU_IF_BUTTON) == 0,
        "continue does not fall back to IF_BUTTON");
    UITree_Free(tree);
}

static void
test_if3_item_uses_only_scripted_ops(void)
{
    struct CacheProvider provider = { 0 };
    struct UITree* tree = UITree_New(4);
    struct UITreeNodeSpec parent = { 0 };
    struct UITreeNodeSpec child = { 0 };
    struct TestEvents events = { .component_id = 1, .mask = (1 << 1) | (1 << 10) };
    struct RS_MinimenuBuildCtx ctx = {
        .tree = tree,
        .provider = &provider,
        .events_for_component = test_events_for_component,
        .events_user = &events,
    };
    struct UIMinimenu menu;
    int32_t parent_index;

    CacheProvider_InitEngineCaches(&provider);
    parent.type = UIELEM_RS_LAYER;
    parent.component_id = events.component_id;
    parent.width = 64;
    parent.height = 64;
    parent_index = UITree_Push(tree, -1, &parent);
    TEST_ASSERT(parent_index >= 0, "IF3 item parent pushed");

    child.type = UIELEM_CC_OBJ;
    child.component_id = 2;
    child.dynamic = 1;
    child.dynamic_child_index = 0;
    child.width = 32;
    child.height = 32;
    child.u.cc_obj.obj_id = 1;
    child.u.cc_obj.obj_count = 1;
    snprintf(child.menu_options.ops[0], sizeof(child.menu_options.ops[0]), "Script op");
    snprintf(child.menu_options.ops[9], sizeof(child.menu_options.ops[9]), "Terminal op");
    {
        int32_t child_index = UITree_Push(tree, parent_index, &child);
        TEST_ASSERT(child_index >= 0, "IF3 item child pushed");
        if( child_index >= 0 )
        {
            tree->components[child_index].item_id = child.u.cc_obj.obj_id;
            tree->components[child_index].item_count = child.u.cc_obj.obj_count;
        }
    }

    UITree_LayoutResolve(tree, 0, 0, 200, 100);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    TEST_ASSERT(menu.option_count == 3, "IF3 cell has Cancel plus exactly two scripted rows");
    TEST_ASSERT(menu_has_substr(&menu, "Script op"), "first scripted row is present");
    TEST_ASSERT(menu_has_substr(&menu, "Terminal op"), "terminal scripted row is present");
    TEST_ASSERT(
        menu_action_count(&menu, REVCONFIG_MINIMENU_OPHELD6) == 0,
        "IF3 cell does not synthesize a second ObjType Examine row");

    UITree_Free(tree);
    CacheProvider_FreeEngineCaches(&provider);
}

static void
test_if3_item_onop_and_target_rows_match_rev239(void)
{
    struct CacheProvider provider = { 0 };
    struct ToriRS_Objtype* obj = calloc(1, sizeof(*obj));
    struct UITree* tree = UITree_New(4);
    struct UITreeNodeSpec parent = { 0 };
    struct UITreeNodeSpec child = { 0 };
    struct TestEvents events = { .component_id = 1, .mask = (1 << 11) };
    struct RS_MinimenuBuildCtx ctx = {
        .tree = tree,
        .provider = &provider,
        .events_for_component = test_events_for_component,
        .events_user = &events,
    };
    struct UIMinimenu menu;
    int32_t parent_index;

    CacheProvider_InitEngineCaches(&provider);
    obj->id = 1;
    snprintf(obj->name, sizeof(obj->name), "Fixture");
    snprintf(obj->inv_actions[0], sizeof(obj->inv_actions[0]), "Wear");
    CacheProvider_ObjtypeAdd(&provider, obj->id, obj);

    parent.type = UIELEM_RS_LAYER;
    parent.component_id = events.component_id;
    parent.width = 64;
    parent.height = 64;
    snprintf(parent.menu_options.target_verb, sizeof(parent.menu_options.target_verb), "Use");
    parent_index = UITree_Push(tree, -1, &parent);
    TEST_ASSERT(parent_index >= 0, "rev239 item parent pushed");
    if( parent_index >= 0 )
        tree->components[parent_index].target_priority = 6;

    child.type = UIELEM_CC_OBJ;
    child.component_id = 2;
    child.dynamic = 1;
    child.dynamic_child_index = 0;
    child.width = 32;
    child.height = 32;
    child.u.cc_obj.obj_id = obj->id;
    child.u.cc_obj.obj_count = 1;
    snprintf(child.menu_options.ops[0], sizeof(child.menu_options.ops[0]), "Wear");
    snprintf(child.menu_options.ops[9], sizeof(child.menu_options.ops[9]), "Examine");
    {
        int32_t child_index = UITree_Push(tree, parent_index, &child);
        TEST_ASSERT(child_index >= 0, "rev239 item child pushed");
        if( child_index >= 0 )
        {
            tree->components[child_index].item_id = child.u.cc_obj.obj_id;
            tree->components[child_index].item_count = child.u.cc_obj.obj_count;
            UITree_HooksMut(&tree->components[child_index])->on_op.script_id = 123;
        }
    }

    UITree_LayoutResolve(tree, 0, 0, 200, 100);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    TEST_ASSERT(menu_has_substr(&menu, "Use @lre@ Fixture"), "target verb builds Use row");
    TEST_ASSERT(menu_has_substr(&menu, "Wear @lre@ Fixture"), "on_op bypasses absent op bit");
    TEST_ASSERT(menu_has_substr(&menu, "Drop @lre@ Fixture"), "default Drop occupies op 7");
    TEST_ASSERT(menu_has_substr(&menu, "Examine @lre@ Fixture"), "on_op exposes terminal op");
    TEST_ASSERT(
        menu_action_count(&menu, REVCONFIG_MINIMENU_OPHELDT_START) == 1,
        "item target verb arms held-item selection");
    {
        int const examine = menu_index_of(&menu, "Examine @lre@ Fixture");
        int const use = menu_index_of(&menu, "Use @lre@ Fixture");
        int const drop = menu_index_of(&menu, "Drop @lre@ Fixture");
        int const wear = menu_index_of(&menu, "Wear @lre@ Fixture");
        /* Options draw in reverse insertion order. Official method5229 inserts
         * the target before slot 6 while walking 9 -> 0, yielding the visible
         * order Wear, Drop, Use, Examine. */
        TEST_ASSERT(
            examine >= 0 && examine < use && use < drop && drop < wear,
            "target priority places visible Use between Drop and Examine");
        TEST_ASSERT(
            menu.options[examine].action > 1000 && menu.options[drop].action < 1000,
            "operations above target priority are deprioritized like method5229");
    }

    UITree_Free(tree);
    CacheProvider_FreeEngineCaches(&provider);
}

static void
test_dat2_stacking_behaviour_is_not_boolean(void)
{
    struct RSCache_Dat2ConfigObj raw = { 0 };
    struct ToriRS_Objtype* obj;

    raw.stacking_behaviour = 2;
    obj = ToriRS_ObjtypeFromRSCacheDat2(0, &raw);
    TEST_ASSERT(obj && obj->stackable == 0, "stacking behaviour 2 is not stackable");
    ToriRS_ObjtypeFree(obj);

    raw.stacking_behaviour = 1;
    obj = ToriRS_ObjtypeFromRSCacheDat2(0, &raw);
    TEST_ASSERT(obj && obj->stackable == 1, "only stacking behaviour 1 is stackable");
    ToriRS_ObjtypeFree(obj);
}

/* ObjType.team (opcode 115) is what App_WorldApplyPlayerAppearance folds into
 * WorldEntity_Player::team for the Attack row's team-cape override. It reached
 * the engine struct only after this adaptor line was added — before it, every
 * team read 0 and the override silently never fired. */
static void
test_dat2_obj_team_decodes(void)
{
    printf("TEST: ObjType.team survives the rscache adaptor\n");

    struct RSCache_Dat2ConfigObj raw = { 0 };
    struct ToriRS_Objtype* obj;

    obj = ToriRS_ObjtypeFromRSCacheDat2(0, &raw);
    TEST_ASSERT(obj && obj->team == 0, "no team by default");
    ToriRS_ObjtypeFree(obj);

    raw.team = 5;
    obj = ToriRS_ObjtypeFromRSCacheDat2(0, &raw);
    TEST_ASSERT(obj && obj->team == 5, "team cape id carried through");
    ToriRS_ObjtypeFree(obj);
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

/*
 * ---------------------------------------------------------------------------
 * Controls-settings Attack options (rs_attack_option.h).
 * ---------------------------------------------------------------------------
 */

/* A world with a local player at (30,30) and one other body on the same tile.
 * `npc` picks which: an NPC named "Goblin" with Attack on op slot 1 and
 * Talk-to on slot 0, or a player "Bob". Both are levelled by `level`. */
struct AttackFixture
{
    struct World* world;
    struct World_PickSet picks;
    char player_ops[5][40];
    int player_ops_primary[5];
};

static void
attack_fixture_init(struct AttackFixture* fx, int local_level, int other_level, bool npc)
{
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();

    memset(fx, 0, sizeof(*fx));
    fx->world = World_TestMakeReady(104);
    fx->world->local_pid = 7;

    int lp = World_PlayerSpawn(fx->world, 200, 0, 30, 30, idle);
    struct WorldEntity_Player* local = World_EntityPoolGet(&fx->world->entities.player, lp);
    local->server_pid = 7;
    local->combat_level = local_level;
    snprintf(local->name, sizeof(local->name), "You");

    if( npc )
    {
        int ni = World_NpcSpawn(fx->world, 201, 500, 0, 30, 30, 1, idle);
        struct WorldEntity_NPC* goblin = World_EntityPoolGet(&fx->world->entities.npc, ni);
        goblin->combat_level = other_level;
        goblin->visible_ops = 0x1f;
        snprintf(goblin->name, sizeof(goblin->name), "Goblin");
        snprintf(goblin->actions[0].name, sizeof(goblin->actions[0].name), "Talk-to");
        snprintf(goblin->actions[1].name, sizeof(goblin->actions[1].name), "Attack");
    }
    else
    {
        int op = World_PlayerSpawn(fx->world, 201, 0, 30, 30, idle);
        struct WorldEntity_Player* other = World_EntityPoolGet(&fx->world->entities.player, op);
        other->server_pid = 8;
        other->combat_level = other_level;
        snprintf(other->name, sizeof(other->name), "Bob");
        snprintf(fx->player_ops[0], sizeof(fx->player_ops[0]), "Follow");
        snprintf(fx->player_ops[1], sizeof(fx->player_ops[1]), "Attack");
        fx->player_ops_primary[0] = 1;
        fx->player_ops_primary[1] = 1;
    }

    World_PickSetReset(&fx->picks);
    World_PickSetAdd(&fx->picks, 201, npc ? WORLD_PICK_NPC : WORLD_PICK_PLAYER, 30, 30, 0);
}

static void
attack_fixture_build(
    struct AttackFixture* fx,
    int player_option,
    int npc_option,
    struct UIMinimenu* out)
{
    struct RS_MinimenuBuildCtx ctx = {
        .selection = { .mode = RS_MINIMENU_SELECT_NONE },
        .player_ops = (char const(*)[40])fx->player_ops,
        .player_ops_primary = fx->player_ops_primary,
        .player_attack_option = player_option,
        .npc_attack_option = npc_option,
        .world = fx->world,
        .world_pickset = &fx->picks,
        .click_in_world = true,
    };
    UIMinimenu_Reset(out);
    RS_Minimenu_AddWorldRows(&ctx, out);
}

/** The row whose text starts with `verb`, or -1. Deprioritized rows carry the
 *  reference's +2000 bias in `action`, which is what these tests read. */
static int
menu_action_for_verb(struct UIMinimenu const* menu, char const* verb)
{
    size_t n = strlen(verb);
    for( int i = 0; i < menu->option_count; i++ )
        if( strncmp(menu->options[i].text, verb, n) == 0 )
            return menu->options[i].action;
    return -1;
}

static void
test_npc_attack_option(void)
{
    printf("TEST: NPC 'Attack' options gate the Attack row\n");

    struct AttackFixture fx;
    struct UIMinimenu menu;

    /* Same level, so "Depends on combat levels" has nothing to act on. */
    attack_fixture_init(&fx, 50, 50, true);

    attack_fixture_build(&fx, RS_ATTACK_OPTION_DEPENDS, RS_ATTACK_OPTION_LEFTCLICK, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") == REVCONFIG_MINIMENU_OPNPC2,
        "Left-click where available keeps Attack at its natural priority");

    attack_fixture_build(&fx, RS_ATTACK_OPTION_DEPENDS, RS_ATTACK_OPTION_RIGHTCLICK, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") ==
            UIMinimenu_ActionDeprioritize(REVCONFIG_MINIMENU_OPNPC2),
        "Always right-click deprioritizes Attack");
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Talk-to") == REVCONFIG_MINIMENU_OPNPC1,
        "and leaves the non-attack ops alone");

    attack_fixture_build(&fx, RS_ATTACK_OPTION_DEPENDS, RS_ATTACK_OPTION_HIDDEN, &menu);
    TEST_ASSERT(!menu_has_substr(&menu, "Attack"), "Hidden emits no Attack row at all");
    TEST_ASSERT(menu_has_substr(&menu, "Talk-to"), "but keeps the other ops");

    /* Equal levels: Depends must NOT deprioritize. */
    attack_fixture_build(&fx, RS_ATTACK_OPTION_DEPENDS, RS_ATTACK_OPTION_DEPENDS, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") == REVCONFIG_MINIMENU_OPNPC2,
        "Depends leaves an equal-level NPC left-clickable");
    World_Free(fx.world);

    /* Higher-level NPC: the reference's level test sits outside its attack
     * pass, so Talk-to sinks with Attack. */
    attack_fixture_init(&fx, 10, 21, true);
    attack_fixture_build(&fx, RS_ATTACK_OPTION_DEPENDS, RS_ATTACK_OPTION_DEPENDS, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") ==
            UIMinimenu_ActionDeprioritize(REVCONFIG_MINIMENU_OPNPC2),
        "Depends deprioritizes Attack on a higher-level NPC");
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Talk-to") ==
            UIMinimenu_ActionDeprioritize(REVCONFIG_MINIMENU_OPNPC1),
        "and every other op with it");

    /* Left-click where available ignores the level difference entirely. */
    attack_fixture_build(&fx, RS_ATTACK_OPTION_DEPENDS, RS_ATTACK_OPTION_LEFTCLICK, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") == REVCONFIG_MINIMENU_OPNPC2,
        "Left-click where available ignores the level difference");
    World_Free(fx.world);
}

static void
test_player_attack_option(void)
{
    printf("TEST: player 'Attack' options gate the Attack row\n");

    struct AttackFixture fx;
    struct UIMinimenu menu;

    attack_fixture_init(&fx, 50, 50, false);

    attack_fixture_build(&fx, RS_ATTACK_OPTION_LEFTCLICK, RS_ATTACK_OPTION_DEPENDS, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") == REVCONFIG_MINIMENU_OPPLAYER2,
        "Left-click where available keeps Attack at its natural priority");

    attack_fixture_build(&fx, RS_ATTACK_OPTION_RIGHTCLICK, RS_ATTACK_OPTION_DEPENDS, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") ==
            UIMinimenu_ActionDeprioritize(REVCONFIG_MINIMENU_OPPLAYER2),
        "Always right-click deprioritizes Attack");
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Follow") == REVCONFIG_MINIMENU_OPPLAYER1,
        "and leaves the other player ops alone");

    attack_fixture_build(&fx, RS_ATTACK_OPTION_HIDDEN, RS_ATTACK_OPTION_DEPENDS, &menu);
    TEST_ASSERT(!menu_has_substr(&menu, "Attack"), "Hidden emits no Attack row");
    TEST_ASSERT(menu_has_substr(&menu, "Follow"), "but keeps Follow");

    attack_fixture_build(&fx, RS_ATTACK_OPTION_DEPENDS, RS_ATTACK_OPTION_DEPENDS, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") == REVCONFIG_MINIMENU_OPPLAYER2,
        "Depends leaves an equal-level player left-clickable");

    /* Team capes override the setting both ways. Unlike the NPC path, the
     * player path never sinks the non-attack ops. */
    {
        struct WorldEntity_Player* local = World_PlayerGetByServerPid(fx.world, 7);
        struct WorldEntity_Player* bob = World_PlayerGetByElementId(fx.world, 201);

        local->team = 3;
        bob->team = 4;
        attack_fixture_build(&fx, RS_ATTACK_OPTION_RIGHTCLICK, RS_ATTACK_OPTION_DEPENDS, &menu);
        TEST_ASSERT(
            menu_action_for_verb(&menu, "Attack") == REVCONFIG_MINIMENU_OPPLAYER2,
            "a different team is left-click-attackable despite Always right-click");

        bob->team = 3;
        attack_fixture_build(&fx, RS_ATTACK_OPTION_LEFTCLICK, RS_ATTACK_OPTION_DEPENDS, &menu);
        TEST_ASSERT(
            menu_action_for_verb(&menu, "Attack") ==
                UIMinimenu_ActionDeprioritize(REVCONFIG_MINIMENU_OPPLAYER2),
            "the same team is never left-click-attackable despite Left-click");
        TEST_ASSERT(
            menu_action_for_verb(&menu, "Follow") == REVCONFIG_MINIMENU_OPPLAYER1,
            "and the team override does not touch the other ops");

        /* A zero on either side hands the decision back to the setting. */
        bob->team = 0;
        attack_fixture_build(&fx, RS_ATTACK_OPTION_LEFTCLICK, RS_ATTACK_OPTION_DEPENDS, &menu);
        TEST_ASSERT(
            menu_action_for_verb(&menu, "Attack") == REVCONFIG_MINIMENU_OPPLAYER2,
            "an untagged target falls back to the setting");
        local->team = 0;
    }
    World_Free(fx.world);

    attack_fixture_init(&fx, 10, 40, false);
    attack_fixture_build(&fx, RS_ATTACK_OPTION_DEPENDS, RS_ATTACK_OPTION_DEPENDS, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") ==
            UIMinimenu_ActionDeprioritize(REVCONFIG_MINIMENU_OPPLAYER2),
        "Depends deprioritizes Attack on a higher-level player");
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Follow") == REVCONFIG_MINIMENU_OPPLAYER1,
        "and, unlike the NPC path, leaves the other ops at normal priority");
    World_Free(fx.world);
}

static bool
test_clan_member_is(void* user, char const* name)
{
    return user && name && strcmp((char const*)user, name) == 0;
}

static void
test_player_attack_option_clan(void)
{
    printf("TEST: 'Right-click for clanmates' consults the clan channel\n");

    struct AttackFixture fx;
    struct UIMinimenu menu;
    char clanmate[] = "Bob";

    attack_fixture_init(&fx, 50, 50, false);

    /* No predicate: this tree has no clan chat, so nobody is a clanmate and
     * the option must behave exactly like Left-click where available. */
    attack_fixture_build(&fx, RS_ATTACK_OPTION_CLAN, RS_ATTACK_OPTION_DEPENDS, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") == REVCONFIG_MINIMENU_OPPLAYER2,
        "without a clan channel the option is Left-click where available");

    {
        struct RS_MinimenuBuildCtx ctx = {
            .selection = { .mode = RS_MINIMENU_SELECT_NONE },
            .player_ops = (char const(*)[40])fx.player_ops,
            .player_ops_primary = fx.player_ops_primary,
            .player_attack_option = RS_ATTACK_OPTION_CLAN,
            .npc_attack_option = RS_ATTACK_OPTION_DEPENDS,
            .is_clan_member = test_clan_member_is,
            .clan_user = clanmate,
            .world = fx.world,
            .world_pickset = &fx.picks,
            .click_in_world = true,
        };
        UIMinimenu_Reset(&menu);
        RS_Minimenu_AddWorldRows(&ctx, &menu);
        TEST_ASSERT(
            menu_action_for_verb(&menu, "Attack") ==
                UIMinimenu_ActionDeprioritize(REVCONFIG_MINIMENU_OPPLAYER2),
            "a clanmate's Attack is deprioritized");

        ctx.clan_user = (void*)"Someone else";
        UIMinimenu_Reset(&menu);
        RS_Minimenu_AddWorldRows(&ctx, &menu);
        TEST_ASSERT(
            menu_action_for_verb(&menu, "Attack") == REVCONFIG_MINIMENU_OPPLAYER2,
            "a non-clanmate's Attack is not");
    }

    World_Free(fx.world);
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
    test_widget_target_priority_default();
    test_dat2_stacking_behaviour_is_not_boolean();
    test_if3_continue_uses_resume();
    test_if3_item_uses_only_scripted_ops();
    test_if3_item_onop_and_target_rows_match_rev239();
    test_player_get_by_element_id();
    test_local_player_pick_expands_stacked_npcs();
    test_other_player_stack_rows();
    test_local_player_pick_expands_ground_items();
    test_obj_pick_expands_siblings();
    test_local_alone_no_player_ops();
    test_npc_attack_option();
    test_player_attack_option();
    test_player_attack_option_clan();
    test_dat2_obj_team_decodes();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}

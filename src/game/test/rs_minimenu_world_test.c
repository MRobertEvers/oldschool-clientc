/*
 * Stack-menu expansion for player picks (Client-TS addViewportOptions):
 * a tile-centred player pick re-emits co-located NPCs/players; the local
 * player never gets OPPLAYER rows.
 */
#include "game/rs_attack_option.h"
#include "game/rs_ui_slots.h"
#include "game/rs_minimenu_build.h"
#include "game/rs_minimenu_world.h"
#include "engine/torirs_objtype_from_rscache.h"
#include "revconfig/revconfig.h"
#include "test_harness.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_minimenu.h"
#include "world.h"
#include "world/wev.h"
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

/*
 * The dat1 half of the case above: a 2004 cache states the continue prompt as
 * `buttontype=pause` and carries no armed events at all (there is no
 * IF_SETEVENTS before rev 230). The row must still be built -- and, critically,
 * must NOT claim a numbered op slot.
 *
 * action_index is what the click dispatcher reads to decide a row is an IF3
 * `IF_BUTTON<n>` for the server. Tagging this row op 0 sent "Click here to
 * continue" out as IF_BUTTON1 on a component the server had armed nothing on,
 * and suppressed the local apply that turns it into RESUME_PAUSEBUTTON -- so
 * every npc dialogue was unclickable while looking perfectly correct.
 */
static void
test_dat1_continue_is_not_a_numbered_op(void)
{
    struct UITree* tree = UITree_New(4);
    struct UITreeNodeSpec spec = { 0 };
    struct UITreeBehavior behavior = { .button_type = REVCONFIG_BUTTON_TYPE_CONTINUE };
    struct RS_MinimenuBuildCtx ctx = { .tree = tree };
    struct UIMinimenu menu;
    int found = 0;

    spec.type = UIELEM_RS_TEXT;
    spec.component_id = 4886;
    spec.width = 350;
    spec.height = 17;
    spec.behavior = &behavior;
    spec.u.rs_text.text = "Click here to continue";
    /* What the dat1 decoder leaves for an empty `option=` on a pause button. */
    snprintf(spec.menu_options.option, sizeof(spec.menu_options.option), "Continue");
    TEST_ASSERT(UITree_Push(tree, -1, &spec) >= 0, "dat1 continue fixture pushed");
    UITree_LayoutResolve(tree, 0, 0, 400, 100);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);

    TEST_ASSERT(
        menu_action_count(&menu, REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON) == 1,
        "a cache buttontype=pause builds one RESUME_PAUSEBUTTON row");
    for( int i = 0; i < menu.option_count; i++ )
    {
        if( menu.options[i].action != REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON )
            continue;
        found = 1;
        TEST_ASSERT(
            menu.options[i].action_index < 0,
            "a button-type row carries no numbered op index");
    }
    TEST_ASSERT(found, "the RESUME_PAUSEBUTTON row was found");
    UITree_Free(tree);
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
    snprintf(
        parent.menu_options.target_verb, sizeof(parent.menu_options.target_verb), "Use");
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
            UITree_HookSet(&UITree_HooksMut(&tree->components[child_index])->on_op,
                           123, NULL, 0, 0, NULL, 0);
        }
    }

    UITree_LayoutResolve(tree, 0, 0, 200, 100);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    TEST_ASSERT(menu_has_substr(&menu, "Use @lre@Fixture"), "target verb builds Use row");
    TEST_ASSERT(menu_has_substr(&menu, "Wear @lre@Fixture"), "on_op bypasses absent op bit");
    TEST_ASSERT(menu_has_substr(&menu, "Drop @lre@Fixture"), "default Drop occupies op 7");
    TEST_ASSERT(menu_has_substr(&menu, "Examine @lre@Fixture"), "on_op exposes terminal op");
    TEST_ASSERT(
        menu_action_count(&menu, REVCONFIG_MINIMENU_OPHELDT_START) == 1,
        "item target verb arms held-item selection");
    {
        int const examine = menu_index_of(&menu, "Examine @lre@Fixture");
        int const use = menu_index_of(&menu, "Use @lre@Fixture");
        int const drop = menu_index_of(&menu, "Drop @lre@Fixture");
        int const wear = menu_index_of(&menu, "Wear @lre@Fixture");
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

/*
 * A SCRIPT-BUILT target button — the sailing crew panel's "Edit navigator".
 *
 * `torirs_sailing_edit_navigator_btn` (clientscript 8779) ends a CC_CREATE
 * chain with `cc_settargetverb("Edit-navigator")`, and content arms the layer
 * that owns those children with
 * `if_setevents(sailing_sidepanel:crew_content_clicklayer, 0, 127,
 * ^if_event_op_all + 16384)` — 2046 op bits plus bit 14, which is
 * `TORIRS_TARGET_MASK_PLAYER` once shifted down by 11.
 *
 * A CC_CREATE node is memset to zero and NO `cc_` opcode can write a target
 * mask, so the widget's own decoded mask is 0 forever. Reading only that half
 * (rather than deob `method12093`'s "server events where declared, decoded
 * flags where not") built no target row at all: right-clicking the button
 * offered nothing but Cancel, and the [opplayert] navigator grant was
 * unreachable with a mouse.
 */
static void
test_if3_script_button_target_row_uses_declared_events(void)
{
    struct UITree* tree = UITree_New(4);
    struct UITreeNodeSpec parent = { 0 };
    struct UITreeNodeSpec child = { 0 };
    /* ^if_event_op_all (2046) + 16384, exactly what boat_sidepanel.rs2 sends. */
    struct TestEvents events = { .component_id = 2, .mask = 2046 + 16384 };
    struct RS_MinimenuBuildCtx ctx = {
        .tree = tree,
        .events_for_component = test_events_for_component,
        .events_user = &events,
    };
    struct UIMinimenu menu;
    int32_t parent_index;
    int32_t child_index;

    parent.type = UIELEM_RS_LAYER;
    parent.component_id = 1;
    parent.width = 200;
    parent.height = 200;
    parent_index = UITree_Push(tree, -1, &parent);
    TEST_ASSERT(parent_index >= 0, "crew clicklayer fixture pushed");

    /* The proc's last CC_CREATE is a transparent rectangle; the target verb
     * lands on it, and it carries no ops and no opBase of its own. */
    child.type = UIELEM_RS_RECT;
    child.component_id = events.component_id;
    child.dynamic = 1;
    child.dynamic_child_index = 0;
    child.width = 120;
    child.height = 30;
    snprintf(
        child.menu_options.target_verb,
        sizeof(child.menu_options.target_verb),
        "Edit-navigator");
    child_index = UITree_Push(tree, parent_index, &child);
    TEST_ASSERT(child_index >= 0, "Edit navigator fixture pushed");
    if( child_index < 0 )
    {
        UITree_Free(tree);
        return;
    }
    tree->components[child_index].if3 = 1;
    /* What keeps a script-built node out of `rs_node_is_decorative_passthrough`
     * so the click collects it at all. A target verb ALONE should be enough —
     * the reference collects every widget under the cursor and lets
     * method12079 decide — and that predicate is the separate defect recorded
     * in editnav-arming-results.json. This fixture is about the row, so it
     * gives the node the op hook that gets it into the hit stack today. */
    UITree_HooksMut(&tree->components[child_index])->on_op.script_id = 88;
    TEST_ASSERT(
        tree->components[child_index].behavior.target_mask == 0,
        "a CC_CREATE child has no decoded target mask of its own");

    UITree_LayoutResolve(tree, 0, 0, 400, 400);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    TEST_ASSERT(
        menu_action_count(&menu, REVCONFIG_MINIMENU_TGT_BUTTON) == 1,
        "IF_SETEVENTS bit 14 arms the script-built target verb");
    {
        int const row = menu_index_of(&menu, "Edit-navigator");
        TEST_ASSERT(row >= 0, "the Edit-navigator row is built");
        if( row >= 0 )
            TEST_ASSERT(
                strcmp(menu.options[row].text, "Edit-navigator") == 0,
                "a target row with no opBase is the verb alone");
    }

    /* The control: the SAME node with the op bits but no target bits. */
    events.mask = 2046;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    TEST_ASSERT(
        menu_action_count(&menu, REVCONFIG_MINIMENU_TGT_BUTTON) == 0,
        "without a declared target bit the verb stays unarmable");

    UITree_Free(tree);
}

/*
 * `cc_settargetpriority(-1)` is a RESET, not a suppression.
 *
 * Statics.java opcode 1312 stores -1 as `field4122 = 1431939116`, and
 * 1431939116 is `4 * -1789498869` — the same 4 the widget constructor seeds.
 * 1..32 store `value - 1`; every other value is ignored outright. Both the
 * sailing button and the native inventory painter
 * (`inventory_noops_allowinteraction_bind_actions_6011`) open with -1, so
 * reading it as "no target row" would have cost the inventory its Use row too.
 */
static void
test_cc_settargetpriority_minus_one_is_the_default(void)
{
    struct UITree* tree = UITree_New(2);
    struct UITreeNodeSpec spec = { 0 };
    int32_t index;

    spec.type = UIELEM_RS_LAYER;
    spec.component_id = 7;
    spec.width = 10;
    spec.height = 10;
    index = UITree_Push(tree, -1, &spec);
    TEST_ASSERT(index >= 0, "target-priority fixture pushed");
    if( index < 0 )
    {
        UITree_Free(tree);
        return;
    }

    TEST_ASSERT(
        UITree_ApplyTargetPriority(tree, spec.component_id, 7) &&
            tree->components[index].target_priority == 6,
        "1..32 stores value - 1");
    TEST_ASSERT(
        UITree_ApplyTargetPriority(tree, spec.component_id, -1) &&
            tree->components[index].target_priority == 4,
        "-1 resets the priority to the rev239 default of 4");
    TEST_ASSERT(
        !UITree_ApplyTargetPriority(tree, spec.component_id, 0) &&
            tree->components[index].target_priority == 4,
        "0 is ignored and leaves the stored priority alone");
    TEST_ASSERT(
        !UITree_ApplyTargetPriority(tree, spec.component_id, 33) &&
            tree->components[index].target_priority == 4,
        "a priority past 32 is ignored and leaves the stored priority alone");
    UITree_Free(tree);
}

/*
 * The placement half of the reset: after `cc_settargetpriority(-1)` the target
 * row must sit where slot 4 sits in the high-to-low walk — under the ops the
 * reference deprioritizes (5..9) and above the ones it does not (0..3).
 */
static void
test_if3_target_row_after_reset_sits_at_slot_four(void)
{
    struct UITree* tree = UITree_New(2);
    struct UITreeNodeSpec spec = { 0 };
    struct TestEvents events = { .component_id = 3, .mask = 2046 + 16384 };
    struct RS_MinimenuBuildCtx ctx = {
        .tree = tree,
        .events_for_component = test_events_for_component,
        .events_user = &events,
    };
    struct UIMinimenu menu;
    int32_t index;

    spec.type = UIELEM_RS_RECT;
    spec.component_id = events.component_id;
    spec.width = 120;
    spec.height = 30;
    snprintf(
        spec.menu_options.target_verb, sizeof(spec.menu_options.target_verb), "Grant");
    snprintf(spec.menu_options.option, sizeof(spec.menu_options.option), "Helm");
    snprintf(spec.menu_options.ops[3], sizeof(spec.menu_options.ops[3]), "Below");
    snprintf(spec.menu_options.ops[5], sizeof(spec.menu_options.ops[5]), "Above");
    index = UITree_Push(tree, -1, &spec);
    TEST_ASSERT(index >= 0, "reset-placement fixture pushed");
    if( index < 0 )
    {
        UITree_Free(tree);
        return;
    }
    tree->components[index].if3 = 1;
    UITree_HooksMut(&tree->components[index])->on_op.script_id = 77;
    TEST_ASSERT(
        UITree_ApplyTargetPriority(tree, spec.component_id, -1),
        "the reset applies to the fixture");

    UITree_LayoutResolve(tree, 0, 0, 400, 400);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    {
        /* Options draw in reverse insertion order: the walk emits 9 -> 0, so a
         * later index is a higher row. */
        int const above = menu_index_of(&menu, "Above Helm");
        int const target = menu_index_of(&menu, "Grant Helm");
        int const below = menu_index_of(&menu, "Below Helm");
        TEST_ASSERT(
            above >= 0 && target >= 0 && below >= 0,
            "the reset priority still builds its target row beside the ops");
        if( above >= 0 && target >= 0 && below >= 0 )
        {
            TEST_ASSERT(
                above < target && target < below,
                "the reset target row lands between op 5 and op 3");
            TEST_ASSERT(
                menu.options[above].action > 1000,
                "the op above priority 4 is deprioritized");
            TEST_ASSERT(
                menu.options[below].action < 1000,
                "the op below priority 4 keeps its normal action");
        }
    }
    UITree_Free(tree);
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

    int n2 = World_NpcSpawn(world, 103, 502, 0, 25, 25, 1, idle);
    struct WorldEntity_NPC* hidden = World_EntityPoolGet(&world->entities.npc, n2);
    hidden->multinpc_hidden = true;
    snprintf(hidden->name, sizeof(hidden->name), "QuestGhost");
    snprintf(hidden->actions[0].name, sizeof(hidden->actions[0].name), "Talk-to");

    struct World_PickSet picks;
    World_PickSetReset(&picks);
    World_PickSetAdd(&picks, 100, WORLD_PICK_PLAYER, 25, 25, 0, 0);

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
    TEST_ASSERT(
        menu_has_substr(&menu, "Talk-to @yel@GoblinA"),
        "NPC colour markup does not add a second visible space");
    TEST_ASSERT(!menu_has_substr(&menu, "QuestGhost"), "hidden multiNpc has no menu rows");
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
    World_PickSetAdd(&picks, 200, WORLD_PICK_PLAYER, 30, 30, 0, 0);

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
    TEST_ASSERT(
        menu_has_substr(&menu, "Follow @whi@Bob"),
        "player colour markup does not add a second visible space");
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

/*
 * The armed half of "Edit-navigator": what a target selection's mask decides
 * about a world player row.
 *
 * `App::targetsel.mask` is copied straight onto `RS_MinimenuSelection::
 * target_mask` (app.c), so this is the exact gate an armed click passes
 * through. It used to be filled from `node->behavior.target_mask`, which is 0
 * for a `cc_create`d child — no `cc_` opcode can write a target mask — and the
 * measured symptom was this test's first half: target mode armed, the guest
 * under the cursor, and a menu with NOTHING in it, because the ordinary
 * Follow/Trade rows are suppressed while a selection is armed and the target
 * row was refused.
 *
 * The mask the arm must use instead is the server's IF_SETEVENTS declaration,
 * shifted the way deob `method7577(method12093(...))` shifts it — pinned below
 * against sailing's own `if_setevents(..., ^if_event_op_all + 16384)`.
 *
 * There is no unit test of `App`'s arm itself: every seam of it
 * (`app_component_target_mask`, `app_targetsel_wire_component`, and the
 * `REVCONFIG_MINIMENU_TGT_BUTTON` arm inside `app_minimenu_run_option`) is
 * static in app.c, reached only through a live `struct App` with a UI tree, a
 * world and a net, and a prototype for them would have to go in app.h. What is
 * testable is the contract they must satisfy, which is this file's subject.
 */
static void
test_target_mask_gates_the_armed_player_row(void)
{
    printf("TEST: an armed target selection needs the declared mask to offer a player\n");

    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct World* world = World_TestMakeReady(104);
    char player_ops[5][40];
    int player_ops_primary[5] = { 1, 1, 0, 0, 0 };
    struct World_PickSet picks;
    struct UIMinimenu menu;
    int op;
    struct WorldEntity_Player* other;

    /* sailing's own arming word, and the mask it must yield. */
    TEST_ASSERT(
        ((2046 + 16384) >> TORIRS_TARGET_MASK_IF3_SHIFT) & TORIRS_TARGET_MASK_IF3_BITS,
        "^if_event_op_all + 16384 carries target bits at all");
    TEST_ASSERT(
        (((2046 + 16384) >> TORIRS_TARGET_MASK_IF3_SHIFT) & TORIRS_TARGET_MASK_IF3_BITS) ==
            TORIRS_TARGET_MASK_PLAYER,
        "boat_sidepanel's if_setevents declares a PLAYER target");

    world->local_pid = 1;
    World_PlayerSpawn(world, 200, 0, 30, 30, idle);
    op = World_PlayerSpawn(world, 201, 0, 31, 30, idle);
    other = World_EntityPoolGet(&world->entities.player, op);
    other->server_pid = 2;
    other->combat_level = 1;
    snprintf(other->name, sizeof(other->name), "Deckhand");

    memset(player_ops, 0, sizeof(player_ops));
    snprintf(player_ops[0], sizeof(player_ops[0]), "Follow");
    snprintf(player_ops[1], sizeof(player_ops[1]), "Trade with");

    World_PickSetReset(&picks);
    World_PickSetAdd(&picks, 201, WORLD_PICK_PLAYER, 31, 30, 0, 0);

    {
        /* The defect: armed with the decoded mask of a cc_create'd child. */
        struct RS_MinimenuBuildCtx ctx = {
            .selection = { .mode = RS_MINIMENU_SELECT_TARGET,
                           .target_op = "Edit-navigator ->",
                           .target_mask = 0 },
            .player_ops = (char const(*)[40])player_ops,
            .player_ops_primary = player_ops_primary,
            .world = world,
            .world_pickset = &picks,
            .click_in_world = true,
        };
        UIMinimenu_Reset(&menu);
        RS_Minimenu_AddWorldRows(&ctx, &menu);
        TEST_ASSERT(
            menu_player_row_count(&menu) == 0,
            "mask 0 offers no target row (the measured symptom)");
        TEST_ASSERT(
            !menu_has_substr(&menu, "Follow"),
            "an armed selection suppresses the plain ops, so mask 0 leaves nothing at all");
    }

    {
        /* The fix: armed with the effective mask the server declared. */
        struct RS_MinimenuBuildCtx ctx = {
            .selection = { .mode = RS_MINIMENU_SELECT_TARGET,
                           .target_op = "Edit-navigator ->",
                           .target_mask = TORIRS_TARGET_MASK_PLAYER },
            .player_ops = (char const(*)[40])player_ops,
            .player_ops_primary = player_ops_primary,
            .world = world,
            .world_pickset = &picks,
            .click_in_world = true,
        };
        UIMinimenu_Reset(&menu);
        RS_Minimenu_AddWorldRows(&ctx, &menu);
        TEST_ASSERT(
            menu_player_row_count(&menu) == 1, "the declared mask offers exactly one row");
        TEST_ASSERT(
            menu_has_substr(&menu, "Edit-navigator -> @whi@Deckhand"),
            "the row is the armed verb joined to the target's name");
        for( int i = 0; i < menu.option_count; i++ )
        {
            if( menu.options[i].pick.kind != UI_MINIMENU_PICK_PLAYER )
                continue;
            TEST_ASSERT(
                menu.options[i].action == REVCONFIG_MINIMENU_TGT_PLAYER,
                "the row is a target row, not an OPPLAYER op");
            /* The number the click puts in OPPLAYERT. It is the GPI index the
             * server published in PLAYER_INFO (pool pid + 1), not the server's
             * own pool index — see the open issue on handle_opplayert. */
            TEST_ASSERT(
                menu.options[i].pick.secondary_id == 2,
                "the row carries the target's published player index");
        }
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
    World_PickSetAdd(&picks, 400, WORLD_PICK_PLAYER, 22, 22, 0, 0);

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
    TEST_ASSERT(
        menu_has_substr(&menu, "Take @lre@Coins"),
        "ground-item colour markup does not add a second visible space");
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
    World_PickSetAdd(&picks, 501, WORLD_PICK_OBJSTACK, 15, 15, 0, 0);

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

/*
 * A pile of ground items produces two rows apiece (Take + Examine) plus the
 * Walk here row, so six objs on one tile is thirteen rows. The menu array used
 * to hold ten and UIMinimenu_AddOption dropped the overflow without a word,
 * which showed up in game as a right-click on a stack listing only the first
 * four items -- the rest were unreachable. Six objs is the smallest count that
 * proves the cap is gone.
 */
static void
test_obj_stack_beyond_old_cap(void)
{
    printf("TEST: a six-obj stack lists every item\n");

    struct World* world = World_TestMakeReady(104);
    char actions[5][32] = { { 0 } };
    static char const* const names[6] = { "Bucket", "Small fishing net", "Tinderbox",
                                          "Bronze axe", "Shears", "Spade" };
    for( int i = 0; i < 6; i++ )
        World_ObjStackAdd(world, 600 + i, 15, 15, 0, 1000 + i, 1, names[i], actions);

    struct World_PickSet picks;
    World_PickSetReset(&picks);
    World_PickSetAdd(&picks, 600, WORLD_PICK_OBJSTACK, 15, 15, 0, 0);

    struct RS_MinimenuBuildCtx ctx = {
        .selection = { .mode = RS_MINIMENU_SELECT_NONE },
        .world = world,
        .world_pickset = &picks,
        .click_in_world = true,
    };
    struct UIMinimenu menu;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);

    for( int i = 0; i < 6; i++ )
        TEST_ASSERT(menu_has_substr(&menu, names[i]), names[i]);
    TEST_ASSERT(menu.option_count >= 13, "six objs emit at least thirteen rows");

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
    World_PickSetAdd(&picks, 300, WORLD_PICK_PLAYER, 40, 40, 0, 0);

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

/* The "Walk here" row of a pickset with no terrain in it. */
static struct UIMinimenuOption const*
menu_walk_row(struct UIMinimenu const* menu)
{
    for( int i = 0; i < menu->option_count; i++ )
        if( menu->options[i].action == REVCONFIG_MINIMENU_WALK )
            return &menu->options[i];
    return NULL;
}

/*
 * A click that hit no terrain (the sky, the void ringing an instance's floor)
 * leaves the pickset without a terrain item. The row is offered either way,
 * but it only walks anywhere when the caller resolved a fallback tile — the
 * closest tile to the click. Without one it stays inert, which is what the
 * reference does with a click on nothing.
 */
static void
test_walk_here_ground_fallback(void)
{
    printf("TEST: no-terrain click walks to the resolved fallback tile\n");

    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct World* world = World_TestMakeReady(104);
    world->local_pid = 7;

    int lp = World_PlayerSpawn(world, 400, 0, 50, 50, idle);
    struct WorldEntity_Player* local = World_EntityPoolGet(&world->entities.player, lp);
    local->server_pid = 7;

    struct World_PickSet picks;
    World_PickSetReset(&picks); /* nothing drew under the cursor at all */

    struct RS_MinimenuBuildCtx ctx = {
        .selection = { .mode = RS_MINIMENU_SELECT_NONE },
        .world = world,
        .world_pickset = &picks,
        .click_in_world = true,
    };
    struct UIMinimenu menu;
    struct UIMinimenuOption const* walk;

    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);
    walk = menu_walk_row(&menu);
    TEST_ASSERT(walk != NULL, "no fallback: Walk here is still offered");
    TEST_ASSERT(
        walk && walk->pick.kind == UI_MINIMENU_PICK_NONE,
        "no fallback: the row carries no destination");

    ctx.ground_fallback_valid = true;
    ctx.ground_fallback_x = 44;
    ctx.ground_fallback_z = 61;
    ctx.ground_fallback_level = 2;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);
    walk = menu_walk_row(&menu);
    TEST_ASSERT(walk != NULL, "fallback: Walk here is offered");
    TEST_ASSERT(
        walk && walk->pick.kind == UI_MINIMENU_PICK_TERRAIN,
        "fallback: the row walks like a picked tile");
    TEST_ASSERT(walk && walk->pick.secondary_id == 44, "fallback tile x reaches the row");
    TEST_ASSERT(walk && walk->pick.tertiary_id == 61, "fallback tile z reaches the row");
    TEST_ASSERT(walk && walk->pick.quaternary_id == 2, "fallback tile level reaches the row");

    /* A pickset that DOES hold terrain is untouched by the fallback: the
     * clicked tile wins, never the nearest one. */
    World_PickSetAdd(&picks, 1, WORLD_PICK_TERRAIN, 51, 52, 0, 0);
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);
    walk = menu_walk_row(&menu);
    TEST_ASSERT(walk && walk->pick.secondary_id == 51, "picked tile beats the fallback (x)");
    TEST_ASSERT(walk && walk->pick.tertiary_id == 52, "picked tile beats the fallback (z)");

    /* Navigation is a bearing even when the pointer is over a deck tile.
     * A 0 heading is due south, and must not be mistaken for absent input. */
    ctx.sailing_navigating = true;
    ctx.sailing_heading_valid = true;
    ctx.sailing_heading = 0;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);
    int heading_row = menu_index_of(&menu, "Set heading");
    TEST_ASSERT(heading_row >= 0, "helm offers the native Set heading row");
    TEST_ASSERT(menu_index_of(&menu, "Walk here") < 0, "helm does not offer player walking");
    TEST_ASSERT(heading_row >= 0 && menu.options[heading_row].pick.kind == UI_MINIMENU_PICK_HEADING,
                "heading row carries a bearing rather than staging tile coordinates");
    TEST_ASSERT(heading_row >= 0 && menu.options[heading_row].pick.id == 0,
                "due south survives as a valid compass choice");
    ctx.sailing_heading_valid = false;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);
    TEST_ASSERT(menu_index_of(&menu, "Set heading") < 0,
                "a ray above the horizon does not invent a bearing");

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
    World_PickSetAdd(&fx->picks, 201, npc ? WORLD_PICK_NPC : WORLD_PICK_PLAYER, 30, 30, 0, 0);
}

static void
attack_fixture_build_model(
    struct AttackFixture* fx,
    int model,
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
        .attack_option_model = model,
        .world = fx->world,
        .world_pickset = &fx->picks,
        .click_in_world = true,
    };
    UIMinimenu_Reset(out);
    RS_Minimenu_AddWorldRows(&ctx, out);
}

/* The two dropdowns only exist in the settings era, so every test that states
 * one of their values builds under that model. */
static void
attack_fixture_build(
    struct AttackFixture* fx,
    int player_option,
    int npc_option,
    struct UIMinimenu* out)
{
    attack_fixture_build_model(
        fx, TORIRS_ATTACK_OPTION_MODEL_SETTINGS, player_option, npc_option, out);
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

/*
 * The classic era has no dropdowns at all, so a client that never hears varp
 * clientcode 18/22 must still offer Attack — which is what a LostCity world
 * (rs289lc) looks like from here. Client-TS addNpcOptions also computes the
 * combat-level bump inside its attack pass alone, so the other ops keep their
 * natural priority against a higher-level NPC.
 */
static void
test_npc_attack_option_classic(void)
{
    printf("TEST: the classic era emits Attack with no settings varp\n");

    struct AttackFixture fx;
    struct UIMinimenu menu;

    attack_fixture_init(&fx, 50, 50, true);
    attack_fixture_build_model(
        &fx, TORIRS_ATTACK_OPTION_MODEL_CLASSIC, RS_ATTACK_OPTION_DEPENDS,
        RS_ATTACK_OPTION_DEPENDS, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") == REVCONFIG_MINIMENU_OPNPC2,
        "an equal-level NPC is left-click-attackable");
    World_Free(fx.world);

    attack_fixture_init(&fx, 10, 21, true);
    attack_fixture_build_model(
        &fx, TORIRS_ATTACK_OPTION_MODEL_CLASSIC, RS_ATTACK_OPTION_DEPENDS,
        RS_ATTACK_OPTION_DEPENDS, &menu);
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Attack") ==
            UIMinimenu_ActionDeprioritize(REVCONFIG_MINIMENU_OPNPC2),
        "a higher-level NPC still sinks its Attack row");
    TEST_ASSERT(
        menu_action_for_verb(&menu, "Talk-to") == REVCONFIG_MINIMENU_OPNPC1,
        "but the bump stays inside the attack pass");
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

/*
 * SAILING_PLAN C5.2's non-terrain half: a SCENERY pick carrying a view id is a
 * DECK loc, whose record lives in the VIEW's own world. It must resolve
 * through ctx.view_world_fn — never ctx->world — build the loc's op rows, and
 * stamp the view id onto every row's pick so the dispatcher can add the
 * staging base. Without a resolver the pick is dropped, never misread against
 * the root world's tables.
 */
static struct World* g_deck_world;

static struct World*
test_deck_world_resolver(void* user, int view_id)
{
    (void)user;
    return view_id == 3 ? g_deck_world : NULL;
}

static void
test_deck_scenery_pick_resolves_through_view_world(void)
{
    printf("TEST: a deck loc's SCENERY pick resolves through the view world\n");

    struct World* root = World_TestMakeReady(104);
    struct World* deck = World_TestMakeReady(64);
    char actions[5][32];
    struct World_PickSet picks;
    struct UIMinimenu menu;
    int helm_rows = 0;

    memset(actions, 0, sizeof(actions));
    snprintf(actions[0], sizeof(actions[0]), "Steer");
    g_deck_world = deck;
    TEST_ASSERT(
        World_SceneryRegister(
            deck, 777, 4242, 5, 6, 1, 1, 1, /* shape scenery */ 10, 0, 0, "Helm",
            (char const(*)[32])actions, 1) >= 0,
        "deck helm loc registered in the view world");

    World_PickSetReset(&picks);
    World_PickSetAdd(&picks, 777, WORLD_PICK_SCENERY, 5, 6, 1, /* view */ 3);

    struct RS_MinimenuBuildCtx ctx = {
        .selection = { .mode = RS_MINIMENU_SELECT_NONE },
        .world = root,
        .world_pickset = &picks,
        .click_in_world = true,
        .view_world_fn = test_deck_world_resolver,
    };
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);

    TEST_ASSERT(menu_has_substr(&menu, "Steer @cya@Helm"), "the deck loc's op row is built");
    TEST_ASSERT(menu_has_substr(&menu, "Examine @cya@Helm"), "and its Examine row");
    for( int i = 0; i < menu.option_count; i++ )
        if( menu.options[i].pick.kind == UI_MINIMENU_PICK_SCENERY )
        {
            helm_rows++;
            TEST_ASSERT(
                menu.options[i].pick.view_id == 3,
                "every scenery row's pick carries the view id");
            TEST_ASSERT(
                menu.options[i].pick.tertiary_id == 5 &&
                    menu.options[i].pick.quaternary_id == 6,
                "and the DECK-LOCAL tile, for the dispatcher's base add");
        }
    TEST_ASSERT(helm_rows >= 2, "op + Examine rows both landed");

    /* No resolver -> the pick is dropped, not misread against the root. */
    ctx.view_world_fn = NULL;
    UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx, &menu);
    TEST_ASSERT(
        !menu_has_substr(&menu, "Helm"),
        "without a resolver the deck pick builds nothing");

    World_Free(deck);
    World_Free(root);
}

static void test_checked_widget_native_actions(void)
{
    struct RS_UISlots slots;RS_UISlots_Init(&slots);
    slots.chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC]=1;
    slots.chat_filter_mode[RS_UI_CHAT_FILTER_PRIVATE]=2;
    slots.side_overlay_id[1]=123;
    struct UITree* replacement=UITree_New(1);
    RS_UISlots_RebindTree(&slots,replacement);
    TEST_ASSERT(slots.chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC]==1 && slots.chat_filter_mode[RS_UI_CHAT_FILTER_PRIVATE]==2 && slots.side_overlay_id[1]==-1,
        "native tree remount preserves chat modes while retiring old mount identities");
    RS_UISlots_SetChatFilter(&slots,RS_UI_CHAT_FILTER_PUBLIC,0);
    TEST_ASSERT(slots.chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC]==0,"later native chat mode remains authoritative after remount");
    UITree_Free(replacement);
    for( int if3=0;if3<2;++if3 )
    {
        struct UITree* tree=UITree_New(4);
        struct UITreeBehavior behavior={.button_type=if3 ? 0 : REVCONFIG_BUTTON_TYPE_SELECT};
        struct UITreeNodeSpec spec={.type=UIELEM_RS_TEXT,.component_id=101,.width=80,.height=20,.behavior=&behavior};
        spec.u.rs_text.text="Control";
        if( if3 ) snprintf(spec.menu_options.ops[1],sizeof(spec.menu_options.ops[1]),"Activate");
        else snprintf(spec.menu_options.option,sizeof(spec.menu_options.option),"Activate");
        int node=UITree_Push(tree,-1,&spec);
        tree->components[node].if3=if3;
        UITree_LayoutResolve(tree,0,0,200,100);
        struct TestEvents events={101,1<<2};
        struct RS_MinimenuBuildCtx ctx={.tree=tree,.events_for_component=test_events_for_component,.events_user=&events};
        struct UIMinimenu menu;UIMinimenu_Reset(&menu);
        TEST_ASSERT(RS_Minimenu_AddWidgetRows(&ctx,node,&menu)==1,"checked widget uses native IF1/IF3 action rows");
        if( menu.option_count )
        {
            struct UIMinimenuOption row=menu.options[0];
            TEST_ASSERT(row.action_index==(if3 ? 1 : -1),"checked widget preserves numbered versus native IF1 operation");
            TEST_ASSERT(row.action==(if3 ? REVCONFIG_MINIMENU_IF_BUTTON : REVCONFIG_MINIMENU_IF_BUTTON_SELECT),"checked widget preserves native button type");
            TEST_ASSERT(row.pick.has_node_identity && row.pick.node_index==node && UITree_MenuPickCurrent(tree,&row.pick),"widget action carries native incarnation and operation signature");
            uint64_t revision=RS_Minimenu_WidgetActionRevision(&row);
            TEST_ASSERT(RS_Minimenu_WidgetActionIndex(&menu,1,revision)==0,"current checked native action resolves");
            UITree_SetTextAt(tree,node,"New target");
            TEST_ASSERT(!UITree_MenuPickCurrent(tree,&row.pick),"retained widget action rejects native target changes");
            UIMinimenu_Reset(&menu);RS_Minimenu_AddWidgetRows(&ctx,node,&menu);
            TEST_ASSERT(RS_Minimenu_WidgetActionIndex(&menu,1,revision)<0,"native action re-resolution rejects changed target signature");
            revision=RS_Minimenu_WidgetActionRevision(&menu.options[0]);
            events.mask|=1<<5;
            UIMinimenu_Reset(&menu);RS_Minimenu_AddWidgetRows(&ctx,node,&menu);
            TEST_ASSERT(RS_Minimenu_WidgetActionIndex(&menu,1,revision)<0,"native action re-resolution rejects changed server mask");
        }
        UITree_SetHideAt(tree,node,1);
        UITree_WidgetSetHidden(tree,UITree_RefAt(tree,node),1,false);
        UIMinimenu_Reset(&menu);
        TEST_ASSERT(RS_Minimenu_AddWidgetRows(&ctx,node,&menu)==0,"native hide blocks widget actions despite plugin show");
        UITree_SetHideAt(tree,node,0);
        if( if3 )
        {
            events.mask=0;
            TEST_ASSERT(RS_Minimenu_AddWidgetRows(&ctx,node,&menu)==0,"widget actions recheck current server event mask");
        }
        UITree_Free(tree);
    }
}

/* An owned control reaches the menu only through the real hit test and only
 * with its plugin row: no native packet op, exact node identity, and a
 * retained row dies when the listener is replaced or the native parent hides. */
static void
test_owned_widget_operation_rows(void)
{
    struct UITree* tree = UITree_New(4);
    struct UITreeNodeSpec spec = { 0 };
    struct RS_MinimenuBuildCtx ctx = { .tree = tree };
    struct UIMinimenu menu;
    int row = -1;

    spec.type = UIELEM_RS_LAYER;
    spec.component_id = 0x360000;
    spec.width = 300;
    spec.height = 200;
    int root = UITree_Push(tree, -1, &spec);
    int own = UITree_WidgetCreateText(tree, UITree_RefAt(tree, root), 3, "button", 0);
    TEST_ASSERT(root >= 0 && own >= 0, "owned control fixture pushed");
    UITree_LayoutResolve(tree, 0, 0, 400, 300);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    int const base_rows = menu.option_count;
    TEST_ASSERT(menu_action_count(&menu, RS_MINIMENU_ACTION_PLUGIN_WIDGET) == 0,
        "an unarmed owned control offers no rows");

    TEST_ASSERT(UITree_WidgetSetOperation(tree, UITree_RefAt(tree, own), 3, 41, "Toggle"),
        "owner arms the control");
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    TEST_ASSERT(menu_action_count(&menu, RS_MINIMENU_ACTION_PLUGIN_WIDGET) == 1,
        "an armed owned control offers exactly one plugin row");
    TEST_ASSERT(menu.option_count == base_rows + 1, "an owned control adds no native rows");
    for( int i = 0; i < menu.option_count; i++ )
        if( menu.options[i].action == RS_MINIMENU_ACTION_PLUGIN_WIDGET ) row = i;
    TEST_ASSERT(row >= 0 && strcmp(menu.options[row].text, "Toggle") == 0, "the row uses the plugin label");
    TEST_ASSERT(row >= 0 && menu.options[row].pick.has_node_identity && menu.options[row].pick.node_index == own,
        "the row carries the exact owned node identity");
    TEST_ASSERT(RS_Minimenu_DefaultOptionIndex(&menu) == row, "an owned control is the left-click default");
    TEST_ASSERT(row >= 0 && UITree_MenuPickCurrent(tree, &menu.options[row].pick), "a fresh row is current");

    struct UIMinimenuOption retained = menu.options[row >= 0 ? row : 0];
    TEST_ASSERT(UITree_WidgetSetOperation(tree, UITree_RefAt(tree, own), 3, 42, "Toggle"),
        "owner replaces the listener");
    TEST_ASSERT(!UITree_MenuPickCurrent(tree, &retained.pick),
        "replacing the listener retires the retained row");
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    TEST_ASSERT(menu_action_count(&menu, RS_MINIMENU_ACTION_PLUGIN_WIDGET) == 1, "the new listener builds a current row");
    for( int i = 0; i < menu.option_count; i++ )
        if( menu.options[i].action == RS_MINIMENU_ACTION_PLUGIN_WIDGET ) row = i;
    retained = menu.options[row];
    UITree_WidgetSetOperation(tree, UITree_RefAt(tree, own), 3, 0, "");
    TEST_ASSERT(!UITree_MenuPickCurrent(tree, &retained.pick), "removing the operation retires the retained row");
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    TEST_ASSERT(menu_action_count(&menu, RS_MINIMENU_ACTION_PLUGIN_WIDGET) == 0, "a disarmed control offers no rows");

    UITree_WidgetSetOperation(tree, UITree_RefAt(tree, own), 3, 43, "Toggle");
    struct UIMinimenu widget_menu;
    UIMinimenu_Reset(&widget_menu);
    TEST_ASSERT(RS_Minimenu_AddWidgetRows(&ctx, own, &widget_menu) == 1 &&
        widget_menu.options[0].action == RS_MINIMENU_ACTION_PLUGIN_WIDGET,
        "the checked widget action query lists the owned operation");
    UITree_SetHideAt(tree, root, 1);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    TEST_ASSERT(menu_action_count(&menu, RS_MINIMENU_ACTION_PLUGIN_WIDGET) == 0,
        "native hiding of the parent removes the owned row");
    UIMinimenu_Reset(&widget_menu);
    TEST_ASSERT(RS_Minimenu_AddWidgetRows(&ctx, own, &widget_menu) == 0,
        "native hiding blocks the checked widget action query too");
    UITree_Free(tree);
}

/* An owned control covering a native button: the control is the topmost hit,
 * so its row is the left-click default and the native row stays available
 * below it in the menu. */
static void
test_owned_widget_row_over_native_button(void)
{
    struct UITree* tree = UITree_New(4);
    struct UITreeNodeSpec spec = { 0 };
    struct UITreeBehavior behavior = { .button_type = REVCONFIG_BUTTON_TYPE_CONTINUE };
    struct RS_MinimenuBuildCtx ctx = { .tree = tree };
    struct UIMinimenu menu;
    int owned_row = -1, native_rows = 0;

    spec.type = UIELEM_RS_LAYER;
    spec.component_id = 0x360000;
    spec.width = 300;
    spec.height = 200;
    int root = UITree_Push(tree, -1, &spec);
    struct UITreeNodeSpec button = { 0 };
    button.type = UIELEM_RS_TEXT;
    button.component_id = 0x360001;
    button.width = 100;
    button.height = 30;
    button.behavior = &behavior;
    button.u.rs_text.text = "Native";
    snprintf(button.menu_options.option, sizeof(button.menu_options.option), "Continue");
    int native = UITree_Push(tree, root, &button);
    int own = UITree_WidgetCreateText(tree, UITree_RefAt(tree, root), 3, "cover", 0);
    TEST_ASSERT(root >= 0 && native >= 0 && own >= 0, "cover fixture pushed");
    TEST_ASSERT(UITree_WidgetSetOperation(tree, UITree_RefAt(tree, own), 3, 51, "Owned"), "cover armed");
    UITree_LayoutResolve(tree, 0, 0, 400, 300);
    RS_Minimenu_Build(&ctx, 10, 10, &menu);
    for( int i = 0; i < menu.option_count; i++ )
    {
        if( menu.options[i].action == RS_MINIMENU_ACTION_PLUGIN_WIDGET ) owned_row = i;
        else if( menu.options[i].action == REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON ) native_rows++;
    }
    TEST_ASSERT(owned_row >= 0 && native_rows == 1, "both the owned row and the covered native row are built");
    TEST_ASSERT(RS_Minimenu_DefaultOptionIndex(&menu) == owned_row,
        "the topmost owned control is the left-click default over a native button");
    UITree_Free(tree);
}

static void
test_hull_menu_mask_and_dedup(void)
{
    puts("TEST: native hull menus use only hull hits and the five-bit mask");
    struct World* root=World_TestMakeReady(64);
    struct WevConfig cfg={.name="Test boat",.ops={"Board","Inspect","Anchor","Follow","Examine"}};
    struct Wevs boats; Wevs_Init(&boats);
    Wevs_Spawn(&boats,1,0,&cfg,1,0,0,0,0,31);
    struct Wev* b=Wevs_Spawn(&boats,2,0,&cfg,1,1024,0,0,0,0x15);
    struct World_PickSet picks; World_PickSetReset(&picks);
    World_PickSetAdd(&picks,111,WORLD_PICK_WEV,-1,-1,-1,1);
    World_PickSetAdd(&picks,112,WORLD_PICK_WEV,-1,-1,-1,2);
    World_PickSetAdd(&picks,113,WORLD_PICK_WEV,-1,-1,-1,2);
    struct RS_MinimenuBuildCtx ctx={.world=root,.world_pickset=&picks,.wevs=&boats,.click_in_world=true};
    struct UIMinimenu menu; UIMinimenu_Reset(&menu);
    RS_Minimenu_AddWorldRows(&ctx,&menu);
    int count=0;
    for( int i=0;i<menu.option_count;++i ) if(menu.options[i].pick.kind==UI_MINIMENU_PICK_WEV)
    {
        ++count;
        TEST_ASSERT(menu.options[i].pick.id==2,"only nearest boat receives rows");
        TEST_ASSERT((0x15 & (1u<<menu.options[i].pick.secondary_id))!=0,"only wire-enabled ops are offered");
    }
    TEST_ASSERT(count==3,"overlapping hull geometry produces three enabled rows once");
    World_PickSetReset(&picks);
    World_PickSetAdd(&picks,114,WORLD_PICK_TERRAIN,3,4,1,2);
    UIMinimenu_Reset(&menu); RS_Minimenu_AddWorldRows(&ctx,&menu);
    TEST_ASSERT(!menu_has_substr(&menu,"Test boat"),"contents-only terrain cannot invent a hull menu");
    picks.items[0].type=WORLD_PICK_WEV; b->flattened=true;
    UIMinimenu_Reset(&menu); RS_Minimenu_AddWorldRows(&ctx,&menu);
    TEST_ASSERT(!menu_has_substr(&menu,"Test boat"),"flat boat never offers hull operations");
    World_Free(root);
}

int
main(void)
{
    test_owned_widget_row_over_native_button();
    test_owned_widget_operation_rows();
    test_checked_widget_native_actions();
    test_hull_menu_mask_and_dedup();
    test_widget_target_priority_default();
    test_dat2_stacking_behaviour_is_not_boolean();
    test_if3_continue_uses_resume();
    test_dat1_continue_is_not_a_numbered_op();
    test_if3_item_uses_only_scripted_ops();
    test_if3_item_onop_and_target_rows_match_rev239();
    test_if3_script_button_target_row_uses_declared_events();
    test_cc_settargetpriority_minus_one_is_the_default();
    test_if3_target_row_after_reset_sits_at_slot_four();
    test_player_get_by_element_id();
    test_local_player_pick_expands_stacked_npcs();
    test_other_player_stack_rows();
    test_target_mask_gates_the_armed_player_row();
    test_local_player_pick_expands_ground_items();
    test_obj_pick_expands_siblings();
    test_obj_stack_beyond_old_cap();
    test_local_alone_no_player_ops();
    test_walk_here_ground_fallback();
    test_npc_attack_option();
    test_npc_attack_option_classic();
    test_player_attack_option();
    test_player_attack_option_clan();
    test_dat2_obj_team_decodes();
    test_deck_scenery_pick_resolves_through_view_world();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}

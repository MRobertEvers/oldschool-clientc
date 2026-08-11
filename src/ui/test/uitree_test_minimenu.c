#include "test_harness.h"
#include "game/rs_minimenu_cross.h"
#include "uitree_cross.h"
#include "uitree_hovertext.h"
#include "uitree_interact.h"

#include <string.h>

/* Rev-254 action ids used by the tests (mirrors revconfig.h values). */
enum
{
    TEST_ACTION_CANCEL = 1106,
    TEST_ACTION_WALK = 718,
    TEST_ACTION_EXAMINE = 1328, /* OPHELD6 */
    TEST_ACTION_OP1 = 582,      /* INV_BUTTON1 */
    TEST_ACTION_OP2 = 113,      /* INV_BUTTON2 */
};

static struct UIMinimenuPick
pick_none(void)
{
    struct UIMinimenuPick pick;
    memset(&pick, 0, sizeof(pick));
    return pick;
}

/* Reference bottom-to-top layout math at the default bold-12 glyph line box. */
static void
test_minimenu_layout_math(void)
{
    struct UIMinimenuLayout layout = UIMinimenu_LayoutFromLineBox(16);
    TEST_ASSERT(layout.row_stride == 15, "row stride = box - 1");
    TEST_ASSERT(layout.header_bar_h == 16, "header bar = box");
    TEST_ASSERT(layout.option_base_y == 31, "option base = 2*box - 1");
    TEST_ASSERT(layout.chrome_h == 21, "chrome = box + 5");
    /* Reference: menuHeight = n * 15 + 21 (chrome 21 + row-stride pad). */
    TEST_ASSERT(UIMinimenu_Height(&layout, 3) == 3 * 15 + 21, "height formula");

    {
        struct UIMinimenu menu;
        UIMinimenu_Reset(&menu);
        UIMinimenu_AddOption(&menu, "Cancel", TEST_ACTION_CANCEL, -1, pick_none());
        UIMinimenu_AddOption(&menu, "Op one", TEST_ACTION_OP1, 0, pick_none());
        UIMinimenu_AddOption(&menu, "Op two", TEST_ACTION_OP2, 1, pick_none());
        UIMinimenu_ShowAt(&menu, layout, 100, 200, 150, 765, 503);
        TEST_ASSERT(menu.visible, "visible after show");
        TEST_ASSERT(menu.width == 100, "width from content");
        TEST_ASSERT(menu.x == 150, "centered on click x");
        /* Bottom-to-top: option 0 is the lowest row. */
        TEST_ASSERT(
            UIMinimenu_OptionY(&menu, 0) > UIMinimenu_OptionY(&menu, 2),
            "option 0 draws below option 2");
        TEST_ASSERT(
            UIMinimenu_OptionY(&menu, 2) == menu.y + layout.option_base_y,
            "top row at option_base_y");
    }
}

static void
test_minimenu_clamping(void)
{
    struct UIMinimenuLayout layout = UIMinimenu_LayoutFromLineBox(16);
    struct UIMinimenu menu;
    UIMinimenu_Reset(&menu);
    UIMinimenu_AddOption(&menu, "Cancel", TEST_ACTION_CANCEL, -1, pick_none());

    /* Click at the far right/bottom corner clamps inside the viewport. */
    UIMinimenu_ShowAt(&menu, layout, 120, 764, 502, 765, 503);
    TEST_ASSERT(menu.x + menu.width <= 765, "clamped right");
    TEST_ASSERT(menu.y + menu.height <= 503, "clamped bottom");

    /* Click at origin clamps to 0. */
    UIMinimenu_ShowAt(&menu, layout, 120, 0, 0, 765, 503);
    TEST_ASSERT(menu.x == 0, "clamped left");
    TEST_ASSERT(menu.y == 0, "clamped top");
}

static void
test_minimenu_sort(void)
{
    struct UIMinimenu menu;
    UIMinimenu_Reset(&menu);
    /* Reference insertion order: Cancel first, then ops, Examine last. */
    UIMinimenu_AddOption(&menu, "Cancel", TEST_ACTION_CANCEL, -1, pick_none());
    UIMinimenu_AddOption(&menu, "Op one", TEST_ACTION_OP1, 0, pick_none());
    UIMinimenu_AddOption(&menu, "Op two", TEST_ACTION_OP2, 1, pick_none());
    UIMinimenu_AddOption(&menu, "Examine", TEST_ACTION_EXAMINE, -1, pick_none());
    UIMinimenu_SortPriorityActions(&menu);

    /* >1000 ids (Cancel/Examine) sink to the low indices = bottom rows;
     * relative order within each class is preserved. */
    TEST_ASSERT(menu.options[0].action == TEST_ACTION_CANCEL, "cancel at bottom");
    TEST_ASSERT(menu.options[1].action == TEST_ACTION_EXAMINE, "examine above cancel");
    TEST_ASSERT(menu.options[2].action == TEST_ACTION_OP1, "op order preserved");
    TEST_ASSERT(menu.options[3].action == TEST_ACTION_OP2, "last op on top");

    TEST_ASSERT(
        UIMinimenu_ActionDeprioritize(TEST_ACTION_OP1) == TEST_ACTION_OP1 + 2000,
        "deprioritize bias");
    TEST_ASSERT(
        UIMinimenu_ActionDeprioritize(TEST_ACTION_CANCEL) == TEST_ACTION_CANCEL,
        "high ids not re-biased");
    TEST_ASSERT(
        UIMinimenu_ActionNormalize(UIMinimenu_ActionDeprioritize(TEST_ACTION_OP1)) ==
            TEST_ACTION_OP1,
        "deprioritized action normalizes before dispatch");
}

static void
test_minimenu_hit_and_hover(void)
{
    struct UIMinimenuLayout layout = UIMinimenu_LayoutFromLineBox(16);
    struct UIMinimenu menu;
    UIMinimenu_Reset(&menu);
    UIMinimenu_AddOption(&menu, "Cancel", TEST_ACTION_CANCEL, -1, pick_none());
    UIMinimenu_AddOption(&menu, "Op one", TEST_ACTION_OP1, 0, pick_none());
    UIMinimenu_ShowAt(&menu, layout, 120, 300, 200, 765, 503);

    {
        int const top_y = UIMinimenu_OptionY(&menu, 1);
        int const bottom_y = UIMinimenu_OptionY(&menu, 0);
        TEST_ASSERT(
            UIMinimenu_HitOption(&menu, menu.x + 10, top_y - 2) == 1, "hit top row");
        TEST_ASSERT(
            UIMinimenu_HitOption(&menu, menu.x + 10, bottom_y - 2) == 0, "hit bottom row");
        /* Title bar is chrome (-1); far outside (past margin) closes (-2). */
        TEST_ASSERT(
            UIMinimenu_HitOption(&menu, menu.x + 10, menu.y + 4) == -1, "chrome no row");
        TEST_ASSERT(
            UIMinimenu_HitOption(&menu, menu.x - 40, menu.y - 40) == -2, "outside closes");

        TEST_ASSERT(
            UIMinimenu_UpdateHover(&menu, menu.x + 10, top_y - 2), "hover change reported");
        TEST_ASSERT(menu.hovered_option == 1, "hovered row tracked");
        TEST_ASSERT(
            !UIMinimenu_UpdateHover(&menu, menu.x + 11, top_y - 2), "same row no change");
    }
}

/* Emit expansion: visible menu produces body+chrome rects, title, and one text
 * per row with hover color; hidden menu emits nothing. */
static void
test_minimenu_emit(void)
{
    struct UITree* tree = UITree_New(8);
    struct UITreeHost host;
    struct TestHostState host_state;
    struct UITreeEmitBuffer emit;
    struct UIMinimenuLayout layout = UIMinimenu_LayoutFromLineBox(16);
    struct UIMinimenu menu;
    int32_t node;

    UITree_TestHostInit(&host, &host_state);
    UITree_EmitBufferInit(&emit);

    node = UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_MINIMENU, 900, 0, 0, 0, 0);
    TEST_ASSERT(node >= 0, "minimenu node pushed");
    tree->components[node].u.minimenu.font_id = 5;
    UITree_TestResolve(tree);

    UIMinimenu_Reset(&menu);
    menu.font_id = 5;
    UIMinimenu_AddOption(&menu, "Cancel", TEST_ACTION_CANCEL, -1, pick_none());
    UIMinimenu_AddOption(&menu, "Op one", TEST_ACTION_OP1, 0, pick_none());
    UIMinimenu_SortPriorityActions(&menu);
    UIMinimenu_ShowAt(&menu, layout, 120, 300, 200, 765, 503);
    menu.hovered_option = 1;

    /* Hidden: host says not visible -> no descs from the minimenu node. */
    host_state.minimenu_visible = 0;
    host_state.minimenu_state = &menu;
    UITree_EmitWalk(tree, &host, &emit, -1);
    TEST_ASSERT(emit.count == 0, "hidden menu emits nothing");

    host_state.minimenu_visible = 1;
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);

    /* 6 rects (body + title bar + 4 strips) + title text + 2 row texts. */
    TEST_ASSERT(emit.count == 9, "desc count body+chrome+title+rows");
    TEST_ASSERT(emit.cmds[0].kind == UITREE_EMIT_RECT, "body rect first");
    TEST_ASSERT(emit.cmds[0].color == UITREE_MINIMENU_COLOR_BODY, "body color");
    TEST_ASSERT(emit.cmds[0].w == menu.width && emit.cmds[0].h == menu.height, "body size");
    TEST_ASSERT(emit.cmds[6].kind == UITREE_EMIT_TEXT, "title text");
    TEST_ASSERT(strcmp(emit.cmds[6].text, "Choose Option") == 0, "title string");
    /* Row order matches option index order; hovered row is yellow. */
    TEST_ASSERT(strcmp(emit.cmds[7].text, "Cancel") == 0, "row 0 text");
    TEST_ASSERT(emit.cmds[7].color == 0xFFFFFF, "unhovered row white");
    TEST_ASSERT(strcmp(emit.cmds[8].text, "Op one") == 0, "row 1 text");
    TEST_ASSERT(emit.cmds[8].color == 0xFFFF00, "hovered row yellow");
    /* Bottom-to-top: row 0 (Cancel) sits below row 1 on screen. */
    TEST_ASSERT(emit.cmds[7].y > emit.cmds[8].y, "cancel below op");

    UITree_EmitBufferFree(&emit);
    UITree_Free(tree);
}

/* Cross model: frame progression walk/interact and 400ms expiry. */
static void
test_cross_model(void)
{
    struct UICross cross;
    UICross_Reset(&cross);
    TEST_ASSERT(!UICross_IsActive(&cross), "starts off");

    UICross_Show(&cross, UI_CROSS_WALK, 10, 20);
    TEST_ASSERT(UICross_IsActive(&cross), "active after show");
    TEST_ASSERT(UICross_AtlasFrame(&cross) == 0, "walk frame 0");
    UICross_Tick(&cross, 250);
    TEST_ASSERT(UICross_AtlasFrame(&cross) == 2, "walk frame 2 at 250ms");
    UICross_Tick(&cross, 150);
    TEST_ASSERT(!UICross_IsActive(&cross), "expired at 400ms");

    UICross_Show(&cross, UI_CROSS_INTERACT, 10, 20);
    TEST_ASSERT(UICross_AtlasFrame(&cross) == 4, "interact frame base 4");
    UICross_Tick(&cross, 399);
    TEST_ASSERT(UICross_AtlasFrame(&cross) == 7, "interact last frame 7");
}

/* Cross colour is a pure function of the action (reference doAction): yellow
 * for Walk, red for world-entity ops, nothing at all for UI actions, Examine
 * and Cancel — so those leave a running cross untouched. */
static void
test_cross_action_policy(void)
{
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_WALK) == UI_CROSS_WALK,
        "walk is yellow");

    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_OPNPC1) == UI_CROSS_INTERACT,
        "opnpc1 is red");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_OPNPC5) == UI_CROSS_INTERACT,
        "opnpc5 is red");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_OPLOC3) == UI_CROSS_INTERACT,
        "oploc3 is red");

    /* Examine rows carry the op6 ids and get no cross in the reference. */
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_OPNPC6) == UI_CROSS_OFF,
        "examine npc has no cross");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_OPLOC6) == UI_CROSS_OFF,
        "examine loc has no cross");

    /* Everything driven purely through the UI. */
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_OPHELD1) == UI_CROSS_OFF,
        "inventory op has no cross");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_OPHELDT_START) == UI_CROSS_OFF,
        "\"Use\" has no cross");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_INV_BUTTON1) == UI_CROSS_OFF,
        "inv button has no cross");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_IF_BUTTON) == UI_CROSS_OFF,
        "if button has no cross");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_CLOSE_MODAL) == UI_CROSS_OFF,
        "close has no cross");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_MESSAGE_PRIVATE) == UI_CROSS_OFF,
        "social row has no cross");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_CANCEL) == UI_CROSS_OFF,
        "cancel has no cross");

    /* Use-on-world and spell-on-world walk the player at a target, so they get
     * the red interact cross like the plain world ops. */
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_USEHELD_ONLOC) == UI_CROSS_INTERACT,
        "use-on-loc red");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_USEHELD_ONNPC) == UI_CROSS_INTERACT,
        "use-on-npc red");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_USEHELD_ONOBJ) == UI_CROSS_INTERACT,
        "use-on-obj red");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_TGT_LOC) == UI_CROSS_INTERACT,
        "spell-on-loc red");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_TGT_NPC) == UI_CROSS_INTERACT,
        "spell-on-npc red");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_TGT_OBJ) == UI_CROSS_INTERACT,
        "spell-on-obj red");
    /* Arming a spell (TGT_BUTTON) and the held-on-held variants do not walk, so
     * no cross. */
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_TGT_BUTTON) == UI_CROSS_OFF,
        "target-button has no cross");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_USEHELD_ONHELD) == UI_CROSS_OFF,
        "use-on-held has no cross");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(REVCONFIG_MINIMENU_TGT_HELD) == UI_CROSS_OFF,
        "spell-on-held has no cross");

    /* Deprioritized rows keep their colour (reference strips _PRIORITY first). */
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(
            UIMinimenu_ActionDeprioritize(REVCONFIG_MINIMENU_OPNPC1)) == UI_CROSS_INTERACT,
        "deprioritized opnpc1 still red");
    TEST_ASSERT(
        RS_Minimenu_CrossModeForAction(
            UIMinimenu_ActionDeprioritize(REVCONFIG_MINIMENU_WALK)) == UI_CROSS_WALK,
        "deprioritized walk still yellow");
}

/* A left press the popup consumed owns its release too: the mouseup edge must
 * not escape into the normal click path and run a second action (which would
 * repaint the cross a different colour mid-animation). */
static void
test_minimenu_release_swallow(void)
{
    struct UIMinimenuLayout layout = UIMinimenu_LayoutFromLineBox(16);
    struct UITree* tree = UITree_New(8);
    struct UITreeHost host;
    struct TestHostState host_state;
    struct UIInteraction interact;
    struct LibToriRS_Input input_storage;
    struct LibToriRS_Input* input;
    struct UIInteractOut out;
    int row_x;
    int row_y;

    UITree_TestHostInit(&host, &host_state);
    UITree_TestResolve(tree);
    UIInteraction_Init(&interact);

    UIMinimenu_AddOption(&interact.minimenu, "Cancel", TEST_ACTION_CANCEL, -1, pick_none());
    UIMinimenu_AddOption(&interact.minimenu, "Op one", TEST_ACTION_OP1, 0, pick_none());
    UIMinimenu_ShowAt(&interact.minimenu, layout, 120, 300, 200, 765, 503);
    row_x = interact.minimenu.x + 10;
    row_y = UIMinimenu_OptionY(&interact.minimenu, 1) - 2;

    input = LibToriRS_Input_Init(&input_storage, 0);

    /* Press on a row: selects, and the app hides the menu straight after. */
    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushMouseMove(input, row_x, row_y);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, row_x, row_y);
    LibToriRS_Input_End(input);
    UITree_InteractFrame(&interact, tree, &host, input, 0, &out);
    TEST_ASSERT(out.minimenu_select == 1, "row selected on press");
    TEST_ASSERT(out.minimenu_consumed_pointer, "row press is consumed by minimenu");
    TEST_ASSERT(interact.swallow_left_click, "release latched");
    UIMinimenu_Hide(&interact.minimenu);

    /* Release at the same point: menu is gone, but nothing may fire. */
    LibToriRS_Input_Begin(input, 20);
    LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, row_x, row_y);
    LibToriRS_Input_End(input);
    UITree_InteractFrame(&interact, tree, &host, input, 20, &out);
    TEST_ASSERT(!out.left_click_miss, "release does not reach the world");
    TEST_ASSERT(out.clicked_com_id < 0, "release does not click a component");
    TEST_ASSERT(out.minimenu_select < 0, "release selects nothing");
    TEST_ASSERT(!out.minimenu_consumed_pointer, "release latch is not a new pointer event");
    TEST_ASSERT(!interact.swallow_left_click, "latch retired after one release");

    /* The next click is a fresh gesture and passes through normally. */
    LibToriRS_Input_Begin(input, 2000);
    LibToriRS_Input_PushMouseMove(input, row_x, row_y);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, row_x, row_y);
    LibToriRS_Input_End(input);
    UITree_InteractFrame(&interact, tree, &host, input, 2000, &out);
    LibToriRS_Input_Begin(input, 2020);
    LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, row_x, row_y);
    LibToriRS_Input_End(input);
    UITree_InteractFrame(&interact, tree, &host, input, 2020, &out);
    TEST_ASSERT(out.left_click_miss, "later click still reaches the world");

    UITree_Free(tree);
}

/*
 * Mouseover text composition (reference proc 4727): top row verbatim, plus
 * "</col> / N more options" once more than one actionable row exists, and
 * nothing at all for a Cancel-only menu (minimenu_numops == 0).
 */
static void
test_hovertext_compose(void)
{
    struct UIHoverText hover;
    struct UIMinimenu menu;

    UIHoverText_Reset(&hover);
    TEST_ASSERT(!hover.visible, "reset hides");
    TEST_ASSERT(hover.h == UITREE_HOVERTEXT_BOX_H, "reset seeds the line box");

    /* Cancel only: numops 0 -> no line. */
    UIMinimenu_Reset(&menu);
    UIMinimenu_AddOption(&menu, "Cancel", TEST_ACTION_CANCEL, -1, pick_none());
    UIMinimenu_SortPriorityActions(&menu);
    TEST_ASSERT(!UIHoverText_Compose(&menu, &hover), "cancel-only composes nothing");
    TEST_ASSERT(!hover.visible, "cancel-only hides");
    TEST_ASSERT(hover.text[0] == '\0', "cancel-only clears text");

    /* One actionable row: bare text, no suffix. */
    UIMinimenu_Reset(&menu);
    UIMinimenu_AddOption(&menu, "Cancel", TEST_ACTION_CANCEL, -1, pick_none());
    UIMinimenu_AddOption(&menu, "Walk here", TEST_ACTION_WALK, -1, pick_none());
    UIMinimenu_SortPriorityActions(&menu);
    TEST_ASSERT(UIHoverText_Compose(&menu, &hover), "single row composes");
    TEST_ASSERT(hover.visible, "single row visible");
    TEST_ASSERT(strcmp(hover.text, "Walk here") == 0, "single row is verbatim");

    /* Three actionable rows: top row (last after the priority bubble) wins and
     * the remaining two are summarised. */
    UIMinimenu_Reset(&menu);
    UIMinimenu_AddOption(&menu, "Cancel", TEST_ACTION_CANCEL, -1, pick_none());
    UIMinimenu_AddOption(&menu, "Walk here", TEST_ACTION_WALK, -1, pick_none());
    UIMinimenu_AddOption(&menu, "Examine @yel@Goblin", TEST_ACTION_EXAMINE, 5, pick_none());
    UIMinimenu_AddOption(&menu, "Attack @yel@Goblin", TEST_ACTION_OP1, 0, pick_none());
    UIMinimenu_SortPriorityActions(&menu);
    TEST_ASSERT(UIHoverText_Compose(&menu, &hover), "multi row composes");
    TEST_ASSERT(
        strcmp(hover.text, "Attack @yel@Goblin</col> / 2 more options") == 0,
        "top row + N-more suffix");

    /* Compose leaves placement alone — the app owns x/y/w. */
    hover.x = 11;
    hover.y = 22;
    hover.w = 33;
    UIHoverText_Compose(&menu, &hover);
    TEST_ASSERT(hover.x == 11 && hover.y == 22 && hover.w == 33, "compose keeps placement");
}

/* The hover half of InteractFrame must hand the host down to the hover walk:
 * without it the sidebar-tab gate cannot run and an unselected tab's
 * components win the (last-match-wins) hover, which is what shrank the stats
 * tab's per-stat tooltip hitbox to a few pixels. */
static void
test_interact_hover_uses_host(void)
{
    struct UITree* tree = UITree_New(16);
    struct UITreeHost host;
    struct TestHostState host_state;
    struct UIInteraction interact;
    struct LibToriRS_Input input_storage;
    struct LibToriRS_Input* input;
    struct UIInteractOut out;

    UITree_TestHostInit(&host, &host_state);
    UIInteraction_Init(&interact);

    {
        int32_t shown = UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_SIDEBAR, 400, 0, 0, 200, 200);
        tree->components[shown].u.sidebar.tabno = 1;
        int32_t shown_cell = UITree_TestPushXy(tree, shown, UIELEM_RS_LAYER, 401, 0, 0, 64, 32);
        tree->components[shown_cell].behavior.over_layer_id = 900;

        int32_t other = UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_SIDEBAR, 410, 0, 0, 200, 200);
        tree->components[other].u.sidebar.tabno = 2;
        int32_t other_cell = UITree_TestPushXy(tree, other, UIELEM_RS_LAYER, 411, 0, 0, 64, 32);
        tree->components[other_cell].behavior.over_layer_id = 911;
    }
    UITree_TestResolve(tree);
    host_state.selected_tab = 1;

    input = LibToriRS_Input_Init(&input_storage, 0);
    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushMouseMove(input, 30, 16);
    LibToriRS_Input_End(input);
    UITree_InteractFrame(&interact, tree, &host, input, 0, &out);

    TEST_ASSERT(out.hover_com_id == 900, "interact hover uses the selected tab");
    UITree_Free(tree);
}

void
test_minimenu(void)
{
    printf("TEST: minimenu / cross\n");
    test_hovertext_compose();
    test_minimenu_layout_math();
    test_minimenu_clamping();
    test_minimenu_sort();
    test_minimenu_hit_and_hover();
    test_minimenu_emit();
    test_cross_model();
    test_cross_action_policy();
    test_minimenu_release_swallow();
    test_interact_hover_uses_host();
}

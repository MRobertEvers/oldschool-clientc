#include "test_harness.h"
#include "uitree_cross.h"

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

/* Reference bottom-to-top layout math at the default line height. */
static void
test_minimenu_layout_math(void)
{
    struct UIMinimenuLayout layout = UIMinimenu_LayoutFromLineHeight(14);
    TEST_ASSERT(layout.row_stride == 15, "row stride = lh + 1");
    TEST_ASSERT(layout.header_bar_h == 16, "header bar = lh + 2");
    TEST_ASSERT(layout.option_base_y == 31, "option base = 2lh + 3");
    TEST_ASSERT(layout.chrome_h == 21, "chrome = lh + 7");
    /* Reference: menuHeight = n * 15 + 22 (chrome 21 + 1 row-stride pad). */
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
    struct UIMinimenuLayout layout = UIMinimenu_LayoutFromLineHeight(14);
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
}

static void
test_minimenu_hit_and_hover(void)
{
    struct UIMinimenuLayout layout = UIMinimenu_LayoutFromLineHeight(14);
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
    struct UIMinimenuLayout layout = UIMinimenu_LayoutFromLineHeight(14);
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

void
test_minimenu(void)
{
    test_minimenu_layout_math();
    test_minimenu_clamping();
    test_minimenu_sort();
    test_minimenu_hit_and_hover();
    test_minimenu_emit();
    test_cross_model();
}

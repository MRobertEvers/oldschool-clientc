/*
 * The CS2 executor's dropdown: a list that OPENS, in the game's own toolkit.
 *
 * This executor had no popup at all. A click on a dropdown STEPPED the
 * selection, because a popup is an open state held across frames plus a hit
 * test of its own and the toolkit had neither -- so the one presentation of
 * the plugin window that runs on every lane was the one where a setting could
 * not be read before it was changed, and where a palette of two thousand loc
 * names had no affordance whatsoever.
 *
 * What is pinned here is the whole of that list as a CONTRACT: it appears when
 * the button is pressed, it carries a component per visible row, choosing one
 * lands on the model as the option that was chosen (not as "the next one"), it
 * scrolls, and it shuts -- on a pick, on the button again, and on a click
 * anywhere else in the window.
 *
 * The component ids below are the executor's own private blocks, restated in
 * the test's terms on purpose: they are how a click gets home, so a build that
 * moved one would be a list that draws and cannot be used, and a test that
 * imported the macro could not see that happen.
 */
#include "test_harness.h"

#include "torirs_chrome_exec.h"
#include "torirs_chrome_metrics.h"
#include "uitree_debug_overlay.h"

/* The private id blocks of ui/torirs_chrome_exec_cs2.c, as the contract. */
#define CS2_T_WIDGET_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x100)
#define CS2_T_DROP_ROW_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x2000)
#define CS2_T_DROP_UP (TORIRS_CHROME_CS2_ID_BASE + 0x24)
#define CS2_T_DROP_DOWN (TORIRS_CHROME_CS2_ID_BASE + 0x25)
/** Rows the list shows at once (TORIRS_CHROME_DROPDOWN_ROWS). */
#define CS2_T_DROP_ROWS 10

static struct ToriRSChrome g_ui;
static struct ToriRSChromeSync g_sync;
static struct UITree* g_tree;

/* The four the reference's own bank-settings dropdown offers, and a long list
 * for the case the scrollbar exists for. */
static char const* const g_tab_display[] = {
    "First item in tab",
    "Digit (1, 2, 3)",
    "Roman numeral (I, II, III)",
    "Hide tab bar",
};
static char const* g_long[40];
static char g_long_text[40][16];

/** A tree with one mount the panel fills, and the executor bound to it. */
static int32_t
cs2_bind(void)
{
    struct ToriRSChromeExec exec;
    int32_t mount;

    g_tree = UITree_New(64);
    /* A root the chrome is allowed into: ToriRSChrome_TreeAcceptsChrome
     * refuses a tree whose first component is already in the chrome's own
     * group, which is the state where chrome got in ahead of the gameframe. */
    mount = UITree_TestPushXy(g_tree, -1, UIELEM_RS_LAYER, 0x0100, 0, 0, 280, 320);
    UITree_TestResolve(g_tree);

    ToriRSChrome_Init(&g_ui);
    exec = ToriRSChromeExec_Cs2(g_tree, mount, 1, -1, -1, NULL, NULL);
    TEST_ASSERT(ToriRSChromeSync_Init(&g_sync, &exec) == 1, "the CS2 executor comes up");
    return mount;
}

static void
cs2_unbind(void)
{
    ToriRSChromeSync_Shutdown(&g_sync);
    UITree_Free(g_tree);
    g_tree = NULL;
}

/** One frame: lay the model out, tell the executor, apply what it sends back. */
static void
cs2_frame(void)
{
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    ToriRSChromeSync_Pump(&g_sync, &g_ui);
    /* Twice, because the pump is what applies a pick and the executor has to
     * hear the answer -- exactly as the host's frame does it. */
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
}

/** How many of the list's row components are in the tree. */
static int
cs2_drop_rows(void)
{
    int n = 0;

    for( uint32_t i = 0; i < g_tree->component_count; i++ )
    {
        int const id = g_tree->components[i].component_id;
        if( id >= CS2_T_DROP_ROW_BASE && id < CS2_T_DROP_ROW_BASE + CS2_T_DROP_ROWS )
            n++;
    }
    return n;
}

/** Is `text` drawn anywhere in the tree? */
static int
cs2_has_text(char const* text)
{
    for( uint32_t i = 0; i < g_tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &g_tree->components[i];
        if( c->type != UIELEM_RS_TEXT || !c->u.rs_text.text )
            continue;
        if( strcmp(c->u.rs_text.text, text) == 0 )
            return 1;
    }
    return 0;
}

/** Is a component with this id in the tree at all? */
static int
cs2_has_id(int id)
{
    return UITree_FindByComponentId(g_tree, id) >= 0;
}

/*
 * The list, end to end: it opens, it says what the options are, and choosing
 * one sets the setting to THAT option.
 */
static void
test_chrome_cs2_dropdown_opens(void)
{
    int panel;
    int dd;

    cs2_bind();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 260, "Bank");
    dd = ToriRSChrome_Dropdown(&g_ui, panel, "Tab display", g_tab_display, 4, 0);
    cs2_frame();

    TEST_ASSERT(cs2_drop_rows() == 0, "a shut dropdown builds no list");
    TEST_ASSERT(cs2_has_text(g_tab_display[0]), "the button shows the chosen option");

    TEST_ASSERT(
        ToriRSChromeExecCs2_Click(CS2_T_WIDGET_BASE + dd) == 1,
        "the button takes a click");
    cs2_frame();
    TEST_ASSERT(cs2_drop_rows() == 4, "which opens a list, one component per option");
    for( int i = 0; i < 4; i++ )
        TEST_ASSERT(cs2_has_text(g_tab_display[i]), "every option is drawn in it");
    /* The press that opened it must not have chosen anything: the whole point
     * of the list is that the setting is read before it is changed. */
    TEST_ASSERT(
        ToriRSChrome_DropdownSelected(&g_ui, dd) == 0, "opening the list chooses nothing");

    /*
     * WHERE it is, which is the half a component count cannot see: a list
     * built at the wrong place still counts ten rows.
     *
     * It hangs off the button -- same left edge plus the list's own pad, top
     * edge at the button's bottom -- which is the geometry the shared metrics
     * table states and the in-canvas chrome draws from the same numbers.
     */
    {
        int32_t const btn = UITree_FindByComponentId(g_tree, CS2_T_WIDGET_BASE + dd);
        int32_t const row0 = UITree_FindByComponentId(g_tree, CS2_T_DROP_ROW_BASE + 0);
        int32_t const row1 = UITree_FindByComponentId(g_tree, CS2_T_DROP_ROW_BASE + 1);

        TEST_ASSERT(btn >= 0 && row0 >= 0 && row1 >= 0, "the button and its rows are in the tree");
        TEST_ASSERT(
            g_tree->components[row0].position.x ==
                g_tree->components[btn].position.x + TORIRS_CHROME_M_DROP_LIST_PAD,
            "the list is as wide as the button, inside its own pad");
        TEST_ASSERT(
            g_tree->components[row0].position.y ==
                g_tree->components[btn].position.y + TORIRS_CHROME_M_ROW_H +
                    TORIRS_CHROME_M_DROP_LIST_PAD,
            "and starts at the button's bottom edge");
        TEST_ASSERT(
            g_tree->components[row1].position.y - g_tree->components[row0].position.y ==
                TORIRS_CHROME_M_DROP_LIST_ROW_H,
            "its rows are the list's own pitch, not a settings row's");
    }

    /* The third row is the third OPTION -- not the next one along, which is
     * what the cycling click this replaced would have given. */
    TEST_ASSERT(ToriRSChromeExecCs2_Click(CS2_T_DROP_ROW_BASE + 2) == 1, "a row takes a click");
    cs2_frame();
    TEST_ASSERT(
        ToriRSChrome_DropdownSelected(&g_ui, dd) == 2, "and picks the option it shows");
    TEST_ASSERT(cs2_drop_rows() == 0, "the list shuts behind the pick");
    TEST_ASSERT(cs2_has_text(g_tab_display[2]), "and the button shows the new value");
    cs2_unbind();
}

/*
 * The three ways it shuts, none of which is choosing a row.
 *
 * A list that can only be dismissed by committing to something is a modal
 * dialogue, and a dropdown is not one.
 */
static void
test_chrome_cs2_dropdown_dismiss(void)
{
    int panel;
    int dd;
    int check;

    cs2_bind();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 260, "Bank");
    dd = ToriRSChrome_Dropdown(&g_ui, panel, "Tab display", g_tab_display, 4, 0);
    check = ToriRSChrome_Checkbox(&g_ui, panel, "Always set placeholders", 0);
    cs2_frame();

    ToriRSChromeExecCs2_Click(CS2_T_WIDGET_BASE + dd);
    cs2_frame();
    TEST_ASSERT(cs2_drop_rows() == 4, "open");
    ToriRSChromeExecCs2_Click(CS2_T_WIDGET_BASE + dd);
    cs2_frame();
    TEST_ASSERT(cs2_drop_rows() == 0, "the button toggles it shut");

    ToriRSChromeExecCs2_Click(CS2_T_WIDGET_BASE + dd);
    cs2_frame();
    TEST_ASSERT(cs2_drop_rows() == 4, "open again");
    TEST_ASSERT(
        ToriRSChromeExecCs2_Click(CS2_T_WIDGET_BASE + check) == 1,
        "a click on another row is still that row's click");
    cs2_frame();
    TEST_ASSERT(cs2_drop_rows() == 0, "and takes the list down with it");
    TEST_ASSERT(
        ToriRSChrome_Checked(&g_ui, check) == 1, "without swallowing what it was for");

    /* A dropdown that goes away takes its list with it, rather than leaving
     * rows on screen that still answer to their component ids. */
    ToriRSChromeExecCs2_Click(CS2_T_WIDGET_BASE + dd);
    cs2_frame();
    TEST_ASSERT(cs2_drop_rows() == 4, "open once more");
    ToriRSChrome_SetHidden(&g_ui, dd, 1);
    cs2_frame();
    TEST_ASSERT(cs2_drop_rows() == 0, "hiding the row shuts the list");
    cs2_unbind();
}

/*
 * A list longer than the window: ten rows, a bar, and arrows that move it.
 *
 * The scroll offset is the executor's own -- the model's list is a popup of
 * prims at the in-canvas window's floating position, which this presentation
 * is not -- so it is worth pinning that the offset moves the ROWS and that
 * the bar's arrows are what move it.
 */
static void
test_chrome_cs2_dropdown_scrolls(void)
{
    int panel;
    int dd;

    for( int i = 0; i < 40; i++ )
    {
        snprintf(g_long_text[i], sizeof(g_long_text[0]), "option %02d", i);
        g_long[i] = g_long_text[i];
    }

    cs2_bind();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 260, "Palette");
    /* Chosen: the LAST option, so the button's own value is not one of the
     * rows on screen -- a list checked by asking what the tree draws would
     * otherwise pass on the string the button is showing. */
    dd = ToriRSChrome_Dropdown(&g_ui, panel, "", g_long, 40, 39);
    cs2_frame();

    ToriRSChromeExecCs2_Click(CS2_T_WIDGET_BASE + dd);
    cs2_frame();
    TEST_ASSERT(cs2_drop_rows() == CS2_T_DROP_ROWS, "a long list shows its ceiling of rows");
    TEST_ASSERT(cs2_has_text(g_long[0]), "starting at the top");
    TEST_ASSERT(cs2_has_text(g_long[39]), "with the chosen option still on the button");
    TEST_ASSERT(!cs2_has_text(g_long[CS2_T_DROP_ROWS]), "and stopping at the ceiling");
    TEST_ASSERT(cs2_has_id(CS2_T_DROP_UP), "with an up arrow of its own");
    TEST_ASSERT(cs2_has_id(CS2_T_DROP_DOWN), "and a down arrow");

    TEST_ASSERT(ToriRSChromeExecCs2_Click(CS2_T_DROP_DOWN) == 1, "the arrow takes a click");
    cs2_frame();
    TEST_ASSERT(!cs2_has_text(g_long[0]), "which scrolls the first option out of the list");
    TEST_ASSERT(cs2_has_text(g_long[CS2_T_DROP_ROWS]), "and the next one in");
    TEST_ASSERT(
        ToriRSChrome_DropdownSelected(&g_ui, dd) == 39, "and chooses nothing on the way");

    /* The row ids are per SCREEN ROW, so the top row after a scroll is the
     * option the offset put there -- the arithmetic the whole block rests on. */
    ToriRSChromeExecCs2_Click(CS2_T_DROP_ROW_BASE + 0);
    cs2_frame();
    TEST_ASSERT(
        ToriRSChrome_DropdownSelected(&g_ui, dd) == 1,
        "picking the top row after one step picks the second option");
    cs2_unbind();
}

void
test_chrome_cs2(void)
{
    printf("TEST: CS2 chrome executor (the dropdown's popup list)\n");

    test_chrome_cs2_dropdown_opens();
    test_chrome_cs2_dropdown_dismiss();
    test_chrome_cs2_dropdown_scrolls();
}

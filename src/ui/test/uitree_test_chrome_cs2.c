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
#include "uitree_emit.h"
#include "uitree_hover.h"

/* The private id blocks of ui/torirs_chrome_exec_cs2.c, as the contract. */
#define CS2_T_WIDGET_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x100)
#define CS2_T_DROP_ROW_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x2000)
#define CS2_T_DROP_UP (TORIRS_CHROME_CS2_ID_BASE + 0x24)
#define CS2_T_DROP_DOWN (TORIRS_CHROME_CS2_ID_BASE + 0x25)
#define CS2_T_LABEL_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x2800)
/** A LISTROW's settings well -- the parallel block its ACTION zone answers on. */
#define CS2_T_ACTION_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x400)
/** The window X this presentation used to draw, and must not draw again. */
#define CS2_T_CLOSE (TORIRS_CHROME_CS2_ID_BASE + 0x23)
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

/**
 * A tree with one mount the panel fills, and the executor bound to it.
 *
 * @param skin_scene_id the scene entry the baked skin was uploaded into, or -1
 *        for a build that baked none. Most of these tests do not care and pass
 *        -1 -- the flat fallback draws the same rows -- but anything about the
 *        ART has to bind with one, because the sprite path is exactly what
 *        `skin_scene_id > 0` switches on.
 */
static int32_t
cs2_bind_skin(int skin_scene_id)
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
    exec = ToriRSChromeExec_Cs2(g_tree, mount, 1, -1, skin_scene_id, NULL, NULL);
    TEST_ASSERT(ToriRSChromeSync_Init(&g_sync, &exec) == 1, "the CS2 executor comes up");
    return mount;
}

/** The common case: no baked skin, so every piece of furniture is flat. */
static int32_t
cs2_bind(void)
{
    return cs2_bind_skin(-1);
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
    /*
     * And the layout pass the host runs after it, which is not decoration
     * here: the panel is a FILL-PARENT layer, so until it is resolved its box
     * is 0x0 -- and both the hover walk and the hit test stop at a parent the
     * pointer is not inside, however well placed its children are.
     */
    UITree_TestResolve(g_tree);
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

/**
 * The edge of the skin GRAPHIC drawn from `slot`, or 0 when none is.
 *
 * Square by construction -- every boolean sprite in this chrome is -- so one
 * number answers both "which art" and "at what size", which is the pair that
 * has to move together when the style changes.
 */
static int
cs2_graphic_side(int slot)
{
    for( uint32_t i = 0; i < g_tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &g_tree->components[i];
        if( c->type != UIELEM_RS_GRAPHIC || c->u.rs_graphic.atlas_index != slot )
            continue;
        return c->position.width;
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


/*
 * NO WINDOW X, and the first row is not pushed down to make room for one.
 *
 * This presentation is a column of components inside the gameframe's popout
 * strip: the strip's own Plugin button opens the window and shuts it again, so
 * a close mark drawn inside the content is a second way out of a window that
 * already has one -- and it reads as belonging to the panel underneath rather
 * than to the strip. The intent still exists for the in-canvas chrome, which
 * IS a floating window with a title bar to put an X in.
 */
static void
test_chrome_cs2_no_window_x(void)
{
    int panel;
    int row;
    int32_t node;
    int bx = 0, by = 0, bw = 0, bh = 0;

    cs2_bind();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 260, "Plugins");
    row = ToriRSChrome_ListRow(&g_ui, panel, "Tile Indicator", 1, 1);
    cs2_frame();

    TEST_ASSERT(!cs2_has_id(CS2_T_CLOSE), "no close button is built");

    /* And the space it took is given back: the first row sits at the panel's
     * own pad, not a button's height below it. */
    node = UITree_FindByComponentId(g_tree, CS2_T_LABEL_BASE + row);
    TEST_ASSERT(node >= 0, "the first row is in the tree");
    UITree_LayoutGetBounds(&g_tree->components[node].position, &bx, &by, &bw, &bh);
    TEST_ASSERT(by == TORIRS_CHROME_M_PAD, "and starts at the panel's top pad");
    cs2_unbind();
}

/*
 * A row's name goes YELLOW under the pointer, the way it does in every other
 * presentation of this window.
 *
 * Pinned as the whole chain rather than as a field on a component, because
 * each link failed independently while the rows stayed white: the hover walk
 * reports a component only if it has an id AND something that changes under
 * the pointer (uitree_hover.c), and the emit walk only swaps the colour for
 * the id it is handed. A label pushed with -1 -- which is what every one of
 * these was -- satisfies neither, however it is coloured.
 */
static void
test_chrome_cs2_row_lights_up(void)
{
    int panel;
    int row;
    int32_t node;
    int bx = 0, by = 0, bw = 0, bh = 0;
    int hovered;
    struct UITreeEmitBuffer buf;
    int lit = 0;

    cs2_bind();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 260, "Plugins");
    row = ToriRSChrome_ListRow(&g_ui, panel, "Tile Indicator", 1, 1);
    cs2_frame();

    node = UITree_FindByComponentId(g_tree, CS2_T_LABEL_BASE + row);
    TEST_ASSERT(node >= 0, "the row's name carries an id of its own");
    UITree_LayoutGetBounds(&g_tree->components[node].position, &bx, &by, &bw, &bh);
    TEST_ASSERT(bw > 0 && bh > 0, "over a box the pointer can be inside");

    hovered = UITree_FindHoveredComponentIdForRegion(
        g_tree, NULL, -1, bx + bw / 2, by + bh / 2, 0, 0, UITREE_LAYOUT_ROOT_W,
        UITREE_LAYOUT_ROOT_H);
    TEST_ASSERT(
        hovered == CS2_T_LABEL_BASE + row, "and the hover walk finds it under the pointer");

    /* The colour the walk then draws it in. TORIRS_CHROME_C_ACCENT is the
     * interfaces' own hover yellow, which is what the in-canvas chrome uses
     * for the same row. */
    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(g_tree, NULL, &buf, hovered);
    for( int i = 0; i < buf.count; i++ )
    {
        if( buf.cmds[i].kind != UITREE_EMIT_TEXT || !buf.cmds[i].text )
            continue;
        if( strcmp(buf.cmds[i].text, "Tile Indicator") != 0 )
            continue;
        lit = buf.cmds[i].color == TORIRS_CHROME_C_ACCENT;
    }
    TEST_ASSERT(lit, "so the name draws in the hover yellow");

    /* And only while the pointer is on it -- a row that stayed lit would be a
     * row that reads as selected. */
    lit = 0;
    buf.count = 0;
    UITree_EmitWalk(g_tree, NULL, &buf, -1);
    for( int i = 0; i < buf.count; i++ )
    {
        if( buf.cmds[i].kind != UITREE_EMIT_TEXT || !buf.cmds[i].text )
            continue;
        if( strcmp(buf.cmds[i].text, "Tile Indicator") != 0 )
            continue;
        lit = buf.cmds[i].color == TORIRS_CHROME_C_ACCENT;
    }
    TEST_ASSERT(!lit, "and goes back to its own colour when it is not");
    UITree_EmitBufferFree(&buf);

    /* The name is still not a click target: the switch beside it is. A TEXT
     * with no click mask and no ops is decorative pass-through, so giving it
     * an id bought a hover and nothing else. */
    TEST_ASSERT(
        ToriRSChromeExecCs2_Click(CS2_T_LABEL_BASE + row) == 0,
        "the name takes no click of its own");
    cs2_unbind();
}

/*
 * The checkbox style reaches THIS presentation, art and box together.
 *
 * The seam test proves the command is emitted; this proves the executor acts
 * on it, which is a different failure: a native executor that stored the style
 * and went on placing 17px boxes would draw the 18px well scaled, and the
 * speckled edge that produces is the exact thing the bake exists to avoid.
 */
static void
test_chrome_cs2_check_style(void)
{
    int panel;
    int check;

    /* Bound WITH a skin: the styles differ in their art, and the flat
     * fallback draws neither. */
    cs2_bind_skin(7);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 200, "Settings");
    check = ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 1);
    cs2_frame();

    TEST_ASSERT(
        cs2_graphic_side(TORIRS_CHROME_SKIN_CHECK_ON) == TORIRS_CHROME_M_BOX,
        "the default style draws the tick, at the tick's own 17");
    TEST_ASSERT(
        cs2_graphic_side(TORIRS_CHROME_SKIN_CHECK_BOX_ON) == 0,
        "and nothing else");

    ToriRSChrome_SetCheckStyle(&g_ui, TORIRS_CHROME_CHECK_STYLE_BOX);
    cs2_frame();

    TEST_ASSERT(
        cs2_graphic_side(TORIRS_CHROME_SKIN_CHECK_BOX_ON) == TORIRS_CHROME_M_BOX_SQUARE,
        "the box style draws the well, at the well's own 18");
    TEST_ASSERT(
        cs2_graphic_side(TORIRS_CHROME_SKIN_CHECK_ON) == 0,
        "and the tick is gone rather than drawn under it");

    ToriRSChrome_SetChecked(&g_ui, check, 0);
    cs2_frame();
    TEST_ASSERT(
        cs2_graphic_side(TORIRS_CHROME_SKIN_CHECK_BOX_OFF) == TORIRS_CHROME_M_BOX_SQUARE,
        "unchecking it draws the empty well, not the red cross");

    cs2_unbind();
}

/*
 * A multiline field, drawn as interface components.
 *
 * This is the presentation with no multiline control to reach for -- the cache
 * builds these boxes out of a type-12 input and no client here has one -- so
 * the value is wrapped and each line becomes a TEXT component of its own. What
 * has to hold is that the lines land where the model would put them (the same
 * ToriRSChrome_WrapText breaks both), that the row RESERVES its full height so
 * the row under it is not drawn through it, and that the box takes a click.
 */
static void
test_chrome_cs2_textarea(void)
{
    int panel;
    int area;
    int below;

    cs2_bind();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 0, "Ground Items");
    ToriRSChrome_PanelFill(&g_ui, panel, 280, 320);
    area = ToriRSChrome_TextArea(&g_ui, panel, "Highlighted items", "whip\ntbow", 3);
    below = ToriRSChrome_Checkbox(&g_ui, panel, "Show highlighted only", 0);
    cs2_frame();

    TEST_ASSERT(cs2_has_text("Highlighted items"), "the caption is drawn");
    TEST_ASSERT(cs2_has_text("whip"), "the first line is drawn on its own");
    TEST_ASSERT(cs2_has_text("tbow"), "and so is the second");
    TEST_ASSERT(
        !cs2_has_text("whip\ntbow"),
        "never as one component -- a TEXT component draws one line");
    TEST_ASSERT(
        cs2_has_id(CS2_T_WIDGET_BASE + area), "the box carries the widget's own id, so it clicks");

    /* The row below has to start UNDER the box, not under the caption: a
     * multiline row measured as one CS2_ROW_H draws the next row through it. */
    {
        int const box = UITree_FindByComponentId(g_tree, CS2_T_WIDGET_BASE + area);
        int const chk = UITree_FindByComponentId(g_tree, CS2_T_WIDGET_BASE + below);
        TEST_ASSERT(box >= 0 && chk >= 0, "both rows are in the tree");
        TEST_ASSERT(
            g_tree->components[chk].position.y >=
                g_tree->components[box].position.y + g_tree->components[box].position.height,
            "the next row clears the whole box");
        TEST_ASSERT(
            g_tree->components[box].position.height >=
                3 * TORIRS_CHROME_M_TEXTAREA_LINE,
            "and the box is as tall as the three lines it was asked for");
    }

    cs2_unbind();
}

/*
 * A LOCKED roster row has NO SWITCH here either.
 *
 * `row_locked` is the model's answer for a plugin that cannot be switched off
 * -- the client's own settings, and the feature flags beside them -- and only
 * the in-canvas chrome was reading it. This executor drew the switch anyway,
 * and drew it from `checked`, which a locked row never sets: a red cross
 * beside Client Settings and Feature Flags, reading as two plugins somebody
 * had turned off and offering a click that would not turn them back on.
 *
 * So the contract is the ABSENCE of the widget's own component. The switch is
 * how a click gets home to a toggle -- see CS2_ID_WIDGET_BASE -- and a row
 * that builds no component in that slot cannot report one however it is
 * clicked, which is a stronger statement than "the sprite is not drawn".
 */
static void
test_chrome_cs2_locked_row(void)
{
    int panel;
    int locked;
    int plain;
    int32_t node;
    int lx = 0, ly = 0, lw = 0, lh = 0;
    int px = 0, py = 0, pw = 0, ph = 0;

    cs2_bind();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 260, "Plugins");
    locked = ToriRSChrome_ListRowLocked(&g_ui, panel, "Client Settings");
    plain = ToriRSChrome_ListRow(&g_ui, panel, "Client Settings", 0, 1);
    cs2_frame();

    TEST_ASSERT(
        cs2_has_id(CS2_T_WIDGET_BASE + plain), "an ordinary roster row carries its switch");
    TEST_ASSERT(
        !cs2_has_id(CS2_T_WIDGET_BASE + locked), "a locked one carries no switch at all");
    TEST_ASSERT(
        cs2_has_id(CS2_T_ACTION_BASE + locked),
        "but keeps its settings well -- with no switch, the page is its only outcome");

    /* And the column the switch is not in goes to the NAME, rather than being
     * left as a hole in the row. */
    node = UITree_FindByComponentId(g_tree, CS2_T_LABEL_BASE + locked);
    TEST_ASSERT(node >= 0, "the locked row's name is in the tree");
    UITree_LayoutGetBounds(&g_tree->components[node].position, &lx, &ly, &lw, &lh);
    node = UITree_FindByComponentId(g_tree, CS2_T_LABEL_BASE + plain);
    TEST_ASSERT(node >= 0, "and so is the ordinary row's");
    UITree_LayoutGetBounds(&g_tree->components[node].position, &px, &py, &pw, &ph);
    TEST_ASSERT(
        lw == pw + TORIRS_CHROME_M_TOGGLE_W,
        "and it is wider by exactly the switch column it does not have");

    cs2_unbind();
}

void
test_chrome_cs2(void)
{
    printf("TEST: CS2 chrome executor (popup list / window chrome / hover)\n");

    test_chrome_cs2_dropdown_opens();
    test_chrome_cs2_dropdown_dismiss();
    test_chrome_cs2_dropdown_scrolls();
    test_chrome_cs2_no_window_x();
    test_chrome_cs2_row_lights_up();
    test_chrome_cs2_check_style();
    test_chrome_cs2_textarea();
    test_chrome_cs2_locked_row();
}

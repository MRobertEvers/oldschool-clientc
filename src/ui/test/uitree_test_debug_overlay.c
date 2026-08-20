/*
 * ToriRSChrome model tests: the parts a BMP cannot check.
 *
 * The visual tests (ui/test/uitree_debug_overlay_visual.c, `make -C src
 * test-debug-overlay-visual`) cover what the overlay *looks* like. These cover
 * what it *promises*: that measurement matches the baked font, that a clean
 * frame does no work, that damage is the union of old and new bounds, that
 * input edits the model the way a single-line edit control does, and that the
 * menu geometry inlined into uitree_debug_overlay.c still equals
 * UIMinimenu_LayoutFromLineBox.
 */
#include "test_harness.h"

#include "uitree_debug_font_metrics.h"
#include "uitree_debug_overlay.h"

/* One live model for the whole file; ~60 KB is well past a sane stack frame. */
static struct ToriRSChrome g_ui;

/*
 * The duplication guard. uitree_debug_overlay.c recomputes
 * UIMinimenu_LayoutFromLineBox rather than including uitree_minimenu.h, because
 * including it would give a no-dependency module a dependency on all of ui/.
 * This test is allowed to include both, so the copy cannot drift unnoticed.
 *
 * It checks the derived quantities the overlay actually uses — panel height,
 * row baselines and the hit box — against the reference layout, for every line
 * box either font could ever have.
 */
static void
test_debug_overlay_menu_geometry(void)
{
    for( int box = 8; box <= 24; box++ )
    {
        struct UIMinimenuLayout const l = UIMinimenu_LayoutFromLineBox(box);
        TEST_ASSERT(l.line_height == box - 2, "minimenu line_height formula");
        TEST_ASSERT(l.row_stride == box - 1, "minimenu row_stride formula");
        TEST_ASSERT(l.header_bar_h == box, "minimenu header_bar_h formula");
        TEST_ASSERT(l.separator_y == box + 2, "minimenu separator_y formula");
        TEST_ASSERT(l.option_base_y == 2 * box - 1, "minimenu option_base_y formula");
        TEST_ASSERT(l.chrome_h == box + 5, "minimenu chrome_h formula");
        TEST_ASSERT(l.hover_above == box - 3, "minimenu hover_above formula");
        TEST_ASSERT(l.hover_below == 3, "minimenu hover_below formula");
        TEST_ASSERT(l.width_pad == 8, "minimenu width_pad is the reference allowance");
        TEST_ASSERT(l.border_inset == box + 3, "minimenu border_inset formula");
    }

    /* The overlay's own menu panel, measured against the reference formulas at
     * the menu font's real line box. */
    {
        struct UIMinimenuLayout const l = UIMinimenu_LayoutFromLineBox(ToriDbgFont_Menu_LINE_BOX);
        int panel;
        int rows[3];
        struct ToriDbgRect rect;

        ToriRSChrome_Init(&g_ui);
        panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_MENU, 40, 60, 0, "Choose Option");
        rows[0] = ToriRSChrome_MenuItem(&g_ui, panel, "Walk here");
        rows[1] = ToriRSChrome_MenuItem(&g_ui, panel, "Examine Guard");
        rows[2] = ToriRSChrome_MenuItem(&g_ui, panel, "Cancel");
        TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "menu panel builds");

        rect = ToriRSChrome_PanelRect(&g_ui, panel);
        TEST_ASSERT(rect.h == 3 * l.row_stride + l.chrome_h, "menu height == UIMinimenu_Height");
        TEST_ASSERT(
            rect.w == ToriRSChrome_MeasureText(TORIDBG_FONT_MENU, "Examine Guard") + l.width_pad,
            "menu width == widest row + width_pad");

        for( int i = 0; i < 3; i++ )
        {
            /* The overlay reads top-to-bottom, so row i is the reference's row
             * i counted from the top: UIMinimenu_OptionY with the reverse
             * already undone. */
            int const baseline = rect.y + i * l.row_stride + l.option_base_y;
            TEST_ASSERT(
                g_ui.widgets[rows[i]].y == baseline - l.hover_above, "menu row hit box top");
            TEST_ASSERT(
                g_ui.widgets[rows[i]].h == l.hover_above + l.hover_below,
                "menu row hit box height");
        }

        /* A click inside a row's band picks that row. The reference band is the
         * open interval (y - hover_above, y + hover_below), which is
         * hover_above + hover_below == box tall while rows are only
         * row_stride == box - 1 apart: consecutive bands overlap by one pixel,
         * and UIMinimenu_HitOption returns the FIRST match, so the seam belongs
         * to the upper row. The overlay's half-open boxes plus first-match
         * iteration reproduce that exactly — asserted here so the two never
         * drift apart on the seam. */
        {
            int const mid = rect.y + 1 * l.row_stride + l.option_base_y;
            TEST_ASSERT(ToriRSChrome_HitTest(&g_ui, rect.x + 4, mid) == rows[1], "menu hit picks row");
            TEST_ASSERT(
                ToriRSChrome_HitTest(&g_ui, rect.x + 4, mid - l.hover_above) == rows[0],
                "menu band seam goes to the row above");
            TEST_ASSERT(
                ToriRSChrome_HitTest(&g_ui, rect.x + 4, mid - l.hover_above + 1) == rows[1],
                "menu hit band top edge");
            TEST_ASSERT(
                ToriRSChrome_HitTest(&g_ui, rect.x + 4, mid + l.hover_below) != rows[1],
                "menu hit band stops below");
        }
    }
}

/* Measurement is the module's one piece of borrowed knowledge: it sums the
 * baked advance table. If that ever stops matching the table the fonts were
 * baked from, every panel is the wrong width. */
static void
test_debug_overlay_measure(void)
{
    static char const* const cases[] = {
        "", "i", "W", "Choose Option", "fps 60  tris 128394", "||  ", "0123456789",
    };

    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        int small = 0;
        int menu = 0;
        for( unsigned char const* p = (unsigned char const*)cases[i]; *p; p++ )
        {
            small += ToriDbgFont_Small_advance_px[*p];
            menu += ToriDbgFont_Menu_advance_px[*p];
        }
        TEST_ASSERT(ToriRSChrome_MeasureText(TORIDBG_FONT_SMALL, cases[i]) == small, "measure small");
        TEST_ASSERT(ToriRSChrome_MeasureText(TORIDBG_FONT_MENU, cases[i]) == menu, "measure menu");
    }

    TEST_ASSERT(
        ToriRSChrome_FontLineHeight(TORIDBG_FONT_SMALL) == ToriDbgFont_Small_LINE_HEIGHT,
        "small ascent");
    TEST_ASSERT(
        ToriRSChrome_FontLineBox(TORIDBG_FONT_MENU) == ToriDbgFont_Menu_LINE_BOX, "menu line box");
    /* A wider face has to measure wider, or the two tables got swapped. */
    TEST_ASSERT(
        ToriRSChrome_MeasureText(TORIDBG_FONT_MENU, "Choose Option") >
            ToriRSChrome_MeasureText(TORIDBG_FONT_SMALL, "Choose Option"),
        "menu face is the wider one");
}

/* The retained-mode claim: a frame where nothing changed rebuilds nothing. */
static void
test_debug_overlay_retained(void)
{
    int panel;
    int label;
    int prim_count = 0;
    int first_count;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 10, 10, 0, "Debug");
    label = ToriRSChrome_Label(&g_ui, panel, "fps 60");

    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "first build rebuilds");
    ToriRSChrome_Prims(&g_ui, &first_count);
    TEST_ASSERT(first_count > 0, "first build produced prims");

    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 0, "clean frame does not rebuild");
    ToriRSChrome_Prims(&g_ui, &prim_count);
    TEST_ASSERT(prim_count == first_count, "clean frame keeps the display list");

    /* Same value written again is not a change. This is what stops an app that
     * pushes its frame counter every frame from relaying out every frame. */
    ToriRSChrome_SetText(&g_ui, label, "fps 60");
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 0, "identical SetText does not dirty");

    ToriRSChrome_SetText(&g_ui, label, "fps 59");
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "changed SetText dirties");

    ToriRSChrome_SetChecked(&g_ui, label, 0);
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 0, "no-op SetChecked does not dirty");

    /* A hover that lands nowhere must not dirty either — mouse motion over an
     * empty screen is the most common event there is. */
    ToriRSChrome_MouseMove(&g_ui, 900, 900);
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 0, "hover miss does not dirty");
}

/* Damage is old ∪ new: the pixels a moved or shrunk panel vacated are invalid
 * too, which is the whole reason the union exists. */
static void
test_debug_overlay_damage(void)
{
    int panel;
    struct ToriDbgRect d;
    struct ToriDbgRect before;
    struct ToriDbgRect after;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 100, 100, 0, "Stats");
    ToriRSChrome_Label(&g_ui, panel, "tris 128394");
    ToriRSChrome_Build(&g_ui);

    before = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(ToriRSChrome_Damage(&g_ui, &d), "first build damages");
    TEST_ASSERT(
        d.x == before.x && d.y == before.y && d.w == before.w && d.h == before.h,
        "first damage is the panel");

    ToriRSChrome_DamageClear(&g_ui);
    TEST_ASSERT(!ToriRSChrome_Damage(&g_ui, &d), "cleared damage is empty");

    ToriRSChrome_PanelMove(&g_ui, panel, 140, 130);
    ToriRSChrome_Build(&g_ui);
    after = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(ToriRSChrome_Damage(&g_ui, &d), "move damages");
    TEST_ASSERT(d.x == before.x && d.y == before.y, "damage starts at the old top-left");
    TEST_ASSERT(
        d.x + d.w == after.x + after.w && d.y + d.h == after.y + after.h,
        "damage ends at the new bottom-right");

    /* Hiding a panel damages what it used to cover, and nothing else. */
    ToriRSChrome_DamageClear(&g_ui);
    ToriRSChrome_PanelSetVisible(&g_ui, panel, 0);
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(ToriRSChrome_Damage(&g_ui, &d), "hide damages");
    TEST_ASSERT(
        d.x == after.x && d.y == after.y && d.w == after.w && d.h == after.h,
        "hide damage is the vacated rect");
    {
        int n = -1;
        ToriRSChrome_Prims(&g_ui, &n);
        TEST_ASSERT(n == 0, "a hidden panel emits nothing");
    }

    /* Two dirty panels union into one box, the way a WM_PAINT invalid region
     * accumulates before the paint. */
    {
        int a;
        int b;
        ToriRSChrome_Init(&g_ui);
        a = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 40, "A");
        b = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 300, 200, 40, "B");
        ToriRSChrome_Label(&g_ui, a, "a");
        ToriRSChrome_Label(&g_ui, b, "b");
        ToriRSChrome_Build(&g_ui);
        TEST_ASSERT(ToriRSChrome_Damage(&g_ui, &d), "two panels damage");
        TEST_ASSERT(d.x == 0 && d.y == 0, "union covers the first panel");
        TEST_ASSERT(d.x + d.w >= 340 && d.y + d.h >= 200, "union covers the second panel");
    }
}

/* Bordered background: body fill and a 1px outline of the same box, which is
 * TORIRSRC_FILL_RECT with filled == 0 and needs no new render command. */
static void
test_debug_overlay_border(void)
{
    int panel;
    struct ToriDbgPrim const* prims;
    int count = 0;
    int outlines = 0;
    int fills = 0;
    struct ToriDbgRect rect;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 20, 20, 120, "Border");
    ToriRSChrome_Label(&g_ui, panel, "body");
    ToriRSChrome_Build(&g_ui);
    rect = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(rect.w == 120, "fixed_w wins over content width");

    prims = ToriRSChrome_Prims(&g_ui, &count);
    for( int i = 0; i < count; i++ )
    {
        if( prims[i].kind != TORIDBG_PRIM_RECT )
            continue;
        if( prims[i].x != rect.x || prims[i].y != rect.y )
            continue;
        if( prims[i].w != rect.w || prims[i].h != rect.h )
            continue;
        if( prims[i].filled )
            fills++;
        else
            outlines++;
    }
    TEST_ASSERT(fills == 1, "panel body is one filled rect");
    TEST_ASSERT(outlines == 1, "panel border is one outlined rect");

    /* Border after body, or the fill paints over the border it belongs to. */
    {
        int body_at = -1;
        int border_at = -1;
        for( int i = 0; i < count; i++ )
        {
            if( prims[i].kind != TORIDBG_PRIM_RECT || prims[i].w != rect.w )
                continue;
            if( prims[i].h != rect.h )
                continue;
            if( prims[i].filled && body_at < 0 )
                body_at = i;
            if( !prims[i].filled && border_at < 0 )
                border_at = i;
        }
        TEST_ASSERT(body_at >= 0 && border_at > body_at, "border draws after the body");
    }

    /* Row content is clipped to the content column, so an over-long label is
     * cut at the border instead of painting across it. */
    for( int i = 0; i < count; i++ )
    {
        if( prims[i].kind != TORIDBG_PRIM_TEXT )
            continue;
        TEST_ASSERT(prims[i].clip.x >= rect.x, "text clip inside the panel");
        TEST_ASSERT(
            prims[i].clip.x + prims[i].clip.w <= rect.x + rect.w, "text clip stops at the border");
    }
}

/* Checkbox: hit box, press/release pairing, toggle, activation latch. */
static void
test_debug_overlay_checkbox(void)
{
    int panel;
    int box;
    int cx;
    int cy;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 30, 30, 0, "Toggles");
    box = ToriRSChrome_Checkbox(&g_ui, panel, "wireframe", 0);
    ToriRSChrome_Build(&g_ui);

    cx = g_ui.widgets[box].x + 2;
    cy = g_ui.widgets[box].y + 2;
    TEST_ASSERT(ToriRSChrome_HitTest(&g_ui, cx, cy) == box, "checkbox hit");
    TEST_ASSERT(ToriRSChrome_HitTest(&g_ui, cx, cy - 40) != box, "miss above the row");

    TEST_ASSERT(ToriRSChrome_MouseDown(&g_ui, cx, cy) == 1, "press over a panel is consumed");
    ToriRSChrome_MouseUp(&g_ui, cx, cy);
    TEST_ASSERT(ToriRSChrome_Checked(&g_ui, box) == 1, "click toggles on");
    TEST_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == box, "toggle latches activation");
    TEST_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == -1, "activation drains once");
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "toggle dirtied the panel");

    /* Press here, release elsewhere: a cancelled click, not a toggle. */
    ToriRSChrome_MouseDown(&g_ui, cx, cy);
    ToriRSChrome_MouseUp(&g_ui, 900, 900);
    TEST_ASSERT(ToriRSChrome_Checked(&g_ui, box) == 1, "drag-off does not toggle");
    TEST_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == -1, "drag-off does not activate");

    /* Hover changes the row's colour, so it has to dirty — and only once. */
    ToriRSChrome_Build(&g_ui);
    ToriRSChrome_MouseMove(&g_ui, cx, cy);
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "hover enter dirties");
    ToriRSChrome_MouseMove(&g_ui, cx + 1, cy + 1);
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 0, "hover within the same row does not dirty");
    ToriRSChrome_MouseMove(&g_ui, 900, 900);
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "hover leave dirties");

    /* Clicks land outside every panel: not consumed, so the app can pass them
     * on to the game's own hit test. */
    TEST_ASSERT(ToriRSChrome_MouseDown(&g_ui, 900, 900) == 0, "press off-panel is not consumed");
}

/* Text input: focus, insertion at the caret, the editing keys, and the bounds
 * that keep a fixed buffer safe. */
static void
test_debug_overlay_textinput(void)
{
    int panel;
    int input;
    int box;
    int cx;
    int cy;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 8, 8, 0, "Console");
    input = ToriRSChrome_TextInput(&g_ui, panel, "cmd", "ab");
    box = ToriRSChrome_Checkbox(&g_ui, panel, "echo", 0);
    ToriRSChrome_Build(&g_ui);

    cx = g_ui.widgets[input].x + g_ui.widgets[input].w - 4;
    cy = g_ui.widgets[input].y + 2;

    TEST_ASSERT(!ToriRSChrome_KeyChar(&g_ui, 'x'), "typing without focus is ignored");
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "ab") == 0, "unfocused text unchanged");

    ToriRSChrome_MouseDown(&g_ui, cx, cy);
    TEST_ASSERT(g_ui.focus == input, "click focuses the input");
    TEST_ASSERT(g_ui.widgets[input].caret == 2, "focus puts the caret at the end");

    TEST_ASSERT(ToriRSChrome_KeyChar(&g_ui, 'c'), "typing consumed");
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "abc") == 0, "append at the caret");

    ToriRSChrome_KeyEdit(&g_ui, TORIDBG_KEY_LEFT);
    ToriRSChrome_KeyChar(&g_ui, 'X');
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "abXc") == 0, "insert mid-string");

    ToriRSChrome_KeyEdit(&g_ui, TORIDBG_KEY_BACKSPACE);
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "abc") == 0, "backspace deletes left");

    ToriRSChrome_KeyEdit(&g_ui, TORIDBG_KEY_HOME);
    TEST_ASSERT(g_ui.widgets[input].caret == 0, "home");
    ToriRSChrome_KeyEdit(&g_ui, TORIDBG_KEY_DELETE);
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "bc") == 0, "delete removes right");
    ToriRSChrome_KeyEdit(&g_ui, TORIDBG_KEY_BACKSPACE);
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "bc") == 0, "backspace at 0 is a no-op");

    ToriRSChrome_KeyEdit(&g_ui, TORIDBG_KEY_END);
    TEST_ASSERT(g_ui.widgets[input].caret == 2, "end");
    ToriRSChrome_KeyEdit(&g_ui, TORIDBG_KEY_RIGHT);
    TEST_ASSERT(g_ui.widgets[input].caret == 2, "right at the end is a no-op");

    TEST_ASSERT(!ToriRSChrome_KeyChar(&g_ui, '\n'), "control bytes are not inserted");
    TEST_ASSERT(!ToriRSChrome_KeyChar(&g_ui, 0x1B), "escape byte is not inserted");
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "bc") == 0, "text survives control bytes");

    ToriRSChrome_KeyEdit(&g_ui, TORIDBG_KEY_ENTER);
    TEST_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == input, "enter commits");

    /* The buffer is fixed; typing past it must stop, not run off the end. */
    for( int i = 0; i < TORIDBG_INPUT_MAX * 2; i++ )
        ToriRSChrome_KeyChar(&g_ui, 'z');
    TEST_ASSERT(
        (int)strlen(ToriRSChrome_Text(&g_ui, input)) == TORIDBG_INPUT_MAX - 1, "input is bounded");

    /* Clicking a non-input widget drops focus, so keys stop being swallowed. */
    ToriRSChrome_MouseDown(&g_ui, g_ui.widgets[box].x + 2, g_ui.widgets[box].y + 2);
    TEST_ASSERT(g_ui.focus == -1, "clicking a checkbox unfocuses the input");
    ToriRSChrome_MouseUp(&g_ui, g_ui.widgets[box].x + 2, g_ui.widgets[box].y + 2);

    /* Escape releases focus too. */
    ToriRSChrome_MouseDown(&g_ui, cx, cy);
    TEST_ASSERT(g_ui.focus == input, "refocus");
    ToriRSChrome_KeyEdit(&g_ui, TORIDBG_KEY_ESCAPE);
    TEST_ASSERT(g_ui.focus == -1, "escape releases focus");

    /* The caret blink repaints, but only while something is focused. */
    ToriRSChrome_Build(&g_ui);
    ToriRSChrome_SetCaretVisible(&g_ui, 0);
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 0, "blink with no focus does not repaint");
    ToriRSChrome_MouseDown(&g_ui, cx, cy);
    ToriRSChrome_Build(&g_ui);
    ToriRSChrome_SetCaretVisible(&g_ui, 1);
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "blink with focus repaints");
}

/* Layout: rows stack in insertion order, the panel sizes to its widest row, and
 * text baselines sit where ToriDraw2D_DrawString's `y -= line_height` puts them. */
static void
test_debug_overlay_layout(void)
{
    int panel;
    int a;
    int b;
    struct ToriDbgRect rect;
    struct ToriDbgPrim const* prims;
    int count = 0;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 0, NULL);
    a = ToriRSChrome_Label(&g_ui, panel, "short");
    ToriRSChrome_Separator(&g_ui, panel);
    b = ToriRSChrome_Label(&g_ui, panel, "a considerably longer row");
    ToriRSChrome_Build(&g_ui);

    rect = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(g_ui.widgets[a].y < g_ui.widgets[b].y, "rows stack downward in order");
    TEST_ASSERT(
        rect.w >= ToriRSChrome_MeasureText(g_ui.theme.font_row, "a considerably longer row"),
        "panel fits its widest row");
    TEST_ASSERT(
        g_ui.widgets[b].y + g_ui.widgets[b].h <= rect.y + rect.h, "last row fits inside the panel");
    /* No title given, so no title bar and no menu-face text. Asserted against
     * the MENU face rather than for the row face: which face rows use is the
     * theme's choice (ToriDbgTheme::font_row), and the claim here is only that
     * the bold title face went unused. */
    prims = ToriRSChrome_Prims(&g_ui, &count);
    for( int i = 0; i < count; i++ )
        TEST_ASSERT(
            prims[i].kind != TORIDBG_PRIM_TEXT || prims[i].font_slot != TORIDBG_FONT_MENU,
            "untitled window panel draws no menu-face text");

    for( int i = 0; i < count; i++ )
    {
        if( prims[i].kind != TORIDBG_PRIM_TEXT )
            continue;
        TEST_ASSERT(prims[i].baseline == 1, "overlay text is baseline-positioned");
        /* The glyph line box is [y - line_height, y - line_height + line_box).
         * Both edges have to be inside the panel or the row is misplaced. */
        TEST_ASSERT(
            prims[i].y - ToriRSChrome_FontLineHeight(prims[i].font_slot) >= rect.y,
            "glyph box top inside the panel");
        TEST_ASSERT(
            prims[i].y - ToriRSChrome_FontLineHeight(prims[i].font_slot) +
                    ToriRSChrome_FontLineBox(prims[i].font_slot) <=
                rect.y + rect.h,
            "glyph box bottom inside the panel");
    }
}

/* Capacity: the model is fixed-size, so over-filling has to report rather than
 * scribble. `overflow` is the honest signal that the display list is short. */
static void
test_debug_overlay_capacity(void)
{
    int panel;

    ToriRSChrome_Init(&g_ui);
    for( int i = 0; i < TORIDBG_MAX_PANELS; i++ )
        TEST_ASSERT(ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 20, "p") >= 0,
                    "panels up to the cap");
    TEST_ASSERT(
        ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 20, "p") == -1,
        "one panel past the cap fails");
    TEST_ASSERT(g_ui.overflow == 1, "panel overflow is reported");

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 20, "p");
    for( int i = 0; i < TORIDBG_MAX_WIDGETS; i++ )
        TEST_ASSERT(ToriRSChrome_Label(&g_ui, panel, "x") >= 0, "widgets up to the cap");
    TEST_ASSERT(ToriRSChrome_Label(&g_ui, panel, "x") == -1, "one widget past the cap fails");

    /* Over-long strings truncate into the fixed buffers, terminator included. */
    ToriRSChrome_Init(&g_ui);
    {
        static char long_text[TORIDBG_INPUT_MAX * 3];
        int w;
        memset(long_text, 'A', sizeof(long_text) - 1);
        long_text[sizeof(long_text) - 1] = '\0';
        panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 0, long_text);
        w = ToriRSChrome_Label(&g_ui, panel, long_text);
        TEST_ASSERT((int)strlen(ToriRSChrome_Text(&g_ui, w)) == TORIDBG_INPUT_MAX - 1,
                    "label text truncates");
        TEST_ASSERT((int)strlen(g_ui.panels[panel].title) == TORIDBG_LABEL_MAX - 1,
                    "panel title truncates");
    }

    /* Every entry point has to survive a bad handle: a debug overlay that
     * crashes the client it is meant to debug is worse than none. */
    ToriRSChrome_Init(&g_ui);
    ToriRSChrome_SetText(&g_ui, 99, "x");
    ToriRSChrome_SetLabel(&g_ui, -1, "x");
    ToriRSChrome_SetChecked(&g_ui, 99, 1);
    ToriRSChrome_SetColor(&g_ui, -5, 0xFFFFFF);
    ToriRSChrome_PanelMove(&g_ui, 99, 0, 0);
    ToriRSChrome_PanelSetVisible(&g_ui, -1, 1);
    TEST_ASSERT(ToriRSChrome_Checked(&g_ui, 99) == 0, "Checked on a bad handle is 0");
    TEST_ASSERT(ToriRSChrome_Text(&g_ui, 99)[0] == '\0', "Text on a bad handle is empty");
    TEST_ASSERT(ToriRSChrome_PanelRect(&g_ui, 99).w == 0, "PanelRect on a bad handle is empty");
    TEST_ASSERT(ToriRSChrome_HitTest(&g_ui, 0, 0) == -1, "hit test on an empty model");
    TEST_ASSERT(ToriRSChrome_Label(&g_ui, 99, "x") == -1, "widget on a bad panel fails");

    /* Reset clears the model but leaves the vacated pixels marked. */
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 50, 50, 60, "gone");
    ToriRSChrome_Label(&g_ui, panel, "x");
    ToriRSChrome_Build(&g_ui);
    ToriRSChrome_DamageClear(&g_ui);
    ToriRSChrome_Reset(&g_ui);
    {
        struct ToriDbgRect d;
        int n = -1;
        TEST_ASSERT(ToriRSChrome_Damage(&g_ui, &d), "reset damages the old bounds");
        TEST_ASSERT(d.x == 50 && d.y == 50, "reset damage is where the panel was");
        ToriRSChrome_Build(&g_ui);
        ToriRSChrome_Prims(&g_ui, &n);
        TEST_ASSERT(n == 0, "reset empties the display list");
        TEST_ASSERT(g_ui.panel_count == 0 && g_ui.widget_count == 0, "reset empties the model");
    }
}

/*
 * The emit pass. Two things matter and neither is visible in a BMP: the whole
 * display list travels as ONE desc holding a pointer (so a steady overlay costs
 * a pointer copy, not N desc copies), and that desc is emitted LAST, so
 * developer chrome is over everything the walk drew.
 */
static void
test_debug_overlay_emit_pass(void)
{
    struct UITree* tree;
    struct UITreeHost host;
    struct TestHostState state;
    struct UITreeEmitBuffer buf;
    struct UITreeNodeSpec spec;
    int panel;
    int overlay_descs = 0;
    int last_is_overlay = 0;
    int prim_count = 0;

    tree = UITree_New(8);
    UITree_TestHostInit(&host, &state);
    UITree_EmitBufferInit(&buf);

    /* Something ordinary for the walk to emit, then the overlay node last —
     * the position a RevConfig root layout normally gives it. */
    UITree_TestPushXy(tree, -1, UIELEM_RS_RECT, 500, 0, 0, 50, 50);
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_DEBUG_OVERLAY;
    spec.component_id = 501;
    spec.u.debug_overlay.font_id_small = 494;
    spec.u.debug_overlay.font_id_menu = 496;
    TEST_ASSERT(UITree_Push(tree, -1, &spec) >= 0, "overlay node pushes");
    UITree_TestResolve(tree);

    /* No overlay on the host: the pass costs one request and emits nothing. */
    UITree_EmitWalk(tree, &host, &buf, -1);
    for( int i = 0; i < buf.count; i++ )
        TEST_ASSERT(buf.cmds[i].kind != UITREE_EMIT_DEBUG_OVERLAY, "no overlay, no desc");

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 4, 4, 0, "Debug");
    ToriRSChrome_Checkbox(&g_ui, panel, "wireframe", 1);
    ToriRSChrome_Label(&g_ui, panel, "fps 60");
    ToriRSChrome_Build(&g_ui);
    ToriRSChrome_Prims(&g_ui, &prim_count);
    state.debug_overlay = &g_ui;

    buf.count = 0;
    UITree_EmitWalk(tree, &host, &buf, -1);
    for( int i = 0; i < buf.count; i++ )
    {
        if( buf.cmds[i].kind != UITREE_EMIT_DEBUG_OVERLAY )
            continue;
        overlay_descs++;
        last_is_overlay = (i == buf.count - 1);
        TEST_ASSERT(buf.cmds[i].debug_prim_count == prim_count, "desc carries the prim count");
        TEST_ASSERT(
            buf.cmds[i].debug_prims == ToriRSChrome_Prims(&g_ui, NULL),
            "desc carries the host's list by pointer, not a copy");
        TEST_ASSERT(buf.cmds[i].component_id == 501, "desc carries the node's component id");
        TEST_ASSERT(
            buf.cmds[i].debug_font_id[TORIDBG_FONT_SMALL] == 494 &&
                buf.cmds[i].debug_font_id[TORIDBG_FONT_MENU] == 496,
            "desc carries the slot -> scene font mapping");
    }
    TEST_ASSERT(overlay_descs == 1, "the whole display list is one desc");
    TEST_ASSERT(last_is_overlay, "the overlay desc is emitted last");
    TEST_ASSERT(buf.count > 1, "the ordinary walk still emitted");

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}

void
test_debug_overlay(void)
{
    printf("TEST: debug overlay (measure / menu geometry / retained / damage / widgets)\n");

    test_debug_overlay_measure();
    test_debug_overlay_menu_geometry();
    test_debug_overlay_retained();
    test_debug_overlay_damage();
    test_debug_overlay_border();
    test_debug_overlay_checkbox();
    test_debug_overlay_textinput();
    test_debug_overlay_layout();
    test_debug_overlay_capacity();
    test_debug_overlay_emit_pass();
}

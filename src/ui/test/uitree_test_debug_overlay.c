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

#include "torirs_chrome_metrics.h"
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
        struct UIMinimenuLayout const l =
            UIMinimenu_LayoutFromLineBox(ToriRSChromeFont_Menu_LINE_BOX);
        int panel;
        int rows[3];
        struct ToriRSChromeRect rect;

        ToriRSChrome_Init(&g_ui);
        panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_MENU, 40, 60, 0, "Choose Option");
        rows[0] = ToriRSChrome_MenuItem(&g_ui, panel, "Walk here");
        rows[1] = ToriRSChrome_MenuItem(&g_ui, panel, "Examine Guard");
        rows[2] = ToriRSChrome_MenuItem(&g_ui, panel, "Cancel");
        TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "menu panel builds");

        rect = ToriRSChrome_PanelRect(&g_ui, panel);
        TEST_ASSERT(rect.h == 3 * l.row_stride + l.chrome_h, "menu height == UIMinimenu_Height");
        TEST_ASSERT(
            rect.w ==
                ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_MENU, 1, "Examine Guard") + l.width_pad,
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
            TEST_ASSERT(
                ToriRSChrome_HitTest(&g_ui, rect.x + 4, mid) == rows[1], "menu hit picks row");
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
            small += ToriRSChromeFont_Small_advance_px[*p];
            menu += ToriRSChromeFont_Menu_advance_px[*p];
        }
        TEST_ASSERT(
            ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, cases[i]) == small,
            "measure small");
        TEST_ASSERT(
            ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_MENU, 1, cases[i]) == menu, "measure menu");
    }

    TEST_ASSERT(
        ToriRSChrome_FontLineHeight(TORIRS_CHROME_FONT_SMALL, 1) ==
            ToriRSChromeFont_Small_LINE_HEIGHT,
        "small ascent");
    TEST_ASSERT(
        ToriRSChrome_FontLineBox(TORIRS_CHROME_FONT_MENU, 1) == ToriRSChromeFont_Menu_LINE_BOX,
        "menu line box");
    /* A wider face has to measure wider, or the two tables got swapped. */
    TEST_ASSERT(
        ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_MENU, 1, "Choose Option") >
            ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, "Choose Option"),
        "menu face is the wider one");
}

/*
 * The scaled-chrome claim: 2x and 3x are the 1x chrome multiplied, exactly.
 *
 * Worth pinning rather than assuming, because it is a property of the BAKE and
 * not of this module -- fontbake scales every glyph and every metric by the
 * same integer, and if it ever stopped doing so (a rounded advance, a glyph
 * resampled instead of block-scaled) the chrome would still lay out and still
 * draw, just fractionally wrong at one size and not the other. Which is
 * precisely the bug nobody finds by looking.
 */
static void
test_debug_overlay_scaled_metrics(void)
{
    static char const* const cases[] = {
        "", "i", "W", "Choose Option", "fps 60  tris 128394", "0123456789",
    };
    static int const slots[] = { TORIRS_CHROME_FONT_SMALL,
                                 TORIRS_CHROME_FONT_BODY,
                                 TORIRS_CHROME_FONT_MENU };

    for( int si = 0; si < 3; si++ )
    {
        int const slot = slots[si];
        for( int scale = TORIRS_CHROME_SCALE_MIN; scale <= TORIRS_CHROME_SCALE_MAX; scale++ )
        {
            TEST_ASSERT(
                ToriRSChrome_FontLineHeight(slot, scale) ==
                    ToriRSChrome_FontLineHeight(slot, 1) * scale,
                "scaled ascent is the 1x ascent times the scale");
            TEST_ASSERT(
                ToriRSChrome_FontLineBox(slot, scale) ==
                    ToriRSChrome_FontLineBox(slot, 1) * scale,
                "scaled line box is the 1x line box times the scale");
            for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
                TEST_ASSERT(
                    ToriRSChrome_MeasureText(slot, scale, cases[i]) ==
                        ToriRSChrome_MeasureText(slot, 1, cases[i]) * scale,
                    "scaled measure is the 1x measure times the scale");
        }
    }
}

/*
 * And the same claim one level up: a scaled PANEL is the 1x panel multiplied.
 *
 * The metrics test above proves the fonts scale; this proves the layout does,
 * which is the half that lives in this file. Row padding, rules, the header
 * block and the content column all have to move together -- a padding that
 * stayed at 1x while the text grew is a panel whose rows overlap, and it is
 * invisible until someone runs the editor on a HighDPI display.
 */
static void
test_debug_overlay_scaled_layout(void)
{
    struct ToriRSChromeRect base = { 0, 0, 0, 0 };

    for( int scale = TORIRS_CHROME_SCALE_MIN; scale <= TORIRS_CHROME_SCALE_MAX; scale++ )
    {
        struct ToriRSChromeRect rect;
        int panel;

        ToriRSChrome_Init(&g_ui);
        ToriRSChrome_SetScale(&g_ui, scale);
        panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 0, "Map Editor");
        ToriRSChrome_Label(&g_ui, panel, "a considerably longer row");
        ToriRSChrome_Checkbox(&g_ui, panel, "block", 1);
        ToriRSChrome_TextInput(&g_ui, panel, "Height", "30");
        ToriRSChrome_Build(&g_ui);
        rect = ToriRSChrome_PanelRect(&g_ui, panel);

        TEST_ASSERT(ToriRSChrome_Scale(&g_ui) == scale, "scale is what was set");
        if( scale == 1 )
            base = rect;
        else
        {
            TEST_ASSERT(rect.w == base.w * scale, "panel width scales exactly");
            TEST_ASSERT(rect.h == base.h * scale, "panel height scales exactly");
        }
    }
    ToriRSChrome_Init(&g_ui);
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 0, "Debug");
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
    struct ToriRSChromeRect d;
    struct ToriRSChromeRect before;
    struct ToriRSChromeRect after;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 100, 100, 0, "Stats");
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
        a = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 40, "A");
        b = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 300, 200, 40, "B");
        ToriRSChrome_Label(&g_ui, a, "a");
        ToriRSChrome_Label(&g_ui, b, "b");
        ToriRSChrome_Build(&g_ui);
        TEST_ASSERT(ToriRSChrome_Damage(&g_ui, &d), "two panels damage");
        TEST_ASSERT(d.x == 0 && d.y == 0, "union covers the first panel");
        TEST_ASSERT(d.x + d.w >= 340 && d.y + d.h >= 200, "union covers the second panel");
    }
}

/*
 * A window panel wears the minimenu's chrome: body fill, black header bar, the
 * separator under it, a bottom rule and a rail down each side -- at the same
 * offsets dbg_build_menu uses, because a panel and a real game menu on screen
 * together have to read as one widget.
 *
 * The outer 1px outline this used to draw is deliberately gone: the minimenu
 * has no such edge, and it was the one thing that still gave a panel away.
 */
static void
test_debug_overlay_border(void)
{
    struct UIMinimenuLayout const l = UIMinimenu_LayoutFromLineBox(ToriRSChromeFont_Menu_LINE_BOX);
    int panel;
    struct ToriRSChromePrim const* prims;
    int count = 0;
    int outlines = 0;
    int fills = 0;
    int header = 0;
    int separator = 0;
    int bottom = 0;
    int rails = 0;
    struct ToriRSChromeRect rect;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 20, 120, "Border");
    ToriRSChrome_Label(&g_ui, panel, "body");
    ToriRSChrome_Build(&g_ui);
    rect = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(rect.w == 120, "fixed_w wins over content width");

    prims = ToriRSChrome_Prims(&g_ui, &count);
    for( int i = 0; i < count; i++ )
    {
        struct ToriRSChromePrim const* q = &prims[i];
        if( q->kind != TORIRS_CHROME_PRIM_RECT )
            continue;
        if( q->x == rect.x && q->y == rect.y && q->w == rect.w && q->h == rect.h )
        {
            if( q->filled )
                fills++;
            else
                outlines++;
        }
        if( q->x == rect.x + 1 && q->y == rect.y + 1 && q->w == rect.w - 2 &&
            q->h == l.header_bar_h )
            header++;
        if( q->x == rect.x + 1 && q->y == rect.y + l.separator_y && q->w == rect.w - 2 &&
            q->h == 1 )
            separator++;
        if( q->x == rect.x + 1 && q->y == rect.y + rect.h - 2 && q->w == rect.w - 2 && q->h == 1 )
            bottom++;
        if( q->y == rect.y + l.separator_y && q->w == 1 && q->h == rect.h - l.border_inset &&
            (q->x == rect.x + 1 || q->x == rect.x + rect.w - 2) )
            rails++;
    }
    TEST_ASSERT(fills == 1, "panel body is one filled rect");
    TEST_ASSERT(outlines == 0, "no outer outline: the minimenu does not draw one");
    TEST_ASSERT(header == 1, "header bar at the minimenu's offset");
    TEST_ASSERT(separator == 1, "separator rule at the minimenu's offset");
    TEST_ASSERT(bottom == 1, "bottom rule at the minimenu's offset");
    TEST_ASSERT(rails == 2, "a rail down each side, starting at the separator");

    /* Chrome after body, or the fill paints over the chrome it belongs to. */
    {
        int body_at = -1;
        int chrome_at = -1;
        for( int i = 0; i < count; i++ )
        {
            struct ToriRSChromePrim const* q = &prims[i];
            if( q->kind != TORIRS_CHROME_PRIM_RECT )
                continue;
            if( body_at < 0 && q->x == rect.x && q->y == rect.y && q->w == rect.w &&
                q->h == rect.h )
                body_at = i;
            if( chrome_at < 0 && q->x == rect.x + 1 && q->y == rect.y + 1 &&
                q->w == rect.w - 2 && q->h == l.header_bar_h )
                chrome_at = i;
        }
        TEST_ASSERT(body_at >= 0 && chrome_at > body_at, "chrome draws after the body");
    }

    /* Row content is clipped to the content column, so an over-long label is
     * cut at the border instead of painting across it. */
    for( int i = 0; i < count; i++ )
    {
        if( prims[i].kind != TORIRS_CHROME_PRIM_TEXT )
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 30, 30, 0, "Toggles");
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 8, 0, "Console");
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

    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_LEFT);
    ToriRSChrome_KeyChar(&g_ui, 'X');
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "abXc") == 0, "insert mid-string");

    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_BACKSPACE);
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "abc") == 0, "backspace deletes left");

    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_HOME);
    TEST_ASSERT(g_ui.widgets[input].caret == 0, "home");
    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_DELETE);
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "bc") == 0, "delete removes right");
    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_BACKSPACE);
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "bc") == 0, "backspace at 0 is a no-op");

    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_END);
    TEST_ASSERT(g_ui.widgets[input].caret == 2, "end");
    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_RIGHT);
    TEST_ASSERT(g_ui.widgets[input].caret == 2, "right at the end is a no-op");

    TEST_ASSERT(!ToriRSChrome_KeyChar(&g_ui, '\n'), "control bytes are not inserted");
    TEST_ASSERT(!ToriRSChrome_KeyChar(&g_ui, 0x1B), "escape byte is not inserted");
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "bc") == 0, "text survives control bytes");

    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_ENTER);
    TEST_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == input, "enter commits");

    /* The buffer is fixed; typing past it must stop, not run off the end. */
    for( int i = 0; i < TORIRS_CHROME_INPUT_MAX * 2; i++ )
        ToriRSChrome_KeyChar(&g_ui, 'z');
    TEST_ASSERT(
        (int)strlen(ToriRSChrome_Text(&g_ui, input)) == TORIRS_CHROME_INPUT_MAX - 1,
        "input is bounded");

    /* Clicking a non-input widget drops focus, so keys stop being swallowed. */
    ToriRSChrome_MouseDown(&g_ui, g_ui.widgets[box].x + 2, g_ui.widgets[box].y + 2);
    TEST_ASSERT(g_ui.focus == -1, "clicking a checkbox unfocuses the input");
    ToriRSChrome_MouseUp(&g_ui, g_ui.widgets[box].x + 2, g_ui.widgets[box].y + 2);

    /* Escape releases focus too. */
    ToriRSChrome_MouseDown(&g_ui, cx, cy);
    TEST_ASSERT(g_ui.focus == input, "refocus");
    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_ESCAPE);
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


/*
 * The multiline field: wrapping, the four keys that differ from a one-line
 * field's, the caret a click places, and the box that follows it.
 *
 * WHY THE WRAP IS TESTED FROM THE OUTSIDE. ToriRSChrome_WrapText is public
 * because more than one presentation breaks the same string for the same box
 * and they have to agree; a wrap that only the draw could see
 * would let them drift with nothing failing.
 */
static void
test_debug_overlay_textarea(void)
{
    int panel;
    int area;
    int one;
    int starts[8];
    int lens[8];
    int n;
    int line_h;

    /* ---- the wrap, on its own ---- */
    n = ToriRSChrome_WrapText(TORIRS_CHROME_FONT_SMALL, 1, "", 100, starts, lens, 8);
    TEST_ASSERT(n == 1 && lens[0] == 0, "an empty value is one empty line");

    n = ToriRSChrome_WrapText(TORIRS_CHROME_FONT_SMALL, 1, "a\nb", 1000, starts, lens, 8);
    TEST_ASSERT(n == 2, "a hard newline breaks a line");
    TEST_ASSERT(starts[0] == 0 && lens[0] == 1, "and is not part of the line before it");
    TEST_ASSERT(starts[1] == 2 && lens[1] == 1, "nor of the one after");

    n = ToriRSChrome_WrapText(TORIRS_CHROME_FONT_SMALL, 1, "a\n", 1000, starts, lens, 8);
    TEST_ASSERT(n == 2 && lens[1] == 0, "a trailing newline still opens a line");

    /* Narrow enough that "aaa bbb" cannot be one line: the break goes after the
     * space, so the slices stay contiguous and a caret offset is still on
     * exactly one of them. */
    {
        int const w = ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, "aaa b");
        n = ToriRSChrome_WrapText(TORIRS_CHROME_FONT_SMALL, 1, "aaa bbb", w, starts, lens, 8);
        TEST_ASSERT(n == 2, "a line too long for the box wraps");
        TEST_ASSERT(starts[1] == starts[0] + lens[0], "wrapped slices are contiguous");
        TEST_ASSERT(lens[0] == 4, "the break goes after the space, not through the word");
    }

    /* A single word wider than the box has nowhere to break BETWEEN words, and
     * must break inside it rather than loop or run off the edge. */
    {
        int const w = ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, "aa");
        n = ToriRSChrome_WrapText(TORIRS_CHROME_FONT_SMALL, 1, "aaaaaa", w, starts, lens, 8);
        TEST_ASSERT(n > 1, "an unbreakable word still wraps");
        TEST_ASSERT(lens[0] > 0, "and every line consumes at least one byte");
    }

    /* ---- the widget ---- */
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 8, 0, "Ground Items");
    area = ToriRSChrome_TextArea(&g_ui, panel, "Highlighted items", "ab", 3);
    one = ToriRSChrome_TextInput(&g_ui, panel, "cmd", "");
    ToriRSChrome_Build(&g_ui);
    line_h = ToriRSChrome_FontLineBox(g_ui.theme.font_row, g_ui.scale);

    TEST_ASSERT(
        g_ui.widgets[area].h > g_ui.widgets[one].h,
        "a multiline row is taller than a one-line one");
    TEST_ASSERT(
        g_ui.widgets[area].h == TORIRS_CHROME_M_ROW_H + 3 * line_h +
                                    2 * TORIRS_CHROME_M_TEXTAREA_PAD_Y + 2,
        "and exactly its caption band plus its own three lines");

    /* A click inside the box focuses it and puts the caret WHERE IT LANDED --
     * the one thing a one-line field does differently, because a list long
     * enough to need this control is one the user is reaching into. */
    ToriRSChrome_MouseDown(
        &g_ui, g_ui.widgets[area].x + 2, g_ui.widgets[area].y + TORIRS_CHROME_M_ROW_H + 4);
    TEST_ASSERT(g_ui.focus == area, "click focuses the box");
    TEST_ASSERT(g_ui.widgets[area].caret == 0, "a click at the left edge lands at column 0");

    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_END);
    TEST_ASSERT(g_ui.widgets[area].caret == 2, "end goes to the end of the LINE");

    /* Enter INSERTS. A one-line field commits on it, and a multiline field that
     * did the same would have no way to type a second line at all. */
    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_ENTER);
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, area), "ab\n") == 0, "enter inserts a newline");
    TEST_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == -1, "and does not commit");

    ToriRSChrome_KeyChar(&g_ui, 'c');
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, area), "ab\nc") == 0, "typing on the new line");

    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_HOME);
    TEST_ASSERT(g_ui.widgets[area].caret == 3, "home goes to the start of the LINE");

    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_UP);
    TEST_ASSERT(g_ui.widgets[area].caret == 0, "up moves a line, keeping the column");
    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_UP);
    TEST_ASSERT(g_ui.widgets[area].caret == 0, "up from the first line is a no-op");
    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_DOWN);
    TEST_ASSERT(g_ui.widgets[area].caret == 3, "down moves back");

    /* A column past the end of the target line clamps to it rather than
     * landing in the middle of the line after. */
    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_END);
    ToriRSChrome_SetText(&g_ui, area, "abcdef\ng");
    g_ui.widgets[area].caret = 6;
    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_DOWN);
    TEST_ASSERT(g_ui.widgets[area].caret == 8, "a short line clamps the column");

    /* The box follows the caret: more lines than it shows scrolls it, and a
     * value that shrinks scrolls it back rather than leaving it past the end. */
    ToriRSChrome_SetText(&g_ui, area, "1\n2\n3\n4\n5");
    g_ui.widgets[area].caret = (int)strlen(ToriRSChrome_Text(&g_ui, area));
    ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_END);
    TEST_ASSERT(g_ui.widgets[area].scroll == 2, "the last line of five is in a three-line box");
    ToriRSChrome_SetText(&g_ui, area, "1");
    TEST_ASSERT(g_ui.widgets[area].scroll == 0, "a shorter value scrolls back into view");

    /* Up and down mean nothing to a one-line field, and must not be swallowed
     * there -- the host has other uses for an arrow key. */
    ToriRSChrome_MouseDown(&g_ui, g_ui.widgets[one].x + 2, g_ui.widgets[one].y + 2);
    TEST_ASSERT(g_ui.focus == one, "focus the one-line field");
    TEST_ASSERT(
        !ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_UP),
        "a one-line field does not consume up");
}

/* Layout: rows stack in insertion order, the panel sizes to its widest row, and
 * text baselines sit where ToriDraw2D_DrawString's `y -= line_height` puts them. */
static void
test_debug_overlay_layout(void)
{
    int panel;
    int a;
    int b;
    struct ToriRSChromeRect rect;
    struct ToriRSChromePrim const* prims;
    int count = 0;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 0, NULL);
    a = ToriRSChrome_Label(&g_ui, panel, "short");
    ToriRSChrome_Separator(&g_ui, panel);
    b = ToriRSChrome_Label(&g_ui, panel, "a considerably longer row");
    ToriRSChrome_Build(&g_ui);

    rect = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(g_ui.widgets[a].y < g_ui.widgets[b].y, "rows stack downward in order");
    TEST_ASSERT(
        rect.w >= ToriRSChrome_MeasureText(g_ui.theme.font_row, 1, "a considerably longer row"),
        "panel fits its widest row");
    TEST_ASSERT(
        g_ui.widgets[b].y + g_ui.widgets[b].h <= rect.y + rect.h, "last row fits inside the panel");
    /* No title given, so no title bar and no menu-face text. Asserted against
     * the MENU face rather than for the row face: which face rows use is the
     * theme's choice (ToriRSChromeTheme::font_row), and the claim here is only that
     * the bold title face went unused. */
    prims = ToriRSChrome_Prims(&g_ui, &count);
    for( int i = 0; i < count; i++ )
        TEST_ASSERT(
            prims[i].kind != TORIRS_CHROME_PRIM_TEXT ||
                prims[i].font_slot != TORIRS_CHROME_FONT_MENU,
            "untitled window panel draws no menu-face text");

    for( int i = 0; i < count; i++ )
    {
        if( prims[i].kind != TORIRS_CHROME_PRIM_TEXT )
            continue;
        TEST_ASSERT(prims[i].baseline == 1, "overlay text is baseline-positioned");
        /* The glyph line box is [y - line_height, y - line_height + line_box).
         * Both edges have to be inside the panel or the row is misplaced. */
        TEST_ASSERT(
            prims[i].y - ToriRSChrome_FontLineHeight(prims[i].font_slot, 1) >= rect.y,
            "glyph box top inside the panel");
        TEST_ASSERT(
            prims[i].y - ToriRSChrome_FontLineHeight(prims[i].font_slot, 1) +
                    ToriRSChrome_FontLineBox(prims[i].font_slot, 1) <=
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
    for( int i = 0; i < TORIRS_CHROME_MAX_PANELS; i++ )
        TEST_ASSERT(ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 20, "p") >= 0,
                    "panels up to the cap");
    TEST_ASSERT(
        ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 20, "p") == -1,
        "one panel past the cap fails");
    TEST_ASSERT(g_ui.overflow == 1, "panel overflow is reported");

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 20, "p");
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        TEST_ASSERT(ToriRSChrome_Label(&g_ui, panel, "x") >= 0, "widgets up to the cap");
    TEST_ASSERT(ToriRSChrome_Label(&g_ui, panel, "x") == -1, "one widget past the cap fails");

    /* Over-long strings truncate into the fixed buffers, terminator included. */
    ToriRSChrome_Init(&g_ui);
    {
        static char long_text[TORIRS_CHROME_INPUT_MAX * 3];
        int w;
        memset(long_text, 'A', sizeof(long_text) - 1);
        long_text[sizeof(long_text) - 1] = '\0';
        panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 0, long_text);
        w = ToriRSChrome_Label(&g_ui, panel, long_text);
        TEST_ASSERT((int)strlen(ToriRSChrome_Text(&g_ui, w)) == TORIRS_CHROME_INPUT_MAX - 1,
                    "label text truncates");
        TEST_ASSERT((int)strlen(g_ui.panels[panel].title) == TORIRS_CHROME_LABEL_MAX - 1,
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 50, 50, 60, "gone");
    ToriRSChrome_Label(&g_ui, panel, "x");
    ToriRSChrome_Build(&g_ui);
    ToriRSChrome_DamageClear(&g_ui);
    ToriRSChrome_Reset(&g_ui);
    {
        struct ToriRSChromeRect d;
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 4, 4, 0, "Debug");
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
            buf.cmds[i].debug_font_id[TORIRS_CHROME_FONT_SMALL] == 494 &&
                buf.cmds[i].debug_font_id[TORIRS_CHROME_FONT_MENU] == 496,
            "desc carries the slot -> scene font mapping");
    }
    TEST_ASSERT(overlay_descs == 1, "the whole display list is one desc");
    TEST_ASSERT(last_is_overlay, "the overlay desc is emitted last");
    TEST_ASSERT(buf.count > 1, "the ordinary walk still emitted");

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}

/* Hidden widgets: no space, no hit box, and the handle survives. */
static void
test_debug_overlay_hidden(void)
{
    struct ToriRSChromeRect full;
    struct ToriRSChromeRect less;
    int panel;
    int a;
    int b;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 0, "T");
    a = ToriRSChrome_Label(&g_ui, panel, "row a");
    b = ToriRSChrome_Checkbox(&g_ui, panel, "row b", 0);
    ToriRSChrome_Build(&g_ui);
    full = ToriRSChrome_PanelRect(&g_ui, panel);

    ToriRSChrome_SetHidden(&g_ui, b, 1);
    ToriRSChrome_Build(&g_ui);
    less = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(less.h < full.h, "hiding a row shrinks the panel");
    TEST_ASSERT(g_ui.widgets[b].w == 0 && g_ui.widgets[b].h == 0, "hidden row has no box");

    ToriRSChrome_SetHidden(&g_ui, b, 0);
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(ToriRSChrome_PanelRect(&g_ui, panel).h == full.h, "unhiding restores the layout");
    (void)a;
}

/* Table layout: labelled controls share one box column; off, they don't. */
static void
test_debug_overlay_table(void)
{
    int panel;
    int a;
    int b;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 0, "T");
    a = ToriRSChrome_TextInput(&g_ui, panel, "X", "one");
    b = ToriRSChrome_TextInput(&g_ui, panel, "Longer label", "two");

    /* The observable is each input's CONTENT text prim -- its x is the box
     * start plus a fixed pad, and "one"/"two" are unique strings. Matching
     * fills by input_bg colour is the trap this replaces: in the osrs theme
     * that black is also the chrome's black, so the filter caught rails and
     * header bars and compared their x instead. */
    {
        int one_x = -1;
        int two_x = -1;
        int count;
        struct ToriRSChromePrim const* prims;

        ToriRSChrome_Build(&g_ui);
        prims = ToriRSChrome_Prims(&g_ui, &count);
        for( int i = 0; i < count; i++ )
        {
            if( prims[i].kind != TORIRS_CHROME_PRIM_TEXT || !prims[i].text )
                continue;
            if( strcmp(prims[i].text, "one") == 0 )
                one_x = prims[i].x;
            if( strcmp(prims[i].text, "two") == 0 )
                two_x = prims[i].x;
        }
        TEST_ASSERT(one_x >= 0 && two_x >= 0, "both input contents drew");
        /*
         * ONE column, always -- "X" and "Longer label" put their boxes at the
         * same x.
         *
         * This used to be opt-in (PanelSetTable) and off by default, so a box
         * started right after its own label and a panel of rows read as a
         * ragged pile. Worse, the column moved whenever a label's text
         * changed, which slid every field in the panel sideways under the
         * cursor. It is now the only behaviour, and it is what the CS2
         * executor lays out too -- see TORIRS_CHROME_M_LABEL_W.
         */
        TEST_ASSERT(one_x == two_x, "every labelled box shares one column");
    }

    /*
     * An UNLABELLED row gets no column: there is nothing to line it up with,
     * and holding 104 pixels open for a caption that is never drawn is how a
     * lone control in a narrow panel ends up half the panel's width.
     */
    {
        int bare;
        int bare_x = -1;
        int labelled_x = -1;
        int count;
        struct ToriRSChromePrim const* prims;

        bare = ToriRSChrome_TextInput(&g_ui, panel, "", "three");
        ToriRSChrome_Build(&g_ui);
        prims = ToriRSChrome_Prims(&g_ui, &count);
        for( int i = 0; i < count; i++ )
        {
            if( prims[i].kind != TORIRS_CHROME_PRIM_TEXT || !prims[i].text )
                continue;
            if( strcmp(prims[i].text, "three") == 0 )
                bare_x = prims[i].x;
            if( strcmp(prims[i].text, "one") == 0 )
                labelled_x = prims[i].x;
        }
        TEST_ASSERT(bare_x >= 0 && labelled_x >= 0, "both contents drew");
        TEST_ASSERT(bare_x < labelled_x, "an unlabelled row reserves no label column");
        (void)bare;
    }
    (void)a;
    (void)b;
}

/*
 * Removal, and the free list under it.
 *
 * The property that matters is not "the slot comes back" but that nothing keeps
 * pointing at a widget that is gone: a stale `focus` or `hover` is a click
 * delivered to whatever gets recycled into the hole.
 */
static void
test_debug_overlay_remove(void)
{
    int panel;
    int a;
    int b;
    int c;
    int reused;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 0, "P");
    a = ToriRSChrome_Label(&g_ui, panel, "a");
    b = ToriRSChrome_Checkbox(&g_ui, panel, "b", 0);
    c = ToriRSChrome_Label(&g_ui, panel, "c");
    ToriRSChrome_Build(&g_ui);

    /* Latch the middle row, then remove it out from under the latches. */
    g_ui.focus = b;
    g_ui.hover = b;
    g_ui.press = b;
    ToriRSChrome_WidgetRemove(&g_ui, b);
    TEST_ASSERT(g_ui.focus == -1, "removal clears focus");
    TEST_ASSERT(g_ui.hover == -1, "removal clears hover");
    TEST_ASSERT(g_ui.press == -1, "removal clears press");
    TEST_ASSERT(g_ui.widgets[b].kind == TORIRS_CHROME_W_FREE, "removed slot is marked free");

    /* The list closed over the hole, in order, and skipped the dead slot. */
    {
        int seen = 0;
        for( int w = g_ui.panels[panel].first_widget; w >= 0; w = g_ui.widgets[w].next )
        {
            TEST_ASSERT(w != b, "removed widget is off the panel's row list");
            seen++;
        }
        TEST_ASSERT(seen == 2, "two rows survive the removal");
        TEST_ASSERT(g_ui.panels[panel].last_widget == c, "tail still points at the last row");
    }

    /* The next add lands in the vacated slot rather than growing the array. */
    reused = ToriRSChrome_Label(&g_ui, panel, "d");
    TEST_ASSERT(reused == b, "the free slot is recycled");
    TEST_ASSERT(g_ui.free_widget == -1, "free list empties as it is drained");

    /* Removing the tail moves the tail; removing the head moves the head. */
    ToriRSChrome_WidgetRemove(&g_ui, reused);
    TEST_ASSERT(g_ui.panels[panel].last_widget == c, "tail follows a tail removal");
    ToriRSChrome_WidgetRemove(&g_ui, a);
    TEST_ASSERT(g_ui.panels[panel].first_widget == c, "head follows a head removal");

    /* Clearing a panel frees the rest and leaves the panel itself standing. */
    ToriRSChrome_PanelClearWidgets(&g_ui, panel);
    TEST_ASSERT(g_ui.panels[panel].first_widget == -1, "cleared panel has no rows");
    TEST_ASSERT(g_ui.panels[panel].last_widget == -1, "cleared panel has no tail");
    TEST_ASSERT(g_ui.panel_count == 1, "clearing rows does not remove the panel");
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(g_ui.panels[panel].last_rect.w > 0, "an emptied panel still draws");

    /* And a rebuild after a clear stays inside the array: this is the property
     * the append-only version could not offer, and the reason a plugin panel
     * could only ever grow. */
    {
        int const before = g_ui.widget_count;
        for( int i = 0; i < 8; i++ )
            ToriRSChrome_Label(&g_ui, panel, "row");
        ToriRSChrome_PanelClearWidgets(&g_ui, panel);
        for( int i = 0; i < 8; i++ )
            ToriRSChrome_Label(&g_ui, panel, "row");
        TEST_ASSERT(g_ui.widget_count <= before + 8, "rebuilds recycle instead of growing");
    }
}

/* Tabs: which rows lay out, and the strip surviving its own switch. */
static void
test_debug_overlay_tabs(void)
{
    static char const* const titles[] = { "One", "Two", "Three" };
    int panel;
    int strip;
    int on_one;
    int on_two;
    int everywhere;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 200, "Tabbed");
    strip = ToriRSChrome_Tabs(&g_ui, panel, titles, 3, 0);
    TEST_ASSERT(strip >= 0, "tab strip is created");
    TEST_ASSERT(g_ui.widgets[strip].tab == -1, "the strip belongs to no single tab");

    everywhere = ToriRSChrome_Label(&g_ui, panel, "always");
    ToriRSChrome_PanelBeginTab(&g_ui, panel, 0);
    on_one = ToriRSChrome_Label(&g_ui, panel, "first");
    ToriRSChrome_PanelBeginTab(&g_ui, panel, 1);
    on_two = ToriRSChrome_Label(&g_ui, panel, "second");
    ToriRSChrome_PanelBeginTab(&g_ui, panel, -1);

    TEST_ASSERT(g_ui.widgets[on_one].tab == 0, "BeginTab stamps the rows after it");
    TEST_ASSERT(g_ui.widgets[on_two].tab == 1, "and the stamp follows the last BeginTab");
    TEST_ASSERT(g_ui.widgets[everywhere].tab == -1, "rows added before any BeginTab are global");

    /* Tab 0 showing: its row is laid out, the other tab's is not. */
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(g_ui.widgets[on_one].h > 0, "the active tab's row lays out");
    TEST_ASSERT(g_ui.widgets[on_two].h == 0, "an inactive tab's row does not");
    TEST_ASSERT(g_ui.widgets[everywhere].h > 0, "a global row lays out on every tab");
    TEST_ASSERT(g_ui.widgets[strip].h > 0, "the strip lays out");

    ToriRSChrome_PanelSetActiveTab(&g_ui, panel, 1);
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(g_ui.widgets[on_one].h == 0, "switching hides the old tab's row");
    TEST_ASSERT(g_ui.widgets[on_two].h > 0, "switching shows the new tab's row");
    TEST_ASSERT(g_ui.widgets[everywhere].h > 0, "a global row survives the switch");
    TEST_ASSERT(g_ui.widgets[strip].h > 0, "the strip survives the switch");
    TEST_ASSERT(
        ToriRSChrome_PanelActiveTab(&g_ui, panel) == 1, "the panel reports its active tab");
    TEST_ASSERT(g_ui.widgets[strip].selected == 1, "the strip's own selection follows");

    /* A hidden row on the active tab stays hidden: the two flags are AND-ed,
     * which is the reason `tab` is not folded into `hidden`. */
    ToriRSChrome_SetHidden(&g_ui, on_two, 1);
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(g_ui.widgets[on_two].h == 0, "hidden beats active-tab");
    ToriRSChrome_SetHidden(&g_ui, on_two, 0);
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(g_ui.widgets[on_two].h > 0, "unhiding on the active tab brings it back");

    /* Clicking a tab switches it, through the real input path. */
    {
        struct ToriRSChromeWidget const* s = &g_ui.widgets[strip];
        int const x = s->x + 2;
        int const y = s->y + s->h / 2;
        ToriRSChrome_MouseMove(&g_ui, x, y);
        ToriRSChrome_MouseDown(&g_ui, x, y);
        ToriRSChrome_MouseUp(&g_ui, x, y);
        TEST_ASSERT(ToriRSChrome_PanelActiveTab(&g_ui, panel) == 0, "clicking tab 0 selects it");
        TEST_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == strip, "the switch is reported");
    }
}

/* Buttons: a box that presses, and a hit area that is the box and not the row. */
static void
test_debug_overlay_button(void)
{
    int panel;
    int button;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 240, "P");
    button = ToriRSChrome_Button(&g_ui, panel, "Save");
    ToriRSChrome_Build(&g_ui);

    TEST_ASSERT(g_ui.widgets[button].h > 0, "a button lays out");
    TEST_ASSERT(
        g_ui.widgets[button].w < 240 - 2, "a button is sized to its caption, not to the row");

    /* Press and release inside activates. */
    {
        int const x = g_ui.widgets[button].x + g_ui.widgets[button].w / 2;
        int const y = g_ui.widgets[button].y + g_ui.widgets[button].h / 2;
        ToriRSChrome_MouseMove(&g_ui, x, y);
        ToriRSChrome_MouseDown(&g_ui, x, y);
        ToriRSChrome_MouseUp(&g_ui, x, y);
        TEST_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == button, "a click activates the button");
    }

    /* The strip to the right of the caption is not the button. */
    {
        int const x = g_ui.widgets[button].x + g_ui.widgets[button].w + 8;
        int const y = g_ui.widgets[button].y + g_ui.widgets[button].h / 2;
        TEST_ASSERT(ToriRSChrome_HitTest(&g_ui, x, y) != button, "past the box is not the button");
    }
}

/*
 * Panel scrolling: rows below the fold become reachable rather than dropped,
 * and a row scrolled out of view stops taking clicks.
 */
static void
test_debug_overlay_panel_scroll(void)
{
    int panel;
    int rows[24];
    int line;

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 160, "Scroller");
    for( int i = 0; i < 24; i++ )
        rows[i] = ToriRSChrome_Checkbox(&g_ui, panel, "row", 0);
    line = ToriRSChrome_FontLineBox(g_ui.theme.font_row, g_ui.scale);

    /* Hand-set short, and NOT scrollable yet: the old behaviour, rows dropped. */
    ToriRSChrome_PanelSetFixedWidth(&g_ui, panel, 160);
    g_ui.panels[panel].fixed_h = 6 * line;
    g_ui.panels[panel].dirty = 1;
    g_ui.dirty = 1;
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(g_ui.widgets[rows[23]].h == 0, "unscrollable: the last row is dropped");

    /* Scrollable: the same panel can now reach it. */
    ToriRSChrome_PanelSetScrollable(&g_ui, panel, 1);
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(g_ui.panels[panel].content_h > g_ui.panels[panel].view_h, "content overflows");
    TEST_ASSERT(g_ui.widgets[rows[0]].h > 0, "the first row is in view at rest");
    TEST_ASSERT(g_ui.widgets[rows[23]].h == 0, "the last row is still below the fold at rest");

    /* Wheel down brings later rows into view and takes the first out of it. */
    {
        int const x = g_ui.panels[panel].last_rect.x + 4;
        int const y = g_ui.panels[panel].last_rect.y + g_ui.panels[panel].last_rect.h / 2;
        for( int i = 0; i < 40; i++ )
            ToriRSChrome_MouseWheel(&g_ui, x, y, -1);
        ToriRSChrome_Build(&g_ui);
        TEST_ASSERT(g_ui.panels[panel].scroll_y > 0, "the wheel scrolled the panel");
        TEST_ASSERT(g_ui.widgets[rows[23]].h > 0, "the last row scrolls into view");
        TEST_ASSERT(g_ui.widgets[rows[0]].h == 0, "the first row scrolls out of the hit test");

        /* Clamped at the end: scrolling forever does not run off. */
        TEST_ASSERT(
            g_ui.panels[panel].scroll_y == g_ui.panels[panel].content_h - g_ui.panels[panel].view_h,
            "scroll clamps to the end of the content");
    }

    /* Growing the panel past its content retires the scroll entirely. */
    g_ui.panels[panel].fixed_h = 0;
    g_ui.panels[panel].dirty = 1;
    g_ui.dirty = 1;
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(g_ui.panels[panel].scroll_y == 0, "a panel that fits its content is not scrolled");
    TEST_ASSERT(g_ui.widgets[rows[0]].h > 0, "and every row is back");
}

void
test_debug_overlay(void)
{
    printf("TEST: debug overlay (measure / menu geometry / retained / damage / widgets)\n");

    test_debug_overlay_measure();
    test_debug_overlay_scaled_metrics();
    test_debug_overlay_scaled_layout();
    test_debug_overlay_menu_geometry();
    test_debug_overlay_retained();
    test_debug_overlay_damage();
    test_debug_overlay_border();
    test_debug_overlay_checkbox();
    test_debug_overlay_textinput();
    test_debug_overlay_textarea();
    test_debug_overlay_layout();
    test_debug_overlay_capacity();
    test_debug_overlay_emit_pass();
    test_debug_overlay_hidden();
    test_debug_overlay_table();
    test_debug_overlay_remove();
    test_debug_overlay_tabs();
    test_debug_overlay_button();
    test_debug_overlay_panel_scroll();
}

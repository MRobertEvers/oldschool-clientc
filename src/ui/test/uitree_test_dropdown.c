/**
 * ToriRSChrome dropdown behaviour, driven through the real input entry points.
 *
 * Rendering a dropdown is the easy half. The half that goes wrong is the
 * interaction: a list that stays open after choosing, a click that lands on the
 * panel underneath the open list, a wheel that scrolls the list AND zooms the
 * camera behind it, a palette swap that leaves stale rows on screen. Each of
 * those is a case here.
 */

#include "ui/uitree_debug_overlay.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    char const* what)
{
    g_checks++;
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static char const* const options[] = { "alpha", "bravo", "charlie", "delta", "echo",
                                       "foxtrot", "golf", "hotel", "india", "juliet",
                                       "kilo", "lima" };
#define OPTION_COUNT ((int)(sizeof(options) / sizeof(options[0])))

/** Centre of a widget's resolved row, which only exists after a Build. */
static void
widget_centre(
    struct ToriRSChrome const* ui,
    int widget,
    int* out_x,
    int* out_y)
{
    *out_x = ui->widgets[widget].x + ui->widgets[widget].w - 6;
    *out_y = ui->widgets[widget].y + ui->widgets[widget].h / 2;
}

/** Click a point: press and release, as a real click arrives. */
static void
click(
    struct ToriRSChrome* ui,
    int x,
    int y)
{
    ToriRSChrome_MouseMove(ui, x, y);
    ToriRSChrome_MouseDown(ui, x, y);
    ToriRSChrome_MouseUp(ui, x, y);
}

int
main(void)
{
    static struct ToriRSChrome ui; /* ~5 MB: never on the stack */
    int panel;
    int dd;
    int box_x;
    int box_y;

    ToriRSChrome_Init(&ui);
    panel = ToriRSChrome_PanelAdd(&ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 0, "Test");
    dd = ToriRSChrome_Dropdown(&ui, panel, "Pick", options, OPTION_COUNT, 0);
    check(dd >= 0, "dropdown was created");
    ToriRSChrome_Build(&ui);

    check(ui.dropdown_open == -1, "list starts closed");
    check(ToriRSChrome_DropdownSelected(&ui, dd) == 0, "starts on the first option");

    /* Open it. */
    widget_centre(&ui, dd, &box_x, &box_y);
    click(&ui, box_x, box_y);
    check(ui.dropdown_open == dd, "clicking the row opens the list");
    ToriRSChrome_Build(&ui);

    /* The open list must actually be in the display list, or it is open only in
     * the model and invisible on screen. */
    {
        int count = 0;
        int found = 0;
        struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&ui, &count);
        for( int i = 0; i < count; i++ )
            if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text &&
                strcmp(prims[i].text, "charlie") == 0 )
                found = 1;
        check(found, "an unselected option is drawn while the list is open");
    }

    /* Choose the third row. */
    {
        /* The centre of row 2, by the hit test's own formula: rows start at
         * rect.y (= widget bottom) + the 2px list pad, and the pitch is the
         * theme row face's line box + 4px of leading, everything scaled.
         * Derived rather than hardcoded because this math has now drifted
         * twice -- first when the default theme grew p12 rows, then when the
         * list gained its pad and leading -- and each drift made this click
         * land on the wrong row while looking reasonable. */
        int const pitch = ToriRSChrome_FontLineBox(ui.theme.font_row, ui.scale) + 4 * ui.scale;
        int const row_y =
            ui.widgets[dd].y + ui.widgets[dd].h + 2 * ui.scale + 2 * pitch + pitch / 2;
        /* Left of the scrollbar column the list grew on its right edge -- a
         * click there is a grip press, not a row choice, by design. */
        click(&ui, box_x - 20, row_y);
        check(ui.dropdown_open == -1, "choosing closes the list");
        check(ToriRSChrome_DropdownSelected(&ui, dd) == 2, "the chosen row became the selection");
        check(ToriRSChrome_TakeActivated(&ui) == dd, "the choice is reported as an activation");
    }

    /* Reopen, then click far away: that dismisses without selecting. */
    ToriRSChrome_Build(&ui);
    click(&ui, box_x, box_y);
    check(ui.dropdown_open == dd, "reopened");
    ToriRSChrome_Build(&ui);
    click(&ui, 700, 700);
    check(ui.dropdown_open == -1, "clicking away closes the list");
    check(
        ToriRSChrome_DropdownSelected(&ui, dd) == 2, "clicking away did not change the selection");

    /* The click that dismisses is SPENT on dismissing: off every panel it is
     * still the chrome's, press and release, so the view behind an open list
     * is not clicked by the click that closes it -- and it stays the chrome's
     * until the pointer is next heard from, which is after the host's own
     * gates for the release frame have run. */
    ToriRSChrome_Build(&ui);
    click(&ui, box_x, box_y);
    check(ui.dropdown_open == dd, "reopened for the dismissing click");
    ToriRSChrome_Build(&ui);
    ToriRSChrome_MouseMove(&ui, 700, 700);
    check(!ToriRSChrome_WantsPointer(&ui, 700, 700), "open ground is not the chrome's before the press");
    check(ToriRSChrome_MouseDown(&ui, 700, 700) == 1, "the press that dismisses is consumed");
    check(ui.dropdown_open == -1, "and it closed the list");
    check(ToriRSChrome_WantsPointer(&ui, 700, 700), "the chrome owns the pointer while it is held");
    check(ToriRSChrome_MouseMove(&ui, 710, 705) == 1, "a drag of it is consumed too");
    check(ToriRSChrome_MouseUp(&ui, 710, 705) == 1, "its release is consumed");
    check(ToriRSChrome_WantsPointer(&ui, 710, 705), "and still owned on the frame of the release");
    ToriRSChrome_MouseMove(&ui, 710, 705);
    check(!ToriRSChrome_WantsPointer(&ui, 710, 705), "the next pointer event gives the ground back");
    check(ToriRSChrome_MouseDown(&ui, 700, 700) == 0, "the next press off the panel is the view's again");
    ToriRSChrome_MouseUp(&ui, 700, 700);

    /* A dismissing press on ANOTHER widget of the panel reaches nothing: a
     * second dropdown under the pointer does not open on the click that
     * closed the first. */
    {
        static char const* const two[] = { "one", "two" };
        int const other = ToriRSChrome_Dropdown(&ui, panel, "Other", two, 2, 0);
        int ox;
        int oy;

        ToriRSChrome_Build(&ui);
        widget_centre(&ui, dd, &box_x, &box_y);
        click(&ui, box_x, box_y);
        check(ui.dropdown_open == dd, "reopened beside a second dropdown");
        ToriRSChrome_Build(&ui);
        /* Left of the open list (it hangs under the first box), on the second
         * row's caption: a point of the second widget the list does not cover. */
        ox = ui.widgets[other].x + 2;
        oy = ui.widgets[other].y + ui.widgets[other].h / 2;
        check(ToriRSChrome_HitTest(&ui, ox, oy) == other, "the point is on the second dropdown");
        click(&ui, ox, oy);
        check(ui.dropdown_open == -1, "the click closed the open list");
        check(ToriRSChrome_TakeActivated(&ui) == -1, "and activated nothing");
        ToriRSChrome_Build(&ui);
        click(&ui, ox, oy);
        check(ui.dropdown_open == other, "the click after it opens the second dropdown");
        /* Escape closes the open list and is consumed, with nothing focused. */
        check(ui.focus == -1, "nothing holds the keyboard");
        check(ToriRSChrome_KeyEdit(&ui, TORIRS_CHROME_KEY_ESCAPE) == 1, "Escape with a list open is consumed");
        check(ui.dropdown_open == -1, "and closes the list");
        check(ToriRSChrome_KeyEdit(&ui, TORIRS_CHROME_KEY_ESCAPE) == 0, "a second Escape is not the chrome's");
        ToriRSChrome_WidgetRemove(&ui, other);
        ToriRSChrome_Build(&ui);
        widget_centre(&ui, dd, &box_x, &box_y);
    }

    /* A menu bar is the one place a dismissing press goes on to act: with
     * File open, a press on Edit closes File and opens Edit, one click. A
     * click on the open menu's OWN title closes it and leaves it closed. */
    {
        static char const* const file_rows[] = { "New", "Open" };
        static char const* const edit_rows[] = { "Undo", "Redo" };
        int const bar = ToriRSChrome_PanelAdd(&ui, TORIRS_CHROME_PANEL_MENUBAR, 0, 300, 400, "");
        int const file = ToriRSChrome_MenuDrop(&ui, bar, "File", file_rows, 2);
        int const edit = ToriRSChrome_MenuDrop(&ui, bar, "Edit", edit_rows, 2);
        int fx;
        int fy;
        int ex;
        int ey;

        ToriRSChrome_Build(&ui);
        fx = ui.widgets[file].x + ui.widgets[file].w / 2;
        fy = ui.widgets[file].y + ui.widgets[file].h / 2;
        ex = ui.widgets[edit].x + ui.widgets[edit].w / 2;
        ey = ui.widgets[edit].y + ui.widgets[edit].h / 2;
        click(&ui, fx, fy);
        check(ui.dropdown_open == file, "the File title opens its menu");
        ToriRSChrome_Build(&ui);
        click(&ui, ex, ey);
        check(ui.dropdown_open == edit, "a press on another menu title switches menus");
        ToriRSChrome_Build(&ui);
        click(&ui, ex, ey);
        check(ui.dropdown_open == -1, "a click on the open menu's own title closes it");
        check(ToriRSChrome_TakeActivated(&ui) == -1, "and no menu row ran");
        /* Escape closes a menu the same way it closes a list. */
        ToriRSChrome_Build(&ui);
        click(&ui, fx, fy);
        check(ui.dropdown_open == file, "File open again");
        check(ToriRSChrome_KeyEdit(&ui, TORIRS_CHROME_KEY_ESCAPE) == 1, "Escape with a menu open is consumed");
        check(ui.dropdown_open == -1, "and closes the menu");
        ToriRSChrome_PanelSetVisible(&ui, bar, 0);
        ToriRSChrome_Build(&ui);
    }

    /* The wheel scrolls the open list and is consumed, so the camera behind it
     * does not also zoom. */
    ToriRSChrome_Build(&ui);
    click(&ui, box_x, box_y);
    ToriRSChrome_Build(&ui);
    {
        int const list_y = ui.widgets[dd].y + ui.widgets[dd].h + 4;
        int const before = ui.widgets[dd].scroll;
        /* One row at a time: 12 options over a 10-row window leaves only two
         * scroll positions, so a larger delta would land on the clamp instead
         * of proving the wheel moved anything. */
        /* Wheel down (negative) shows later options, as a panel does. */
        check(
            ToriRSChrome_MouseWheel(&ui, box_x, list_y, -1) == 1, "wheel over the list is consumed");
        check(ui.widgets[dd].scroll == before + 1, "wheel down scrolled the list on");
        /* Past the end it must clamp, and still be consumed. */
        check(
            ToriRSChrome_MouseWheel(&ui, box_x, list_y, -999) == 1,
            "wheel past the end is still consumed");
        check(
            ui.widgets[dd].scroll == OPTION_COUNT - TORIRS_CHROME_DROPDOWN_ROWS,
            "scroll clamps to the last full page");
        check(
            ToriRSChrome_MouseWheel(&ui, 700, 700, 3) == 0,
            "wheel away from the list is not consumed");
    }

    /* Swapping the option list must close an open one: the rows under the
     * cursor just became different strings. */
    {
        static char const* const other[] = { "one", "two" };
        ToriRSChrome_DropdownSetOptions(&ui, dd, other, 2, 1);
        check(ui.dropdown_open == -1, "changing the options closes the open list");
        check(ToriRSChrome_DropdownSelected(&ui, dd) == 1, "new selection took");

        /* A shorter list must not leave the selection out of range. */
        ToriRSChrome_DropdownSetOptions(&ui, dd, other, 1, 5);
        check(ToriRSChrome_DropdownSelected(&ui, dd) == 0, "selection clamps to the shorter list");
    }

    /* A dropdown with no options must not open an empty box. */
    {
        int const empty = ToriRSChrome_Dropdown(&ui, panel, "Empty", NULL, 0, -1);
        ToriRSChrome_Build(&ui);
        widget_centre(&ui, empty, &box_x, &box_y);
        click(&ui, box_x, box_y);
        check(ui.dropdown_open == -1, "an empty dropdown does not open");
    }

    /* ---- the wheel on a CLOSED dropdown, and over bare panel ------------- */

    /* The cases above left a one-row list, which cannot step anywhere; give
     * the wheel something to step through. */
    ToriRSChrome_DropdownSetOptions(&ui, dd, options, OPTION_COUNT, 1);

    /* A CLOSED dropdown ignores the wheel (it stepped its selection until
     * 2026-11-09): the pointer rests wherever it was when a page was scrolled,
     * and a control that changes value under a resting pointer changes it for
     * someone who was scrolling. Still consumed -- it is over a panel -- so
     * the camera behind stays still; nothing is selected and nothing is
     * reported. */
    ToriRSChrome_Build(&ui);
    check(ui.dropdown_open == -1, "list closed before the closed-wheel cases");
    {
        int const before = ToriRSChrome_DropdownSelected(&ui, dd);
        widget_centre(&ui, dd, &box_x, &box_y);
        check(ToriRSChrome_MouseWheel(&ui, box_x, box_y, -1), "wheel over the closed row consumed");
        check(
            ToriRSChrome_DropdownSelected(&ui, dd) == before,
            "wheel down over a closed dropdown leaves its value alone");
        check(ToriRSChrome_TakeActivated(&ui) == -1, "and reports nothing");
        check(ToriRSChrome_MouseWheel(&ui, box_x, box_y, 1), "wheel up consumed");
        check(
            ToriRSChrome_DropdownSelected(&ui, dd) == before, "wheel up leaves it alone too");
        check(ui.dropdown_open == -1, "and the wheel never opens the list");
    }

    /* In a panel that scrolls, the wheel over the closed dropdown scrolls the
     * panel: three rows a notch. */
    {
        int const scroller =
            ToriRSChrome_PanelAdd(&ui, TORIRS_CHROME_PANEL_WINDOW, 300, 10, 200, "Scrolls");
        int rows[16];
        int const before_rows = 4;
        int sd;
        int sx;
        int sy;
        int pitch;

        for( int i = 0; i < before_rows; i++ )
            rows[i] = ToriRSChrome_Checkbox(&ui, scroller, "row", 0);
        sd = ToriRSChrome_Dropdown(&ui, scroller, "Sort", options, OPTION_COUNT, 3);
        for( int i = before_rows; i < 16; i++ )
            rows[i] = ToriRSChrome_Checkbox(&ui, scroller, "row", 0);
        ToriRSChrome_PanelSetScrollable(&ui, scroller, 1);
        ToriRSChrome_PanelSetFixedHeight(&ui, scroller, 160);
        ToriRSChrome_Build(&ui);
        check(
            ui.panels[scroller].content_h > ui.panels[scroller].view_h,
            "the second panel has rows past its view");
        /* One row's pitch: two neighbouring rows' tops. */
        pitch = ui.widgets[rows[1]].y - ui.widgets[rows[0]].y;
        check(pitch > 0, "rows are stacked");
        widget_centre(&ui, sd, &sx, &sy);
        check(ToriRSChrome_HitTest(&ui, sx, sy) == sd, "the pointer rests on the closed dropdown");
        check(ToriRSChrome_MouseWheel(&ui, sx, sy, -1), "wheel over it is consumed");
        check(ToriRSChrome_DropdownSelected(&ui, sd) == 3, "its value did not move");
        check(ToriRSChrome_TakeActivated(&ui) == -1, "nothing was activated");
        check(
            ToriRSChrome_PanelScroll(&ui, scroller) == 3 * pitch,
            "one notch scrolled the panel three rows");
        check(ToriRSChrome_MouseWheel(&ui, sx, sy, 1), "wheel up consumed");
        check(ToriRSChrome_PanelScroll(&ui, scroller) == 0, "and one notch back is the top again");
        /* The precise path moves the pixels it is given, and the integer
         * wheel that came with them is not applied on top. */
        {
            struct ToriRSChromeWheelFrame f;
            memset(&f, 0, sizeof(f));
            f.x = sx;
            f.y = sy;
            f.wheel = -1;
            f.scroll_dy = -48.0f;
            f.px_per_notch = 48;
            f.trackpad = 1;
            check(ToriRSChrome_WheelFrame(&ui, &f) == 1, "a trackpad scroll over the panel is taken");
            check(
                ToriRSChrome_PanelScroll(&ui, scroller) == 48,
                "by exactly its pixels, not by a notch as well");
            /* A mouse's notch with the same 48 px beside it (what Windows
             * sends) is one notch, once. */
            ToriRSChrome_PanelSetScroll(&ui, scroller, 0);
            f.trackpad = 0;
            check(ToriRSChrome_WheelFrame(&ui, &f) == 1, "a mouse notch over the panel is taken");
            check(
                ToriRSChrome_PanelScroll(&ui, scroller) == 3 * pitch,
                "as one notch: three rows, the exact scroll beside it ignored");
        }
        ToriRSChrome_PanelSetVisible(&ui, scroller, 0);
        ToriRSChrome_Build(&ui);
    }
    widget_centre(&ui, dd, &box_x, &box_y);

    /* Over panel body (no widget): consumed into nothing. Off panel: not. */
    {
        struct ToriRSChromeRect const r = ToriRSChrome_PanelRect(&ui, panel);
        check(
            ToriRSChrome_MouseWheel(&ui, r.x + 2, r.y + 2, -1),
            "wheel over the panel body is consumed");
        check(
            !ToriRSChrome_MouseWheel(&ui, r.x + r.w + 40, r.y, -1),
            "wheel off the panel is not consumed");
        check(
            ToriRSChrome_WantsWheel(&ui, r.x + 2, r.y + 2),
            "WantsWheel claims the panel");
        check(
            !ToriRSChrome_WantsWheel(&ui, r.x + r.w + 40, r.y),
            "WantsWheel declines open ground");
    }

    /* One semantic limit is enforced before the model reaches either web
     * runtime, and a shared page budget makes every legal snapshot deliverable. */
    {
        char const* oversized[TORIRS_CHROME_OPTION_MAX + 1];
        int lists[TORIRS_CHROME_LEGACY_OPTIONS_TOTAL_MAX /
                  TORIRS_CHROME_OPTION_MAX];
        int exhausted;

        for( int i = 0; i < TORIRS_CHROME_OPTION_MAX + 1; i++ )
            oversized[i] = "entry";
        ToriRSChrome_Reset(&ui);
        panel = ToriRSChrome_PanelAdd(
            &ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 0, "Bounded");
        for( int i = 0; i < (int)(sizeof(lists) / sizeof(lists[0])); i++ )
            lists[i] = ToriRSChrome_Dropdown(
                &ui, panel, "Many", oversized,
                TORIRS_CHROME_OPTION_MAX + 1, TORIRS_CHROME_OPTION_MAX);
        check(
            ui.widgets[lists[0]].option_count == TORIRS_CHROME_OPTION_MAX &&
                ui.widgets[lists[0]].selected == TORIRS_CHROME_OPTION_MAX - 1,
            "an oversized borrowed list clamps to the shared protocol maximum");
        check(
            ui.legacy_option_count == TORIRS_CHROME_LEGACY_OPTIONS_TOTAL_MAX,
            "legacy option lists consume one explicit page-wide budget");
        exhausted = ToriRSChrome_Dropdown(
            &ui, panel, "No room", oversized,
            TORIRS_CHROME_OPTION_MAX, 0);
        check(
            exhausted >= 0 && ui.widgets[exhausted].option_count == 0,
            "a list beyond the page budget retains an explicit empty prefix");
        ToriRSChrome_WidgetRemove(&ui, lists[0]);
        ToriRSChrome_DropdownSetOptions(
            &ui, exhausted, oversized, TORIRS_CHROME_OPTION_MAX + 1, 0);
        check(
            ui.widgets[exhausted].option_count == TORIRS_CHROME_OPTION_MAX &&
                ui.legacy_option_count == TORIRS_CHROME_LEGACY_OPTIONS_TOTAL_MAX,
            "removal returns its entries and replacement atomically reuses them");
    }

    printf("uitree_test_dropdown: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

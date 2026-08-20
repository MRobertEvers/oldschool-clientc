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
    struct ToriRSChrome ui;
    int panel;
    int dd;
    int box_x;
    int box_y;

    ToriRSChrome_Init(&ui);
    panel = ToriRSChrome_PanelAdd(&ui, TORIDBG_PANEL_WINDOW, 10, 10, 0, "Test");
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
        struct ToriDbgPrim const* prims = ToriRSChrome_Prims(&ui, &count);
        for( int i = 0; i < count; i++ )
            if( prims[i].kind == TORIDBG_PRIM_TEXT && prims[i].text &&
                strcmp(prims[i].text, "charlie") == 0 )
                found = 1;
        check(found, "an unselected option is drawn while the list is open");
    }

    /* Choose the third row. */
    {
        int const row_y = ui.widgets[dd].y + ui.widgets[dd].h + 1 +
                          2 * ToriRSChrome_FontLineBox(TORIDBG_FONT_SMALL) + 2;
        click(&ui, box_x, row_y);
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
    check(ToriRSChrome_DropdownSelected(&ui, dd) == 2, "clicking away did not change the selection");

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
        check(ToriRSChrome_MouseWheel(&ui, box_x, list_y, 1) == 1, "wheel over the list is consumed");
        check(ui.widgets[dd].scroll == before + 1, "wheel scrolled the list");
        /* Past the end it must clamp, and still be consumed. */
        check(
            ToriRSChrome_MouseWheel(&ui, box_x, list_y, 999) == 1,
            "wheel past the end is still consumed");
        check(
            ui.widgets[dd].scroll == OPTION_COUNT - TORIDBG_DROPDOWN_ROWS,
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

    printf("uitree_test_dropdown: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

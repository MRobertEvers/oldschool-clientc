#include "chrome_widget_sync.h"

#include <stdio.h>
#include <string.h>

/* VLIST: the chrome holds a row COUNT and the visible window; the host fills
 * the rows it is asked for. 50,000 rows cost one widget and `rows` strings. */
void
cw_test_vlist(void)
{
    struct ToriRSChromeCmd const* cmd;
    char text[32];
    int panel;
    int list;
    int first;
    int count;
    struct ToriRSChromeWidget const* w;

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Catalog");
    list = ToriRSChrome_VList(&g_cw_ui, panel, "Results", 8, 50000);
    CW_CHECK(list >= 0, "a 50,000-row list is one widget");
    CW_CHECK(ToriRSChrome_VListWindow(&g_cw_ui, list, &first, &count) && first == 0 && count == 8,
        "the window is the first eight rows");
    CW_CHECK(ToriRSChrome_VListNeedsRows(&g_cw_ui, list), "and it asks for their text");
    for( int r = first; r < first + count; r++ )
    {
        snprintf(text, sizeof(text), "loc_%d", r);
        ToriRSChrome_VListSetRow(&g_cw_ui, list, r, text);
    }
    ToriRSChrome_VListSetRow(&g_cw_ui, list, 9000, "offscreen");
    CW_CHECK(!ToriRSChrome_VListNeedsRows(&g_cw_ui, list), "filled, it asks for nothing more");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_ADD, list);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_W_VLIST && cmd->h == 8,
        "the ADD carries the visible row count");
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_VALUE, list);
    CW_CHECK(cmd && cmd->value == -1 && cmd->x == 0 && cmd->y == 50000,
        "VALUE carries selection, first row and count");
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_WIDGET_ROW) == 8, "exactly the visible rows are sent");
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_ROW, list);
    CW_CHECK(cmd && cmd->value == 0 && strcmp(cmd->text, "loc_0") == 0,
        "a row command names its absolute row");

    /* Scroll by one: seven rows keep their text, one is asked for. */
    ToriRSChrome_VListSetScroll(&g_cw_ui, list, 1);
    {
        int missing = 0;
        for( int r = 1; r < 9; r++ )
        {
            int needs;
            ToriRSChrome_VListWindow(&g_cw_ui, list, &first, &count);
            needs = 1;
            for( int i = 0; i < TORIRS_CHROME_VROW_POOL; i++ )
                if( g_cw_ui.vrows[i].owner == list && g_cw_ui.vrows[i].index == r )
                    needs = 0;
            missing += needs;
        }
        CW_CHECK(missing == 1, "a scroll by one invalidates only the row that came into view");
    }
    ToriRSChrome_VListSetRow(&g_cw_ui, list, 8, "loc_8");
    cw_run();
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_WIDGET_VALUE) == 1, "the scroll is one VALUE");

    /* Native click selects; the intent seam does the same. */
    (void)ToriRSChrome_Build(&g_cw_ui);
    w = &g_cw_ui.widgets[list];
    cw_click(w->x + 20, w->y + 18 + 1 + 2 * (ToriRSChrome_FontLineBox(g_cw_ui.theme.font_row, 1) + 2) + 3);
    CW_CHECK(ToriRSChrome_VListSelected(&g_cw_ui, list) == 3, "a click on the third visible row picks row 3");
    CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == list, "and activates the list");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_PICK, panel, list, 4242, 0, 0, NULL) == 1 &&
                 ToriRSChrome_VListSelected(&g_cw_ui, list) == 4242,
        "PICK selects any row by index, on screen or not");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_PICK, panel, list, 50000, 0, 0, NULL) == 0,
        "a PICK past the end is refused");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_SCROLL, panel, list, 100, 0, 0, NULL) == 1 &&
                 g_cw_ui.widgets[list].imin == 100,
        "SCROLL moves the window");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_SCROLL, panel, list, 999999, 0, 0, NULL) == 1 &&
                 g_cw_ui.widgets[list].imin == 50000 - 8,
        "and is clamped to the last full window");
    (void)ToriRSChrome_TakeActivated(&g_cw_ui);

    /* Wheel over the list scrolls it. */
    ToriRSChrome_VListSetScroll(&g_cw_ui, list, 10);
    (void)ToriRSChrome_Build(&g_cw_ui);
    CW_CHECK(ToriRSChrome_MouseWheel(&g_cw_ui, w->x + 20, w->y + 40, 1) == 1 &&
                 g_cw_ui.widgets[list].imin == 13,
        "the wheel scrolls three rows");

    /* Compare-then-set on rows. */
    cw_run();
    ToriRSChrome_VListSetScroll(&g_cw_ui, list, 0);
    for( int r = 0; r < 8; r++ )
    {
        snprintf(text, sizeof(text), "loc_%d", r);
        ToriRSChrome_VListSetRow(&g_cw_ui, list, r, text);
    }
    cw_run();
    for( int r = 0; r < 8; r++ )
    {
        snprintf(text, sizeof(text), "loc_%d", r);
        ToriRSChrome_VListSetRow(&g_cw_ui, list, r, text);
    }
    CW_CHECK(cw_run() == 0, "restating the same rows records nothing");

    /* Removal returns the pool slots. */
    ToriRSChrome_WidgetRemove(&g_cw_ui, list);
    {
        int owned = 0;
        for( int i = 0; i < TORIRS_CHROME_VROW_POOL; i++ )
            owned += g_cw_ui.vrows[i].owner == list;
        CW_CHECK(owned == 0, "a removed list gives its rows back to the pool");
    }
}

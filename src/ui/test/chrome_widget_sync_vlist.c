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

/* VLIST cells (workbench 6.2): the Catalog's rows -- a picture, the name
 * with its display name dimmed, the shapes as keycaps a click picks, the id
 * right-aligned -- through the canvas, the hit test and the executor seam. */
void
cw_test_vlist_cells(void)
{
    struct ToriRSChromeCmd const* cmd;
    struct ToriRSChromePrim const* prims;
    struct ToriRSChromeWidget const* w;
    int panel;
    int list;
    int plain;
    int n = 0;
    int sprites = 0;
    int caps_text = 0;
    int elided = 0;
    int too_wide = 0;
    int line_h;
    int row_y;

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 260, "Catalog");
    list = ToriRSChrome_VListCells(&g_cw_ui, panel,
        TORIRS_CHROME_CELL_SEP_STR "name" TORIRS_CHROME_CELL_SEP_STR TORIRS_CHROME_CELL_SEP_STR
        "shapes" TORIRS_CHROME_CELL_SEP_STR "id", 6, 100);
    plain = ToriRSChrome_VList(&g_cw_ui, panel, "", 2, 2);
    CW_CHECK(list >= 0 && plain >= 0, "a cells list is a VLIST");
    ToriRSChrome_VListSetRow(&g_cw_ui, list, 0,
        "777" TORIRS_CHROME_CELL_SEP_STR "fence_wood" TORIRS_CHROME_CELL_SEP_STR "Fence"
        TORIRS_CHROME_CELL_SEP_STR "0 2 9" TORIRS_CHROME_CELL_SEP_STR "980");
    ToriRSChrome_VListSetRow(&g_cw_ui, list, 1,
        TORIRS_CHROME_CELL_SEP_STR "fence_wood_dark_gate_closed_with_a_very_long_name_indeed"
        TORIRS_CHROME_CELL_SEP_STR "A display name long enough to need cutting" TORIRS_CHROME_CELL_SEP_STR
        "0 1 2 3 4 5 6 7" TORIRS_CHROME_CELL_SEP_STR "1023");
    ToriRSChrome_VListSetRow(&g_cw_ui, plain, 0, "plain row");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_ADD, list);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_W_VLIST && cmd->w == TORIRS_CHROME_VLIST_CELLS && cmd->h == 6,
        "WIDGET_ADD says the list's rows are cells");
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_ADD, plain);
    CW_CHECK(cmd && cmd->w == 0, "a plain list's add says nothing of cells");
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_ROW, list);
    CW_CHECK(cmd && cmd->value == 0 && strchr(cmd->text, TORIRS_CHROME_CELL_SEP),
        "a cells row travels as its text, fields and all");

    /* The canvas: one picture, keycaps, nothing wider than its column. */
    (void)ToriRSChrome_Build(&g_cw_ui);
    prims = ToriRSChrome_Prims(&g_cw_ui, &n);
    w = &g_cw_ui.widgets[list];
    for( int i = 0; i < n; i++ )
    {
        if( prims[i].kind == TORIRS_CHROME_PRIM_SPRITE && prims[i].sprite_scene_id == 777 )
        {
            sprites++;
            CW_CHECK(prims[i].w == 16 && prims[i].h == 16, "the picture is 16 x 16 at 1x");
        }
        if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text )
        {
            if( strcmp(prims[i].text, "2") == 0 || strcmp(prims[i].text, "9") == 0 )
                caps_text++;
            if( strstr(prims[i].text, "...") )
                elided++;
            if( prims[i].x + ToriRSChrome_MeasureTextQ(prims[i].font_slot, g_cw_ui.scale_q, prims[i].text) >
                w->x + w->w )
                too_wide++;
        }
    }
    CW_CHECK(sprites == 1, "row 0 draws its picture; row 1, with none, the empty cell");
    CW_CHECK(caps_text >= 2, "the shapes are keycaps");
    CW_CHECK(elided >= 1, "a name too long for its column ends in an ellipsis");
    CW_CHECK(too_wide == 0, "no cell's text runs past the list (R2)");

    /* A click on the second keycap of row 0 is cell 2; on the name, cell 0. */
    line_h = ToriRSChrome_FontLineBox(g_cw_ui.theme.font_row, 1) + 2;
    row_y = w->y + 18 + 1 + line_h / 2;
    {
        int key_x = -1;
        int keys_seen = 0;
        for( int i = 0; i < n; i++ )
            if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text && strcmp(prims[i].text, "2") == 0 &&
                prims[i].y < row_y + line_h && keys_seen++ == 0 )
                key_x = prims[i].x + 1;
        CW_CHECK(key_x > 0, "the keycap '2' is drawn on row 0");
        cw_click(key_x, row_y);
        CW_CHECK(ToriRSChrome_VListSelected(&g_cw_ui, list) == 0 && ToriRSChrome_VListPickedCell(&g_cw_ui, list) == 2,
            "a click on keycap '2' picks row 0 at cell 2");
        CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == list, "and activates the list");
        (void)ToriRSChrome_MouseMove(&g_cw_ui, key_x, row_y);
        {
            int cell = -1;
            CW_CHECK(ToriRSChrome_VListHoverRow(&g_cw_ui, list, &cell) == 0 && cell == 2,
                "hovering the keycap names its row and cell");
        }
    }
    cw_click(w->x + 30, row_y);
    CW_CHECK(ToriRSChrome_VListPickedCell(&g_cw_ui, list) == 0, "a click on the name is the row body, cell 0");
    (void)ToriRSChrome_TakeActivated(&g_cw_ui);

    /* The executor seam: a PICK's x is the cell. */
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_PICK, panel, list, 1, 3, 0, NULL) == 1 &&
                 ToriRSChrome_VListSelected(&g_cw_ui, list) == 1 && ToriRSChrome_VListPickedCell(&g_cw_ui, list) == 3,
        "PICK with x 3 picks row 1 at its third keycap");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_PICK, panel, list, 1, 99, 0, NULL) == 1 &&
                 ToriRSChrome_VListPickedCell(&g_cw_ui, list) == 0,
        "a cell past the keycaps a row holds is the body");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_PICK, panel, plain, 0, 2, 0, NULL) == 1 &&
                 ToriRSChrome_VListPickedCell(&g_cw_ui, plain) == 0,
        "a plain list has no cells");
    (void)ToriRSChrome_TakeActivated(&g_cw_ui);
}

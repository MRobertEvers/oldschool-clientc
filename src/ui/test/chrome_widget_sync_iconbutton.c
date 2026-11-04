#include "chrome_widget_sync.h"

#include "ui/torirs_chrome_metrics.h"

#include <string.h>

/* ICONBUTTON and the TOOLBAR panel: a sprite-faced button, pressed state on
 * CHECKED, the slot on the ADD and on VALUE; a toolbar lays them out in a
 * strip wrapped at its fixed width. */
void
cw_test_iconbutton(void)
{
    struct ToriRSChromeCmd const* cmd;
    int rail;
    int select;
    int raise;
    int del;
    int label;
    struct ToriRSChromeWidget const* a;
    struct ToriRSChromeWidget const* b;

    cw_reset();
    /* One button wide: the vertical tool rail. */
    rail = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_TOOLBAR, 0, 40, 38, "Tools");
    select = ToriRSChrome_IconButton(&g_cw_ui, rail, TORIRS_CHROME_SKIN_TOOL_SELECT, "Select");
    raise = ToriRSChrome_IconButton(&g_cw_ui, rail, TORIRS_CHROME_SKIN_TOOL_RAISE, "Raise");
    del = ToriRSChrome_IconButton(&g_cw_ui, rail, TORIRS_CHROME_SKIN_TOOL_DELETE, "Delete");
    label = ToriRSChrome_Label(&g_cw_ui, rail, "not a button");
    CW_CHECK(select >= 0 && raise >= 0 && del >= 0 && label >= 0, "a rail of tool buttons");
    ToriRSChrome_SetChecked(&g_cw_ui, select, 1);
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_PANEL_OPEN, -1);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_PANEL_TOOLBAR, "the panel opens as a TOOLBAR");
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_ADD, raise);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_W_ICONBUTTON &&
                 cmd->w == TORIRS_CHROME_SKIN_TOOL_RAISE,
        "an icon button's ADD carries the slot it wears");
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_CHECKED, select);
    CW_CHECK(cmd && cmd->value == 1, "the pressed tool is sent pressed");
    CW_CHECK(cw_prims_with_slot(TORIRS_CHROME_SKIN_TOOL_DELETE) == 1 &&
                 cw_prims_with_slot(TORIRS_CHROME_SKIN_TOOL_SELECT) == 1,
        "each button draws its baked icon");

    a = &g_cw_ui.widgets[select];
    b = &g_cw_ui.widgets[raise];
    CW_CHECK(a->w == 32 && a->h == 32 && b->x == a->x && b->y > a->y,
        "a rail one button wide stacks its buttons downwards");
    CW_CHECK(g_cw_ui.widgets[label].w == 0, "a non-button in a strip takes no space");

    cw_click(b->x + 16, b->y + 16);
    CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == raise, "a click activates the button");
    ToriRSChrome_SetDisabled(&g_cw_ui, del, 1);
    cw_click(g_cw_ui.widgets[del].x + 16, g_cw_ui.widgets[del].y + 16);
    CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == -1, "a disabled button does not fire");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_ACTIVATE, rail, del, 0, 0, 0, NULL) == 0,
        "nor does an ACTIVATE intent for it");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_ACTIVATE, rail, raise, 0, 0, 0, NULL) == 1 &&
                 ToriRSChrome_TakeActivated(&g_cw_ui) == raise,
        "ACTIVATE fires an enabled one");

    cw_run();
    ToriRSChrome_IconButtonSetIcon(&g_cw_ui, raise, TORIRS_CHROME_SKIN_TOOL_LOWER);
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_VALUE, raise);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_SKIN_TOOL_LOWER && g_cw_rec.count == 3,
        "a new icon is one VALUE");
    ToriRSChrome_IconButtonSetIcon(&g_cw_ui, raise, TORIRS_CHROME_SKIN_TOOL_LOWER);
    CW_CHECK(cw_run() == 0, "the same icon again records nothing");

    /* The chord badge (the workbench design's section 15, surface 2): the
     * setter, a Small-face badge over a black plate in the icon box's
     * bottom-right corner, and WIDGET_CHORD. */
    cw_run();
    ToriRSChrome_IconButtonSetChord(&g_cw_ui, raise, "R");
    CW_CHECK(strcmp(b->chord, "R") == 0, "the setter copies the chord");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_CHORD, raise);
    CW_CHECK(cmd && strcmp(cmd->label, "R") == 0 && g_cw_rec.count == 3,
        "a badge is one WIDGET_CHORD");
    CW_CHECK(cw_find(TORIRS_CHROME_CMD_WIDGET_CHORD, select) == NULL,
        "a button with no chord is sent none");
    {
        int n = 0;
        int found_text = 0;
        int found_plate = 0;
        int const badge_w = ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, "R") +
                            2 * TORIRS_CHROME_M_KEYCAP_PAD_X;
        struct ToriRSChromePrim const* prims;

        (void)ToriRSChrome_Build(&g_cw_ui);
        prims = ToriRSChrome_Prims(&g_cw_ui, &n);
        for( int i = 0; i < n; i++ )
        {
            if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text == b->chord &&
                prims[i].font_slot == TORIRS_CHROME_FONT_SMALL &&
                prims[i].color == g_cw_ui.theme.menu_hover_text )
                found_text = 1;
            if( prims[i].kind == TORIRS_CHROME_PRIM_RECT && prims[i].filled &&
                prims[i].color == g_cw_ui.theme.menu_chrome && prims[i].w == badge_w &&
                prims[i].h == TORIRS_CHROME_M_BADGE_H &&
                prims[i].x + prims[i].w == b->x + b->w - TORIRS_CHROME_M_BADGE_INSET &&
                prims[i].y + prims[i].h == b->y + b->h - TORIRS_CHROME_M_BADGE_INSET )
                found_plate = 1;
        }
        CW_CHECK(found_plate, "a black plate in the box's bottom-right corner");
        CW_CHECK(found_text, "the chord on it, Small face, hint yellow, borrowed");
    }
    CW_CHECK(b->w == 32 && b->h == 32, "a badge does not change the button's box");
    cw_click(b->x + b->w - 3, b->y + b->h - 3);
    CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == raise,
        "a click on the badge presses the button");
    cw_run();
    ToriRSChrome_IconButtonSetChord(&g_cw_ui, raise, "R");
    CW_CHECK(cw_run() == 0, "the same chord again records nothing");
    ToriRSChrome_IconButtonSetChord(&g_cw_ui, raise, "");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_CHORD, raise);
    CW_CHECK(cmd && cmd->label[0] == '\0', "clearing it is sent as an empty chord");

    /* No fixed width: one row, the viewer window's thin toolbar. */
    ToriRSChrome_PanelSetFixedWidth(&g_cw_ui, rail, 0);
    (void)ToriRSChrome_Build(&g_cw_ui);
    CW_CHECK(b->y == a->y && b->x > a->x, "an unsized toolbar is one row");
}

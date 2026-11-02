#include "chrome_widget_sync.h"

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

    /* No fixed width: one row, the viewer window's thin toolbar. */
    ToriRSChrome_PanelSetFixedWidth(&g_cw_ui, rail, 0);
    (void)ToriRSChrome_Build(&g_cw_ui);
    CW_CHECK(b->y == a->y && b->x > a->x, "an unsized toolbar is one row");
}

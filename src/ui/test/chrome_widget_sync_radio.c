#include "chrome_widget_sync.h"

/* RADIO: a checkbox with a group on the ADD; choosing one clears its group,
 * and only its group. */
void
cw_test_radio(void)
{
    struct ToriRSChromeCmd const* cmd;
    int panel;
    int level[4];
    int shape;
    struct ToriRSChromeWidget const* w;

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Level");
    for( int i = 0; i < 4; i++ )
    {
        char name[16];
        snprintf(name, sizeof(name), "Level %d", i);
        level[i] = ToriRSChrome_Radio(&g_cw_ui, panel, name, 1, i == 0);
    }
    shape = ToriRSChrome_Radio(&g_cw_ui, panel, "Walls", 2, 1);
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_ADD, level[2]);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_W_RADIO && cmd->w == 1,
        "a radio's ADD carries its group");
    CW_CHECK(cw_prims_with_slot(TORIRS_CHROME_SKIN_RADIO_ON) == 2 &&
                 cw_prims_with_slot(TORIRS_CHROME_SKIN_RADIO_OFF) == 3,
        "the settings page's radio art, on and off");

    w = &g_cw_ui.widgets[level[2]];
    cw_click(w->x + 8, w->y + 9);
    CW_CHECK(ToriRSChrome_Checked(&g_cw_ui, level[2]) && !ToriRSChrome_Checked(&g_cw_ui, level[0]),
        "a click chooses it and clears the group");
    CW_CHECK(ToriRSChrome_Checked(&g_cw_ui, shape), "another group is untouched");
    CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == level[2], "and it activates");
    cw_run();
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_WIDGET_CHECKED) == 2, "two CHECKED deltas: on and off");

    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_TOGGLE, panel, level[3], 1, 0, 0, NULL) == 1 &&
                 ToriRSChrome_Checked(&g_cw_ui, level[3]) &&
                 !ToriRSChrome_Checked(&g_cw_ui, level[2]),
        "TOGGLE on chooses one through the seam");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_TOGGLE, panel, level[3], 0, 0, 0, NULL) == 0 &&
                 ToriRSChrome_Checked(&g_cw_ui, level[3]),
        "a radio cannot be toggled off");
    (void)ToriRSChrome_TakeActivated(&g_cw_ui);
}

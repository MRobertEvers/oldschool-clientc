#include "chrome_widget_sync.h"

#include <string.h>

/* TOOLTIP: hover text on any widget, shown after the delay by the host's
 * clock, drawn in the minimenu palette, sent as WIDGET_TOOLTIP. */
void
cw_test_tooltip(void)
{
    struct ToriRSChromeCmd const* cmd;
    int panel;
    int button;
    int tool;
    int n = 0;
    int found = 0;
    struct ToriRSChromePrim const* prims;
    struct ToriRSChromeWidget const* w;

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Panel");
    button = ToriRSChrome_Button(&g_cw_ui, panel, "Build");
    ToriRSChrome_SetTooltip(&g_cw_ui, button, "Pack the server scripts");
    tool = ToriRSChrome_IconButton(&g_cw_ui, panel, TORIRS_CHROME_SKIN_RUN, "Run the lane");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_TOOLTIP, button);
    CW_CHECK(cmd && strcmp(cmd->label, "Pack the server scripts") == 0,
        "WIDGET_TOOLTIP carries the hover text");
    CW_CHECK(cw_find(TORIRS_CHROME_CMD_WIDGET_TOOLTIP, tool) == NULL,
        "a widget with no tooltip of its own is not sent one");

    w = &g_cw_ui.widgets[button];
    ToriRSChrome_Tick(&g_cw_ui, 1000);
    ToriRSChrome_MouseMove(&g_cw_ui, w->x + 5, w->y + 5);
    ToriRSChrome_Tick(&g_cw_ui, 1010);
    CW_CHECK(ToriRSChrome_TooltipWidget(&g_cw_ui) == -1, "not before the delay");
    ToriRSChrome_Tick(&g_cw_ui, 1010 + TORIRS_CHROME_TOOLTIP_DELAY_MS);
    CW_CHECK(ToriRSChrome_TooltipWidget(&g_cw_ui) == button, "shown after the delay");
    (void)ToriRSChrome_Build(&g_cw_ui);
    prims = ToriRSChrome_Prims(&g_cw_ui, &n);
    for( int i = 0; i < n; i++ )
        if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text &&
            strcmp(prims[i].text, "Pack the server scripts") == 0 && i > n - 4 )
            found = 1;
    CW_CHECK(found, "drawn last, over everything");

    ToriRSChrome_MouseMove(&g_cw_ui, 700, 500);
    CW_CHECK(ToriRSChrome_TooltipWidget(&g_cw_ui) == -1, "gone the moment the pointer leaves");

    /* An icon button falls back to its label. */
    (void)ToriRSChrome_Build(&g_cw_ui);
    w = &g_cw_ui.widgets[tool];
    ToriRSChrome_MouseMove(&g_cw_ui, w->x + 5, w->y + 5);
    ToriRSChrome_Tick(&g_cw_ui, 5000);
    ToriRSChrome_Tick(&g_cw_ui, 5000 + TORIRS_CHROME_TOOLTIP_DELAY_MS);
    CW_CHECK(ToriRSChrome_TooltipWidget(&g_cw_ui) == tool, "an icon button's label is its tooltip");

    cw_run();
    ToriRSChrome_SetTooltip(&g_cw_ui, button, "Pack the server scripts");
    CW_CHECK(cw_run() == 0, "the same tooltip again records nothing");
    ToriRSChrome_SetTooltip(&g_cw_ui, button, NULL);
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_TOOLTIP, button);
    CW_CHECK(cmd && cmd->label[0] == '\0', "clearing it is sent as an empty tooltip");
}

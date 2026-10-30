#include "chrome_widget_sync.h"

#include <string.h>

/* SPLITTER / docking on the WINDOW style: docked panels are placed from the
 * surface in panel order, FILL takes the rest, a docked panel's inner edge
 * drags its size, PANEL_MODE carries dock and size, the layout saves as text. */
void
cw_test_splitter(void)
{
    struct ToriRSChromeCmd const* cmd;
    struct ToriRSChromeRect left;
    struct ToriRSChromeRect right;
    struct ToriRSChromeRect bottom;
    struct ToriRSChromeRect view;
    char saved[256];
    int catalog;
    int inspector;
    int console;
    int viewp;

    cw_reset();
    catalog = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 0, "Catalog");
    inspector = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 0, "Inspector");
    console = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 0, "Output");
    viewp = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 0, "View");
    ToriRSChrome_PanelSetDock(&g_cw_ui, catalog, TORIRS_CHROME_DOCK_LEFT, 200);
    ToriRSChrome_PanelSetDock(&g_cw_ui, inspector, TORIRS_CHROME_DOCK_RIGHT, 220);
    ToriRSChrome_PanelSetDock(&g_cw_ui, console, TORIRS_CHROME_DOCK_BOTTOM, 120);
    ToriRSChrome_PanelSetDock(&g_cw_ui, viewp, TORIRS_CHROME_DOCK_FILL, 0);
    (void)ToriRSChrome_Label(&g_cw_ui, catalog, "rows");
    cw_run();
    left = ToriRSChrome_PanelRect(&g_cw_ui, catalog);
    right = ToriRSChrome_PanelRect(&g_cw_ui, inspector);
    bottom = ToriRSChrome_PanelRect(&g_cw_ui, console);
    view = ToriRSChrome_PanelRect(&g_cw_ui, viewp);
    CW_CHECK(left.x == 0 && left.y == 0 && left.w == 200 && left.h == 600, "LEFT takes its width off the surface");
    CW_CHECK(right.x == 800 - 220 && right.w == 220 && right.h == 600, "RIGHT off what is left");
    CW_CHECK(bottom.x == 200 && bottom.w == 800 - 200 - 220 && bottom.y == 600 - 120 && bottom.h == 120,
        "BOTTOM between them");
    CW_CHECK(view.x == 200 && view.y == 0 && view.w == 380 && view.h == 480, "FILL takes the rest");
    cmd = cw_find(TORIRS_CHROME_CMD_PANEL_MODE, -1);
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_PANEL_MODE) == 4 && cmd && cmd->panel == catalog &&
                 cmd->x == TORIRS_CHROME_DOCK_LEFT && cmd->w == 200 && cmd->value == 0,
        "PANEL_MODE carries each panel's dock and size");
    CW_CHECK(cw_prims_with_slot(TORIRS_CHROME_SKIN_SPLITTER_GRIP) == 3, "a grip on every splitter");

    /* Drag the catalog's inner edge from 198 to 260. */
    CW_CHECK(ToriRSChrome_WantsPointer(&g_cw_ui, 198, 300), "the splitter band is chrome");
    ToriRSChrome_MouseMove(&g_cw_ui, 198, 300);
    ToriRSChrome_MouseDown(&g_cw_ui, 198, 300);
    ToriRSChrome_MouseMove(&g_cw_ui, 260, 310);
    ToriRSChrome_MouseUp(&g_cw_ui, 260, 310);
    cw_run();
    left = ToriRSChrome_PanelRect(&g_cw_ui, catalog);
    view = ToriRSChrome_PanelRect(&g_cw_ui, viewp);
    CW_CHECK(left.w == 260 && view.x == 260 && view.w == 320, "the drag resizes and the rest reflows");
    cmd = cw_find(TORIRS_CHROME_CMD_PANEL_MODE, -1);
    CW_CHECK(cmd && cmd->panel == catalog && cmd->w == 260, "the new size is sent");

    /* A docked panel's header does not move it. */
    ToriRSChrome_MouseDown(&g_cw_ui, 20, 5);
    ToriRSChrome_MouseMove(&g_cw_ui, 120, 105);
    ToriRSChrome_MouseUp(&g_cw_ui, 120, 105);
    (void)ToriRSChrome_Build(&g_cw_ui);
    CW_CHECK(ToriRSChrome_PanelRect(&g_cw_ui, catalog).x == 0, "a docked panel stays docked");

    /* The intent and the saved layout. */
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_SPLIT, inspector, -1, 300, 0, 0, NULL) == 1, "SPLIT applies");
    (void)ToriRSChrome_Build(&g_cw_ui);
    CW_CHECK(ToriRSChrome_PanelRect(&g_cw_ui, inspector).w == 300, "and resizes the panel");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_SPLIT, viewp, -1, 10, 0, 0, NULL) == 1 &&
                 cw_intent(TORIRS_CHROME_INTENT_SPLIT, 63, -1, 10, 0, 0, NULL) == 0,
        "SPLIT is refused for a panel that does not exist");
    CW_CHECK(ToriRSChrome_LayoutSave(&g_cw_ui, saved, (int)sizeof(saved)) > 0 &&
                 strstr(saved, "Catalog=1:260;") && strstr(saved, "Inspector=2:300;"),
        "the layout saves as text");
    ToriRSChrome_PanelSetDock(&g_cw_ui, catalog, TORIRS_CHROME_DOCK_LEFT, 150);
    CW_CHECK(ToriRSChrome_LayoutLoad(&g_cw_ui, saved) == 4 &&
                 g_cw_ui.panels[catalog].dock_size == 260,
        "and loads back");
    CW_CHECK(ToriRSChrome_LayoutLoad(&g_cw_ui, "Nope=1:5;Catalog=garbage;") == 0,
        "unknown or malformed entries are ignored");
    CW_CHECK(ToriRSChrome_LayoutSave(&g_cw_ui, saved, 8) == -1, "a short buffer says so");
}

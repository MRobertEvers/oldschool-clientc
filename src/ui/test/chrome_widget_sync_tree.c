#include "chrome_widget_sync.h"

/* TREE: depth rides the ADD, the expander rides VALUE, the selection rides
 * CHECKED; a click on the expander flips it, a click on the body selects. */
void
cw_test_tree(void)
{
    struct ToriRSChromeCmd const* cmd;
    int panel;
    int root;
    int child;
    int other;
    struct ToriRSChromeWidget const* w;

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Tree");
    root = ToriRSChrome_TreeRow(&g_cw_ui, panel, "interface 160", 0, TORIRS_CHROME_TREE_CLOSED);
    child = ToriRSChrome_TreeRow(&g_cw_ui, panel, "component 3", 2, TORIRS_CHROME_TREE_LEAF);
    other = ToriRSChrome_TreeRow(&g_cw_ui, panel, "component 4", 2, TORIRS_CHROME_TREE_LEAF);
    CW_CHECK(root >= 0 && child >= 0 && other >= 0, "tree rows are made");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_ADD, child);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_W_TREE && cmd->w == 2,
        "a tree row's ADD carries its kind and depth");
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_VALUE, root);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_TREE_CLOSED, "the expander rides WIDGET_VALUE");
    CW_CHECK(cw_prims_with_slot(TORIRS_CHROME_SKIN_TREE_CLOSED) == 1,
        "one closed expander is drawn from the baked skin");

    /* Native: click the expander of the root. */
    w = &g_cw_ui.widgets[root];
    cw_click(w->x + 4, w->y + w->h / 2);
    CW_CHECK(ToriRSChrome_TreeExpander(&g_cw_ui, root) == TORIRS_CHROME_TREE_OPEN,
        "a click on the expander opens the row");
    CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == root &&
                 ToriRSChrome_ActivationWasExpand(&g_cw_ui),
        "the activation says it was the expander");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_VALUE, root);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_TREE_OPEN && g_cw_rec.count == 3,
        "the delta is exactly the new expander state");
    CW_CHECK(cw_prims_with_slot(TORIRS_CHROME_SKIN_TREE_OPEN) == 1, "the open art is drawn");

    /* Native: click a row's body selects it and only it. */
    w = &g_cw_ui.widgets[child];
    cw_click(w->x + w->w - 10, w->y + w->h / 2);
    CW_CHECK(ToriRSChrome_Checked(&g_cw_ui, child), "a click on the body selects the row");
    CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == child &&
                 !ToriRSChrome_ActivationWasExpand(&g_cw_ui),
        "and activates it as a selection");

    cw_run();
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_WIDGET_CHECKED) == 1, "the selection is one CHECKED");

    /* Intents: what a DOM presentation reports. */
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_ACTIVATE, panel, other, 0, 0, 0, NULL) == 1,
        "ACTIVATE on a tree row applies");
    CW_CHECK(ToriRSChrome_Checked(&g_cw_ui, other) && !ToriRSChrome_Checked(&g_cw_ui, child),
        "and the selection is exclusive within the panel");
    cw_run();
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_WIDGET_CHECKED) == 2, "moving the selection sends both rows");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_EXPAND, panel, root, TORIRS_CHROME_TREE_CLOSED, 0, 0,
                 NULL) == 1 &&
                 ToriRSChrome_TreeExpander(&g_cw_ui, root) == TORIRS_CHROME_TREE_CLOSED,
        "EXPAND sets the expander");
    (void)ToriRSChrome_TakeActivated(&g_cw_ui);

    /* Compare-then-set: restating changes nothing. */
    cw_run();
    ToriRSChrome_TreeSetExpander(&g_cw_ui, root, TORIRS_CHROME_TREE_CLOSED);
    CW_CHECK(cw_run() == 0, "an equal expander records nothing");
}

#include "chrome_widget_sync.h"

#include <string.h>

/* The raised capacities: 64 panels and 4,096 widgets are all addressable,
 * the worst legal snapshot of the editor widgets fits one transaction, and a
 * truncated string is logged once per widget serial rather than silently. */
static int g_count;

static void
count_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    (void)user;
    (void)cmd;
    g_count++;
}

void
cw_test_capacity(void)
{
    struct ToriRSChromeExec exec;
    char long_label[200];
    int panel = -1;
    int made = 0;

    CW_CHECK(TORIRS_CHROME_MAX_PANELS == 64 && TORIRS_CHROME_MAX_WIDGETS == 4096 &&
                 TORIRS_CHROME_MAX_PRIMS == 16384,
        "the editor's capacities");

    cw_reset();
    for( int p = 0; p < TORIRS_CHROME_MAX_PANELS; p++ )
    {
        panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 300, "P");
        CW_CHECK(panel == p, "every panel is made");
    }
    CW_CHECK(ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 300, "X") == -1,
        "the 65th is refused");
    /* Fill every pool and every widget slot with editor widgets. */
    for( int i = 0; i < TORIRS_CHROME_VROW_POOL / TORIRS_CHROME_VROW_WINDOW_MAX; i++ )
    {
        int const list = ToriRSChrome_Code(&g_cw_ui, i % 64, "c", TORIRS_CHROME_VROW_WINDOW_MAX, 100000);
        struct ToriRSChromeSpanInput span = { 0, 3, 0xffffffu };
        made += list >= 0;
        for( int r = 0; r < TORIRS_CHROME_VROW_WINDOW_MAX; r++ )
        {
            ToriRSChrome_CodeSetLine(&g_cw_ui, list, r, "abcdef");
            (void)ToriRSChrome_CodeSetLineSpans(&g_cw_ui, list, r, &span, 1);
        }
    }
    {
        int const graph = ToriRSChrome_Graph(&g_cw_ui, 0, "g", 100);
        ToriRSChrome_GraphBegin(&g_cw_ui, graph);
        for( int i = 0; i < TORIRS_CHROME_GRAPH_ITEMS_MAX; i++ )
            (void)ToriRSChrome_GraphRect(&g_cw_ui, graph, i % 64, i / 64, 1, 1, 0x808080u, 1, i + 1);
        ToriRSChrome_GraphEnd(&g_cw_ui, graph);
        made++;
        CW_CHECK(g_cw_ui.graph_item_count == TORIRS_CHROME_GRAPH_ITEMS_MAX, "the graph pool fills");
    }
    while( ToriRSChrome_TreeRow(&g_cw_ui, made % 64, "t", 1, 1) >= 0 )
        made++;
    CW_CHECK(made == TORIRS_CHROME_MAX_WIDGETS, "every widget slot is addressable");

    memset(&exec, 0, sizeof(exec));
    exec.apply = count_apply;
    g_count = 0;
    (void)ToriRSChromeSync_Init(&g_cw_sync, &exec);
    (void)ToriRSChromeSync_Run(&g_cw_sync, &g_cw_ui);
    CW_CHECK(g_count > 0 && g_count <= TORIRS_CHROME_PROTOCOL_COMMAND_MAX,
        "the worst editor snapshot fits one WEB/BROWSER transaction");

    /* Truncation is logged once per serial. */
    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 300, "T");
    memset(long_label, 'a', sizeof(long_label) - 1);
    long_label[sizeof(long_label) - 1] = '\0';
    {
        int const label = ToriRSChrome_Label(&g_cw_ui, panel, long_label);
        CW_CHECK(strlen(ToriRSChrome_Text(&g_cw_ui, label)) == TORIRS_CHROME_INPUT_MAX - 1,
            "a long text is cut to the limit");
        CW_CHECK(g_cw_ui.widgets[label].truncation_logged == 1, "and the cut is logged");
        ToriRSChrome_SetText(&g_cw_ui, label, long_label);
        CW_CHECK(g_cw_ui.widgets[label].truncation_logged == 1, "once, not on every restatement");
        ToriRSChrome_WidgetRemove(&g_cw_ui, label);
        CW_CHECK(ToriRSChrome_Label(&g_cw_ui, panel, "short") >= 0 &&
                     g_cw_ui.widgets[label].truncation_logged == 0,
            "a new widget in the slot starts unlogged");
    }
}

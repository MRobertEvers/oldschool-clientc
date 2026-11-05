#include "chrome_widget_sync.h"

#include <string.h>

int g_cw_checks;
int g_cw_failed;
struct ToriRSChrome g_cw_ui;
struct ToriRSChromeSync g_cw_sync;
struct ToriRSChromeRecorder g_cw_rec;

void
cw_reset(void)
{
    struct ToriRSChromeExec exec;

    ToriRSChrome_Init(&g_cw_ui);
    ToriRSChrome_SetSurface(&g_cw_ui, 800, 600);
    for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT; i++ )
        ToriRSChrome_SetSkinAvailable(&g_cw_ui, i, 1);
    g_cw_ui.skin_tile_w = 88;
    g_cw_ui.skin_tile_h = 60;
    ToriRSChromeRecorder_Init(&g_cw_rec);
    exec = ToriRSChromeExec_Recorder(&g_cw_rec);
    (void)ToriRSChromeSync_Init(&g_cw_sync, &exec);
}

int
cw_run(void)
{
    g_cw_rec.count = 0;
    g_cw_rec.overflow = 0;
    (void)ToriRSChrome_Build(&g_cw_ui);
    (void)ToriRSChromeSync_Run(&g_cw_sync, &g_cw_ui);
    return g_cw_rec.count;
}

struct ToriRSChromeCmd const*
cw_find(int kind, int widget)
{
    for( int i = 0; i < g_cw_rec.count; i++ )
        if( g_cw_rec.cmds[i].kind == kind && (widget < 0 || g_cw_rec.cmds[i].widget == widget) )
            return &g_cw_rec.cmds[i];
    return NULL;
}

int
cw_count(int kind)
{
    return ToriRSChromeRecorder_CountKind(&g_cw_rec, kind);
}

void
cw_click(int x, int y)
{
    (void)ToriRSChrome_Build(&g_cw_ui);
    ToriRSChrome_MouseMove(&g_cw_ui, x, y);
    ToriRSChrome_MouseDown(&g_cw_ui, x, y);
    ToriRSChrome_MouseUp(&g_cw_ui, x, y);
}

int
cw_prims_with_slot(int slot)
{
    int count = 0;
    int n = 0;
    struct ToriRSChromePrim const* prims;

    (void)ToriRSChrome_Build(&g_cw_ui);
    prims = ToriRSChrome_Prims(&g_cw_ui, &n);
    for( int i = 0; i < n; i++ )
        if( prims[i].kind == TORIRS_CHROME_PRIM_SPRITE && prims[i].sprite_slot == slot )
            count++;
    return count;
}

int
cw_intent(int kind, int panel, int widget, int value, int x, int y, char const* text)
{
    struct ToriRSChromeIntent intent;

    memset(&intent, 0, sizeof(intent));
    intent.kind = kind;
    intent.panel = panel;
    intent.widget = widget;
    intent.value = value;
    intent.x = x;
    intent.y = y;
    if( text )
        snprintf(intent.text, sizeof(intent.text), "%s", text);
    return ToriRSChromeIntent_Apply(&g_cw_ui, &intent);
}

int
main(void)
{
    cw_test_tree();
    cw_test_vlist();
    cw_test_iconbutton();
    cw_test_slider();
    cw_test_radio();
    cw_test_modal();
    cw_test_tooltip();
    cw_test_code();
    cw_test_graph();
    cw_test_splitter();
    cw_test_skin();
    cw_test_capacity();
    cw_test_button();
    cw_test_truncation();
    cw_test_scale();
    cw_test_glyphs();
    cw_test_gesture();
    printf("chrome_widget_sync_test: %d checks, %d failed\n", g_cw_checks, g_cw_failed);
    return g_cw_failed ? 1 : 0;
}

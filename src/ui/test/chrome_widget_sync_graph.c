#include "chrome_widget_sync.h"

#include <string.h>

/* GRAPH: the host lists rects, lines and captions with hit keys; the chrome
 * draws them clipped to the well and reports gestures with the key under the
 * pointer. Restating the same picture records nothing. */
static void
draw_squares(int graph, int shift)
{
    ToriRSChrome_GraphBegin(&g_cw_ui, graph);
    for( int z = 0; z < 4; z++ )
        for( int x = 0; x < 4; x++ )
            ToriRSChrome_GraphRect(
                &g_cw_ui, graph, 4 + x * 20 + shift, 4 + z * 20, 18, 18,
                0x406020u + (uint32_t)(x * 16), 1, 1 + z * 4 + x);
    ToriRSChrome_GraphLine(&g_cw_ui, graph, 0, 0, 80, 80, 0xffff00u);
    ToriRSChrome_GraphText(&g_cw_ui, graph, 4, 100, 0xffffffu, "Lumbridge");
    ToriRSChrome_GraphEnd(&g_cw_ui, graph);
}

void
cw_test_graph(void)
{
    struct ToriRSChromeCmd const* cmd;
    struct ToriRSChromeEvent ev;
    int panel;
    int graph;
    int n = 0;
    int rects = 0;
    int lines = 0;
    struct ToriRSChromePrim const* prims;
    struct ToriRSChromeWidget const* w;
    int ox;
    int oy;

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Squares");
    graph = ToriRSChrome_Graph(&g_cw_ui, panel, "Surface", 120);
    CW_CHECK(graph >= 0, "a graph well");
    draw_squares(graph, 0);
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_ADD, graph);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_W_GRAPH && cmd->h == 120, "ADD carries the height");
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_GRAPH, graph);
    CW_CHECK(cmd && cmd->value == 18, "GRAPH announces its 18 items");
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_WIDGET_GRAPH_ITEM) == 18, "and every item follows");
    for( int i = 0; i < g_cw_rec.count; i++ )
        if( g_cw_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_GRAPH_ITEM &&
            g_cw_rec.cmds[i].tab == TORIRS_CHROME_GRAPH_TEXT )
            CW_CHECK(strcmp(g_cw_rec.cmds[i].label, "Lumbridge") == 0, "a caption rides `label`");

    prims = ToriRSChrome_Prims(&g_cw_ui, &n);
    w = &g_cw_ui.widgets[graph];
    ox = w->x + 1;
    oy = w->y + 18 + 1;
    for( int i = 0; i < n; i++ )
    {
        if( prims[i].kind == TORIRS_CHROME_PRIM_RECT && prims[i].w == 18 && prims[i].h == 18 )
        {
            rects++;
            CW_CHECK(prims[i].clip.x >= ox && prims[i].clip.y >= oy, "cells are clipped to the well");
        }
        if( prims[i].kind == TORIRS_CHROME_PRIM_LINE )
            lines++;
    }
    CW_CHECK(rects == 16 && lines == 1, "the chrome draws the host's cells and links");

    /* Gestures: press on cell key 6 (x=1, z=1), drag, release, menu, wheel. */
    ToriRSChrome_MouseMove(&g_cw_ui, ox + 4 + 20 + 5, oy + 4 + 20 + 5);
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_GRAPH_HOVER &&
                 ev.key == 6,
        "hover names the cell under the pointer");
    ToriRSChrome_MouseDown(&g_cw_ui, ox + 4 + 20 + 5, oy + 4 + 20 + 5);
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_GRAPH_PRESS &&
                 ev.key == 6 && ev.x == 4 + 20 + 5,
        "a press carries the key and the well-local point");
    ToriRSChrome_MouseMove(&g_cw_ui, ox + 50, oy + 50);
    ToriRSChrome_MouseMove(&g_cw_ui, ox + 60, oy + 61);
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_GRAPH_DRAG &&
                 ev.x == 60 && ev.y == 61,
        "consecutive drags coalesce to the newest point");
    CW_CHECK(!ToriRSChrome_TakeEvent(&g_cw_ui, &ev), "one drag event, not two");
    ToriRSChrome_MouseUp(&g_cw_ui, ox + 60, oy + 61);
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_GRAPH_RELEASE,
        "release ends the gesture");
    CW_CHECK(ToriRSChrome_SecondaryClick(&g_cw_ui, ox + 4 + 5, oy + 4 + 5) == 1 &&
                 ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_GRAPH_MENU &&
                 ev.key == 1,
        "a secondary click is a MENU on the cell");
    CW_CHECK(ToriRSChrome_MouseWheel(&g_cw_ui, ox + 10, oy + 10, -1) == 1 &&
                 ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_GRAPH_WHEEL &&
                 ev.value == -1,
        "the wheel is a WHEEL event (the host zooms)");

    /* Through the seam: the model resolves the key itself. */
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_GRAPH, panel, graph, TORIRS_CHROME_EVENT_GRAPH_PRESS,
                 4 + 60 + 1, 4 + 60 + 1, NULL) == 1 &&
                 ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.key == 16,
        "a GRAPH intent is hit-tested against the model's items");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_GRAPH_WHEEL, panel, graph, 2, 0, 0, NULL) == 1 &&
                 ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.value == 2,
        "GRAPH_WHEEL carries the delta");
    /* A key on the focused well from the DOM (workbench 6.4): x the key,
     * y the modifier bits, as the native well's GRAPH_KEY says them. */
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_GRAPH, panel, graph, TORIRS_CHROME_EVENT_GRAPH_KEY,
                 -TORIRS_CHROME_KEY_ENTER, 2, NULL) == 1 &&
                 ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_GRAPH_KEY &&
                 ev.value == -TORIRS_CHROME_KEY_ENTER && ev.col == 2 && ev.widget == graph,
        "a GRAPH_KEY intent is the well's key, Shift with it");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_GRAPH, panel, graph, TORIRS_CHROME_EVENT_GRAPH_PINCH, 0, 0, NULL) == 0,
        "a GRAPH intent of any other kind past HOVER is refused");
    /* The native well hands the arrows, Enter, Backspace and Escape on. */
    ToriRSChrome_FocusWidget(&g_cw_ui, graph, 0);
    CW_CHECK(ToriRSChrome_KeyEdit(&g_cw_ui, TORIRS_CHROME_KEY_ENTER) && ToriRSChrome_TakeEvent(&g_cw_ui, &ev) &&
                 ev.kind == TORIRS_CHROME_EVENT_GRAPH_KEY && ev.value == -TORIRS_CHROME_KEY_ENTER,
        "the focused well hands Enter on");
    CW_CHECK(ToriRSChrome_KeyEdit(&g_cw_ui, TORIRS_CHROME_KEY_BACKSPACE) && ToriRSChrome_TakeEvent(&g_cw_ui, &ev) &&
                 ev.value == -TORIRS_CHROME_KEY_BACKSPACE,
        "...and Backspace");
    /* Workbench 6.6: the dialogue graph deletes its selected node on it. */
    CW_CHECK(ToriRSChrome_KeyEdit(&g_cw_ui, TORIRS_CHROME_KEY_DELETE) && ToriRSChrome_TakeEvent(&g_cw_ui, &ev) &&
                 ev.kind == TORIRS_CHROME_EVENT_GRAPH_KEY && ev.value == -TORIRS_CHROME_KEY_DELETE,
        "...and Delete");
    CW_CHECK(ToriRSChrome_KeyEdit(&g_cw_ui, TORIRS_CHROME_KEY_ESCAPE) && ToriRSChrome_TakeEvent(&g_cw_ui, &ev) &&
                 ev.value == -TORIRS_CHROME_KEY_ESCAPE && g_cw_ui.focus != graph,
        "...and Escape, then lets go of the focus");
    while( ToriRSChrome_TakeEvent(&g_cw_ui, &ev) )
        ;

    /* Compare-then-set over the whole picture. */
    cw_run();
    draw_squares(graph, 0);
    CW_CHECK(cw_run() == 0, "the same picture again records nothing");
    draw_squares(graph, 1);
    cw_run();
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_WIDGET_GRAPH) == 1 &&
                 cw_count(TORIRS_CHROME_CMD_WIDGET_GRAPH_ITEM) == 18,
        "a moved picture is restated whole");

    /* Two wells share the pool; rebuilding one keeps the other intact. */
    {
        int other = ToriRSChrome_Graph(&g_cw_ui, panel, NULL, 60);
        ToriRSChrome_GraphBegin(&g_cw_ui, other);
        ToriRSChrome_GraphRect(&g_cw_ui, other, 0, 0, 10, 10, 0xff0000u, 1, 99);
        ToriRSChrome_GraphEnd(&g_cw_ui, other);
        draw_squares(graph, 2);
        CW_CHECK(ToriRSChrome_GraphKeyAt(&g_cw_ui, other, 5, 5) == 99 &&
                     ToriRSChrome_GraphKeyAt(&g_cw_ui, graph, 4 + 2 + 1, 4 + 1) == 1,
            "rebuilding one well leaves the other's items intact");
        ToriRSChrome_WidgetRemove(&g_cw_ui, graph);
        CW_CHECK(g_cw_ui.graph_item_count == 1 &&
                     ToriRSChrome_GraphKeyAt(&g_cw_ui, other, 5, 5) == 99,
            "removing a well returns its items and keeps the rest addressable");
    }

    /* The wheel over a well in a page that scrolls is the PAGE's, unless the
     * well said it uses the wheel (ToriRSChrome_GraphSetWheel). The map
     * editor's Palette is wells between rows: each well took the notch and
     * dropped it, so the page scrolled only from a label, and a long turn of
     * the wheel stopped where the page brought a well under the pointer. */
    {
        struct ToriRSChromeWheelFrame frame;
        struct ToriRSChromeWidget const* gw;
        int page;
        int well;
        int gx;
        int gy;
        int most;

        cw_reset();
        page = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Palette");
        (void)ToriRSChrome_Label(&g_cw_ui, page, "Underlay");
        well = ToriRSChrome_Graph(&g_cw_ui, page, NULL, 200);
        for( int i = 0; i < 30; i++ )
            (void)ToriRSChrome_Label(&g_cw_ui, page, "a row under the well");
        ToriRSChrome_PanelSetFixedHeight(&g_cw_ui, page, 260);
        ToriRSChrome_PanelSetScrollable(&g_cw_ui, page, 1);
        cw_run();
        gw = &g_cw_ui.widgets[well];
        gx = gw->x + 20;
        gy = gw->y + gw->h / 2;
        most = g_cw_ui.panels[page].content_h - g_cw_ui.panels[page].view_h;
        CW_CHECK(most > 400, "the page has more rows than its view");
        while( ToriRSChrome_TakeEvent(&g_cw_ui, &ev) )
            ;
        CW_CHECK(ToriRSChrome_MouseWheel(&g_cw_ui, gx, gy, -1) == 1 && !ToriRSChrome_TakeEvent(&g_cw_ui, &ev) &&
                     g_cw_ui.panels[page].scroll_y > 0,
            "a notch over a well in a page that scrolls moves the page, with no WHEEL event");
        /* The pointer rests where it was while the page passes under it:
         * whatever arrives there, every notch still scrolls. */
        for( int i = 0; i < 40; i++ )
        {
            cw_run();
            (void)ToriRSChrome_MouseWheel(&g_cw_ui, gx, gy, -1);
        }
        CW_CHECK(g_cw_ui.panels[page].scroll_y == most && !ToriRSChrome_TakeEvent(&g_cw_ui, &ev),
            "a long turn of the wheel reaches the end of the page");
        ToriRSChrome_PanelSetScroll(&g_cw_ui, page, 0);
        cw_run();
        /* A trackpad's exact scroll over the well is the page's too. */
        memset(&frame, 0, sizeof(frame));
        frame.x = gx;
        frame.y = gy;
        frame.trackpad = 1;
        frame.scroll_dy = -30.0f;
        frame.px_per_notch = 48;
        CW_CHECK(ToriRSChrome_WheelFrame(&g_cw_ui, &frame) == 1 && g_cw_ui.panels[page].scroll_y == 30 &&
                     !ToriRSChrome_TakeEvent(&g_cw_ui, &ev),
            "a trackpad scroll over the well moves the page by its pixels");
        ToriRSChrome_PanelSetScroll(&g_cw_ui, page, 0);
        cw_run();
        /* Ctrl+wheel is the well's either way: a pinch about the pointer. */
        memset(&frame, 0, sizeof(frame));
        frame.x = gx;
        frame.y = gy;
        frame.wheel = 1;
        frame.ctrl = 1;
        CW_CHECK(ToriRSChrome_WheelFrame(&g_cw_ui, &frame) == 1 && ToriRSChrome_TakeEvent(&g_cw_ui, &ev) &&
                     ev.kind == TORIRS_CHROME_EVENT_GRAPH_PINCH && g_cw_ui.panels[page].scroll_y == 0,
            "ctrl+wheel over the well is a pinch for it, and the page stays");
        /* A well that uses the wheel keeps it, wherever it sits. */
        ToriRSChrome_GraphSetWheel(&g_cw_ui, well, 1);
        CW_CHECK(ToriRSChrome_MouseWheel(&g_cw_ui, gx, gy, -1) == 1 && ToriRSChrome_TakeEvent(&g_cw_ui, &ev) &&
                     ev.kind == TORIRS_CHROME_EVENT_GRAPH_WHEEL && ev.value == -1 &&
                     g_cw_ui.panels[page].scroll_y == 0,
            "a well that said it uses the wheel gets the WHEEL event and the page stays");
        ToriRSChrome_GraphSetWheel(&g_cw_ui, well, 0);
        CW_CHECK(ToriRSChrome_MouseWheel(&g_cw_ui, gx, gy, -1) == 1 && !ToriRSChrome_TakeEvent(&g_cw_ui, &ev) &&
                     g_cw_ui.panels[page].scroll_y > 0,
            "and one that takes it back leaves it to the page again");
    }
}

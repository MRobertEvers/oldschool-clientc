#include "chrome_widget_sync.h"

#include <string.h>

/* Gesture frames (workbench 6.1, design section 14): ToriRSChrome_Gesture
 * routes a PAN / PINCH / TWIST to the widget under it -- a GRAPH pans and
 * zooms about the point, a MODELVIEW orbits and zooms, a VLIST scrolls by
 * pixels and ignores a pinch -- and says whether it took the frame, so the
 * World behind takes the rest. The executor's GESTURE intent reaches the
 * same rules for a well the browser hit-tested. */

static struct ToriRSChromeGesture
gesture(int kind, int x, int y, int dx, int dy, int fingers, int scale_q16, int phase)
{
    struct ToriRSChromeGesture g;

    memset(&g, 0, sizeof(g));
    g.kind = kind;
    g.x = x;
    g.y = y;
    g.dx = dx;
    g.dy = dy;
    g.fingers = fingers;
    g.scale_q16 = scale_q16;
    g.phase = phase;
    return g;
}

static int
drain(void)
{
    struct ToriRSChromeEvent ev;
    int n = 0;

    while( ToriRSChrome_TakeEvent(&g_cw_ui, &ev) )
        n++;
    return n;
}

void
cw_test_gesture(void)
{
    struct ToriRSChromeEvent ev;
    struct ToriRSChromeGesture g;
    struct ToriRSChromeWidget const* w;
    int panel;
    int graph;
    int view;
    int list;
    int gx;
    int gy;
    int vx;
    int vy;
    int lx;
    int ly;
    int row_h;

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Gestures");
    graph = ToriRSChrome_Graph(&g_cw_ui, panel, NULL, 80);
    view = ToriRSChrome_ModelView(&g_cw_ui, panel, 120, 64);
    list = ToriRSChrome_VList(&g_cw_ui, panel, NULL, 6, 400);
    CW_CHECK(graph >= 0 && view >= 0 && list >= 0, "a well, a model view and a list");
    ToriRSChrome_GraphBegin(&g_cw_ui, graph);
    ToriRSChrome_GraphRect(&g_cw_ui, graph, 0, 0, 40, 40, 0x406020u, 1, 7);
    ToriRSChrome_GraphEnd(&g_cw_ui, graph);
    cw_run();

    w = &g_cw_ui.widgets[graph];
    gx = w->x + 1 + 20;
    gy = w->y + 1 + 20;
    w = &g_cw_ui.widgets[view];
    vx = w->x + w->w / 2;
    vy = w->y + w->h / 2;
    w = &g_cw_ui.widgets[list];
    lx = w->x + w->w / 2;
    ly = w->y + w->h / 2;
    CW_CHECK(ToriRSChrome_HitTest(&g_cw_ui, gx, gy) == graph, "the probe point is on the well");
    CW_CHECK(ToriRSChrome_HitTest(&g_cw_ui, vx, vy) == view, "the probe point is on the model view");
    CW_CHECK(ToriRSChrome_HitTest(&g_cw_ui, lx, ly) == list, "the probe point is on the list");
    drain();

    /* GRAPH: a two-finger pan is GRAPH_SCROLL with the content following
     * the fingers (x flipped into a scroll's sense); a pinch is GRAPH_PINCH
     * about the point, the exact ratio; a twist is taken and does nothing. */
    g = gesture(TORIRS_CHROME_GESTURE_PAN, gx, gy, 7, -5, 2, 0, TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1, "a pan over a well is taken");
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_GRAPH_SCROLL &&
                 ev.widget == graph && ev.dx == -7 * 256 && ev.dy == -5 * 256 && ev.x == 20 && ev.y == 20 &&
                 ev.key == 7,
        "a well's pan is a GRAPH_SCROLL at the well-local point, content following the fingers");
    g = gesture(TORIRS_CHROME_GESTURE_PINCH, gx, gy, 0, 0, 2, 81920, TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1, "a pinch over a well is taken");
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_GRAPH_PINCH &&
                 ev.value == 81920 && ev.x == 20 && ev.y == 20,
        "a well's pinch is GRAPH_PINCH by its ratio, about its centre");
    g = gesture(TORIRS_CHROME_GESTURE_TWIST, gx, gy, 0, 0, 2, 0, TORIRS_CHROME_GESTURE_UPDATE);
    g.radians_q16 = 6554;
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1 && drain() == 0,
        "a twist over a well is taken and asks nothing of it");

    /* MODELVIEW: a pan orbits (dx yaw, dy pitch, logical px in 1/256),
     * one finger or two; a pinch zooms; a twist (a roll) is nothing. */
    g = gesture(TORIRS_CHROME_GESTURE_PAN, vx, vy, 12, 3, 1, 0, TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1, "a pan over a model view is taken");
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_VIEW_ORBIT &&
                 ev.widget == view && ev.dx == 12 * 256 && ev.dy == 3 * 256,
        "a model view's pan is VIEW_ORBIT: dx yaw, dy pitch");
    g = gesture(TORIRS_CHROME_GESTURE_PINCH, vx, vy, 0, 0, 2, 52429, TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1 && ToriRSChrome_TakeEvent(&g_cw_ui, &ev) &&
                 ev.kind == TORIRS_CHROME_EVENT_VIEW_ZOOM && ev.value == 52429,
        "a model view's pinch is VIEW_ZOOM by its ratio");
    g = gesture(TORIRS_CHROME_GESTURE_TWIST, vx, vy, 0, 0, 2, 0, TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1 && drain() == 0, "a twist is no roll");

    /* VLIST: two fingers scroll by PIXELS, the rows under the fingers; the
     * part row waits for the next frame. Fingers up (dy < 0) bring later
     * rows in. */
    row_h = ToriRSChrome_FontLineBox(g_cw_ui.theme.font_row, g_cw_ui.scale) + 2;
    g = gesture(TORIRS_CHROME_GESTURE_PAN, lx, ly, 0, -(2 * row_h + 3), 2, 0, TORIRS_CHROME_GESTURE_BEGIN);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1, "a pan over a list is taken");
    CW_CHECK(g_cw_ui.widgets[list].imin == 2 && g_cw_ui.widgets[list].pan_rem == 3,
        "two rows and three pixels: two rows move, the part row waits");
    g = gesture(TORIRS_CHROME_GESTURE_PAN, lx, ly, 0, -(row_h - 3), 2, 0, TORIRS_CHROME_GESTURE_UPDATE);
    (void)ToriRSChrome_Gesture(&g_cw_ui, &g);
    CW_CHECK(g_cw_ui.widgets[list].imin == 3 && g_cw_ui.widgets[list].pan_rem == 0,
        "the waiting pixels and the next frame's make the third row");
    g = gesture(TORIRS_CHROME_GESTURE_PAN, lx, ly, 0, row_h, 2, 0, TORIRS_CHROME_GESTURE_UPDATE);
    (void)ToriRSChrome_Gesture(&g_cw_ui, &g);
    CW_CHECK(g_cw_ui.widgets[list].imin == 2, "fingers down bring the rows back");
    g = gesture(TORIRS_CHROME_GESTURE_PINCH, lx, ly, 0, 0, 2, 131072, TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1 && g_cw_ui.widgets[list].imin == 2 && drain() == 0,
        "a pinch over a list is taken and ignored: a stray finger never jumps it");
    g = gesture(TORIRS_CHROME_GESTURE_PAN, lx, ly, 0, -5 * row_h, 1, 0, TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1 && g_cw_ui.widgets[list].imin == 2,
        "a one-finger pan is the pointer's drag, not a scroll");
    g = gesture(TORIRS_CHROME_GESTURE_PAN, lx, ly, 0, 1000 * row_h, 2, 0, TORIRS_CHROME_GESTURE_UPDATE);
    (void)ToriRSChrome_Gesture(&g_cw_ui, &g);
    CW_CHECK(g_cw_ui.widgets[list].imin == 0 && g_cw_ui.widgets[list].pan_rem == 0,
        "past the top the list stops, and keeps no travel to undo");

    /* Over nothing: not the chrome's, the World takes it. */
    g = gesture(TORIRS_CHROME_GESTURE_PAN, 700, 560, 4, 4, 2, 0, TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 0, "a pan over no panel is not taken");
    g = gesture(TORIRS_CHROME_GESTURE_PINCH, 700, 560, 0, 0, 2, 70000, TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 0, "nor a pinch");
    g = gesture(TORIRS_CHROME_GESTURE_TWIST, 700, 560, 0, 0, 2, 0, TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 0 && drain() == 0, "nor a twist, and nothing queued");

    /* The executor seam: a GESTURE intent for a well the browser hit-tested,
     * widget-local logical units, "dx dy fingers scale_q16 radians_q16 phase". */
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_GESTURE, panel, view, TORIRS_CHROME_GESTURE_PAN, 30, 20,
                 "4 -2 1 65536 0 1") == 1 &&
                 ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_VIEW_ORBIT &&
                 ev.dx == 4 * 256 && ev.dy == -2 * 256,
        "a GESTURE intent orbits a model view by the same rule");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_GESTURE, panel, graph, TORIRS_CHROME_GESTURE_PINCH, 10, 10,
                 "0 0 2 98304 0 1") == 1 &&
                 ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_GRAPH_PINCH &&
                 ev.value == 98304 && ev.x == 10 && ev.y == 10 && ev.key == 7,
        "and pinches a well about its well-local point");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_GESTURE, panel, list, TORIRS_CHROME_GESTURE_PAN, 0, 0,
                 "0 -40 2 65536 0 0") == 1 && g_cw_ui.widgets[list].imin == 40 / row_h,
        "and scrolls a list by pixels");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_GESTURE, panel, view, TORIRS_CHROME_GESTURE_PAN, 0, 0,
                 "4 x") == 0 && drain() == 0,
        "a malformed frame is dropped like any unknown intent");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_GESTURE, panel, view, 9, 0, 0, "0 0 1 65536 0 1") == 0,
        "an unknown gesture kind is dropped");

    /* A dialog takes every frame, wherever it lands. */
    {
        int const modal = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 400, 300, 200, "Ask");
        ToriRSChrome_Label(&g_cw_ui, modal, "Sure?");
        ToriRSChrome_PanelSetModal(&g_cw_ui, modal, 1);
        cw_run();
        g = gesture(TORIRS_CHROME_GESTURE_PINCH, 700, 560, 0, 0, 2, 70000, TORIRS_CHROME_GESTURE_UPDATE);
        CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1 && drain() == 0,
            "an open modal swallows a frame beside it");
    }

    /* At 2x the deltas reach the host in logical px. */
    cw_reset();
    ToriRSChrome_SetScale(&g_cw_ui, 2);
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Gestures");
    view = ToriRSChrome_ModelView(&g_cw_ui, panel, 120, 64);
    cw_run();
    w = &g_cw_ui.widgets[view];
    g = gesture(TORIRS_CHROME_GESTURE_PAN, w->x + w->w / 2, w->y + w->h / 2, 12, 6, 2, 0,
        TORIRS_CHROME_GESTURE_UPDATE);
    CW_CHECK(ToriRSChrome_Gesture(&g_cw_ui, &g) == 1 && ToriRSChrome_TakeEvent(&g_cw_ui, &ev) &&
                 ev.kind == TORIRS_CHROME_EVENT_VIEW_ORBIT && ev.dx == 6 * 256 && ev.dy == 3 * 256,
        "at 2x a model view's orbit is in logical px");
}

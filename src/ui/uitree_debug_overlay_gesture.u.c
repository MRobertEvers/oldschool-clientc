/*
 * uitree_debug_overlay_gesture.u.c -- gesture frames into the chrome.
 *
 * NOT a translation unit: uitree_debug_overlay.c includes it at its end,
 * after the widgets' half, and it shares their statics (dbg_graph_at, the
 * event queue, the panel scroll). Workbench 6.1, design section 14: the
 * bus's continuous PAN / PINCH / TWIST, routed to the widget under the
 * gesture, beside the wheel's own routing (ToriRSChrome_WheelFrame).
 *
 * The rules (one table, every surface):
 *
 *   - a GRAPH well pans and zooms about the gesture, continuously;
 *   - a MODELVIEW orbits on a pan and zooms on a pinch; a twist (a roll)
 *     is nothing a turntable has;
 *   - a list, code or text box scrolls by PIXELS under two fingers, the rows
 *     following the fingers, and ignores a pinch (a stray second finger
 *     never jumps a list);
 *   - anything else on a panel scrolls the panel under two fingers;
 *   - a one-finger pan is the pointer's: the touch layer already drags it
 *     as a pressed button, so the frame is taken and nothing more is done.
 *
 * Nothing here allocates; the one piece of state a frame leaves behind is a
 * list's part row (ToriRSChromeWidget::pan_rem).
 */

static double
dbg_gesture_scale(struct ToriRSChromeGesture const* gesture)
{
    return (double)gesture->scale_q16 / 65536.0;
}

/** Scroll a panel's content by a two-finger pan: the content follows the
 *  fingers, so fingers moving down (dy > 0) bring the top back. */
static void
dbg_gesture_panel_pan(struct ToriRSChrome* ui, int panel, int dy)
{
    if( panel < 0 || !dbg_panel_scrolls(&ui->panels[panel]) )
        return;
    (void)dbg_panel_scroll_to(ui, panel, ui->panels[panel].scroll_y - dy);
}

/**
 * Move a row-scrolled window by `dy` surface px of finger travel, rows of
 * `row_h`: whole rows are taken, the part row waits in `pan_rem` for the
 * next frame. Returns the rows to move by (positive = down the list).
 */
static int
dbg_gesture_rows(struct ToriRSChromeWidget* w, int dy, int row_h, int phase)
{
    int rows;

    assert(row_h > 0);
    if( phase == TORIRS_CHROME_GESTURE_BEGIN )
        w->pan_rem = 0;
    /* Fingers down pull the content down: the first row goes UP the list. */
    w->pan_rem -= dy;
    rows = w->pan_rem / row_h;
    w->pan_rem -= rows * row_h;
    if( phase == TORIRS_CHROME_GESTURE_END )
        w->pan_rem = 0;
    return rows;
}

static int
dbg_gesture_vlist(struct ToriRSChrome* ui, int widget, struct ToriRSChromeGesture const* gesture)
{
    struct ToriRSChromeWidget* w = &ui->widgets[widget];
    int const row_h = w->kind == TORIRS_CHROME_W_CODE ? DBG_CODE_LINE_H : DBG_VROW_H;
    int const rows = dbg_gesture_rows(w, gesture->dy, row_h, gesture->phase);
    int const first = dbg_clampi(w->imin + rows, 0, dbg_vlist_max_first(w));

    /* At either end the travel has nowhere to go: kept, it would have to
     * be undone before the list moved the other way. */
    if( first != w->imin + rows )
        w->pan_rem = 0;
    ToriRSChrome_VListSetScroll(ui, widget, first);
    return 1;
}

static int
dbg_gesture_textarea(struct ToriRSChrome* ui, int widget, struct ToriRSChromeGesture const* gesture)
{
    struct ToriRSChromeWidget* w = &ui->widgets[widget];
    int starts[DBG_TEXTAREA_LINES_MAX];
    int lens[DBG_TEXTAREA_LINES_MAX];
    int const count = dbg_textarea_wrap(ui, w, starts, lens);
    int const shown = dbg_textarea_rows(w);
    int rows;
    int scroll;

    /* A box whose lines all show has nothing of its own to scroll: the
     * panel under it does. */
    if( count <= shown )
    {
        dbg_gesture_panel_pan(ui, w->panel, gesture->dy);
        return 1;
    }
    rows = dbg_gesture_rows(w, gesture->dy, dbg_textarea_line_h(ui), gesture->phase);
    scroll = dbg_clampi(w->scroll + rows, 0, count - shown);
    if( scroll != w->scroll + rows )
        w->pan_rem = 0;
    if( scroll != w->scroll )
    {
        w->scroll = scroll;
        dbg_dirty_widget_paint(ui, widget);
    }
    return 1;
}

static void
dbg_gesture_view_event(
    struct ToriRSChrome* ui, int widget, int kind, int dx_q8, int dy_q8, int value)
{
    struct ToriRSChromeEvent ev;

    dbg_event_init(&ev, ui, kind, widget);
    ev.dx = dx_q8;
    ev.dy = dy_q8;
    ev.value = value;
    (void)ToriRSChrome_PushEvent(ui, &ev);
}

/** A pinch ratio as an event value: q16, at least 1, inside an int. */
static int
dbg_gesture_q16(double scale)
{
    double q = scale * 65536.0 + 0.5;
    if( q < 1.0 )
        q = 1.0;
    if( q > 2147483000.0 )
        q = 2147483000.0;
    return (int)q;
}

int
ToriRSChrome_Gesture(struct ToriRSChrome* ui, struct ToriRSChromeGesture const* gesture)
{
    double scale;
    int hit;
    int kind;

    assert(ui);
    assert(gesture);
    assert(gesture->kind >= TORIRS_CHROME_GESTURE_PAN);
    assert(gesture->kind <= TORIRS_CHROME_GESTURE_TWIST);
    assert(ui->scale_q >= TORIRS_CHROME_SCALE_Q_MIN);
    /* The rational scale: surface px over logical px, 1.5 at q = 6. */
    scale = (double)ui->scale_q / (double)TORIRS_CHROME_SCALE_Q_ONE;
    if( dbg_modal_blocks(ui, gesture->x, gesture->y) )
        return 1;
    /* An open list or colour picker is over everything, and none of the
     * three means anything to it: taken over it, the caller's elsewhere. */
    if( ui->dropdown_open >= 0 || ui->colorpick_open >= 0 )
        return ToriRSChrome_WantsPointer(ui, gesture->x, gesture->y);
    if( dbg_panel_at(ui, gesture->x, gesture->y) < 0 )
        return 0;

    hit = ToriRSChrome_HitTest(ui, gesture->x, gesture->y);
    kind = hit >= 0 ? ui->widgets[hit].kind : -1;

    if( gesture->kind == TORIRS_CHROME_GESTURE_PINCH )
    {
        assert(gesture->scale_q16 > 0);
        if( kind == TORIRS_CHROME_W_GRAPH )
            (void)ToriRSChrome_Pinch(ui, gesture->x, gesture->y, dbg_gesture_scale(gesture));
        else if( kind == TORIRS_CHROME_W_MODELVIEW )
            dbg_gesture_view_event(ui, hit, TORIRS_CHROME_EVENT_VIEW_ZOOM, 0, 0, gesture->scale_q16);
        return 1;
    }
    if( gesture->kind == TORIRS_CHROME_GESTURE_TWIST )
        return 1;

    /* PAN. One finger is the pointer's own drag (pressed by the touch
     * layer), so only the MODELVIEW, which has no drag of its own, orbits
     * under one; the rest want two. */
    if( kind == TORIRS_CHROME_W_MODELVIEW )
    {
        dbg_gesture_view_event(ui, hit, TORIRS_CHROME_EVENT_VIEW_ORBIT,
            dbg_q8((double)gesture->dx / scale), dbg_q8((double)gesture->dy / scale), 0);
        return 1;
    }
    if( gesture->fingers < 2 )
        return 1;
    if( kind == TORIRS_CHROME_W_GRAPH )
    {
        /* GRAPH_SCROLL's convention is a scroll's (+dx scroll right = content
         * left, +dy scroll up = content down); a pan's content follows the
         * fingers, so x flips and y does not. */
        (void)ToriRSChrome_Scroll(ui, gesture->x, gesture->y, -(double)gesture->dx, (double)gesture->dy);
        return 1;
    }
    if( hit >= 0 && dbg_is_vlist_kind(kind) )
        return dbg_gesture_vlist(ui, hit, gesture);
    if( kind == TORIRS_CHROME_W_TEXTAREA )
        return dbg_gesture_textarea(ui, hit, gesture);
    dbg_gesture_panel_pan(ui, dbg_panel_at(ui, gesture->x, gesture->y), gesture->dy);
    return 1;
}

int
ToriRSChrome_GestureWidget(
    struct ToriRSChrome* ui,
    int widget,
    struct ToriRSChromeGesture const* gesture)
{
    struct ToriRSChromeWidget* w;

    assert(ui);
    assert(gesture);
    assert(dbg_valid_widget(ui, widget));
    assert(gesture->kind >= TORIRS_CHROME_GESTURE_PAN);
    assert(gesture->kind <= TORIRS_CHROME_GESTURE_TWIST);
    w = &ui->widgets[widget];

    if( w->kind == TORIRS_CHROME_W_MODELVIEW )
    {
        if( gesture->kind == TORIRS_CHROME_GESTURE_PAN )
            dbg_gesture_view_event(ui, widget, TORIRS_CHROME_EVENT_VIEW_ORBIT,
                dbg_q8((double)gesture->dx), dbg_q8((double)gesture->dy), 0);
        else if( gesture->kind == TORIRS_CHROME_GESTURE_PINCH && gesture->scale_q16 > 0 )
            dbg_gesture_view_event(ui, widget, TORIRS_CHROME_EVENT_VIEW_ZOOM, 0, 0, gesture->scale_q16);
        return gesture->kind != TORIRS_CHROME_GESTURE_TWIST;
    }
    if( w->kind == TORIRS_CHROME_W_GRAPH )
    {
        struct ToriRSChromeEvent ev;

        if( gesture->kind == TORIRS_CHROME_GESTURE_TWIST )
            return 0;
        if( gesture->kind == TORIRS_CHROME_GESTURE_PINCH )
        {
            if( gesture->scale_q16 <= 0 )
                return 0;
            dbg_event_init(&ev, ui, TORIRS_CHROME_EVENT_GRAPH_PINCH, widget);
            ev.value = dbg_gesture_q16(dbg_gesture_scale(gesture));
        }
        else
        {
            dbg_event_init(&ev, ui, TORIRS_CHROME_EVENT_GRAPH_SCROLL, widget);
            ev.dx = dbg_q8(-(double)gesture->dx);
            ev.dy = dbg_q8((double)gesture->dy);
        }
        ev.x = gesture->x;
        ev.y = gesture->y;
        ev.key = ToriRSChrome_GraphKeyAt(ui, widget, ev.x, ev.y);
        return ToriRSChrome_PushEvent(ui, &ev);
    }
    if( dbg_is_vlist_kind(w->kind) && gesture->kind == TORIRS_CHROME_GESTURE_PAN )
    {
        struct ToriRSChromeGesture surface = *gesture;
        /* The executor speaks logical px; the part row is kept in surface. */
        surface.dy = ToriRSChrome_ScalePx(ui->scale_q, gesture->dy);
        return dbg_gesture_vlist(ui, widget, &surface);
    }
    return 0;
}

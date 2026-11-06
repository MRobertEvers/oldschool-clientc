/*
 * uitree_debug_overlay_widgets.u.c -- the map editor's widgets.
 *
 * NOT a translation unit. uitree_debug_overlay.c includes this once, at its
 * end, so it is that file's second half: it shares every static helper above
 * it (the prim pushers, the change journal, the dirty and damage plumbing),
 * and the hooks the older code calls into it are forward-declared at that
 * file's top. Kept apart only so the editor's additions read as one piece
 * rather than as edits scattered through eight thousand lines.
 *
 * What is here (docs/MAPED_DESIGN.html #chrome, "Widgets to add"):
 *
 *   TREE        a LISTROW with depth, an expander and a selection
 *   VLIST       a list that owns a row COUNT and asks the host for the text
 *               of the rows on screen
 *   ICONBUTTON  a sprite-faced button; TOOLBAR is a panel of them
 *   SLIDER      a track and a knob over an int range
 *   RADIO       a checkbox with an exclusive group
 *   CODE        a VLIST of source lines with a gutter, caret and highlight
 *   GRAPH       a CUSTOM well the chrome draws from the host's item list,
 *               with hit keys
 *   MODAL       a panel that takes the pointer and keyboard until it closes
 *   TOOLTIP     hover text after a delay, on any widget
 *   SPLITTER    docked panels, and the grip between them
 *
 * The rules of the module hold here too: no allocation (pools are fixed
 * arrays in the POD), nothing borrowed past the call that handed it over,
 * compare-then-set setters, and every real mutation journalled for the
 * executor seam.
 */

/* ---- metrics ------------------------------------------------------------ */

/** An icon button's square, at 1x. 32 so the bank's 29x22 art and the 25x25
 *  run glyph sit inside it at their baked size -- drawn smaller they speckle. */
#define DBG_ICON_BOX DBG_PX(32)
#define DBG_ICON_GAP DBG_PX(2)
/** TREE: indent per depth level, and the expander's box. */
#define DBG_TREE_INDENT DBG_PX(12)
#define DBG_TREE_EXP DBG_PX(16)
#define DBG_TREE_GAP DBG_PX(3)
/** SLIDER: the knob's box and the column the value is printed in. */
#define DBG_KNOB DBG_PX(16)
#define DBG_SLIDER_VALUE_W DBG_PX(36)
#define DBG_SLIDER_TRACK_H DBG_PX(6)
/** The baked track: 14 px tall, 8 px end caps, a 16 px middle repeated. */
#define DBG_SLIDER_SKIN_H DBG_PX(14)
#define DBG_SLIDER_CAP_W DBG_PX(8)
#define DBG_SLIDER_TILE_W DBG_PX(16)
/** A slider's caption column: at most this share of the row, so the track
 *  always gets at least half of it (the fixed 104 px column left a 196 px
 *  panel's track 40 px long). */
#define DBG_SLIDER_LABEL_PERCENT 30
#define DBG_SLIDER_LABEL_GAP DBG_PX(6)
/** A docked panel's splitter band, on its inner edge. */
#define DBG_SPLIT DBG_PX(5)
/** Smallest a splitter drag may make a docked panel. */
#define DBG_DOCK_MIN DBG_PX(48)
/** One VLIST row, and one CODE line (the menu face, as the design says). */
#define DBG_VROW_H (ToriRSChrome_FontLineBoxQ(ui->theme.font_row, ui->scale_q) + DBG_PX(2))
#define DBG_CODE_LINE_H (ToriRSChrome_FontLineBoxQ(TORIRS_CHROME_FONT_MENU, ui->scale_q) + DBG_PX(1))
#define DBG_GUTTER_PAD DBG_PX(4)
#define DBG_TOOLTIP_PAD DBG_PX(3)
/** Chords at the action (torirs_chrome_metrics.h, "keycaps and badges"). */
#define DBG_KEYCAP_INSET DBG_PX(TORIRS_CHROME_M_KEYCAP_INSET)
#define DBG_KEYCAP_PAD_X DBG_PX(TORIRS_CHROME_M_KEYCAP_PAD_X)
#define DBG_KEYCAP_GAP DBG_PX(TORIRS_CHROME_M_KEYCAP_GAP)
#define DBG_BADGE_H DBG_PX(TORIRS_CHROME_M_BADGE_H)
#define DBG_BADGE_INSET DBG_PX(TORIRS_CHROME_M_BADGE_INSET)
/** Transparency of the veil under a modal (the reference's sense: 0 opaque). */
#define DBG_MODAL_VEIL_TRANS 128

/*
 * The baked size of each editor slot, at 1x.
 *
 * The module links no bake (it has no dependencies; see the header note), and
 * a sprite prim with no box draws at native size whatever the chrome scale --
 * so an icon at 2x needs its size from somewhere. These are the sizes
 * spritebake wrote (src/engine/torirs_chrome_skin_baked.c's header);
 * test-chrome-widgets checks every row against the bake, so a re-bake that
 * moves one fails there rather than drawing a stretched icon.
 */
static const unsigned char k_dbg_slot_size[TORIRS_CHROME_SKIN_SLOT_COUNT][2] = {
    [TORIRS_CHROME_SKIN_TOOL_SELECT] = { 15, 15 },
    [TORIRS_CHROME_SKIN_TOOL_RAISE] = { 16, 16 },
    [TORIRS_CHROME_SKIN_TOOL_LOWER] = { 16, 16 },
    [TORIRS_CHROME_SKIN_TOOL_FLATTEN] = { 15, 15 },
    [TORIRS_CHROME_SKIN_TOOL_SMOOTH] = { 15, 15 },
    [TORIRS_CHROME_SKIN_TOOL_PAINT_UNDERLAY] = { 15, 15 },
    [TORIRS_CHROME_SKIN_TOOL_PAINT_OVERLAY] = { 15, 15 },
    [TORIRS_CHROME_SKIN_TOOL_FLAGS] = { 15, 15 },
    [TORIRS_CHROME_SKIN_TOOL_PLACE] = { 15, 15 },
    [TORIRS_CHROME_SKIN_TOOL_MOVE] = { 15, 15 },
    [TORIRS_CHROME_SKIN_TOOL_ROTATE] = { 15, 15 },
    [TORIRS_CHROME_SKIN_TOOL_DELETE] = { 29, 22 },
    [TORIRS_CHROME_SKIN_SEARCH] = { 29, 22 },
    [TORIRS_CHROME_SKIN_TREE_CLOSED] = { 16, 16 },
    [TORIRS_CHROME_SKIN_TREE_OPEN] = { 16, 16 },
    [TORIRS_CHROME_SKIN_RADIO_ON] = { 17, 17 },
    [TORIRS_CHROME_SKIN_RADIO_OFF] = { 17, 17 },
    [TORIRS_CHROME_SKIN_SLIDER_TRACK] = { 16, 14 },
    [TORIRS_CHROME_SKIN_SLIDER_KNOB] = { 16, 16 },
    [TORIRS_CHROME_SKIN_SLIDER_TRACK_LEFT] = { 8, 14 },
    [TORIRS_CHROME_SKIN_SLIDER_TRACK_RIGHT] = { 8, 14 },
    [TORIRS_CHROME_SKIN_SPLITTER_GRIP] = { 7, 11 },
    [TORIRS_CHROME_SKIN_WARNING] = { 7, 18 },
    [TORIRS_CHROME_SKIN_ERROR] = { 16, 16 },
    [TORIRS_CHROME_SKIN_RUN] = { 25, 25 },
    [TORIRS_CHROME_SKIN_STOP] = { 19, 19 },
    [TORIRS_CHROME_SKIN_BUILD] = { 19, 17 },
    [TORIRS_CHROME_SKIN_SAVE] = { 16, 16 },
    [TORIRS_CHROME_SKIN_CLOSE] = { 16, 16 },
    [TORIRS_CHROME_SKIN_CHECK_ON] = { 17, 17 },
    [TORIRS_CHROME_SKIN_CHECK_OFF] = { 17, 17 },
    [TORIRS_CHROME_SKIN_PLUGIN_ICON] = { 33, 36 },
};

int
ToriRSChrome_SkinSlotSize(int slot, int* out_w, int* out_h)
{
    int w = 0;
    int h = 0;

    if( slot >= 0 && slot < TORIRS_CHROME_SKIN_SLOT_COUNT )
    {
        w = k_dbg_slot_size[slot][0];
        h = k_dbg_slot_size[slot][1];
    }
    if( out_w )
        *out_w = w;
    if( out_h )
        *out_h = h;
    return w > 0 && h > 0;
}

/* ---- small helpers -------------------------------------------------------- */

static int
dbg_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int
dbg_is_vlist_kind(int kind)
{
    return kind == TORIRS_CHROME_W_VLIST || kind == TORIRS_CHROME_W_CODE;
}

/**
 * Note -- once per widget serial -- that `src` did not fit a `cap`-byte field.
 *
 * Two answers, both kept: the stderr line every host already gets, and a
 * record in the ring the host drains (ToriRSChrome_TakeTruncation), so the
 * map editor's console can say it where the person editing will see it. The
 * limits stay (63-byte labels, 191-byte text); what changes is that hitting
 * one is never silent (the workbench contract's R2).
 */
static void
dbg_note_truncation(
    struct ToriRSChrome* ui, int widget, int field, char const* src, int cap)
{
    struct ToriRSChromeWidget* w = &ui->widgets[widget];
    struct ToriRSChromeTruncation* rec;
    size_t length;
    int slot;

    if( !src || w->truncation_logged )
        return;
    length = strlen(src);
    if( length < (size_t)cap )
        return;
    w->truncation_logged = 1;
    fprintf(
        stderr,
        "chrome: widget %d (serial %d) string cut to %d bytes: \"%.40s...\"\n",
        widget,
        w->serial,
        cap - 1,
        src);
    /* A full ring overwrites its oldest: the newest cut is the one on the page
     * somebody just opened. */
    if( ui->truncation_count == TORIRS_CHROME_TRUNCATION_RING )
    {
        ui->truncation_head = (ui->truncation_head + 1) % TORIRS_CHROME_TRUNCATION_RING;
        ui->truncation_count--;
        ui->truncation_dropped++;
    }
    slot = (ui->truncation_head + ui->truncation_count) % TORIRS_CHROME_TRUNCATION_RING;
    rec = &ui->truncations[slot];
    rec->widget = widget;
    rec->serial = w->serial;
    rec->widget_kind = w->kind;
    rec->field = field;
    rec->kept = cap - 1;
    rec->length = length > 0x7FFFFFFF ? 0x7FFFFFFF : (int)length;
    dbg_copy(rec->head, TORIRS_CHROME_TRUNCATION_HEAD, src);
    ui->truncation_count++;
}

/** Which field of `w` the buffer `dst` is, for the truncation record. */
static int
dbg_widget_field(struct ToriRSChromeWidget const* w, char const* dst)
{
    if( dst == w->label )
        return TORIRS_CHROME_TRUNC_LABEL;
    if( dst == w->tooltip )
        return TORIRS_CHROME_TRUNC_TOOLTIP;
    if( dst == w->chord )
        return TORIRS_CHROME_TRUNC_CHORD;
    if( dst == w->tooltip_chord )
        return TORIRS_CHROME_TRUNC_TOOLTIP_CHORD;
    return TORIRS_CHROME_TRUNC_TEXT;
}

/** Copy a caller string into one of a widget's fields, noting a cut. */
static void
dbg_copy_widget(
    struct ToriRSChrome* ui, int widget, char* dst, int cap, char const* src)
{
    dbg_note_truncation(ui, widget, dbg_widget_field(&ui->widgets[widget], dst), src, cap);
    dbg_copy(dst, cap, src);
}

int
ToriRSChrome_TakeTruncation(struct ToriRSChrome* ui, struct ToriRSChromeTruncation* out)
{
    assert(ui);
    assert(out);
    if( ui->truncation_count == 0 )
    {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    *out = ui->truncations[ui->truncation_head];
    ui->truncation_head = (ui->truncation_head + 1) % TORIRS_CHROME_TRUNCATION_RING;
    ui->truncation_count--;
    return 1;
}

int
ToriRSChrome_TakeTruncationsDropped(struct ToriRSChrome* ui)
{
    int was;

    assert(ui);
    was = ui->truncation_dropped;
    ui->truncation_dropped = 0;
    return was;
}

char const*
ToriRSChrome_TruncationFieldName(int field)
{
    assert(field >= TORIRS_CHROME_TRUNC_LABEL);
    assert(field <= TORIRS_CHROME_TRUNC_TOOLTIP_CHORD);
    switch( field )
    {
    case TORIRS_CHROME_TRUNC_LABEL:
        return "label";
    case TORIRS_CHROME_TRUNC_TEXT:
        return "text";
    case TORIRS_CHROME_TRUNC_TOOLTIP:
        return "tooltip";
    case TORIRS_CHROME_TRUNC_CHORD:
        return "chord";
    default:
        return "tooltip chord";
    }
}

/** Draw one skin slot centred on (cx, cy) at its baked size times the chrome
 *  scale. @return 0 when the drawer has no image for it (caller draws flat). */
static int
dbg_push_icon(
    struct ToriRSChrome* ui, int slot, int cx, int cy, int trans, struct ToriRSChromeRect clip)
{
    int sw;
    int sh;
    int const before = ui->prim_count;

    if( slot < 0 || slot >= TORIRS_CHROME_SKIN_SLOT_COUNT || !dbg_skin_has(ui, slot) )
        return 0;
    if( !ToriRSChrome_SkinSlotSize(slot, &sw, &sh) )
    {
        sw = 16;
        sh = 16;
    }
    sw = DBG_PX(sw);
    sh = DBG_PX(sh);
    dbg_push_sprite_box(ui, cx - sw / 2, cy - sh / 2, sw, sh, slot, clip);
    if( trans && ui->prim_count > before )
        ui->prims[ui->prim_count - 1].trans = trans;
    return 1;
}

static void
dbg_push_line(
    struct ToriRSChrome* ui,
    int x0,
    int y0,
    int x1,
    int y1,
    uint32_t color,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromePrim* p = dbg_prim_push(ui);

    if( !p )
        return;
    p->kind = TORIRS_CHROME_PRIM_LINE;
    p->x = x0 < x1 ? x0 : x1;
    p->y = y0 < y1 ? y0 : y1;
    p->w = x0 < x1 ? x1 - x0 : x0 - x1;
    p->h = y0 < y1 ? y1 - y0 : y0 - y1;
    p->line_direction = ((x0 < x1) != (y0 < y1)) ? 1 : 0;
    p->line_width = 0;
    p->color = color;
    p->clip = clip;
}

static struct ToriRSChromeRect
dbg_surface_rect(struct ToriRSChrome const* ui)
{
    struct ToriRSChromeRect r;

    r.x = 0;
    r.y = 0;
    r.w = ui->surface_w > 0 ? ui->surface_w : 4096;
    r.h = ui->surface_h > 0 ? ui->surface_h : 4096;
    return r;
}

/* ---- lifecycle ------------------------------------------------------------ */

static void
dbg_ext_init(struct ToriRSChrome* ui)
{
    for( int i = 0; i < TORIRS_CHROME_VROW_POOL; i++ )
    {
        ui->vrows[i].owner = -1;
        ui->vrows[i].index = -1;
        ui->vrows[i].text[0] = '\0';
    }
    for( int i = 0; i < TORIRS_CHROME_SPAN_POOL; i++ )
        ui->spans[i].owner = -1;
    ui->span_count = 0;
    ui->graph_item_count = 0;
    ui->graph_building = -1;
    ui->event_count = 0;
    ui->event_overflow = 0;
    ui->slider_drag = -1;
    ui->graph_drag = -1;
    ui->graph_hover = -1;
    ui->graph_hover_key = 0;
    ui->split_panel = -1;
    ui->hover_since_ms = -1;
    ui->tooltip_widget = -1;
    ui->tooltip_candidate = -1;
    memset(&ui->tooltip_rect, 0, sizeof(ui->tooltip_rect));
    ui->activated_expand = 0;
    ui->taken_expand = 0;
}

void
ToriRSChrome_SetSkinAvailable(struct ToriRSChrome* ui, int slot, int available)
{
    uint64_t bit;

    assert(ui);
    assert(slot >= 0);
    assert(slot < TORIRS_CHROME_SKIN_SLOT_COUNT);
    bit = (uint64_t)1 << slot;
    if( ((ui->skin_avail & bit) != 0) == (available != 0) )
        return;
    if( available )
        ui->skin_avail |= bit;
    else
        ui->skin_avail &= ~bit;
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_dirty_panel(ui, i);
}

/* ---- the row pool (VLIST, CODE) ------------------------------------------ */

static int
dbg_vrow_reserve(struct ToriRSChrome* ui, int widget, int rows)
{
    for( int first = 0; first + rows <= TORIRS_CHROME_VROW_POOL; first++ )
    {
        int run = 0;
        while( run < rows && ui->vrows[first + run].owner < 0 )
            run++;
        if( run < rows )
        {
            first += run;
            continue;
        }
        for( int i = 0; i < rows; i++ )
        {
            ui->vrows[first + i].owner = widget;
            ui->vrows[first + i].index = -1;
            ui->vrows[first + i].text[0] = '\0';
        }
        return first;
    }
    return -1;
}

/** The pool slot that holds absolute row `row` of a VLIST/CODE: a RING over
 *  the widget's `rows` slots, so scrolling by one keeps every row that is
 *  still on screen and invalidates only the one that came into view. */
static struct ToriRSChromeVRow*
dbg_vrow_slot(struct ToriRSChrome* ui, struct ToriRSChromeWidget const* w, int row)
{
    if( w->vrow_first < 0 || w->rows <= 0 || row < 0 )
        return NULL;
    return &ui->vrows[w->vrow_first + row % w->rows];
}

static struct ToriRSChromeVRow const*
dbg_vrow_slot_const(
    struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int row)
{
    if( w->vrow_first < 0 || w->rows <= 0 || row < 0 )
        return NULL;
    return &ui->vrows[w->vrow_first + row % w->rows];
}

/** Text of row `row` if the host has filled it for that row, else NULL. */
static char const*
dbg_vrow_text(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int row)
{
    struct ToriRSChromeVRow const* slot = dbg_vrow_slot_const(ui, w, row);
    if( !slot || slot->index != row )
        return NULL;
    return slot->text;
}

/* ---- the graph pool --------------------------------------------------------- */

/** Take a graph's items out of the pool, closing the gap. */
static void
dbg_graph_release(struct ToriRSChrome* ui, int widget)
{
    struct ToriRSChromeWidget* w = &ui->widgets[widget];
    int const first = w->graph_first;
    int const count = w->graph_count;

    if( first < 0 || count <= 0 )
    {
        w->graph_first = -1;
        w->graph_count = 0;
        return;
    }
    memmove(
        &ui->graph_items[first],
        &ui->graph_items[first + count],
        (size_t)(ui->graph_item_count - first - count) * sizeof(ui->graph_items[0]));
    ui->graph_item_count -= count;
    for( int i = 0; i < ui->widget_count; i++ )
    {
        struct ToriRSChromeWidget* other = &ui->widgets[i];
        if( other->kind == TORIRS_CHROME_W_GRAPH && other->graph_first > first )
            other->graph_first -= count;
    }
    w->graph_first = -1;
    w->graph_count = 0;
}

static uint64_t
dbg_graph_hash(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    uint64_t hash = 1469598103934665603ull ^ (uint64_t)(unsigned)w->graph_count;

    for( int i = 0; i < w->graph_count; i++ )
    {
        struct ToriRSChromeGraphItem const* it = &ui->graph_items[w->graph_first + i];
        int const fields[9] = {
            it->kind, it->x, it->y, it->w, it->h, (int)it->color, it->key, it->scene_id, it->trans
        };
        for( int f = 0; f < 9; f++ )
        {
            hash ^= (uint64_t)(uint32_t)fields[f];
            hash *= 1099511628211ull;
        }
        dbg_hash_text(&hash, it->text, TORIRS_CHROME_GRAPH_TEXT_MAX);
    }
    return hash;
}

/* ---- forgetting a widget ----------------------------------------------------- */

/** Every latch and pool entry a removed widget held. Called by
 *  dbg_widget_forget, i.e. before the slot is cleared. */
static void
dbg_ext_forget(struct ToriRSChrome* ui, int widget)
{
    struct ToriRSChromeWidget* w = &ui->widgets[widget];

    if( dbg_is_vlist_kind(w->kind) && w->vrow_first >= 0 )
    {
        for( int i = 0; i < w->rows; i++ )
        {
            ui->vrows[w->vrow_first + i].owner = -1;
            ui->vrows[w->vrow_first + i].index = -1;
        }
        w->vrow_first = -1;
    }
    if( w->kind == TORIRS_CHROME_W_CODE )
        for( int i = 0; i < ui->span_count; i++ )
            if( ui->spans[i].owner == widget )
                ui->spans[i].owner = -1;
    if( w->kind == TORIRS_CHROME_W_GRAPH )
        dbg_graph_release(ui, widget);
    if( ui->graph_building == widget )
        ui->graph_building = -1;
    if( ui->slider_drag == widget )
        ui->slider_drag = -1;
    if( ui->graph_drag == widget )
        ui->graph_drag = -1;
    if( ui->graph_hover == widget )
    {
        ui->graph_hover = -1;
        ui->graph_hover_key = 0;
    }
    if( ui->tooltip_widget == widget )
    {
        dbg_damage_add(ui, ui->tooltip_rect);
        ui->tooltip_widget = -1;
        ui->dirty = 1;
    }
    if( ui->tooltip_candidate == widget )
        ui->tooltip_candidate = -1;
}

/* ---- events ------------------------------------------------------------------ */

int
ToriRSChrome_PushEvent(struct ToriRSChrome* ui, struct ToriRSChromeEvent const* event)
{
    assert(ui);
    assert(event);
    /* A drag or a hover is a POSITION, not a history: a fast sweep that queued
     * one per motion event would fill the queue with points nobody needs and
     * push out the release that ends it. The newest replaces the last. */
    if( ui->event_count > 0 &&
        (event->kind == TORIRS_CHROME_EVENT_GRAPH_DRAG ||
         event->kind == TORIRS_CHROME_EVENT_GRAPH_HOVER) )
    {
        struct ToriRSChromeEvent* last = &ui->events[ui->event_count - 1];
        if( last->kind == event->kind && last->widget == event->widget &&
            last->serial == event->serial )
        {
            *last = *event;
            return 1;
        }
    }
    if( ui->event_count >= TORIRS_CHROME_EVENT_MAX )
    {
        ui->event_overflow = 1;
        return 0;
    }
    ui->events[ui->event_count++] = *event;
    return 1;
}

int
ToriRSChrome_TakeEvent(struct ToriRSChrome* ui, struct ToriRSChromeEvent* out)
{
    assert(ui);
    assert(out);
    while( ui->event_count > 0 )
    {
        struct ToriRSChromeEvent const head = ui->events[0];
        int const live = dbg_valid_widget(ui, head.widget) &&
                         ui->widgets[head.widget].serial == head.serial;

        memmove(
            &ui->events[0],
            &ui->events[1],
            (size_t)(ui->event_count - 1) * sizeof(ui->events[0]));
        ui->event_count--;
        /* The widget went away (or its handle was recycled) since this was
         * queued: the event is about something that no longer exists. */
        if( !live )
            continue;
        *out = head;
        return 1;
    }
    memset(out, 0, sizeof(*out));
    return 0;
}

int
ToriRSChrome_TakeEventOverflow(struct ToriRSChrome* ui)
{
    int const was = ui->event_overflow;

    assert(ui);
    ui->event_overflow = 0;
    return was;
}

static void
dbg_event_init(
    struct ToriRSChromeEvent* ev, struct ToriRSChrome const* ui, int kind, int widget)
{
    memset(ev, 0, sizeof(*ev));
    ev->kind = kind;
    ev->widget = widget;
    ev->serial = ui->widgets[widget].serial;
}

/* ---- keycaps ------------------------------------------------------------------ */

/** A keycap's width for `chord`: the chord in the Small face and its air. */
static int
dbg_keycap_width(struct ToriRSChrome const* ui, char const* chord)
{
    return ToriRSChrome_MeasureTextQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q, chord) +
           2 * DBG_KEYCAP_PAD_X;
}

/**
 * `chord` as it fits `width` pixels of the Small face: whole when it fits;
 * else the key it ends on after an ellipsis ("...+1" for Ctrl+Shift+1), so a
 * cut chord never reads as a different, shorter one ("Ctrl+S"); else elided.
 * The tooltip carries it whole. Never clipped mid-glyph (rule R2).
 */
static char const*
dbg_fit_chord(struct ToriRSChrome* ui, char const* chord, int width)
{
    char buffer[TORIRS_CHROME_CHORD_MAX + 4];
    char const* plus;
    int len;

    if( ToriRSChrome_MeasureTextQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q, chord) <= width )
        return chord;
    /* The last segment: "Ctrl++" ends on the key '+', not on an empty one. */
    plus = strrchr(chord, '+');
    if( plus && plus[1] == '\0' && plus > chord && plus[-1] == '+' )
        plus--;
    if( plus && plus > chord )
    {
        len = snprintf(buffer, sizeof(buffer), "...%s", plus);
        if( len > 0 && len < (int)sizeof(buffer) &&
            ToriRSChrome_MeasureTextQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q, buffer) <= width )
        {
            char const* fitted = dbg_wrap_line(ui, buffer, len);
            if( fitted )
                return fitted;
        }
    }
    return width > 0 ? dbg_elide_text(ui, TORIRS_CHROME_FONT_SMALL, chord, width) : "";
}

/**
 * One keycap: a dark box with the grey inset rule a field wears, and the
 * chord in the Small face centred on it in `ink`.
 *
 * The chord prim borrows `chord`, which is a widget's own field, the same as
 * every caption here. The fill is the field black and the rule the field
 * inset, so a keycap reads as a key set INTO the control rather than as a
 * second button on it.
 */
static void
dbg_push_keycap(
    struct ToriRSChrome* ui,
    struct ToriRSChromeRect cap,
    char const* chord,
    uint32_t ink,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    int const line_box = ToriRSChrome_FontLineBoxQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q);
    int const ascent = ToriRSChrome_FontLineHeightQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q);
    int chord_w;

    /* A key narrower than its chord (a plate's room) shows it fitted. */
    chord = dbg_fit_chord(ui, chord, cap.w - 2 * DBG_KEYCAP_PAD_X);
    chord_w = ToriRSChrome_MeasureTextQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q, chord);

    dbg_push_rect(ui, cap.x, cap.y, cap.w, cap.h, th->input_bg, 1, clip);
    dbg_push_rect(ui, cap.x, cap.y, cap.w, cap.h, th->dropdown_border_inner, 0, clip);
    dbg_push_text(
        ui,
        cap.x + (cap.w - chord_w) / 2,
        cap.y + (cap.h - line_box) / 2 + ascent,
        chord,
        ink,
        TORIRS_CHROME_FONT_SMALL,
        0,
        dbg_rect_clip(clip, cap));
}

/* ---- tooltips ---------------------------------------------------------------- */

/** Whether `w` shows a tooltip, and its own text: the tip, an ICONBUTTON's
 *  label, or a BUTTON's caption whole when Build elided it. NULL for none.
 *  @see dbg_tooltip_compose for the text the box shows. */
static char const*
dbg_tooltip_text(struct ToriRSChromeWidget const* w)
{
    if( w->tooltip[0] )
        return w->tooltip;
    if( w->kind == TORIRS_CHROME_W_ICONBUTTON && w->label[0] )
        return w->label;
    if( w->kind == TORIRS_CHROME_W_BUTTON && w->caption_cut )
        return w->text[0] ? w->text : w->label;
    return NULL;
}

/** The tooltip's text into `out`: dbg_tooltip_text, led by the caption whole
 *  ("Redo: Redo the step undone last") when Build elided a BUTTON's caption
 *  and the button has a tip of its own. @return 0 and "" for none. */
static int
dbg_tooltip_compose(struct ToriRSChromeWidget const* w, char* out, int out_size)
{
    char const* text = dbg_tooltip_text(w);

    out[0] = '\0';
    if( !text )
        return 0;
    if( w->kind == TORIRS_CHROME_W_BUTTON && w->caption_cut && w->tooltip[0] )
        snprintf(out, (size_t)out_size, "%s: %s", w->text[0] ? w->text : w->label, w->tooltip);
    else
        snprintf(out, (size_t)out_size, "%s", text);
    return 1;
}

int
ToriRSChrome_TooltipText(struct ToriRSChrome const* ui, int widget, char* out, int out_size)
{
    assert(ui);
    assert(out);
    assert(out_size > 0);
    assert(widget >= 0);
    assert(widget < ui->widget_count);
    return dbg_tooltip_compose(&ui->widgets[widget], out, out_size);
}

/** The chord a tooltip ends with: its own, else the chord a BUTTON or
 *  ICONBUTTON wears. NULL for none. */
static char const*
dbg_tooltip_chord(struct ToriRSChromeWidget const* w)
{
    if( w->tooltip_chord[0] )
        return w->tooltip_chord;
    if( (w->kind == TORIRS_CHROME_W_BUTTON || w->kind == TORIRS_CHROME_W_ICONBUTTON) &&
        w->chord[0] )
        return w->chord;
    return NULL;
}

/** A tooltip that is up redraws when anything it shows changed. */
static void
dbg_tooltip_touched(struct ToriRSChrome* ui, int widget)
{
    if( ui->tooltip_widget != widget )
        return;
    dbg_damage_add(ui, ui->tooltip_rect);
    ui->dirty = 1;
}

/**
 * Copy a chord into `dst` (a widget's chord or tooltip chord), noting a cut.
 * @return 1 when the stored chord changed.
 */
static int
dbg_set_chord(struct ToriRSChrome* ui, int widget, char* dst, char const* chord)
{
    char buf[TORIRS_CHROME_CHORD_MAX];

    dbg_note_truncation(
        ui, widget, dbg_widget_field(&ui->widgets[widget], dst), chord, TORIRS_CHROME_CHORD_MAX);
    dbg_copy(buf, TORIRS_CHROME_CHORD_MAX, chord);
    if( strcmp(buf, dst) == 0 )
        return 0;
    memcpy(dst, buf, sizeof(buf));
    return 1;
}

void
ToriRSChrome_SetTooltipChord(struct ToriRSChrome* ui, int widget, char const* chord)
{
    assert(ui);
    assert(dbg_valid_widget(ui, widget));
    assert(chord);
    if( !dbg_set_chord(ui, widget, ui->widgets[widget].tooltip_chord, chord) )
        return;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_TOOLTIP);
    dbg_tooltip_touched(ui, widget);
}

void
ToriRSChrome_ButtonSetChord(struct ToriRSChrome* ui, int widget, char const* chord)
{
    assert(ui);
    assert(dbg_valid_widget(ui, widget));
    assert(ui->widgets[widget].kind == TORIRS_CHROME_W_BUTTON);
    assert(chord);
    if( !dbg_set_chord(ui, widget, ui->widgets[widget].chord, chord) )
        return;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_CHORD);
    /* The plate is the label column wide whatever it holds, so the keycap
     * moves no row: a repaint of this one is the whole of it. */
    dbg_dirty_widget_paint(ui, widget);
    dbg_tooltip_touched(ui, widget);
}

void
ToriRSChrome_SetTooltip(struct ToriRSChrome* ui, int widget, char const* text)
{
    char buf[TORIRS_CHROME_LABEL_MAX];
    struct ToriRSChromeWidget* w;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    dbg_note_truncation(ui, widget, TORIRS_CHROME_TRUNC_TOOLTIP, text, TORIRS_CHROME_LABEL_MAX);
    dbg_copy(buf, TORIRS_CHROME_LABEL_MAX, text ? text : "");
    if( strcmp(buf, w->tooltip) == 0 )
        return;
    memcpy(w->tooltip, buf, sizeof(buf));
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_TOOLTIP);
    dbg_tooltip_touched(ui, widget);
}

int
ToriRSChrome_TooltipWidget(struct ToriRSChrome const* ui)
{
    assert(ui);
    return ui->tooltip_widget;
}

static void
dbg_tooltip_hide(struct ToriRSChrome* ui)
{
    if( ui->tooltip_widget < 0 )
        return;
    dbg_damage_add(ui, ui->tooltip_rect);
    ui->tooltip_widget = -1;
    ui->dirty = 1;
}

void
ToriRSChrome_Tick(struct ToriRSChrome* ui, int64_t now_ms)
{
    int candidate = -1;

    assert(ui);
    ui->now_ms = now_ms;
    /* Nothing hovers while a gesture is in flight: a tooltip that popped up
     * under a dragged knob would cover the value being set. */
    if( ui->hover >= 0 && ui->press < 0 && ui->slider_drag < 0 && ui->graph_drag < 0 &&
        ui->drag_panel < 0 && ui->resize_panel < 0 && ui->split_panel < 0 &&
        dbg_valid_widget(ui, ui->hover) && dbg_tooltip_text(&ui->widgets[ui->hover]) )
        candidate = ui->hover;
    if( candidate != ui->tooltip_candidate )
    {
        ui->tooltip_candidate = candidate;
        ui->hover_since_ms = now_ms;
        if( ui->tooltip_widget != candidate )
            dbg_tooltip_hide(ui);
        return;
    }
    if( candidate >= 0 && ui->tooltip_widget != candidate &&
        now_ms - ui->hover_since_ms >= TORIRS_CHROME_TOOLTIP_DELAY_MS )
    {
        ui->tooltip_widget = candidate;
        ui->dirty = 1;
    }
}

static void
dbg_build_tooltip(struct ToriRSChrome* ui)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    struct ToriRSChromeWidget const* w;
    struct ToriRSChromeRect const surface = dbg_surface_rect(ui);
    struct ToriRSChromeRect box;
    char composed[TORIRS_CHROME_INPUT_MAX + TORIRS_CHROME_LABEL_MAX + 4];
    char const* text;
    char const* chord;
    int text_w;
    int cap_w = 0;

    if( ui->tooltip_widget < 0 )
        return;
    if( !dbg_valid_widget(ui, ui->tooltip_widget) )
    {
        ui->tooltip_widget = -1;
        return;
    }
    w = &ui->widgets[ui->tooltip_widget];
    if( !dbg_tooltip_compose(w, composed, (int)sizeof(composed)) || !dbg_widget_shown(ui, w) ||
        !ui->panels[w->panel].visible )
        return;
    /* The text prim borrows the widget's own string; only a composed one
     * ("Caption: tip") is copied to the frame's pool, past this stack. */
    text = dbg_tooltip_text(w);
    if( strcmp(composed, text) != 0 )
    {
        char const* stashed = dbg_wrap_line(ui, composed, (int)strlen(composed));
        if( stashed )
            text = stashed;
    }
    chord = dbg_tooltip_chord(w);
    text_w = ToriRSChrome_MeasureTextQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q, text);
    /* "text  [chord]": the box widens by the gap and the keycap. */
    if( chord )
        cap_w = dbg_keycap_width(ui, chord);
    box.w = text_w + (chord ? DBG_KEYCAP_GAP + cap_w : 0) + 2 * DBG_TOOLTIP_PAD;
    box.h = ToriRSChrome_FontLineBoxQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q) +
            2 * DBG_TOOLTIP_PAD;
    box.x = ui->hover_x + DBG_PX(12);
    box.y = ui->hover_y + DBG_PX(16);
    if( box.x + box.w > surface.x + surface.w )
        box.x = surface.x + surface.w - box.w;
    if( box.y + box.h > surface.y + surface.h )
        box.y = ui->hover_y - box.h - DBG_PX(2);
    if( box.x < surface.x )
        box.x = surface.x;
    if( box.y < surface.y )
        box.y = surface.y;
    /* The minimenu's own palette: body brown, black chrome, the hover ink. */
    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->menu_body, 1, surface);
    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->menu_chrome, 0, surface);
    dbg_push_text(
        ui,
        box.x + DBG_TOOLTIP_PAD,
        box.y + DBG_TOOLTIP_PAD + ToriRSChrome_FontLineHeightQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q),
        text,
        th->menu_hover_text,
        TORIRS_CHROME_FONT_SMALL,
        1,
        surface);
    if( chord )
    {
        /* The keycap sits on the text's line box: the tooltip's pad is its
         * frame, so it needs no inset of its own. */
        struct ToriRSChromeRect cap;
        cap.x = box.x + DBG_TOOLTIP_PAD + text_w + DBG_KEYCAP_GAP;
        cap.y = box.y + DBG_TOOLTIP_PAD;
        cap.w = cap_w;
        cap.h = ToriRSChrome_FontLineBoxQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q);
        dbg_push_keycap(ui, cap, chord, th->menu_hover_text, surface);
    }
    if( box.x != ui->tooltip_rect.x || box.y != ui->tooltip_rect.y ||
        box.w != ui->tooltip_rect.w || box.h != ui->tooltip_rect.h )
    {
        dbg_damage_add(ui, ui->tooltip_rect);
        dbg_damage_add(ui, box);
    }
    ui->tooltip_rect = box;
}

/* ---- TREE ----------------------------------------------------------------------- */

int
ToriRSChrome_TreeRow(
    struct ToriRSChrome* ui, int panel, char const* label, int depth, int expander)
{
    int h;

    assert(ui);
    h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_TREE);
    if( h < 0 )
        return -1;
    dbg_copy_widget(ui, h, ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    ui->widgets[h].iaux = dbg_clampi(depth, 0, 64);
    ui->widgets[h].ivalue = dbg_clampi(expander, TORIRS_CHROME_TREE_LEAF, TORIRS_CHROME_TREE_OPEN);
    return h;
}

void
ToriRSChrome_TreeSetExpander(struct ToriRSChrome* ui, int widget, int expander)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_TREE )
        return;
    expander = dbg_clampi(expander, TORIRS_CHROME_TREE_LEAF, TORIRS_CHROME_TREE_OPEN);
    if( ui->widgets[widget].ivalue == expander )
        return;
    ui->widgets[widget].ivalue = expander;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_VALUE);
    dbg_dirty_widget_paint(ui, widget);
}

int
ToriRSChrome_TreeExpander(struct ToriRSChrome const* ui, int widget)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_TREE )
        return TORIRS_CHROME_TREE_LEAF;
    return ui->widgets[widget].ivalue;
}

void
ToriRSChrome_TreeSelect(struct ToriRSChrome* ui, int widget)
{
    int panel;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_TREE )
        return;
    panel = ui->widgets[widget].panel;
    for( int i = ui->panels[panel].first_widget; i >= 0; i = ui->widgets[i].next )
        if( i != widget && ui->widgets[i].kind == TORIRS_CHROME_W_TREE && ui->widgets[i].checked )
            ToriRSChrome_SetChecked(ui, i, 0);
    ToriRSChrome_SetChecked(ui, widget, 1);
}

int
ToriRSChrome_ActivationWasExpand(struct ToriRSChrome const* ui)
{
    assert(ui);
    return ui->taken_expand;
}

/* ---- VLIST / CODE ----------------------------------------------------------------- */

static int
dbg_vlist_add(
    struct ToriRSChrome* ui, int panel, int kind, char const* label, int rows, int count)
{
    int h;
    int first;

    h = dbg_widget_add(ui, panel, kind);
    if( h < 0 )
        return -1;
    rows = dbg_clampi(rows, 1, TORIRS_CHROME_VROW_WINDOW_MAX);
    first = dbg_vrow_reserve(ui, h, rows);
    if( first < 0 )
    {
        /* The pool is the page's budget, like the option pool: out of it, the
         * widget is not made rather than made with a window it cannot fill. */
        ToriRSChrome_WidgetRemove(ui, h);
        ui->overflow = 1;
        return -1;
    }
    ui->widgets[h].vrow_first = first;
    ui->widgets[h].rows = rows;
    ui->widgets[h].imax = count < 0 ? 0 : count;
    ui->widgets[h].imin = 0;
    ui->widgets[h].ivalue = kind == TORIRS_CHROME_W_VLIST ? -1 : 0;
    dbg_copy_widget(ui, h, ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    return h;
}

int
ToriRSChrome_VList(
    struct ToriRSChrome* ui, int panel, char const* label, int rows, int row_count)
{
    assert(ui);
    return dbg_vlist_add(ui, panel, TORIRS_CHROME_W_VLIST, label, rows, row_count);
}

int
ToriRSChrome_VListCells(
    struct ToriRSChrome* ui, int panel, char const* label, int rows, int row_count)
{
    int h;

    assert(ui);
    h = dbg_vlist_add(ui, panel, TORIRS_CHROME_W_VLIST, label, rows, row_count);
    if( h >= 0 )
        ui->widgets[h].iaux = TORIRS_CHROME_VLIST_CELLS; /* shape, never changed */
    return h;
}

int
ToriRSChrome_VListCellsWide(
    struct ToriRSChrome* ui, int panel, char const* label, int rows, int row_count)
{
    int h;

    assert(ui);
    h = dbg_vlist_add(ui, panel, TORIRS_CHROME_W_VLIST, label, rows, row_count);
    if( h >= 0 )
        ui->widgets[h].iaux = TORIRS_CHROME_VLIST_CELLS | TORIRS_CHROME_VLIST_CELLS_WIDE;
    return h;
}

int
ToriRSChrome_Code(
    struct ToriRSChrome* ui, int panel, char const* label, int rows, int line_count)
{
    assert(ui);
    return dbg_vlist_add(ui, panel, TORIRS_CHROME_W_CODE, label, rows, line_count);
}

static int
dbg_vlist_max_first(struct ToriRSChromeWidget const* w)
{
    int const max_first = w->imax - w->rows;
    return max_first > 0 ? max_first : 0;
}

void
ToriRSChrome_VListSetCount(struct ToriRSChrome* ui, int widget, int row_count)
{
    struct ToriRSChromeWidget* w;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || !dbg_is_vlist_kind(ui->widgets[widget].kind) )
        return;
    w = &ui->widgets[widget];
    row_count = row_count < 0 ? 0 : row_count;
    if( w->imax == row_count )
        return;
    w->imax = row_count;
    w->imin = dbg_clampi(w->imin, 0, dbg_vlist_max_first(w));
    if( w->kind == TORIRS_CHROME_W_VLIST && w->ivalue >= row_count )
        w->ivalue = -1;
    if( w->kind == TORIRS_CHROME_W_CODE )
        w->ivalue = dbg_clampi(w->ivalue, 0, row_count > 0 ? row_count - 1 : 0);
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_VALUE);
    dbg_dirty_widget_paint(ui, widget);
}

void
ToriRSChrome_VListSetScroll(struct ToriRSChrome* ui, int widget, int first)
{
    struct ToriRSChromeWidget* w;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || !dbg_is_vlist_kind(ui->widgets[widget].kind) )
        return;
    w = &ui->widgets[widget];
    first = dbg_clampi(first, 0, dbg_vlist_max_first(w));
    if( w->imin == first )
        return;
    w->imin = first;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_VALUE);
    dbg_dirty_widget_paint(ui, widget);
}

void
ToriRSChrome_VListSetSelected(struct ToriRSChrome* ui, int widget, int row)
{
    struct ToriRSChromeWidget* w;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_VLIST )
        return;
    w = &ui->widgets[widget];
    row = row < 0 || row >= w->imax ? -1 : row;
    if( w->ivalue == row )
        return;
    w->ivalue = row;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_VALUE);
    dbg_dirty_widget_paint(ui, widget);
}

int
ToriRSChrome_VListSelected(struct ToriRSChrome const* ui, int widget)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_VLIST )
        return -1;
    return ui->widgets[widget].ivalue;
}

int
ToriRSChrome_VListWindow(
    struct ToriRSChrome const* ui, int widget, int* out_first, int* out_count)
{
    struct ToriRSChromeWidget const* w;
    int count;

    assert(ui);
    if( out_first )
        *out_first = 0;
    if( out_count )
        *out_count = 0;
    if( !dbg_valid_widget(ui, widget) || !dbg_is_vlist_kind(ui->widgets[widget].kind) )
        return 0;
    w = &ui->widgets[widget];
    count = w->imax - w->imin;
    if( count > w->rows )
        count = w->rows;
    if( count < 0 )
        count = 0;
    if( out_first )
        *out_first = w->imin;
    if( out_count )
        *out_count = count;
    return count > 0;
}

int
ToriRSChrome_VListNeedsRows(struct ToriRSChrome const* ui, int widget)
{
    int first;
    int count;

    assert(ui);
    if( !ToriRSChrome_VListWindow(ui, widget, &first, &count) )
        return 0;
    for( int r = first; r < first + count; r++ )
        if( !dbg_vrow_text(ui, &ui->widgets[widget], r) )
            return 1;
    return 0;
}

void
ToriRSChrome_VListSetRow(struct ToriRSChrome* ui, int widget, int row, char const* text)
{
    struct ToriRSChromeWidget* w;
    struct ToriRSChromeVRow* slot;
    char buf[TORIRS_CHROME_INPUT_MAX];

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || !dbg_is_vlist_kind(ui->widgets[widget].kind) )
        return;
    w = &ui->widgets[widget];
    /* Off-screen rows are not retained: that is the whole point of the
     * widget, and a host that fills more than the window is told nothing. */
    if( row < w->imin || row >= w->imin + w->rows || row >= w->imax )
        return;
    slot = dbg_vrow_slot(ui, w, row);
    if( !slot )
        return;
    dbg_note_truncation(ui, widget, TORIRS_CHROME_TRUNC_TEXT, text, TORIRS_CHROME_INPUT_MAX);
    dbg_copy(buf, TORIRS_CHROME_INPUT_MAX, text ? text : "");
    if( slot->index == row && strcmp(slot->text, buf) == 0 )
        return;
    slot->index = row;
    memcpy(slot->text, buf, sizeof(buf));
    w->rows_rev++;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_ROWS);
    dbg_dirty_widget_paint(ui, widget);
}

void
ToriRSChrome_CodeSetLine(struct ToriRSChrome* ui, int widget, int line, char const* text)
{
    ToriRSChrome_VListSetRow(ui, widget, line, text);
}

void
ToriRSChrome_CodeSetCaret(struct ToriRSChrome* ui, int widget, int line, int col)
{
    struct ToriRSChromeWidget* w;
    int first;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_CODE )
        return;
    w = &ui->widgets[widget];
    line = dbg_clampi(line, 0, w->imax > 0 ? w->imax - 1 : 0);
    col = col < 0 ? 0 : col;
    first = w->imin;
    if( line < first )
        first = line;
    if( line >= first + w->rows )
        first = line - w->rows + 1;
    first = dbg_clampi(first, 0, dbg_vlist_max_first(w));
    if( w->ivalue == line && w->caret == col && w->imin == first )
        return;
    w->ivalue = line;
    w->caret = col;
    w->imin = first;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_VALUE);
    dbg_dirty_widget_paint(ui, widget);
}

void
ToriRSChrome_CodeClearSpans(struct ToriRSChrome* ui, int widget)
{
    int removed = 0;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_CODE )
        return;
    for( int i = 0; i < ui->span_count; i++ )
        if( ui->spans[i].owner == widget )
        {
            ui->spans[i].owner = -1;
            removed = 1;
        }
    if( !removed )
        return;
    ui->widgets[widget].rows_rev++;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_ROWS);
    dbg_dirty_widget_paint(ui, widget);
}

int
ToriRSChrome_CodeSetLineSpans(
    struct ToriRSChrome* ui,
    int widget,
    int line,
    struct ToriRSChromeSpanInput const* spans,
    int count)
{
    int have = 0;
    int same = 1;
    int free_slots = 0;

    assert(ui);
    if( count > 0 )
        assert(spans);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_CODE )
        return 0;
    count = count < 0 ? 0 : count;

    /* Same spans in the same order is no change at all. */
    for( int i = 0; i < ui->span_count; i++ )
    {
        struct ToriRSChromeSpan const* s = &ui->spans[i];
        if( s->owner != widget || s->line != line )
            continue;
        if( have >= count || s->start != spans[have].start || s->len != spans[have].len ||
            s->color != spans[have].color )
            same = 0;
        have++;
    }
    if( same && have == count )
        return 1;

    for( int i = 0; i < TORIRS_CHROME_SPAN_POOL; i++ )
        if( ui->spans[i].owner < 0 ||
            (ui->spans[i].owner == widget && ui->spans[i].line == line) )
            free_slots++;
    if( free_slots < count )
    {
        ui->overflow = 1;
        return 0;
    }

    for( int i = 0; i < ui->span_count; i++ )
        if( ui->spans[i].owner == widget && ui->spans[i].line == line )
            ui->spans[i].owner = -1;
    for( int n = 0, i = 0; n < count && i < TORIRS_CHROME_SPAN_POOL; i++ )
    {
        if( ui->spans[i].owner >= 0 )
            continue;
        ui->spans[i].owner = widget;
        ui->spans[i].line = line;
        ui->spans[i].start = spans[n].start < 0 ? 0 : spans[n].start;
        ui->spans[i].len = spans[n].len < 0 ? 0 : spans[n].len;
        ui->spans[i].color = spans[n].color;
        if( i >= ui->span_count )
            ui->span_count = i + 1;
        n++;
    }
    ui->widgets[widget].rows_rev++;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_ROWS);
    dbg_dirty_widget_paint(ui, widget);
    return 1;
}

/* ---- ICONBUTTON ----------------------------------------------------------------- */

int
ToriRSChrome_IconButton(struct ToriRSChrome* ui, int panel, int skin_slot, char const* label)
{
    int h;

    assert(ui);
    h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_ICONBUTTON);
    if( h < 0 )
        return -1;
    ui->widgets[h].ivalue = skin_slot;
    dbg_copy_widget(ui, h, ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    return h;
}

void
ToriRSChrome_IconButtonSetIcon(struct ToriRSChrome* ui, int widget, int skin_slot)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) ||
        ui->widgets[widget].kind != TORIRS_CHROME_W_ICONBUTTON ||
        ui->widgets[widget].ivalue == skin_slot )
        return;
    ui->widgets[widget].ivalue = skin_slot;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_VALUE);
    dbg_dirty_widget_paint(ui, widget);
}

void
ToriRSChrome_IconButtonSetChord(struct ToriRSChrome* ui, int widget, char const* chord)
{
    assert(ui);
    assert(dbg_valid_widget(ui, widget));
    assert(ui->widgets[widget].kind == TORIRS_CHROME_W_ICONBUTTON);
    assert(chord);
    if( !dbg_set_chord(ui, widget, ui->widgets[widget].chord, chord) )
        return;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_CHORD);
    dbg_dirty_widget_paint(ui, widget);
    dbg_tooltip_touched(ui, widget);
}

/* ---- SLIDER ---------------------------------------------------------------------- */

static int
dbg_slider_snap(struct ToriRSChromeWidget const* w, int value)
{
    int const step = w->istep > 0 ? w->istep : 1;

    value = dbg_clampi(value, w->imin, w->imax);
    value = w->imin + ((value - w->imin + step / 2) / step) * step;
    return dbg_clampi(value, w->imin, w->imax);
}

int
ToriRSChrome_Slider(
    struct ToriRSChrome* ui,
    int panel,
    char const* label,
    int min,
    int max,
    int value,
    int step)
{
    int h;

    assert(ui);
    assert(max >= min);
    h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_SLIDER);
    if( h < 0 )
        return -1;
    dbg_copy_widget(ui, h, ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    ui->widgets[h].imin = min;
    ui->widgets[h].imax = max;
    ui->widgets[h].istep = step > 0 ? step : 1;
    ui->widgets[h].ivalue = dbg_slider_snap(&ui->widgets[h], value);
    return h;
}

void
ToriRSChrome_SliderSetRange(struct ToriRSChrome* ui, int widget, int min, int max, int step)
{
    struct ToriRSChromeWidget* w;

    assert(ui);
    assert(max >= min);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_SLIDER )
        return;
    w = &ui->widgets[widget];
    step = step > 0 ? step : 1;
    if( w->imin == min && w->imax == max && w->istep == step )
        return;
    w->imin = min;
    w->imax = max;
    w->istep = step;
    w->ivalue = dbg_slider_snap(w, w->ivalue);
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_VALUE);
    dbg_dirty_widget_paint(ui, widget);
}

void
ToriRSChrome_SliderSet(struct ToriRSChrome* ui, int widget, int value)
{
    struct ToriRSChromeWidget* w;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_SLIDER )
        return;
    w = &ui->widgets[widget];
    value = dbg_slider_snap(w, value);
    if( w->ivalue == value )
        return;
    w->ivalue = value;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_VALUE);
    dbg_dirty_widget_paint(ui, widget);
}

int
ToriRSChrome_SliderValue(struct ToriRSChrome const* ui, int widget)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_SLIDER )
        return 0;
    return ui->widgets[widget].ivalue;
}

/**
 * A slider's caption column: the row's fixed label column, narrowed to
 * DBG_SLIDER_LABEL_PERCENT of the row, but never narrower than the caption
 * itself plus a gap. Stacked and unlabelled rows keep what every row gets.
 */
static int
dbg_slider_label_w(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    int const fixed = dbg_row_box_offset(ui, w);
    int const share = w->w * DBG_SLIDER_LABEL_PERCENT / 100;
    int text;

    if( fixed <= 0 || w->w <= 0 || share >= fixed )
        return fixed;
    text = ToriRSChrome_MeasureTextQ(ui->theme.font_row, ui->scale_q, w->label) + DBG_SLIDER_LABEL_GAP;
    if( text > fixed )
        return fixed;
    return share > text ? share : text;
}

/** The track's box in a laid-out slider row. */
static struct ToriRSChromeRect
dbg_slider_track(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    struct ToriRSChromeRect r;

    r.x = w->x + dbg_slider_label_w(ui, w);
    r.y = w->y;
    r.w = w->x + w->w - DBG_SLIDER_VALUE_W - r.x;
    r.h = DBG_ROW_H;
    if( r.w < DBG_KNOB )
        r.w = DBG_KNOB;
    return r;
}

static int
dbg_slider_value_at(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int x)
{
    struct ToriRSChromeRect const t = dbg_slider_track(ui, w);
    int const travel = t.w - DBG_KNOB;
    int const span = w->imax - w->imin;
    int along;

    if( travel <= 0 || span <= 0 )
        return w->imin;
    along = dbg_clampi(x - t.x - DBG_KNOB / 2, 0, travel);
    return dbg_slider_snap(w, w->imin + (int)(((int64_t)along * span + travel / 2) / travel));
}

/* ---- RADIO ------------------------------------------------------------------------ */

int
ToriRSChrome_Radio(
    struct ToriRSChrome* ui, int panel, char const* label, int group, int checked)
{
    int h;

    assert(ui);
    h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_RADIO);
    if( h < 0 )
        return -1;
    dbg_copy_widget(ui, h, ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    ui->widgets[h].iaux = group;
    if( checked )
        ToriRSChrome_RadioSelect(ui, h);
    return h;
}

void
ToriRSChrome_RadioSelect(struct ToriRSChrome* ui, int widget)
{
    struct ToriRSChromeWidget const* w;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_RADIO )
        return;
    w = &ui->widgets[widget];
    for( int i = ui->panels[w->panel].first_widget; i >= 0; i = ui->widgets[i].next )
    {
        struct ToriRSChromeWidget const* o = &ui->widgets[i];
        if( i != widget && o->kind == TORIRS_CHROME_W_RADIO && o->iaux == w->iaux && o->checked )
            ToriRSChrome_SetChecked(ui, i, 0);
    }
    ToriRSChrome_SetChecked(ui, widget, 1);
}

/* ---- GRAPH ------------------------------------------------------------------------ */

int
ToriRSChrome_Graph(struct ToriRSChrome* ui, int panel, char const* label, int height)
{
    int h;

    assert(ui);
    h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_GRAPH);
    if( h < 0 )
        return -1;
    dbg_copy_widget(ui, h, ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    ui->widgets[h].view_h = dbg_custom_height(ui, height);
    ui->widgets[h].graph_first = -1;
    ui->widgets[h].graph_count = 0;
    ui->widgets[h].graph_hash = dbg_graph_hash(ui, &ui->widgets[h]);
    return h;
}

void
ToriRSChrome_GraphBegin(struct ToriRSChrome* ui, int widget)
{
    assert(ui);
    /* One picture at a time: an interleaved Begin would move the first
     * graph's half-listed items out from under it. */
    assert(ui->graph_building < 0);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_GRAPH )
        return;
    dbg_graph_release(ui, widget);
    ui->widgets[widget].graph_first = ui->graph_item_count;
    ui->widgets[widget].graph_count = 0;
    ui->graph_building = widget;
}

static struct ToriRSChromeGraphItem*
dbg_graph_append(struct ToriRSChrome* ui, int widget)
{
    struct ToriRSChromeGraphItem* it;

    assert(ui->graph_building == widget);
    if( ui->graph_item_count >= TORIRS_CHROME_GRAPH_ITEMS_MAX )
    {
        ui->overflow = 1;
        return NULL;
    }
    it = &ui->graph_items[ui->graph_item_count++];
    memset(it, 0, sizeof(*it));
    ui->widgets[widget].graph_count++;
    return it;
}

int
ToriRSChrome_GraphRect(
    struct ToriRSChrome* ui,
    int widget,
    int x,
    int y,
    int w,
    int h,
    uint32_t color,
    int filled,
    int key)
{
    struct ToriRSChromeGraphItem* it;

    assert(ui);
    if( ui->graph_building != widget )
        return 0;
    it = dbg_graph_append(ui, widget);
    if( !it )
        return 0;
    it->kind = filled ? TORIRS_CHROME_GRAPH_RECT : TORIRS_CHROME_GRAPH_RECT_OUTLINE;
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->color = color;
    it->key = key;
    return 1;
}

int
ToriRSChrome_GraphLine(
    struct ToriRSChrome* ui, int widget, int x0, int y0, int x1, int y1, uint32_t color)
{
    struct ToriRSChromeGraphItem* it;

    assert(ui);
    if( ui->graph_building != widget )
        return 0;
    it = dbg_graph_append(ui, widget);
    if( !it )
        return 0;
    it->kind = TORIRS_CHROME_GRAPH_LINE;
    it->x = x0;
    it->y = y0;
    it->w = x1 - x0;
    it->h = y1 - y0;
    it->color = color;
    return 1;
}

int
ToriRSChrome_GraphText(
    struct ToriRSChrome* ui, int widget, int x, int baseline, uint32_t color, char const* text)
{
    struct ToriRSChromeGraphItem* it;

    assert(ui);
    if( ui->graph_building != widget )
        return 0;
    it = dbg_graph_append(ui, widget);
    if( !it )
        return 0;
    it->kind = TORIRS_CHROME_GRAPH_TEXT;
    it->x = x;
    it->y = baseline;
    it->color = color;
    dbg_copy(it->text, TORIRS_CHROME_GRAPH_TEXT_MAX, text ? text : "");
    return 1;
}

int
ToriRSChrome_GraphImage(
    struct ToriRSChrome* ui, int widget, int x, int y, int w, int h, int scene_id, int trans, uint32_t version)
{
    struct ToriRSChromeGraphItem* it;

    assert(ui);
    assert(scene_id > 0);
    if( ui->graph_building != widget )
        return 0;
    it = dbg_graph_append(ui, widget);
    if( !it )
        return 0;
    it->kind = TORIRS_CHROME_GRAPH_IMAGE;
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->scene_id = scene_id;
    it->trans = trans < 0 ? 0 : trans > 255 ? 255 : trans;
    it->color = version;
    return 1;
}

void
ToriRSChrome_GraphEnd(struct ToriRSChrome* ui, int widget)
{
    struct ToriRSChromeWidget* w;
    uint64_t hash;

    assert(ui);
    if( ui->graph_building != widget )
        return;
    ui->graph_building = -1;
    w = &ui->widgets[widget];
    if( w->graph_count == 0 )
        w->graph_first = -1;
    hash = dbg_graph_hash(ui, w);
    if( hash == w->graph_hash )
        return;
    w->graph_hash = hash;
    w->rows_rev++;
    dbg_change_widget(ui, widget, TORIRS_CHROME_CHANGE_WIDGET_ROWS);
    dbg_dirty_widget_paint(ui, widget);
}

int
ToriRSChrome_GraphKeyAt(struct ToriRSChrome const* ui, int widget, int x, int y)
{
    struct ToriRSChromeWidget const* w;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_GRAPH )
        return 0;
    w = &ui->widgets[widget];
    for( int i = w->graph_count - 1; i >= 0; i-- )
    {
        struct ToriRSChromeGraphItem const* it = &ui->graph_items[w->graph_first + i];
        if( it->key == 0 || (it->kind != TORIRS_CHROME_GRAPH_RECT &&
                             it->kind != TORIRS_CHROME_GRAPH_RECT_OUTLINE) )
            continue;
        if( x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h )
            return it->key;
    }
    return 0;
}

/** The well's inner box and its visible part, as laid out by the last Build.
 *  @return 0 when the row is offscreen or not a graph. */
static int
dbg_graph_region(
    struct ToriRSChrome const* ui,
    int widget,
    struct ToriRSChromeRect* out_region,
    struct ToriRSChromeRect* out_clip)
{
    struct ToriRSChromeWidget const* w = &ui->widgets[widget];

    if( w->kind != TORIRS_CHROME_W_GRAPH || w->custom_clip_w <= 0 || w->custom_clip_h <= 0 )
        return 0;
    /* The region's origin is the well's own corner, not the visible part's:
     * a well scrolled half off the top still counts from its real top. */
    out_region->x = w->x + DBG_RULE;
    out_region->y = w->y + (w->label[0] ? DBG_ROW_H : 0) + DBG_RULE;
    out_region->w = w->w - 2 * DBG_RULE;
    out_region->h = w->view_h;
    out_clip->x = w->custom_clip_x;
    out_clip->y = w->custom_clip_y;
    out_clip->w = w->custom_clip_w;
    out_clip->h = w->custom_clip_h;
    return 1;
}

/** The scale (quarter units) a GRAPH well's items are drawn at: the integer
 *  tier (dbg_draw_graph's `ui->scale`), so a well's pointer maps back to
 *  item coordinates at the same factor its items were drawn at. At a
 *  fractional q this is not q: mapping with q made a press, hover, drag or
 *  key land on another item than the one under the pointer. */
static int
dbg_graph_q(struct ToriRSChrome const* ui)
{
    return ui->scale * TORIRS_CHROME_SCALE_Q_ONE;
}

static int
dbg_graph_push(struct ToriRSChrome* ui, int widget, int kind, int x, int y, int value)
{
    struct ToriRSChromeRect region;
    struct ToriRSChromeRect clip;
    struct ToriRSChromeEvent ev;
    int const scale = dbg_graph_q(ui);

    if( !dbg_graph_region(ui, widget, &region, &clip) )
        return 0;
    dbg_event_init(&ev, ui, kind, widget);
    ev.x = ToriRSChrome_UnscalePx(scale, x - region.x);
    ev.y = ToriRSChrome_UnscalePx(scale, y - region.y);
    ev.key = ToriRSChrome_GraphKeyAt(ui, widget, ev.x, ev.y);
    ev.value = value;
    return ToriRSChrome_PushEvent(ui, &ev);
}

/** A key for the focused well: TORIRS_CHROME_EVENT_GRAPH_KEY, no position. */
static void
dbg_graph_key(struct ToriRSChrome* ui, int widget, int value)
{
    struct ToriRSChromeEvent ev;

    dbg_event_init(&ev, ui, TORIRS_CHROME_EVENT_GRAPH_KEY, widget);
    ev.value = value;
    (void)ToriRSChrome_PushEvent(ui, &ev);
}

/* ---- MODAL ------------------------------------------------------------------------- */

int
ToriRSChrome_ModalPanel(struct ToriRSChrome const* ui)
{
    assert(ui);
    for( int i = ui->panel_count - 1; i >= 0; i-- )
        if( ui->panels[i].visible && ui->panels[i].modal )
            return i;
    return -1;
}

static void
dbg_damage_surface(struct ToriRSChrome* ui)
{
    dbg_damage_add(ui, dbg_surface_rect(ui));
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_dirty_panel(ui, i);
}

/* ---- MAXIMISE ---------------------------------------------------------------------- */

/** A press on `panel`'s title: 1 when it completes a double-click (latched
 *  for ToriRSChrome_TakeTitleDoubleClick), else it is remembered as the
 *  first half of one. */
static int
dbg_title_press(struct ToriRSChrome* ui, int panel, int x, int y)
{
    int const slop = DBG_PX(TORIRS_CHROME_DOUBLE_CLICK_SLOP);

    /* No clock (a host that never calls ToriRSChrome_Tick), no double-click:
     * every press would be at the same instant. */
    if( ui->now_ms <= 0 )
        return 0;
    if( ui->title_press_panel == panel && ui->now_ms - ui->title_press_ms <= TORIRS_CHROME_DOUBLE_CLICK_MS &&
        ui->now_ms >= ui->title_press_ms && abs(x - ui->title_press_x) <= slop &&
        abs(y - ui->title_press_y) <= slop )
    {
        ui->title_press_panel = -1;
        ui->title_double = panel;
        return 1;
    }
    ui->title_press_panel = panel;
    ui->title_press_ms = ui->now_ms;
    ui->title_press_x = x;
    ui->title_press_y = y;
    return 0;
}

int
ToriRSChrome_TakeTitleDoubleClick(struct ToriRSChrome* ui)
{
    int panel;

    assert(ui);
    panel = ui->title_double;
    ui->title_double = -1;
    return panel;
}

int
ToriRSChrome_PanelIsMaximised(struct ToriRSChrome const* ui, int panel)
{
    assert(ui);
    assert(dbg_valid_panel(ui, panel));
    return ui->panels[panel].maximised;
}

int
ToriRSChrome_PanelRestoreBox(
    struct ToriRSChrome const* ui,
    int panel,
    int* out_x,
    int* out_y,
    int* out_fixed_w,
    int* out_fixed_h,
    int* out_user_placed)
{
    struct ToriRSChromePanel const* p;

    assert(ui);
    assert(dbg_valid_panel(ui, panel));
    assert(out_x);
    assert(out_y);
    assert(out_fixed_w);
    assert(out_fixed_h);
    assert(out_user_placed);
    p = &ui->panels[panel];
    if( !p->maximised )
        return 0;
    *out_x = p->restore_x;
    *out_y = p->restore_y;
    *out_fixed_w = p->restore_fixed_w;
    *out_fixed_h = p->restore_fixed_h;
    *out_user_placed = p->restore_user_placed;
    return 1;
}

void
ToriRSChrome_PanelRestore(struct ToriRSChrome* ui, int panel)
{
    struct ToriRSChromePanel* p;

    assert(ui);
    assert(dbg_valid_panel(ui, panel));
    p = &ui->panels[panel];
    if( !p->maximised )
        return;
    p->maximised = 0;
    ToriRSChrome_PanelSetFixedWidth(ui, panel, p->restore_fixed_w);
    ToriRSChrome_PanelSetFixedHeight(ui, panel, p->restore_fixed_h);
    ToriRSChrome_PanelSetScrollable(ui, panel, p->restore_scrollable);
    ToriRSChrome_PanelMove(ui, panel, p->restore_x, p->restore_y);
    p->user_placed = p->restore_user_placed;
    /* Everything it covered shows again. */
    dbg_damage_surface(ui);
}

void
ToriRSChrome_PanelMaximise(struct ToriRSChrome* ui, int panel, struct ToriRSChromeRect area)
{
    struct ToriRSChromePanel* p;

    assert(ui);
    assert(dbg_valid_panel(ui, panel));
    assert(area.w > 0);
    assert(area.h > 0);
    for( int i = 0; i < ui->panel_count; i++ )
        if( i != panel && ui->panels[i].maximised )
            ToriRSChrome_PanelRestore(ui, i);
    p = &ui->panels[panel];
    if( !p->maximised )
    {
        p->restore_rect = p->last_rect;
        p->restore_x = p->x;
        p->restore_y = p->y;
        p->restore_fixed_w = p->fixed_w;
        p->restore_fixed_h = p->fixed_h;
        p->restore_user_placed = p->user_placed;
        p->restore_scrollable = p->scrollable;
        p->maximised = 1;
        dbg_damage_surface(ui);
    }
    ToriRSChrome_PanelSetFixedWidth(ui, panel, area.w);
    ToriRSChrome_PanelSetFixedHeight(ui, panel, area.h);
    /* A hand-set height drops rows past the bottom unless it scrolls. */
    ToriRSChrome_PanelSetScrollable(ui, panel, 1);
    ToriRSChrome_PanelMove(ui, panel, area.x, area.y);
    /* A host that lays its windows out itself must leave this one alone. */
    p->user_placed = 1;
}

/**
 * The drag of a maximised panel's title: still inside the slop it does not
 * move; past it the panel is restored first, under the pointer at the same
 * fraction of its width the press was at, keeping the press's height in the
 * title. @return 1 when the drag should move the panel now.
 */
static int
dbg_unmaximise_for_drag(struct ToriRSChrome* ui, int x, int y)
{
    int const panel = ui->drag_panel;
    struct ToriRSChromePanel* p = &ui->panels[panel];
    int const press_x = ui->drag_from_x + ui->drag_grab_x;
    int const press_y = ui->drag_from_y + ui->drag_grab_y;
    int const slop = DBG_PX(TORIRS_CHROME_DOUBLE_CLICK_SLOP);
    struct ToriRSChromeRect const maxed = p->last_rect;
    int restored_w;

    if( !p->maximised )
        return 1;
    if( abs(x - press_x) <= slop && abs(y - press_y) <= slop )
        return 0;
    restored_w = p->restore_fixed_w > 0 ? p->restore_fixed_w : p->restore_rect.w;
    ToriRSChrome_PanelRestore(ui, panel);
    if( maxed.w > 0 && restored_w > 0 )
        ui->drag_grab_x = (int)((long)(press_x - maxed.x) * restored_w / maxed.w);
    ui->drag_grab_y = press_y - maxed.y;
    /* Picked up from where it was restored to, so the release counts it as
     * placed only by where it is then dropped. */
    ui->drag_from_x = press_x - ui->drag_grab_x;
    ui->drag_from_y = press_y - ui->drag_grab_y;
    return 1;
}

/** The edge resize of a maximised panel: the same slop, then restored first
 *  and resized from the restored box by the pointer's travel. */
static int
dbg_unmaximise_for_resize(struct ToriRSChrome* ui, int x, int y)
{
    int const panel = ui->resize_panel;
    struct ToriRSChromePanel* p = &ui->panels[panel];
    int const slop = DBG_PX(TORIRS_CHROME_DOUBLE_CLICK_SLOP);

    if( !p->maximised )
        return 1;
    if( abs(x - ui->resize_press_x) <= slop && abs(y - ui->resize_press_y) <= slop )
        return 0;
    ToriRSChrome_PanelRestore(ui, panel);
    ui->resize_from_x = p->restore_x;
    ui->resize_from_y = p->restore_y;
    ui->resize_from_w = p->restore_fixed_w > 0 ? p->restore_fixed_w : p->restore_rect.w;
    ui->resize_from_h = p->restore_fixed_h > 0 ? p->restore_fixed_h : p->restore_rect.h;
    return 1;
}

void
ToriRSChrome_PanelSetModal(struct ToriRSChrome* ui, int panel, int modal)
{
    struct ToriRSChromePanel* p;

    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    p = &ui->panels[panel];
    modal = modal ? 1 : 0;
    if( p->modal == modal )
        return;
    p->modal = modal;
    p->modal_centred = modal;
    /* The keyboard goes with the pointer: a field behind the veil that kept
     * focus would take the keys typed at the dialog in front of it. */
    if( modal && ui->focus >= 0 && ui->widgets[ui->focus].panel != panel )
        dbg_focus_release(ui);
    dbg_change_panel(ui, panel, TORIRS_CHROME_CHANGE_PANEL_MODE);
    dbg_damage_surface(ui);
}

int
ToriRSChrome_ModalAdd(struct ToriRSChrome* ui, char const* title, int width)
{
    int panel;

    assert(ui);
    panel = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, width, title);
    if( panel < 0 )
        return -1;
    ToriRSChrome_PanelSetClosable(ui, panel, 1);
    ToriRSChrome_PanelSetModal(ui, panel, 1);
    return panel;
}

/**
 * Whether a modal panel blocks this point: there is one, and the point is
 * neither on it nor on a popup it opened. A blocked point is consumed into
 * nothing -- which is what a dialog means.
 */
static int
dbg_modal_blocks(struct ToriRSChrome const* ui, int x, int y)
{
    int const modal = ToriRSChrome_ModalPanel(ui);
    struct ToriRSChromePanel const* p;

    if( modal < 0 )
        return 0;
    p = &ui->panels[modal];
    if( dbg_point_in(x, y, p->last_rect.x, p->last_rect.y, p->last_rect.w, p->last_rect.h) )
        return 0;
    if( ui->dropdown_open >= 0 )
    {
        struct ToriRSChromeRect const r = dbg_dropdown_rect(ui);
        if( r.w > 0 && dbg_point_in_rect(x, y, r) )
            return 0;
    }
    if( ui->colorpick_open >= 0 && dbg_point_in_rect(x, y, dbg_colorpick_rect(ui)) )
        return 0;
    return 1;
}

/* ---- docking and splitters ------------------------------------------------------- */

void
ToriRSChrome_PanelSetDock(struct ToriRSChrome* ui, int panel, int dock, int size)
{
    struct ToriRSChromePanel* p;

    assert(ui);
    assert(dock >= TORIRS_CHROME_DOCK_NONE);
    assert(dock <= TORIRS_CHROME_DOCK_FILL);
    if( !dbg_valid_panel(ui, panel) )
        return;
    p = &ui->panels[panel];
    size = size < 0 ? 0 : size;
    if( p->dock == dock && p->dock_size == size )
        return;
    p->dock = dock;
    p->dock_size = size;
    if( p->dock_min <= 0 )
        p->dock_min = DBG_DOCK_MIN;
    if( dock == TORIRS_CHROME_DOCK_NONE )
    {
        p->fixed_w = 0;
        p->fixed_h = 0;
    }
    dbg_change_panel(ui, panel, TORIRS_CHROME_CHANGE_PANEL_MODE);
    dbg_damage_surface(ui);
}

void
ToriRSChrome_PanelSetDockSize(struct ToriRSChrome* ui, int panel, int size)
{
    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    ToriRSChrome_PanelSetDock(ui, panel, ui->panels[panel].dock, size);
}

/** Place every docked panel against the surface, in panel order. Runs in
 *  Build, so a resized surface or a dragged splitter reflows on the next one. */
static void
dbg_dock_layout(struct ToriRSChrome* ui)
{
    struct ToriRSChromeRect area;

    if( ui->surface_w <= 0 || ui->surface_h <= 0 )
        return;
    area.x = 0;
    area.y = 0;
    area.w = ui->surface_w;
    area.h = ui->surface_h;
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct ToriRSChromePanel* p = &ui->panels[i];
        struct ToriRSChromeRect r = area;
        int size;

        if( !p->visible || p->dock == TORIRS_CHROME_DOCK_NONE || p->style != TORIRS_CHROME_PANEL_WINDOW )
            continue;
        size = p->dock_size;
        switch( p->dock )
        {
        case TORIRS_CHROME_DOCK_LEFT:
            r.w = dbg_clampi(size, 0, area.w);
            area.x += r.w;
            area.w -= r.w;
            break;
        case TORIRS_CHROME_DOCK_RIGHT:
            r.w = dbg_clampi(size, 0, area.w);
            r.x = area.x + area.w - r.w;
            area.w -= r.w;
            break;
        case TORIRS_CHROME_DOCK_TOP:
            r.h = dbg_clampi(size, 0, area.h);
            area.y += r.h;
            area.h -= r.h;
            break;
        case TORIRS_CHROME_DOCK_BOTTOM:
            r.h = dbg_clampi(size, 0, area.h);
            r.y = area.y + area.h - r.h;
            area.h -= r.h;
            break;
        case TORIRS_CHROME_DOCK_FILL:
        default:
            area.w = 0;
            area.h = 0;
            break;
        }
        if( r.w <= 0 || r.h <= 0 )
        {
            r.w = r.w > 0 ? r.w : 1;
            r.h = r.h > 0 ? r.h : 1;
        }
        if( p->x != r.x || p->y != r.y || p->fixed_w != r.w || p->fixed_h != r.h )
        {
            p->x = r.x;
            p->y = r.y;
            p->fixed_w = r.w;
            p->fixed_h = r.h;
            p->dirty = 1;
            p->paint_only_dirty = 0;
            dbg_change_panel(ui, i, TORIRS_CHROME_CHANGE_PANEL_RECT);
        }
    }
}

/** The grip band of a docked panel: its inner edge. w/h 0 for none. */
static struct ToriRSChromeRect
dbg_split_band(struct ToriRSChrome const* ui, struct ToriRSChromePanel const* p)
{
    struct ToriRSChromeRect r = { 0, 0, 0, 0 };
    struct ToriRSChromeRect const b = p->last_rect;

    if( !p->visible || b.w <= 0 || b.h <= 0 )
        return r;
    switch( p->dock )
    {
    case TORIRS_CHROME_DOCK_LEFT:
        r.x = b.x + b.w - DBG_SPLIT;
        r.y = b.y;
        r.w = DBG_SPLIT;
        r.h = b.h;
        break;
    case TORIRS_CHROME_DOCK_RIGHT:
        r.x = b.x;
        r.y = b.y;
        r.w = DBG_SPLIT;
        r.h = b.h;
        break;
    case TORIRS_CHROME_DOCK_TOP:
        r.x = b.x;
        r.y = b.y + b.h - DBG_SPLIT;
        r.w = b.w;
        r.h = DBG_SPLIT;
        break;
    case TORIRS_CHROME_DOCK_BOTTOM:
        r.x = b.x;
        r.y = b.y;
        r.w = b.w;
        r.h = DBG_SPLIT;
        break;
    default:
        break;
    }
    return r;
}

static int
dbg_split_at(struct ToriRSChrome const* ui, int x, int y)
{
    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct ToriRSChromeRect const band = dbg_split_band(ui, &ui->panels[i]);
        if( band.w > 0 && dbg_point_in_rect(x, y, band) )
            return i;
    }
    return -1;
}

static void
dbg_split_drag_to(struct ToriRSChrome* ui, int x, int y)
{
    struct ToriRSChromePanel const* p = &ui->panels[ui->split_panel];
    int size = p->dock_size;
    int limit;

    switch( p->dock )
    {
    case TORIRS_CHROME_DOCK_LEFT:
        size = x - p->x;
        limit = ui->surface_w;
        break;
    case TORIRS_CHROME_DOCK_RIGHT:
        size = p->x + p->w - x;
        limit = ui->surface_w;
        break;
    case TORIRS_CHROME_DOCK_TOP:
        size = y - p->y;
        limit = ui->surface_h;
        break;
    case TORIRS_CHROME_DOCK_BOTTOM:
        size = p->y + p->h - y;
        limit = ui->surface_h;
        break;
    default:
        return;
    }
    limit = limit > 0 ? limit - p->dock_min : size;
    size = dbg_clampi(size, p->dock_min, limit > p->dock_min ? limit : p->dock_min);
    ToriRSChrome_PanelSetDockSize(ui, ui->split_panel, size);
}

static void
dbg_push_splitter(struct ToriRSChrome* ui, struct ToriRSChromePanel const* p)
{
    struct ToriRSChromeRect band;
    struct ToriRSChromeRect live;
    int const hot = ui->split_panel >= 0 && &ui->panels[ui->split_panel] == p;

    /* Drawn from the live box (last_rect is written after this build). */
    live.x = p->x;
    live.y = p->y;
    live.w = p->w;
    live.h = p->h;
    {
        struct ToriRSChromePanel tmp = *p;
        tmp.last_rect = live;
        band = dbg_split_band(ui, &tmp);
    }
    if( band.w <= 0 )
        return;
    dbg_push_rect(
        ui, band.x, band.y, band.w, band.h, hot ? ui->theme.accent : ui->theme.panel_border, 1,
        band);
    (void)dbg_push_icon(
        ui, TORIRS_CHROME_SKIN_SPLITTER_GRIP, band.x + band.w / 2, band.y + band.h / 2, 0, band);
}

int
ToriRSChrome_LayoutSave(struct ToriRSChrome const* ui, char* out, int cap)
{
    int used = 0;

    assert(ui);
    assert(out);
    assert(cap > 0);
    out[0] = '\0';
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct ToriRSChromePanel const* p = &ui->panels[i];
        int n;

        if( p->dock == TORIRS_CHROME_DOCK_NONE || !p->title[0] ||
            strchr(p->title, ';') || strchr(p->title, '=') )
            continue;
        n = snprintf(out + used, (size_t)(cap - used), "%s=%d:%d;", p->title, p->dock, p->dock_size);
        if( n < 0 || n >= cap - used )
        {
            out[used] = '\0';
            return -1;
        }
        used += n;
    }
    return used;
}

int
ToriRSChrome_LayoutLoad(struct ToriRSChrome* ui, char const* text)
{
    int applied = 0;
    char const* at;

    assert(ui);
    assert(text);
    at = text;
    while( *at )
    {
        char title[TORIRS_CHROME_LABEL_MAX];
        char const* eq = strchr(at, '=');
        char const* end = strchr(at, ';');
        int dock = 0;
        int size = 0;
        int len;

        if( !end )
            end = at + strlen(at);
        if( !eq || eq > end )
        {
            at = *end ? end + 1 : end;
            continue;
        }
        len = (int)(eq - at);
        if( len > 0 && len < TORIRS_CHROME_LABEL_MAX &&
            sscanf(eq + 1, "%d:%d", &dock, &size) == 2 && dock >= TORIRS_CHROME_DOCK_NONE &&
            dock <= TORIRS_CHROME_DOCK_FILL )
        {
            memcpy(title, at, (size_t)len);
            title[len] = '\0';
            for( int i = 0; i < ui->panel_count; i++ )
                if( strcmp(ui->panels[i].title, title) == 0 )
                {
                    ToriRSChrome_PanelSetDock(ui, i, dock, size);
                    applied++;
                    break;
                }
        }
        at = *end ? end + 1 : end;
    }
    return applied;
}

/* ---- measuring ----------------------------------------------------------------------- */

/** Width of an editor row, or -1 for a kind this file does not own. */
static int
dbg_ext_width(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    switch( w->kind )
    {
    case TORIRS_CHROME_W_TREE:
        return w->iaux * DBG_TREE_INDENT + DBG_TREE_EXP + DBG_TREE_GAP +
               dbg_measure_tabbed(ui->theme.font_row, ui->scale_q, w->label, DBG_PX(16));
    case TORIRS_CHROME_W_VLIST:
    case TORIRS_CHROME_W_CODE:
    case TORIRS_CHROME_W_GRAPH:
        return DBG_LABEL_W + DBG_INPUT_MIN_W;
    case TORIRS_CHROME_W_ICONBUTTON:
        return DBG_ICON_BOX + (w->label[0] ? DBG_CHECK_GAP +
                                   ToriRSChrome_MeasureTextQ(ui->theme.font_row, ui->scale_q, w->label)
                                           : 0);
    case TORIRS_CHROME_W_SLIDER:
        return (w->label[0] ? ToriRSChrome_MeasureTextQ(ui->theme.font_row, ui->scale_q, w->label)
                                  + DBG_SLIDER_LABEL_GAP
                            : 0)
               + DBG_INPUT_MIN_W + DBG_SLIDER_VALUE_W;
    case TORIRS_CHROME_W_RADIO:
        return DBG_PX(TORIRS_CHROME_M_BOX) + DBG_CHECK_GAP +
               ToriRSChrome_MeasureTextQ(ui->theme.font_row, ui->scale_q, w->label);
    default:
        return -1;
    }
}

/** Height of an editor row, or -1 for a kind this file does not own. */
static int
dbg_ext_height(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    int const caption = w->label[0] ? DBG_ROW_H : 0;

    switch( w->kind )
    {
    case TORIRS_CHROME_W_TREE:
    case TORIRS_CHROME_W_SLIDER:
    case TORIRS_CHROME_W_RADIO:
        return DBG_ROW_H;
    case TORIRS_CHROME_W_VLIST:
        return caption + w->rows * DBG_VROW_H + 2 * DBG_RULE;
    case TORIRS_CHROME_W_CODE:
        return caption + w->rows * DBG_CODE_LINE_H + 2 * DBG_RULE;
    case TORIRS_CHROME_W_GRAPH:
        return caption + w->view_h + 2 * DBG_RULE;
    case TORIRS_CHROME_W_ICONBUTTON:
        return DBG_ICON_BOX;
    default:
        return -1;
    }
}

/* ---- drawing ---------------------------------------------------------------------------- */

static void
dbg_draw_iconbutton(
    struct ToriRSChrome* ui,
    struct ToriRSChromeWidget const* w,
    int x,
    int y,
    int hovered,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    int const box = DBG_ICON_BOX;
    int const pressed = w->checked;

    if( pressed )
    {
        struct ToriRSChromeRect field;
        field.x = x;
        field.y = y;
        field.w = box;
        field.h = box;
        dbg_push_field_chrome(ui, field, clip);
        dbg_push_rect_trans(
            ui, x + DBG_RULE, y + DBG_RULE, box - 2 * DBG_RULE, box - 2 * DBG_RULE,
            th->dropdown_veil, 1, th->dropdown_band_trans_alt, clip);
    }
    if( !dbg_push_icon(ui, w->ivalue, x + box / 2, y + box / 2, w->disabled ? 128 : 0, clip) )
    {
        /* No art: the label's first letters in the field ink, so a button on
         * a build without the skin still says what it is. */
        char initials[3] = { 0, 0, 0 };
        initials[0] = w->label[0];
        initials[1] = w->label[0] ? w->label[1] : 0;
        dbg_push_rect(ui, x + DBG_PX(3), y + DBG_PX(3), box - DBG_PX(6), box - DBG_PX(6),
            th->input_bg, 1, clip);
        if( initials[0] )
            dbg_push_text(
                ui,
                x + DBG_PX(5),
                dbg_row_text_baseline(ui, y, box),
                dbg_wrap_line(ui, initials, (int)strlen(initials)),
                w->disabled ? th->text_dim : th->text,
                ui->theme.font_row,
                0,
                clip);
    }
    if( hovered && !w->disabled )
        dbg_push_rect(ui, x, y, box, box, th->accent, 0, clip);
    if( w->chord[0] )
    {
        /* The chord as a badge: Small face over a black plate in the bottom-
         * right corner, inside the hover outline so the outline never covers
         * it. No wider than the box, and a chord wider than that is fitted to
         * it rather than spilling onto its neighbour or cut mid-glyph. */
        struct ToriRSChromeRect badge;
        struct ToriRSChromeRect icon;
        int const line_box = ToriRSChrome_FontLineBoxQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q);

        icon.x = x;
        icon.y = y;
        icon.w = box;
        icon.h = box;
        char const* shown;

        badge.w = dbg_keycap_width(ui, w->chord);
        if( badge.w > box - 2 * DBG_BADGE_INSET )
            badge.w = box - 2 * DBG_BADGE_INSET;
        badge.h = DBG_BADGE_H;
        badge.x = x + box - DBG_BADGE_INSET - badge.w;
        badge.y = y + box - DBG_BADGE_INSET - badge.h;
        /* A chord wider than the icon box shows fitted ("...+1"), never cut
         * mid-chord into one that reads as another; the tooltip has it whole. */
        shown = dbg_fit_chord(ui, w->chord, badge.w - 2 * DBG_KEYCAP_PAD_X);
        icon = dbg_rect_clip(clip, icon);
        dbg_push_rect(ui, badge.x, badge.y, badge.w, badge.h, th->menu_chrome, 1, icon);
        dbg_push_text(
            ui,
            badge.x + DBG_KEYCAP_PAD_X,
            badge.y + (badge.h - line_box) / 2 +
                ToriRSChrome_FontLineHeightQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q),
            shown,
            w->disabled ? th->text_dim : th->menu_hover_text,
            TORIRS_CHROME_FONT_SMALL,
            0,
            dbg_rect_clip(icon, badge));
    }
}

/** CODE: the gutter's width, wide enough for the largest line number. */
static int
dbg_code_gutter_w(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    char digits[16];
    snprintf(digits, sizeof(digits), "%d", w->imax > 9 ? w->imax : 99);
    return ToriRSChrome_MeasureTextQ(TORIRS_CHROME_FONT_MENU, ui->scale_q, digits) +
           2 * DBG_GUTTER_PAD;
}

/** The list box (below the caption) of a laid-out VLIST/CODE/GRAPH row. */
static struct ToriRSChromeRect
dbg_list_box(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    struct ToriRSChromeRect r;
    r.x = w->x;
    r.y = w->y + (w->label[0] ? DBG_ROW_H : 0);
    r.w = w->w;
    r.h = (w->kind == TORIRS_CHROME_W_GRAPH ? w->view_h
                                            : w->rows * (w->kind == TORIRS_CHROME_W_CODE
                                                             ? DBG_CODE_LINE_H
                                                             : DBG_VROW_H)) +
          2 * DBG_RULE;
    return r;
}

/** Row of a VLIST/CODE under (x, y), absolute, or -1. Uses the full laid-out
 *  height rather than the clipped hit box, so a half-scrolled list maps rows
 *  the same way it draws them. */
static int
dbg_list_row_at(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int x, int y)
{
    struct ToriRSChromeRect const box = dbg_list_box(ui, w);
    int const line_h = w->kind == TORIRS_CHROME_W_CODE ? DBG_CODE_LINE_H : DBG_VROW_H;
    int row;

    if( !dbg_point_in(x, y, box.x, box.y + DBG_RULE, box.w, box.h - 2 * DBG_RULE) )
        return -1;
    row = w->imin + (y - box.y - DBG_RULE) / line_h;
    return row < w->imax && row < w->imin + w->rows ? row : -1;
}

/** Whether (x, y) is on a list's scrollbar column. */
static int
dbg_list_on_bar(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int x, int y)
{
    struct ToriRSChromeRect const box = dbg_list_box(ui, w);
    return w->imax > w->rows && x >= box.x + box.w - DBG_RULE - DBG_SCROLL_W &&
           dbg_point_in(x, y, box.x, box.y, box.w, box.h);
}

/* ---- VLIST cells (ToriRSChrome_VListCells) ---------------------------------------- */

/** The five fields of a cells row, and its keycaps: pointers into the text
 *  with lengths (the text is the row's pool slot or the widget's label, both
 *  alive until the end of the build). */
struct dbg_cells
{
    char const* field[6];
    int len[6];
    int key_count;
    char const* key[TORIRS_CHROME_CELL_KEYS_MAX];
    int key_len[TORIRS_CHROME_CELL_KEYS_MAX];
};

enum
{
    DBG_CELL_SPRITE = 0,
    DBG_CELL_NAME,
    DBG_CELL_DIM,
    DBG_CELL_KEYS,
    DBG_CELL_RIGHT,
    DBG_CELL_MARK,
    DBG_CELL_FIELDS,
};

static void
dbg_cells_split(char const* text, struct dbg_cells* out)
{
    char const* at = text;

    memset(out, 0, sizeof(*out));
    for( int f = 0; f < DBG_CELL_FIELDS; f++ )
    {
        char const* end = strchr(at, TORIRS_CHROME_CELL_SEP);
        out->field[f] = at;
        out->len[f] = end ? (int)(end - at) : (int)strlen(at);
        if( !end )
        {
            for( int g = f + 1; g < DBG_CELL_FIELDS; g++ )
                out->field[g] = at + out->len[f];
            break;
        }
        at = end + 1;
    }
    {
        char const* k = out->field[DBG_CELL_KEYS];
        char const* stop = k + out->len[DBG_CELL_KEYS];
        while( k < stop && out->key_count < TORIRS_CHROME_CELL_KEYS_MAX )
        {
            char const* space;
            while( k < stop && *k == ' ' )
                k++;
            if( k >= stop )
                break;
            space = k;
            while( space < stop && *space != ' ' )
                space++;
            out->key[out->key_count] = k;
            out->key_len[out->key_count] = (int)(space - k);
            out->key_count++;
            k = space;
        }
    }
}

/** Where a cells row's parts sit in its line (one row's box in the list). */
struct dbg_cells_layout
{
    int thumb_x;
    int name_x;
    int name_w;
    int keys_x;
    int keys_w;
    /** Keycaps shown and their boxes; `more` the ones that did not fit (drawn
     *  as one "+n" cap after them, which is no cell). */
    int key_shown;
    int key_x[TORIRS_CHROME_CELL_KEYS_MAX];
    int key_w[TORIRS_CHROME_CELL_KEYS_MAX];
    int more;
    int more_x;
    int more_w;
    int cap_y;
    int cap_h;
    int right_x;
    int right_w;
};

/** A cell's text as its own string, from the build's wrap pool (NULL when
 *  it is empty or the pool is full). */
static char const*
dbg_cells_text(struct ToriRSChrome* ui, char const* text, int len)
{
    return len > 0 ? dbg_wrap_line(ui, text, len) : NULL;
}

static int
dbg_cells_key_w(struct ToriRSChrome const* ui, char const* key, int len)
{
    char buffer[32];
    int const n = len < (int)sizeof(buffer) - 1 ? len : (int)sizeof(buffer) - 1;
    memcpy(buffer, key, (size_t)n);
    buffer[n] = '\0';
    return dbg_keycap_width(ui, buffer);
}

/** A row with no picture well (its sprite field is "-"). */
static int
dbg_cells_no_well(struct dbg_cells const* cells)
{
    return cells->len[DBG_CELL_SPRITE] == 1 && cells->field[DBG_CELL_SPRITE][0] == TORIRS_CHROME_CELL_NO_SPRITE[0];
}

/** The row's mark (its sixth field's first byte), 0 for none. */
static char
dbg_cells_mark(struct dbg_cells const* cells)
{
    return cells->len[DBG_CELL_MARK] > 0 ? cells->field[DBG_CELL_MARK][0] : 0;
}

/**
 * The columns: a 16 px thumbnail, the name, the keycaps in a column a third
 * of the row wide (at most 96 px; `wide`, TORIRS_CHROME_VLIST_CELLS_WIDE:
 * half the row, uncapped), the right field in a column six digits wide. The
 * same numbers for every row and the header, so the columns line up; the
 * name takes what is left. A row with no well starts its name at the well's
 * place.
 */
static void
dbg_cells_layout(
    struct ToriRSChrome const* ui,
    struct dbg_cells const* cells,
    struct ToriRSChromeRect line,
    int wide,
    struct dbg_cells_layout* out)
{
    int const pad = DBG_INPUT_PAD_X;
    int const gap = DBG_PX(3);
    int const thumb = DBG_PX(16);
    int const right_w = ToriRSChrome_MeasureTextQ(ui->theme.font_row, ui->scale_q, "000000");
    int const line_box = ToriRSChrome_FontLineBoxQ(TORIRS_CHROME_FONT_SMALL, ui->scale_q);
    int keys_w = line.w / 3;
    int x;

    memset(out, 0, sizeof(*out));
    if( wide )
        keys_w = line.w / 2;
    else if( keys_w > DBG_PX(96) )
        keys_w = DBG_PX(96);
    out->thumb_x = line.x + pad;
    out->right_w = right_w;
    out->right_x = line.x + line.w - pad - right_w;
    out->keys_w = keys_w;
    out->keys_x = out->right_x - gap - keys_w;
    out->name_x = dbg_cells_no_well(cells) ? out->thumb_x : out->thumb_x + thumb + gap;
    out->name_w = out->keys_x - gap - out->name_x;
    if( out->name_w < 0 )
        out->name_w = 0;
    out->cap_h = line_box + DBG_PX(2);
    if( out->cap_h > line.h )
        out->cap_h = line.h;
    out->cap_y = line.y + (line.h - out->cap_h) / 2;

    x = out->keys_x;
    for( int k = 0; k < cells->key_count; k++ )
    {
        int const w = dbg_cells_key_w(ui, cells->key[k], cells->key_len[k]);
        int const left = cells->key_count - k - 1;
        /* Room for this cap, and for a "+n" after it when more follow. */
        int const tail = left > 0 ? gap + dbg_keycap_width(ui, "+9") : 0;
        if( x + w + tail > out->keys_x + keys_w )
        {
            out->more = cells->key_count - k;
            break;
        }
        out->key_x[k] = x;
        out->key_w[k] = w;
        out->key_shown++;
        x += w + gap;
    }
    if( out->more > 0 )
    {
        char more[8];
        snprintf(more, sizeof(more), "+%d", out->more);
        out->more_x = x;
        out->more_w = dbg_keycap_width(ui, more);
    }
}

/** The line box of visible row `row` of a VLIST. */
static struct ToriRSChromeRect
dbg_cells_line(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int row)
{
    struct ToriRSChromeRect const box = dbg_list_box(ui, w);
    struct ToriRSChromeRect line;
    line.x = box.x + DBG_RULE;
    line.y = box.y + DBG_RULE + (row - w->imin) * DBG_VROW_H;
    line.w = box.w - 2 * DBG_RULE - (w->imax > w->rows ? DBG_SCROLL_W : 0);
    line.h = DBG_VROW_H;
    return line;
}

/** The cell of a cells row under surface `x`: n for the n-th keycap, else 0. */
static int
dbg_cells_cell_at(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int row, int x)
{
    struct dbg_cells cells;
    struct dbg_cells_layout lay;
    char const* text = dbg_vrow_text(ui, w, row);

    if( !text )
        return 0;
    dbg_cells_split(text, &cells);
    dbg_cells_layout(ui, &cells, dbg_cells_line(ui, w, row), (w->iaux & TORIRS_CHROME_VLIST_CELLS_WIDE) != 0, &lay);
    for( int k = 0; k < lay.key_shown; k++ )
        if( x >= lay.key_x[k] && x < lay.key_x[k] + lay.key_w[k] )
            return k + 1;
    return 0;
}

/** One cells row, or (`header` 1) the header: every field dimmed, no
 *  thumbnail, the keys field one caption at the keys column. */
static void
dbg_draw_cells(
    struct ToriRSChrome* ui,
    char const* text,
    struct ToriRSChromeRect line,
    int header,
    int wide,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    int const font = ui->theme.font_row;
    int const baseline = line.y + (line.h - ToriRSChrome_FontLineBoxQ(font, ui->scale_q)) / 2 +
                         ToriRSChrome_FontLineHeightQ(font, ui->scale_q);
    struct dbg_cells cells;
    struct dbg_cells_layout lay;
    char const* name;
    int name_used = 0;
    char mark;
    uint32_t name_ink;
    uint32_t dim_ink;
    uint32_t key_ink;

    dbg_cells_split(text, &cells);
    dbg_cells_layout(ui, &cells, line, wide, &lay);
    mark = header ? 0 : dbg_cells_mark(&cells);
    name_ink = header ? th->text_dim : th->text;
    dim_ink = th->text_dim;
    key_ink = th->accent;
    if( mark == TORIRS_CHROME_CELL_MARK_ALERT )
        name_ink = dim_ink = key_ink = TORIRS_CHROME_CELL_MARK_INK;
    else if( mark == TORIRS_CHROME_CELL_MARK_ACCENT )
        name_ink = th->accent;

    /* The thumbnail: an empty well until the host has a picture for it; none
     * at all for a row that says "-". */
    if( !header && !dbg_cells_no_well(&cells) )
    {
        int const thumb = DBG_PX(16);
        int const ty = line.y + (line.h - thumb) / 2;
        int sprite = 0;
        for( int i = 0; i < cells.len[DBG_CELL_SPRITE]; i++ )
        {
            char const c = cells.field[DBG_CELL_SPRITE][i];
            if( c < '0' || c > '9' )
            {
                sprite = 0;
                break;
            }
            sprite = sprite * 10 + (c - '0');
        }
        dbg_push_rect(ui, lay.thumb_x, ty, thumb, thumb, th->input_bg, 1, clip);
        if( sprite > 0 )
        {
            struct ToriRSChromePrim* p = dbg_prim_push(ui);
            if( p )
            {
                struct ToriRSChromeRect well;
                well.x = lay.thumb_x;
                well.y = ty;
                well.w = thumb;
                well.h = thumb;
                p->kind = TORIRS_CHROME_PRIM_SPRITE;
                p->x = well.x;
                p->y = well.y;
                p->w = thumb;
                p->h = thumb;
                p->sprite_scene_id = sprite;
                p->clip = dbg_rect_clip(clip, well);
            }
        }
    }

    /* The name, then the dim part in what the name leaves: measured and
     * ellipsised, never clipped (R2). */
    name = dbg_cells_text(ui, cells.field[DBG_CELL_NAME], cells.len[DBG_CELL_NAME]);
    if( name && lay.name_w > 0 )
    {
        name = dbg_elide_text(ui, font, name, lay.name_w);
        dbg_push_text(ui, lay.name_x, baseline, name, name_ink, font, 0, clip);
        name_used = ToriRSChrome_MeasureTextQ(font, ui->scale_q, name);
    }
    {
        char const* dim = dbg_cells_text(ui, cells.field[DBG_CELL_DIM], cells.len[DBG_CELL_DIM]);
        int const space = ToriRSChrome_MeasureTextQ(font, ui->scale_q, " ");
        int const room = lay.name_w - name_used - (name_used ? space : 0);
        int const least = ToriRSChrome_MeasureTextQ(font, ui->scale_q, "a...");
        if( dim && room >= least )
            dbg_push_text(
                ui, lay.name_x + name_used + (name_used ? space : 0), baseline,
                dbg_elide_text(ui, font, dim, room), dim_ink, font, 0, clip);
    }

    /* The keycaps; the header's is one caption at the column. */
    if( header )
    {
        char const* caption = dbg_cells_text(ui, cells.field[DBG_CELL_KEYS], cells.len[DBG_CELL_KEYS]);
        if( caption )
            dbg_push_text(
                ui, lay.keys_x, baseline, dbg_elide_text(ui, font, caption, lay.keys_w), th->text_dim, font, 0,
                clip);
    }
    else
    {
        for( int k = 0; k < lay.key_shown; k++ )
        {
            struct ToriRSChromeRect cap;
            char const* key = dbg_cells_text(ui, cells.key[k], cells.key_len[k]);
            if( !key )
                break;
            cap.x = lay.key_x[k];
            cap.y = lay.cap_y;
            cap.w = lay.key_w[k];
            cap.h = lay.cap_h;
            dbg_push_keycap(ui, cap, key, key_ink, clip);
        }
        if( lay.more > 0 )
        {
            char more[8];
            struct ToriRSChromeRect cap;
            int const n = snprintf(more, sizeof(more), "+%d", lay.more);
            char const* label = dbg_wrap_line(ui, more, n);
            cap.x = lay.more_x;
            cap.y = lay.cap_y;
            cap.w = lay.more_w;
            cap.h = lay.cap_h;
            if( label )
                dbg_push_keycap(ui, cap, label, th->text_dim, clip);
        }
    }

    /* The right field, right-aligned in its column. */
    {
        char const* right = dbg_cells_text(ui, cells.field[DBG_CELL_RIGHT], cells.len[DBG_CELL_RIGHT]);
        if( right )
        {
            right = dbg_elide_text(ui, font, right, lay.right_w);
            dbg_push_text(
                ui, lay.right_x + lay.right_w - ToriRSChrome_MeasureTextQ(font, ui->scale_q, right), baseline,
                right, th->text_dim, font, 0, clip);
        }
    }
}

int
ToriRSChrome_VListPickedCell(struct ToriRSChrome const* ui, int widget)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_VLIST )
        return 0;
    return ui->widgets[widget].vlist_cell;
}

int
ToriRSChrome_VListHoverRow(struct ToriRSChrome const* ui, int widget, int* out_cell)
{
    struct ToriRSChromeWidget const* w;
    int row;

    assert(ui);
    if( out_cell )
        *out_cell = 0;
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_VLIST ||
        ui->hover != widget )
        return -1;
    w = &ui->widgets[widget];
    row = dbg_list_row_at(ui, w, ui->hover_x, ui->hover_y);
    if( row >= 0 && w->iaux && out_cell )
        *out_cell = dbg_cells_cell_at(ui, w, row, ui->hover_x);
    return row;
}

static void
dbg_draw_list(
    struct ToriRSChrome* ui,
    struct ToriRSChromeWidget* w,
    int widget,
    int row_x,
    int row_y,
    int hovered,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    int const code = w->kind == TORIRS_CHROME_W_CODE;
    int const line_h = code ? DBG_CODE_LINE_H : DBG_VROW_H;
    int const font = code ? TORIRS_CHROME_FONT_MENU : ui->theme.font_row;
    int const bar = w->imax > w->rows ? DBG_SCROLL_W : 0;
    int const gutter = code ? dbg_code_gutter_w(ui, w) : 0;
    int const hover_row = hovered ? dbg_list_row_at(ui, w, ui->hover_x, ui->hover_y) : -1;
    struct ToriRSChromeRect box;
    struct ToriRSChromeRect inner;
    struct ToriRSChromeRect text_clip;

    (void)row_x;
    (void)row_y;
    box = dbg_list_box(ui, w);
    if( w->label[0] && !code && w->iaux )
    {
        /* A cells list's caption is its header row, in the rows' columns. */
        struct ToriRSChromeRect head;
        head.x = box.x + DBG_RULE;
        head.y = w->y;
        head.w = box.w - 2 * DBG_RULE - bar;
        head.h = DBG_ROW_H;
        dbg_draw_cells(ui, w->label, head, 1, (w->iaux & TORIRS_CHROME_VLIST_CELLS_WIDE) != 0, clip);
    }
    else if( w->label[0] )
        dbg_push_text(
            ui, w->x, dbg_row_text_baseline(ui, w->y, DBG_ROW_H), w->label,
            w->color ? w->color : th->text_dim, ui->theme.font_row, 0, clip);
    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->textarea_bg, 1, clip);
    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->dropdown_border, 0, clip);
    dbg_push_rect(
        ui, box.x + DBG_RULE, box.y + DBG_RULE, box.w - 2 * DBG_RULE, box.h - 2 * DBG_RULE,
        ui->focus == widget ? th->input_border_focus : th->dropdown_border_inner, 0, clip);
    inner.x = box.x + DBG_RULE;
    inner.y = box.y + DBG_RULE;
    inner.w = box.w - 2 * DBG_RULE - bar;
    inner.h = box.h - 2 * DBG_RULE;
    if( inner.w <= 0 || inner.h <= 0 )
        return;
    text_clip = dbg_rect_clip(clip, inner);
    if( bar )
    {
        struct ToriRSChromeRect b;
        b.x = inner.x + inner.w;
        b.y = inner.y;
        b.w = bar;
        b.h = inner.h;
        dbg_push_scrollbar(ui, b, w->imax * line_h, w->rows * line_h, w->imin * line_h, clip);
    }
    if( gutter )
        dbg_push_rect(ui, inner.x, inner.y, gutter, inner.h, th->input_bg, 1, text_clip);

    for( int i = 0; i < w->rows; i++ )
    {
        int const r = w->imin + i;
        int const ly = inner.y + i * line_h;
        int const tx = inner.x + gutter + DBG_INPUT_PAD_X;
        int const baseline =
            ly + (line_h - ToriRSChrome_FontLineBoxQ(font, ui->scale_q)) / 2 +
            ToriRSChrome_FontLineHeightQ(font, ui->scale_q);
        char const* text;

        if( r >= w->imax )
            break;
        if( !code && r == w->ivalue )
            dbg_push_rect_trans(
                ui, inner.x, ly, inner.w, line_h, th->accent, 1, 160, text_clip);
        else if( r == hover_row )
            dbg_push_rect_trans(
                ui, inner.x, ly, inner.w, line_h, th->dropdown_veil, 1,
                th->dropdown_row_trans_hover, text_clip);
        if( code )
        {
            char number[16];
            int const n = snprintf(number, sizeof(number), "%d", r + 1);
            char const* num = dbg_wrap_line(ui, number, n);
            if( num )
                dbg_push_text(
                    ui,
                    inner.x + gutter - DBG_GUTTER_PAD -
                        ToriRSChrome_MeasureTextQ(font, ui->scale_q, num),
                    baseline, num, th->text_dim, font, 0, text_clip);
        }
        text = dbg_vrow_text(ui, w, r);
        if( !text )
            continue;
        if( !code && w->iaux )
        {
            struct ToriRSChromeRect line;
            line.x = inner.x;
            line.y = ly;
            line.w = inner.w;
            line.h = line_h;
            dbg_draw_cells(ui, text, line, 0, (w->iaux & TORIRS_CHROME_VLIST_CELLS_WIDE) != 0, text_clip);
            continue;
        }
        {
            struct ToriRSChromeRect line_clip = text_clip;
            int const left = inner.x + gutter;
            if( line_clip.x < left )
            {
                line_clip.w -= left - line_clip.x;
                line_clip.x = left;
            }
            if( line_clip.w < 0 )
                line_clip.w = 0;
            dbg_push_text(
                ui, tx, baseline, text,
                code ? th->input_text : th->text, font,
                0, line_clip);
            if( code )
            {
                int const len = (int)strlen(text);
                for( int s = 0; s < ui->span_count; s++ )
                {
                    struct ToriRSChromeSpan const* sp = &ui->spans[s];
                    int start;
                    int n;
                    char const* seg;

                    if( sp->owner != widget || sp->line != r || sp->start >= len )
                        continue;
                    start = sp->start;
                    n = sp->len;
                    if( start + n > len )
                        n = len - start;
                    if( n <= 0 )
                        continue;
                    seg = dbg_wrap_line(ui, text + start, n);
                    if( !seg )
                        break;
                    /* Drawn over the plain run: the glyph masks are the same,
                     * so the later colour simply wins on those pixels. */
                    dbg_push_text(
                        ui, tx + dbg_measure_prefix(font, ui->scale_q, text, start), baseline, seg,
                        sp->color, font, 0, line_clip);
                }
                if( ui->focus == widget && ui->caret_visible && r == w->ivalue )
                {
                    int const col = w->caret < len ? w->caret : len;
                    dbg_push_rect(
                        ui, tx + dbg_measure_prefix(font, ui->scale_q, text, col), ly + DBG_RULE,
                        DBG_RULE, line_h - 2 * DBG_RULE, th->input_text, 1, line_clip);
                }
            }
        }
    }
    /* A caret on a line the host has not filled yet still shows where it is. */
    if( code && ui->focus == widget && ui->caret_visible && w->ivalue >= w->imin &&
        w->ivalue < w->imin + w->rows && !dbg_vrow_text(ui, w, w->ivalue) )
        dbg_push_rect(
            ui, inner.x + gutter + DBG_INPUT_PAD_X, inner.y + (w->ivalue - w->imin) * line_h,
            DBG_RULE, line_h, th->input_text, 1, text_clip);
}

static void
dbg_draw_graph(
    struct ToriRSChrome* ui,
    struct ToriRSChromeWidget* w,
    int widget,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    struct ToriRSChromeRect const box = dbg_list_box(ui, w);
    struct ToriRSChromeRect inner;
    struct ToriRSChromeRect visible;
    int const s = ui->scale;

    if( w->label[0] )
        dbg_push_text(
            ui, w->x, dbg_row_text_baseline(ui, w->y, DBG_ROW_H), w->label,
            w->color ? w->color : th->text_dim, ui->theme.font_row, 0, clip);
    if( box.w <= 2 * DBG_RULE || box.h <= 2 * DBG_RULE )
        return;
    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->textarea_bg, 1, clip);
    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->dropdown_border, 0, clip);
    dbg_push_rect(
        ui, box.x + DBG_RULE, box.y + DBG_RULE, box.w - 2 * DBG_RULE, box.h - 2 * DBG_RULE,
        ui->focus == widget ? th->input_border_focus : th->dropdown_border_inner, 0, clip);
    inner.x = box.x + DBG_RULE;
    inner.y = box.y + DBG_RULE;
    inner.w = box.w - 2 * DBG_RULE;
    inner.h = box.h - 2 * DBG_RULE;
    visible = dbg_rect_clip(clip, inner);
    w->custom_clip_x = visible.x;
    w->custom_clip_y = visible.y;
    w->custom_clip_w = visible.w;
    w->custom_clip_h = visible.h;
    if( visible.w <= 0 || visible.h <= 0 )
        return;
    for( int i = 0; i < w->graph_count; i++ )
    {
        struct ToriRSChromeGraphItem const* it = &ui->graph_items[w->graph_first + i];
        int const x = inner.x + it->x * s;
        int const y = inner.y + it->y * s;

        switch( it->kind )
        {
        case TORIRS_CHROME_GRAPH_RECT:
        case TORIRS_CHROME_GRAPH_RECT_OUTLINE:
            /* Culled here rather than left to the clip: a square picker zoomed
             * in has thousands of cells and shows a few dozen, and a prim per
             * offscreen cell is the display list spent on nothing. */
            if( x + it->w * s <= visible.x || y + it->h * s <= visible.y ||
                x >= visible.x + visible.w || y >= visible.y + visible.h )
                break;
            dbg_push_rect(
                ui, x, y, it->w * s, it->h * s, it->color,
                it->kind == TORIRS_CHROME_GRAPH_RECT, visible);
            break;
        case TORIRS_CHROME_GRAPH_LINE:
            dbg_push_line(ui, x, y, x + it->w * s, y + it->h * s, it->color, visible);
            break;
        case TORIRS_CHROME_GRAPH_TEXT:
            if( it->text[0] )
                dbg_push_text(ui, x, y, it->text, it->color, ui->theme.font_row, 0, visible);
            break;
        case TORIRS_CHROME_GRAPH_IMAGE:
        {
            struct ToriRSChromePrim* p;
            if( x + it->w * s <= visible.x || y + it->h * s <= visible.y ||
                x >= visible.x + visible.w || y >= visible.y + visible.h || it->w <= 0 || it->h <= 0 )
                break;
            /* Clipped to the visible well, as MODELVIEW's preview: the
             * picture may start left of or above the well. */
            p = dbg_prim_push(ui);
            if( !p )
                break;
            p->kind = TORIRS_CHROME_PRIM_SPRITE;
            p->x = x;
            p->y = y;
            p->w = it->w * s;
            p->h = it->h * s;
            p->sprite_scene_id = it->scene_id;
            p->trans = it->trans;
            p->clip = visible;
            break;
        }
        default:
            break;
        }
    }
}

/**
 * Draw one editor row of a WINDOW panel. @return 0 for a kind this file does
 * not own, so the caller's own switch carries on.
 */
static int
dbg_ext_draw(
    struct ToriRSChrome* ui,
    struct ToriRSChromeWidget* w,
    int widget,
    int row_x,
    int row_y,
    int row_h,
    int hovered,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;

    switch( w->kind )
    {
    case TORIRS_CHROME_W_TREE:
    {
        int const x = row_x + w->iaux * DBG_TREE_INDENT;
        int const selected = w->checked;
        uint32_t ink = selected ? th->accent : hovered ? th->menu_hover_text : th->text;

        if( selected )
            dbg_push_rect_trans(
                ui, row_x, row_y, w->w, row_h, th->dropdown_veil, 1,
                th->dropdown_band_trans_alt, clip);
        if( w->ivalue != TORIRS_CHROME_TREE_LEAF )
        {
            int const slot = w->ivalue == TORIRS_CHROME_TREE_OPEN ? TORIRS_CHROME_SKIN_TREE_OPEN
                                                                  : TORIRS_CHROME_SKIN_TREE_CLOSED;
            if( !dbg_push_icon(ui, slot, x + DBG_TREE_EXP / 2, row_y + row_h / 2, 0, clip) )
                dbg_push_text(
                    ui, x + DBG_PX(4), dbg_row_text_baseline(ui, row_y, row_h),
                    w->ivalue == TORIRS_CHROME_TREE_OPEN ? "v" : ">", th->text_dim,
                    ui->theme.font_row, 0, clip);
        }
        if( w->color )
            ink = w->color;
        {
            /* "name\tchord": the part after the tab at the row's right edge. */
            char left[TORIRS_CHROME_LABEL_MAX];
            char const* right = dbg_tab_split(w->label, left, (int)sizeof(left));
            int const baseline = dbg_row_text_baseline(ui, row_y, row_h);
            /* The prim borrows its string: the left part goes through the
             * build's scratch, the right part is the label's own tail. */
            dbg_push_text(
                ui, x + DBG_TREE_EXP + DBG_TREE_GAP, baseline,
                right ? dbg_wrap_line(ui, w->label, (int)(right - 1 - w->label)) : w->label, ink, ui->theme.font_row,
                0, clip);
            if( right )
                dbg_push_text(
                    ui, row_x + w->w - DBG_PX(4) - ToriRSChrome_MeasureTextQ(ui->theme.font_row, ui->scale_q, right),
                    baseline, right, ink, ui->theme.font_row, 0, clip);
        }
        return 1;
    }

    case TORIRS_CHROME_W_VLIST:
    case TORIRS_CHROME_W_CODE:
        dbg_draw_list(ui, w, widget, row_x, row_y, hovered, clip);
        return 1;

    case TORIRS_CHROME_W_GRAPH:
        dbg_draw_graph(ui, w, widget, clip);
        return 1;

    case TORIRS_CHROME_W_ICONBUTTON:
        dbg_draw_iconbutton(ui, w, row_x, row_y, hovered, clip);
        if( w->label[0] )
            dbg_push_text(
                ui, row_x + DBG_ICON_BOX + DBG_CHECK_GAP, dbg_row_text_baseline(ui, row_y, row_h),
                w->label, w->disabled ? th->text_dim : (hovered ? th->accent : th->text),
                ui->theme.font_row, 0, clip);
        return 1;

    case TORIRS_CHROME_W_SLIDER:
    {
        struct ToriRSChromeRect const t = dbg_slider_track(ui, w);
        int const travel = t.w - DBG_KNOB;
        int const span = w->imax - w->imin;
        int const knob_x =
            t.x + (travel > 0 && span > 0
                       ? (int)((int64_t)(w->ivalue - w->imin) * travel / span)
                       : 0);
        int const mid_y = row_y + row_h / 2;
        char value[16];
        int n;

        if( w->label[0] )
            dbg_push_text(
                ui, row_x, dbg_row_text_baseline(ui, row_y, row_h), w->label,
                w->color ? w->color : th->text_dim, ui->theme.font_row, 0, clip);
        if( th->skin_dropdown && dbg_skin_has(ui, TORIRS_CHROME_SKIN_SLIDER_TRACK)
            && dbg_skin_has(ui, TORIRS_CHROME_SKIN_SLIDER_TRACK_LEFT)
            && dbg_skin_has(ui, TORIRS_CHROME_SKIN_SLIDER_TRACK_RIGHT) && t.w >= 2 * DBG_SLIDER_CAP_W )
        {
            /* The settings tab's track, one continuous bar: the pointed caps
             * at the two ends and the plain middle repeated between them,
             * each drawn as a box at the chrome's scale (a native-size blit
             * stepped by a scaled pitch leaves a gap after every tile). */
            int const y = mid_y - DBG_SLIDER_SKIN_H / 2;
            int const mid_x0 = t.x + DBG_SLIDER_CAP_W;
            int const mid_x1 = t.x + t.w - DBG_SLIDER_CAP_W;
            struct ToriRSChromeRect middle = { mid_x0, y, mid_x1 - mid_x0, DBG_SLIDER_SKIN_H };
            struct ToriRSChromeRect const middle_clip = dbg_rect_clip(clip, middle);

            dbg_push_sprite_box(
                ui, t.x, y, DBG_SLIDER_CAP_W, DBG_SLIDER_SKIN_H, TORIRS_CHROME_SKIN_SLIDER_TRACK_LEFT, clip);
            if( middle_clip.w > 0 && middle_clip.h > 0 )
                for( int x = mid_x0; x < mid_x1; x += DBG_SLIDER_TILE_W )
                    dbg_push_sprite_box(
                        ui, x, y, DBG_SLIDER_TILE_W, DBG_SLIDER_SKIN_H, TORIRS_CHROME_SKIN_SLIDER_TRACK,
                        middle_clip);
            dbg_push_sprite_box(
                ui, mid_x1, y, DBG_SLIDER_CAP_W, DBG_SLIDER_SKIN_H, TORIRS_CHROME_SKIN_SLIDER_TRACK_RIGHT,
                clip);
        }
        else
        {
            dbg_push_rect(
                ui, t.x, mid_y - DBG_SLIDER_TRACK_H / 2, t.w, DBG_SLIDER_TRACK_H, th->scroll_track,
                1, clip);
            dbg_push_rect(
                ui, t.x, mid_y - DBG_SLIDER_TRACK_H / 2, t.w, DBG_SLIDER_TRACK_H,
                th->dropdown_border, 0, clip);
        }
        if( !(th->skin_dropdown &&
              dbg_push_icon(
                  ui, TORIRS_CHROME_SKIN_SLIDER_KNOB, knob_x + DBG_KNOB / 2, mid_y,
                  w->disabled ? 128 : 0, clip)) )
            dbg_push_rect(
                ui, knob_x, mid_y - DBG_KNOB / 2, DBG_KNOB, DBG_KNOB,
                hovered || ui->slider_drag == widget ? th->accent : th->scroll_grip, 1, clip);
        if( hovered || ui->focus == widget || ui->slider_drag == widget )
            dbg_push_rect(ui, knob_x, mid_y - DBG_KNOB / 2, DBG_KNOB, DBG_KNOB, th->accent, 0, clip);
        n = snprintf(value, sizeof(value), "%d", w->ivalue);
        {
            char const* text = dbg_wrap_line(ui, value, n);
            if( text )
                dbg_push_text(
                    ui,
                    row_x + w->w - ToriRSChrome_MeasureTextQ(ui->theme.font_row, ui->scale_q, text),
                    dbg_row_text_baseline(ui, row_y, row_h), text, th->text, ui->theme.font_row, 0,
                    clip);
        }
        return 1;
    }

    case TORIRS_CHROME_W_RADIO:
    {
        int const size = DBG_PX(TORIRS_CHROME_M_BOX);
        int const box_y = row_y + (row_h - size) / 2;
        int const slot = w->checked ? TORIRS_CHROME_SKIN_RADIO_ON : TORIRS_CHROME_SKIN_RADIO_OFF;
        int const fallback = ToriRSChrome_CheckSlot(TORIRS_CHROME_CHECK_STYLE_TICK, w->checked);

        if( th->skin_dropdown && dbg_skin_has(ui, slot) )
            dbg_push_sprite_box(ui, row_x, box_y, size, size, slot, clip);
        else if( th->skin_dropdown && dbg_skin_has(ui, fallback) )
            dbg_push_sprite_box(ui, row_x, box_y, size, size, fallback, clip);
        else
        {
            dbg_push_rect(ui, row_x, box_y, size, size, th->input_bg, 1, clip);
            dbg_push_rect(
                ui, row_x, box_y, size, size, hovered ? th->accent : th->check_box, 0, clip);
            if( w->checked )
                dbg_push_rect(
                    ui, row_x + DBG_PX(4), box_y + DBG_PX(4), size - DBG_PX(8), size - DBG_PX(8),
                    th->check_mark, 1, clip);
        }
        dbg_push_text(
            ui, row_x + size + DBG_CHECK_GAP, dbg_row_text_baseline(ui, row_y, row_h), w->label,
            w->color ? w->color : (hovered ? th->accent : th->text), ui->theme.font_row, 0, clip);
        return 1;
    }

    default:
        return 0;
    }
}

/* ---- the TOOLBAR panel ---------------------------------------------------------------- */

static void
dbg_build_toolbar(struct ToriRSChrome* ui, struct ToriRSChromePanel* p)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    int const pad = DBG_ICON_GAP + DBG_RULE;
    int const pitch = DBG_ICON_BOX + DBG_ICON_GAP;
    int shown = 0;
    int per_row;
    int rows;
    int index = 0;
    struct ToriRSChromeRect clip;

    for( int i = p->first_widget; i >= 0; i = ui->widgets[i].next )
        if( ui->widgets[i].kind == TORIRS_CHROME_W_ICONBUTTON &&
            dbg_widget_shown(ui, &ui->widgets[i]) )
            shown++;
    if( p->fixed_w > 0 )
    {
        per_row = (p->fixed_w - 2 * pad + DBG_ICON_GAP) / pitch;
        if( per_row < 1 )
            per_row = 1;
    }
    else
        per_row = shown > 0 ? shown : 1;
    rows = shown > 0 ? (shown + per_row - 1) / per_row : 1;
    p->w = p->fixed_w > 0 ? p->fixed_w : 2 * pad + per_row * pitch - DBG_ICON_GAP;
    p->h = p->fixed_h > 0 ? p->fixed_h : 2 * pad + rows * pitch - DBG_ICON_GAP;
    p->content_y = p->y + pad;
    p->content_h = rows * pitch;
    p->view_h = p->h - 2 * pad;

    clip.x = p->x;
    clip.y = p->y;
    clip.w = p->w;
    clip.h = p->h;
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->panel_body, 1, clip);
    if( th->skin_panel_body && dbg_skin_has(ui, TORIRS_CHROME_SKIN_PANEL_BODY) )
        dbg_fill_tiled(
            ui, p->x, p->y, p->w, p->h, TORIRS_CHROME_SKIN_PANEL_BODY, ui->skin_tile_w,
            ui->skin_tile_h, clip);
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->panel_border, 0, clip);

    for( int i = p->first_widget; i >= 0; i = ui->widgets[i].next )
    {
        struct ToriRSChromeWidget* w = &ui->widgets[i];
        if( w->kind != TORIRS_CHROME_W_ICONBUTTON || !dbg_widget_shown(ui, w) )
        {
            /* A strip holds buttons: anything else takes no space and no clicks. */
            w->x = w->y = w->w = w->h = 0;
            continue;
        }
        w->x = p->x + pad + (index % per_row) * pitch;
        w->y = p->y + pad + (index / per_row) * pitch;
        w->w = DBG_ICON_BOX;
        w->h = DBG_ICON_BOX;
        if( w->y + w->h > p->y + p->h )
            w->w = w->h = 0;
        else
            dbg_draw_iconbutton(ui, w, w->x, w->y, ui->hover == i, clip);
        index++;
    }
}

/* ---- input ------------------------------------------------------------------------------- */

/** A press on an editor row. @return 1 when it was one of ours and is handled
 *  (the caller returns), 0 to let the existing chain carry on. */
static int
dbg_ext_mouse_down(struct ToriRSChrome* ui, int hit, int x, int y)
{
    struct ToriRSChromeWidget* w = &ui->widgets[hit];

    switch( w->kind )
    {
    case TORIRS_CHROME_W_SLIDER:
        if( w->disabled )
            return 1;
        (void)ToriRSChrome_FocusWidget(ui, hit, 0);
        ui->slider_drag = hit;
        ToriRSChrome_SliderSet(ui, hit, dbg_slider_value_at(ui, w, x));
        dbg_dirty_widget_paint(ui, hit);
        return 1;
    case TORIRS_CHROME_W_GRAPH:
    {
        struct ToriRSChromeRect region;
        struct ToriRSChromeRect clip;
        if( !dbg_graph_region(ui, hit, &region, &clip) || !dbg_point_in_rect(x, y, clip) )
        {
            ui->press = -1;
            dbg_focus_release(ui);
            return 1;
        }
        (void)ToriRSChrome_FocusWidget(ui, hit, 0);
        ui->graph_drag = hit;
        (void)dbg_graph_push(ui, hit, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
        return 1;
    }
    case TORIRS_CHROME_W_VLIST:
    case TORIRS_CHROME_W_CODE:
        if( dbg_list_on_bar(ui, w, x, y) )
        {
            /* The bar pages: above the grip goes up a window, below goes down. */
            struct ToriRSChromeRect const box = dbg_list_box(ui, w);
            int const line_h = w->kind == TORIRS_CHROME_W_CODE ? DBG_CODE_LINE_H : DBG_VROW_H;
            int const grip_y = box.y + DBG_RULE + (w->imax > 0 ? w->imin * w->rows * line_h / w->imax : 0);
            ToriRSChrome_VListSetScroll(ui, hit, w->imin + (y < grip_y ? -w->rows : w->rows));
            ui->press = -1;
            return 1;
        }
        (void)ToriRSChrome_FocusWidget(ui, hit, w->caret);
        if( w->kind == TORIRS_CHROME_W_CODE )
        {
            int const row = dbg_list_row_at(ui, w, x, y);
            if( row >= 0 )
            {
                char const* text = dbg_vrow_text(ui, w, row);
                int col = 0;
                if( text )
                {
                    struct ToriRSChromeRect const box = dbg_list_box(ui, w);
                    int const tx = box.x + DBG_RULE + dbg_code_gutter_w(ui, w) + DBG_INPUT_PAD_X;
                    int const len = (int)strlen(text);
                    while( col < len &&
                           dbg_measure_prefix(TORIRS_CHROME_FONT_MENU, ui->scale_q, text, col + 1) <=
                               x - tx )
                        col++;
                }
                ToriRSChrome_CodeSetCaret(ui, hit, row, col);
            }
        }
        return 1;
    default:
        return 0;
    }
}

static int
dbg_ext_mouse_move(struct ToriRSChrome* ui, int x, int y)
{
    if( ui->split_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->split_panel) )
            dbg_split_drag_to(ui, x, y);
        else
            ui->split_panel = -1;
        return 1;
    }
    if( ui->slider_drag >= 0 )
    {
        if( dbg_valid_widget(ui, ui->slider_drag) )
            ToriRSChrome_SliderSet(
                ui, ui->slider_drag, dbg_slider_value_at(ui, &ui->widgets[ui->slider_drag], x));
        else
            ui->slider_drag = -1;
        return 1;
    }
    if( ui->graph_drag >= 0 )
    {
        if( dbg_valid_widget(ui, ui->graph_drag) )
            (void)dbg_graph_push(ui, ui->graph_drag, TORIRS_CHROME_EVENT_GRAPH_DRAG, x, y, 0);
        else
            ui->graph_drag = -1;
        return 1;
    }
    return 0;
}

/** Hover bookkeeping after the ordinary hit test: graph hover keys, and the
 *  tooltip going away the moment the pointer leaves its widget. */
static void
dbg_ext_hover(struct ToriRSChrome* ui, int hit, int x, int y)
{
    if( ui->tooltip_widget >= 0 && hit != ui->tooltip_widget )
        dbg_tooltip_hide(ui);
    if( hit >= 0 && ui->widgets[hit].kind == TORIRS_CHROME_W_GRAPH )
    {
        struct ToriRSChromeRect region;
        struct ToriRSChromeRect clip;
        int key = 0;
        int const scale = dbg_graph_q(ui);

        if( dbg_graph_region(ui, hit, &region, &clip) && dbg_point_in_rect(x, y, clip) )
            key = ToriRSChrome_GraphKeyAt(
                ui, hit, ToriRSChrome_UnscalePx(scale, x - region.x),
                ToriRSChrome_UnscalePx(scale, y - region.y));
        if( hit != ui->graph_hover || key != ui->graph_hover_key )
        {
            ui->graph_hover = hit;
            ui->graph_hover_key = key;
            (void)dbg_graph_push(ui, hit, TORIRS_CHROME_EVENT_GRAPH_HOVER, x, y, 0);
        }
        return;
    }
    if( ui->graph_hover >= 0 )
    {
        int const left = ui->graph_hover;
        ui->graph_hover = -1;
        ui->graph_hover_key = 0;
        if( dbg_valid_widget(ui, left) )
        {
            struct ToriRSChromeEvent ev;
            dbg_event_init(&ev, ui, TORIRS_CHROME_EVENT_GRAPH_HOVER, left);
            ev.x = -1;
            ev.y = -1;
            (void)ToriRSChrome_PushEvent(ui, &ev);
        }
    }
}

static int
dbg_ext_mouse_up(struct ToriRSChrome* ui, int x, int y)
{
    if( ui->split_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->split_panel) )
            dbg_split_drag_to(ui, x, y);
        ui->split_panel = -1;
        ui->press = -1;
        return 1;
    }
    if( ui->slider_drag >= 0 )
    {
        int const slider = ui->slider_drag;
        ui->slider_drag = -1;
        ui->press = -1;
        if( dbg_valid_widget(ui, slider) )
        {
            ToriRSChrome_SliderSet(ui, slider, dbg_slider_value_at(ui, &ui->widgets[slider], x));
            ui->activated = slider;
            dbg_dirty_widget_paint(ui, slider);
        }
        return 1;
    }
    if( ui->graph_drag >= 0 )
    {
        int const graph = ui->graph_drag;
        ui->graph_drag = -1;
        ui->press = -1;
        if( dbg_valid_widget(ui, graph) )
            (void)dbg_graph_push(ui, graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x, y, 0);
        return 1;
    }
    return 0;
}

/** A completed click (press and release on the same row). @return 1 when the
 *  row was one of ours. */
static int
dbg_ext_click(struct ToriRSChrome* ui, int hit, int x, int y)
{
    struct ToriRSChromeWidget* w = &ui->widgets[hit];

    switch( w->kind )
    {
    case TORIRS_CHROME_W_TREE:
    {
        int const exp_x = w->x + w->iaux * DBG_TREE_INDENT;
        if( w->ivalue != TORIRS_CHROME_TREE_LEAF && x >= exp_x && x < exp_x + DBG_TREE_EXP )
        {
            ToriRSChrome_TreeSetExpander(
                ui, hit,
                w->ivalue == TORIRS_CHROME_TREE_OPEN ? TORIRS_CHROME_TREE_CLOSED
                                                     : TORIRS_CHROME_TREE_OPEN);
            ui->activated = hit;
            ui->activated_expand = 1;
            return 1;
        }
        ToriRSChrome_TreeSelect(ui, hit);
        ui->activated = hit;
        ui->activated_expand = 0;
        return 1;
    }
    case TORIRS_CHROME_W_RADIO:
        if( !w->checked )
            ToriRSChrome_RadioSelect(ui, hit);
        ui->activated = hit;
        return 1;
    case TORIRS_CHROME_W_ICONBUTTON:
        if( !w->disabled )
            ui->activated = hit;
        dbg_dirty_widget_paint(ui, hit);
        return 1;
    case TORIRS_CHROME_W_VLIST:
    {
        int const row = dbg_list_row_at(ui, w, x, y);
        if( row >= 0 )
        {
            /* A cells list says which keycap the click was on (0: the body). */
            w->vlist_cell = w->iaux ? dbg_cells_cell_at(ui, w, row, x) : 0;
            ToriRSChrome_VListSetSelected(ui, hit, row);
            ui->activated = hit;
        }
        return 1;
    }
    case TORIRS_CHROME_W_CODE:
    case TORIRS_CHROME_W_SLIDER:
    case TORIRS_CHROME_W_GRAPH:
        return 1;
    default:
        return 0;
    }
}

static int
dbg_ext_wheel(struct ToriRSChrome* ui, int x, int y, int delta)
{
    int const hit = ToriRSChrome_HitTest(ui, x, y);
    struct ToriRSChromeWidget* w;

    if( hit < 0 )
        return 0;
    w = &ui->widgets[hit];
    switch( w->kind )
    {
    case TORIRS_CHROME_W_VLIST:
    case TORIRS_CHROME_W_CODE:
        ToriRSChrome_VListSetScroll(ui, hit, w->imin + delta * 3);
        return 1;
    case TORIRS_CHROME_W_SLIDER:
        if( !w->disabled )
        {
            int const before = w->ivalue;
            ToriRSChrome_SliderSet(ui, hit, w->ivalue - delta * (w->istep > 0 ? w->istep : 1));
            if( w->ivalue != before )
                ui->activated = hit;
        }
        return 1;
    case TORIRS_CHROME_W_GRAPH:
    {
        struct ToriRSChromeRect region;
        struct ToriRSChromeRect clip;
        if( dbg_graph_region(ui, hit, &region, &clip) && dbg_point_in_rect(x, y, clip) )
            (void)dbg_graph_push(ui, hit, TORIRS_CHROME_EVENT_GRAPH_WHEEL, x, y, delta);
        return 1;
    }
    default:
        return 0;
    }
}

/** The GRAPH well whose visible part holds surface (x, y), or -1. */
static int
dbg_graph_at(struct ToriRSChrome* ui, int x, int y)
{
    int const hit = ToriRSChrome_HitTest(ui, x, y);
    struct ToriRSChromeRect region;
    struct ToriRSChromeRect clip;

    if( hit < 0 || ui->widgets[hit].kind != TORIRS_CHROME_W_GRAPH )
        return -1;
    if( !dbg_graph_region(ui, hit, &region, &clip) || !dbg_point_in_rect(x, y, clip) )
        return -1;
    return hit;
}

int
ToriRSChrome_Pinch(struct ToriRSChrome* ui, int x, int y, double scale)
{
    int graph;
    double q;

    assert(ui);
    assert(scale > 0.0);
    if( dbg_modal_blocks(ui, x, y) )
        return 1;
    /* An open list or colour picker is over everything; a pinch there has
     * nothing to scale and must not reach a well underneath. */
    if( ui->dropdown_open >= 0 || ui->colorpick_open >= 0 )
        return 0;
    graph = dbg_graph_at(ui, x, y);
    if( graph < 0 )
        return 0;
    q = scale * 65536.0 + 0.5;
    if( q < 1.0 )
        q = 1.0;
    if( q > 2147483000.0 )
        q = 2147483000.0;
    (void)dbg_graph_push(ui, graph, TORIRS_CHROME_EVENT_GRAPH_PINCH, x, y, (int)q);
    return 1;
}

static int
dbg_q8(double v)
{
    double const q = v * 256.0;
    if( q > 2147483000.0 )
        return 2147483000;
    if( q < -2147483000.0 )
        return -2147483000;
    return (int)(q < 0 ? q - 0.5 : q + 0.5);
}

int
ToriRSChrome_Scroll(struct ToriRSChrome* ui, int x, int y, double dx, double dy)
{
    int graph;
    int scale;
    struct ToriRSChromeRect region;
    struct ToriRSChromeRect clip;
    struct ToriRSChromeEvent ev;

    assert(ui);
    scale = dbg_graph_q(ui);
    if( dbg_modal_blocks(ui, x, y) )
        return 1;
    if( ui->dropdown_open >= 0 || ui->colorpick_open >= 0 )
        return 0;
    graph = dbg_graph_at(ui, x, y);
    if( graph < 0 || !dbg_graph_region(ui, graph, &region, &clip) )
        return 0;
    dbg_event_init(&ev, ui, TORIRS_CHROME_EVENT_GRAPH_SCROLL, graph);
    ev.x = ToriRSChrome_UnscalePx(scale, x - region.x);
    ev.y = ToriRSChrome_UnscalePx(scale, y - region.y);
    ev.key = ToriRSChrome_GraphKeyAt(ui, graph, ev.x, ev.y);
    ev.dx = dbg_q8(dx * (double)TORIRS_CHROME_SCALE_Q_ONE / (double)scale);
    ev.dy = dbg_q8(dy * (double)TORIRS_CHROME_SCALE_Q_ONE / (double)scale);
    (void)ToriRSChrome_PushEvent(ui, &ev);
    return 1;
}

int
ToriRSChrome_WheelFrame(struct ToriRSChrome* ui, struct ToriRSChromeWheelFrame const* frame)
{
    int consumed = 0;
    int wheel_spent = 0;

    assert(ui);
    assert(frame);

    if( frame->pinch_scale > 0.0f &&
        ToriRSChrome_Pinch(ui, frame->pinch_x, frame->pinch_y, (double)frame->pinch_scale) )
    {
        consumed = 1;
        if( frame->pinch_from_wheel )
            wheel_spent = 1;
    }

    if( !wheel_spent && frame->ctrl && (frame->wheel != 0 || frame->scroll_dy != 0.0f) &&
        dbg_graph_at(ui, frame->x, frame->y) >= 0 )
    {
        /* Notches in the integer wheel's own direction (positive up): the
         * exact deltas when there are any, the integer wheel otherwise. */
        double notches = (double)frame->wheel;
        if( frame->px_per_notch > 0 && frame->scroll_dy != 0.0f )
        {
            notches = (double)frame->scroll_dy / (double)frame->px_per_notch;
            if( frame->flipped )
                notches = -notches;
        }
        if( notches != 0.0 )
        {
            /* GRAPH_WHEEL's direction: a notch up (positive) zooms OUT. */
            double const scale = exp(-notches * log(TORIRS_CHROME_PINCH_WHEEL_STEP));
            if( ToriRSChrome_Pinch(ui, frame->x, frame->y, scale) )
            {
                consumed = 1;
                wheel_spent = 1;
            }
        }
    }

    if( !wheel_spent && frame->trackpad && (frame->scroll_dx != 0.0f || frame->scroll_dy != 0.0f) &&
        ToriRSChrome_Scroll(ui, frame->x, frame->y, (double)frame->scroll_dx, (double)frame->scroll_dy) )
    {
        consumed = 1;
        wheel_spent = 1;
    }

    if( !wheel_spent && frame->wheel != 0 &&
        ToriRSChrome_MouseWheel(ui, frame->x, frame->y, frame->wheel) )
        consumed = 1;
    return consumed;
}

static int
dbg_ext_secondary(struct ToriRSChrome* ui, int x, int y)
{
    int const hit = ToriRSChrome_HitTest(ui, x, y);
    struct ToriRSChromeRect region;
    struct ToriRSChromeRect clip;

    if( hit < 0 || ui->widgets[hit].kind != TORIRS_CHROME_W_GRAPH )
        return 0;
    if( !dbg_graph_region(ui, hit, &region, &clip) || !dbg_point_in_rect(x, y, clip) )
        return 0;
    return dbg_graph_push(ui, hit, TORIRS_CHROME_EVENT_GRAPH_MENU, x, y, 0);
}

/* ---- keys ---------------------------------------------------------------------------------- */

/** Queue a CODE edit and mirror its effect on the visible line, so the model
 *  shows what was typed until the host's document restates it. */
static void
dbg_code_edit(
    struct ToriRSChrome* ui, int widget, int kind, int line, int col, char const* text)
{
    struct ToriRSChromeEvent ev;

    dbg_event_init(&ev, ui, kind, widget);
    ev.line = line;
    ev.col = col;
    dbg_copy(ev.text, TORIRS_CHROME_INPUT_MAX, text ? text : "");
    (void)ToriRSChrome_PushEvent(ui, &ev);
    if( kind == TORIRS_CHROME_EVENT_CODE_REPLACE )
        ToriRSChrome_VListSetRow(ui, widget, line, text);
}

static int
dbg_ext_key_char(struct ToriRSChrome* ui, int ch)
{
    struct ToriRSChromeWidget* w = &ui->widgets[ui->focus];
    char const* text;
    char line[TORIRS_CHROME_INPUT_MAX];
    int len;
    int col;

    if( w->kind == TORIRS_CHROME_W_GRAPH )
    {
        /* The zoom keys go to the host; anything else is not the well's. */
        if( ch != '+' && ch != '-' && ch != '=' && ch != '_' )
            return 0;
        dbg_graph_key(ui, ui->focus, ch);
        return 1;
    }
    if( w->kind != TORIRS_CHROME_W_CODE )
        return 0;
    if( ch < 0x20 || ch > 0x7E )
        return 1;
    text = dbg_vrow_text(ui, w, w->ivalue);
    if( !text )
        return 1;
    len = (int)strlen(text);
    if( len >= TORIRS_CHROME_INPUT_MAX - 1 )
        return 1;
    col = w->caret < len ? w->caret : len;
    memcpy(line, text, (size_t)col);
    line[col] = (char)ch;
    memcpy(line + col + 1, text + col, (size_t)(len - col) + 1);
    dbg_code_edit(ui, ui->focus, TORIRS_CHROME_EVENT_CODE_REPLACE, w->ivalue, col + 1, line);
    ToriRSChrome_CodeSetCaret(ui, ui->focus, w->ivalue, col + 1);
    return 1;
}

static int
dbg_ext_key_edit(struct ToriRSChrome* ui, int key)
{
    int const focus = ui->focus;
    struct ToriRSChromeWidget* w = &ui->widgets[focus];

    switch( w->kind )
    {
    case TORIRS_CHROME_W_GRAPH:
        /* The arrows, Enter and Backspace are the host's (the square picker
         * moves, opens and goes back with them: workbench 6.4); Escape is
         * the host's too, and then releases the well. */
        if( key == TORIRS_CHROME_KEY_LEFT || key == TORIRS_CHROME_KEY_RIGHT || key == TORIRS_CHROME_KEY_UP ||
            key == TORIRS_CHROME_KEY_DOWN || key == TORIRS_CHROME_KEY_ENTER || key == TORIRS_CHROME_KEY_BACKSPACE ||
            key == TORIRS_CHROME_KEY_ESCAPE )
            dbg_graph_key(ui, focus, -key);
        if( key == TORIRS_CHROME_KEY_ESCAPE )
            (void)ToriRSChrome_FocusWidget(ui, -1, 0);
        /* Every other editing key stops at the well, as before. */
        return 1;

    case TORIRS_CHROME_W_SLIDER:
        if( key == TORIRS_CHROME_KEY_LEFT || key == TORIRS_CHROME_KEY_RIGHT ||
            key == TORIRS_CHROME_KEY_HOME || key == TORIRS_CHROME_KEY_END )
        {
            int const before = w->ivalue;
            int const step = w->istep > 0 ? w->istep : 1;
            int next = key == TORIRS_CHROME_KEY_LEFT    ? w->ivalue - step
                       : key == TORIRS_CHROME_KEY_RIGHT ? w->ivalue + step
                       : key == TORIRS_CHROME_KEY_HOME  ? w->imin
                                                        : w->imax;
            ToriRSChrome_SliderSet(ui, focus, next);
            if( w->ivalue != before )
                ui->activated = focus;
            return 1;
        }
        if( key == TORIRS_CHROME_KEY_ESCAPE )
        {
            (void)ToriRSChrome_FocusWidget(ui, -1, 0);
            return 1;
        }
        return 0;

    case TORIRS_CHROME_W_VLIST:
    {
        int row = w->ivalue;
        if( key == TORIRS_CHROME_KEY_UP )
            row = row > 0 ? row - 1 : 0;
        else if( key == TORIRS_CHROME_KEY_DOWN )
            row = row + 1 < w->imax ? row + 1 : w->imax - 1;
        else if( key == TORIRS_CHROME_KEY_HOME )
            row = 0;
        else if( key == TORIRS_CHROME_KEY_END )
            row = w->imax - 1;
        else if( key == TORIRS_CHROME_KEY_ENTER )
        {
            w->vlist_cell = 0;
            if( w->ivalue >= 0 )
                ui->activated = focus;
            return 1;
        }
        else if( key == TORIRS_CHROME_KEY_ESCAPE )
        {
            (void)ToriRSChrome_FocusWidget(ui, -1, 0);
            return 1;
        }
        else
            return 0;
        if( row < 0 )
            return 1;
        w->vlist_cell = 0;
        ToriRSChrome_VListSetSelected(ui, focus, row);
        if( row < w->imin )
            ToriRSChrome_VListSetScroll(ui, focus, row);
        else if( row >= w->imin + w->rows )
            ToriRSChrome_VListSetScroll(ui, focus, row - w->rows + 1);
        ui->activated = focus;
        return 1;
    }

    case TORIRS_CHROME_W_CODE:
    {
        char const* text = dbg_vrow_text(ui, w, w->ivalue);
        int const len = text ? (int)strlen(text) : 0;
        int const col = w->caret < len ? w->caret : len;
        int const line = w->ivalue;
        char buf[TORIRS_CHROME_INPUT_MAX];

        switch( key )
        {
        case TORIRS_CHROME_KEY_LEFT:
            if( col > 0 )
                ToriRSChrome_CodeSetCaret(ui, focus, line, col - 1);
            else if( line > 0 )
                ToriRSChrome_CodeSetCaret(ui, focus, line - 1, TORIRS_CHROME_INPUT_MAX);
            return 1;
        case TORIRS_CHROME_KEY_RIGHT:
            if( col < len )
                ToriRSChrome_CodeSetCaret(ui, focus, line, col + 1);
            else if( line + 1 < w->imax )
                ToriRSChrome_CodeSetCaret(ui, focus, line + 1, 0);
            return 1;
        case TORIRS_CHROME_KEY_UP:
            if( line > 0 )
                ToriRSChrome_CodeSetCaret(ui, focus, line - 1, col);
            return 1;
        case TORIRS_CHROME_KEY_DOWN:
            if( line + 1 < w->imax )
                ToriRSChrome_CodeSetCaret(ui, focus, line + 1, col);
            return 1;
        case TORIRS_CHROME_KEY_HOME:
            ToriRSChrome_CodeSetCaret(ui, focus, line, 0);
            return 1;
        case TORIRS_CHROME_KEY_END:
            ToriRSChrome_CodeSetCaret(ui, focus, line, len);
            return 1;
        case TORIRS_CHROME_KEY_ENTER:
            if( !text )
                return 1;
            dbg_code_edit(ui, focus, TORIRS_CHROME_EVENT_CODE_SPLIT, line, col, text);
            return 1;
        case TORIRS_CHROME_KEY_BACKSPACE:
            if( !text )
                return 1;
            if( col > 0 )
            {
                memcpy(buf, text, (size_t)(col - 1));
                memcpy(buf + col - 1, text + col, (size_t)(len - col) + 1);
                dbg_code_edit(ui, focus, TORIRS_CHROME_EVENT_CODE_REPLACE, line, col - 1, buf);
                ToriRSChrome_CodeSetCaret(ui, focus, line, col - 1);
            }
            else if( line > 0 )
                dbg_code_edit(ui, focus, TORIRS_CHROME_EVENT_CODE_JOIN, line - 1, 0, "");
            return 1;
        case TORIRS_CHROME_KEY_DELETE:
            if( !text )
                return 1;
            if( col < len )
            {
                memcpy(buf, text, (size_t)col);
                memcpy(buf + col, text + col + 1, (size_t)(len - col));
                dbg_code_edit(ui, focus, TORIRS_CHROME_EVENT_CODE_REPLACE, line, col, buf);
            }
            else if( line + 1 < w->imax )
                dbg_code_edit(ui, focus, TORIRS_CHROME_EVENT_CODE_JOIN, line, col, "");
            return 1;
        case TORIRS_CHROME_KEY_ESCAPE:
            (void)ToriRSChrome_FocusWidget(ui, -1, 0);
            return 1;
        default:
            return 0;
        }
    }

    default:
        return 0;
    }
}

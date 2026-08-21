#include "uitree_debug_overlay.h"
#include <assert.h>

/* The only include with any content in it: `static const int` advance tables
 * emitted by fontbake. Nothing else in this file reaches outside the C
 * library — see the dependency note in the header. */
#include "uitree_debug_font_metrics.h"

#include <string.h>

/* ---- chrome metrics ------------------------------------------------------
 *
 * Client chrome, in the same category as UIMinimenu_LayoutFromLineBox: not
 * config, not content, no cache behind it. Everything derives from the two
 * baked fonts' line boxes so a different bake reflows rather than overlaps.
 *
 * Every one of them is a CHROME pixel, not a screen pixel: DBG_PX multiplies
 * by the overlay's scale, so the whole of this table is one authored size that
 * a HighDPI or scaled display reads at 2x or 3x. Scale 1 is the identity, so
 * the numbers below are still literally the geometry a 1x chrome lays out.
 */

/**
 * A chrome pixel count at this overlay's scale.
 *
 * Expands `ui`, exactly as DBG_DROP_ROW_H already did. That is deliberate:
 * every chrome metric is read inside a function that has the ToriRSChrome in
 * hand, so a site that does not have one fails to COMPILE rather than
 * silently laying itself out at 1x underneath text drawn at 3x. Scaled
 * geometry that goes wrong quietly is the failure mode worth designing out --
 * it looks like a rendering bug, not like a missing multiply.
 */
#define DBG_PX(px) ((px) * ui->scale)
/** The chrome's hairline: every rule, rail and border inset is one of these.
 *  A 1px rule under a 3x font reads as a rendering fault, not as a border. */
#define DBG_RULE DBG_PX(1)

/** Inner left/right padding of a window panel's content column. */
#define DBG_PAD_X DBG_PX(5)
/** Inner top/bottom padding of a window panel's content column. */
#define DBG_PAD_Y DBG_PX(4)
/** Extra pixels between consecutive rows. */
#define DBG_ROW_GAP DBG_PX(2)
/** Checkbox square edge. */
#define DBG_CHECK_SIZE DBG_PX(9)
/** Gap between a checkbox/label and what follows it. */
#define DBG_CHECK_GAP DBG_PX(5)
/** Horizontal padding inside a text input's box. */
#define DBG_INPUT_PAD_X DBG_PX(3)
/** Vertical padding inside a text input's box. */
#define DBG_INPUT_PAD_Y DBG_PX(2)
/** A text input never lays out narrower than this, even when empty. */
#define DBG_INPUT_MIN_W DBG_PX(60)
/**
 * Edge of the arrow button on a closed dropdown, and the width of a scrollbar.
 *
 * One number because in the reference it is one sprite: the same 16x16 arrow
 * (graphic_788 down, graphic_773 up) sits on the left of every closed dropdown
 * and at both ends of every scrollbar, and ~script31 builds the bar 16 wide to
 * hold it. Scaled here, so a 3x chrome gets a 48px bar with the arrow blown up
 * to fill it rather than a 16px sprite marooned in a 48px column.
 */
#define DBG_SCROLL_W DBG_PX(16)
#define DBG_DROP_ARROW_W DBG_SCROLL_W
/** The scrollbar grip's two fixed caps (~script31: `cc_setsize(0, 5, 1, 0)`). */
#define DBG_SCROLL_CAP_H DBG_PX(5)
/** Shortest the grip may get, caps included (~script31: `if ($height9 < 10)`). */
#define DBG_SCROLL_GRIP_MIN DBG_PX(10)
/** Air around the open list's rows (script_9114 sizes it `$int26 + 4`). */
#define DBG_DROP_LIST_PAD DBG_PX(2)
/** Row pitch inside the open dropdown list.
 *
 *  The reference's rows are 20 tall around a 14px line. Written as the line
 *  box plus its air rather than as 20 so a re-bake reflows the list instead of
 *  cropping it, which is the same rule every other metric here follows. */
#define DBG_DROP_ROW_H (ToriRSChrome_FontLineBox(ui->theme.font_row, ui->scale) + DBG_PX(4))
/** Height of a separator row: a rule with air above and below. */
#define DBG_SEP_H DBG_PX(5)
/** Air around a button's caption, inside its border. Wider than it is tall, so
 *  a one-word button still reads as a box rather than as a framed glyph. */
#define DBG_BUTTON_PAD_X DBG_PX(8)
#define DBG_BUTTON_PAD_Y DBG_PX(3)
/** Air around a tab's caption. The vertical pad is the tab's whole height over
 *  the line box; there is no bottom border on the selected tab, which is what
 *  joins it to the content below (see dbg_push_tabstrip). */
#define DBG_TAB_PAD_X DBG_PX(7)
#define DBG_TAB_PAD_Y DBG_PX(3)
/** Narrowest a compressed tab may get before its caption is simply clipped. */
#define DBG_TAB_MIN_W DBG_PX(12)
/** Narrowest a panel may be, before borders. */
#define DBG_MIN_CONTENT_W DBG_PX(16)
/** Edge of one dot in a resize grip's caret. */
#define DBG_GRIP_DOT DBG_PX(2)
/** Centre-to-centre step between grip dots, along both axes. */
#define DBG_GRIP_PITCH DBG_PX(3)
/** Carets in a grip: three nested chevrons, the shortest in the corner. A
 *  COUNT, not a length -- a 2x grip is the same three chevrons drawn twice the
 *  size, not six of them. */
#define DBG_GRIP_LINES 3
/** The grip's drawn extent, both axes. */
#define DBG_GRIP_SIZE ((DBG_GRIP_LINES - 1) * DBG_GRIP_PITCH + DBG_GRIP_DOT)
/** The grip's grab box: the drawn triangle plus a pixel of slop each way. */
#define DBG_GRIP_HIT (DBG_GRIP_SIZE + DBG_PX(2))
/** Narrowest a panel may be dragged. Below this the grip and the title bar
 *  would be fighting over the same pixels. */
#define DBG_MIN_PANEL_W (DBG_GRIP_HIT + 2 * DBG_PAD_X + DBG_PX(2))
/** Shortest a panel may be dragged: its header block, a pad, and the strip the
 *  grip lives in. Anything less is a panel with no room to grab. */
#define DBG_MIN_PANEL_H                                                                       \
    (ToriRSChrome_FontLineBox(TORIDBG_FONT_MENU, ui->scale) + DBG_PX(3) + DBG_PAD_Y +          \
     DBG_GRIP_HIT + DBG_RULE)

struct ToriDbgTheme const toridbg_theme_default = {
    .panel_body = 0x232323,
    .panel_border = 0x6E6E60,
    .panel_title_bg = 0x000000,
    .panel_title_text = 0xC8C8C8,
    .text = 0xDCDCDC,
    .text_dim = 0x8C8C8C,
    .accent = 0xFFFF00,
    .input_bg = 0x101010,
    .input_border = 0x5A5A50,
    .input_border_focus = 0xFFFF00,
    .input_text = 0xFFFFFF,
    .check_box = 0x9A9A8C,
    .check_mark = 0x50FF50,
    /* The minimenu's own palette (UITREE_MINIMENU_COLOR_BODY and the black
     * chrome strips emit_minimenu draws), so a debug menu and a game minimenu
     * on screen together read as the same widget. */
    .menu_body = 0x5D5447,
    .menu_chrome = 0x000000,
    .menu_text = 0xFFFFFF,
    .menu_hover_text = 0xFFFF00,
    .separator = 0x4B4B4B,
    /* The flat look keeps the dropdown's *structure* -- inset button, veiled
     * rows, a veil under the cursor -- and only drops the reference palette
     * for its own grey one. The bands are near-invisible (250/255) so the
     * zebra reads as a hairline rather than as stripes on a grey panel, and
     * the hover veil is the one that has to be obvious. */
    .dropdown_border = 0x1A1A1A,
    .dropdown_border_inner = 0x5A5A50,
    .dropdown_text = 0xDCDCDC,
    .dropdown_veil = 0x000000,
    .dropdown_band_trans = 250,
    .dropdown_band_trans_alt = 255,
    .dropdown_row_trans_hover = 150,
    .dropdown_hover_trans = 220,
    .scroll_track = 0x23201B,
    .scroll_grip = 0x4D4233,
    .scroll_grip_hi = 0x766654,
    .scroll_grip_lo = 0x332D25,
    .text_shadowed = 0,
    .font_row = TORIDBG_FONT_SMALL,
    .skin_panel_body = 0,
    .skin_dropdown = 0,
};

/*
 * The reference interface palette.
 *
 * Note what this is NOT: an invented brown. Every constant below already
 * appears in the client -- 0x5D5447 is UITREE_MINIMENU_COLOR_BODY, and the
 * black chrome and 0xFFFF00 hover are what emit_minimenu draws. Picking them
 * from what the game draws rather than from a screenshot is what keeps a panel
 * and a real widget agreeing when they sit next to each other.
 */
struct ToriDbgTheme const toridbg_theme_osrs = {
    .panel_body = 0x5D5447,
    /* Black, like every menu and interface edge the game draws. */
    .panel_border = 0x000000,
    .panel_title_bg = 0x000000,
    /* The minimenu's own title colour -- the body brown on the black bar, which
     * is exactly what emit_minimenu draws "Choose Option" in. It used to be the
     * interfaces' heading orange, and a panel titled in orange next to a
     * minimenu titled in brown read as two different widgets. */
    .panel_title_text = 0x5D5447,
    .text = 0xFFFFFF,
    .text_dim = 0xC8C8C8,
    .accent = 0xFFFF00,
    /* Text entry is black-on-black-bordered in the reference, not inset grey. */
    .input_bg = 0x000000,
    .input_border = 0x3E3529,
    .input_border_focus = 0xFFFF00,
    .input_text = 0xFFFFFF,
    .check_box = 0x000000,
    /* The green/red pair the interfaces use for on/off state. */
    .check_mark = 0x00FF00,
    .menu_body = 0x5D5447,
    .menu_chrome = 0x000000,
    .menu_text = 0xFFFFFF,
    .menu_hover_text = 0xFFFF00,
    .separator = 0x000000,
    /* script_3850, verbatim: the closed button is graphic_297 tiled, framed in
     * 0x0e0e0c with a 0x474745 inset, its value set in 0xff981f. */
    .dropdown_border = 0x0E0E0C,
    .dropdown_border_inner = 0x474745,
    .dropdown_text = 0xFF981F,
    /* script_9114, verbatim: black bands at 220 and 200, 240 under the
     * cursor. The hovered row is the LEAST veiled one, so the highlight is
     * the tiled body showing through rather than a colour painted on. */
    .dropdown_veil = 0x000000,
    .dropdown_band_trans = 220,
    .dropdown_band_trans_alt = 200,
    .dropdown_row_trans_hover = 240,
    .dropdown_hover_trans = 220,
    .scroll_track = 0x23201B,
    .scroll_grip = 0x4D4233,
    .scroll_grip_hi = 0x766654,
    .scroll_grip_lo = 0x332D25,
    .text_shadowed = 1,
    /* p12: the face the reference sets interface body text in. Rows grow from
     * the 12px debug box to the game's 16px one, which is the point -- this
     * chrome is meant to sit beside real widgets, not below them. */
    .font_row = TORIDBG_FONT_BODY,
    .skin_panel_body = 1,
    .skin_dropdown = 1,
};

/*
 * The minimenu's geometry, recomputed here rather than included.
 *
 * This is UIMinimenu_LayoutFromLineBox (src/ui/uitree_minimenu.c) verbatim.
 * It is duplicated because including uitree_minimenu.h would drag uitree.h and
 * the whole ui/ layer into a module whose entire point is that it has no
 * dependencies. The duplication is pinned by a test that asserts the two agree
 * for every line box (uitree_test_debug_overlay.c), so it cannot drift
 * silently — if that test fails, this block is the one that is wrong.
 */
struct DbgMenuLayout
{
    int line_height;
    int row_stride;
    int header_bar_h;
    int separator_y;
    int option_base_y;
    int chrome_h;
    int hover_above;
    int hover_below;
    int width_pad;
    int border_inset;
};

static struct DbgMenuLayout
dbg_menu_layout(int line_box, int scale)
{
    int const box = line_box > 0 ? line_box : ToriDbgFont_Menu_LINE_BOX * scale;
    struct DbgMenuLayout l;
    /* `box` arrives already scaled (it is a scaled font's line box); the
     * constants around it are chrome pixels and are scaled here, so the whole
     * layout is the 1x layout multiplied. At scale 1 this is
     * UIMinimenu_LayoutFromLineBox verbatim, which is what the pin test in
     * uitree_test_debug_overlay.c compares against. */
    l.line_height = box - 2 * scale;
    l.row_stride = box - scale;
    l.header_bar_h = box;
    l.separator_y = box + 2 * scale;
    l.option_base_y = 2 * box - scale;
    l.chrome_h = box + 5 * scale;
    l.hover_above = box - 3 * scale;
    l.hover_below = 3 * scale;
    l.width_pad = 8 * scale;
    l.border_inset = box + 3 * scale;
    return l;
}

/* ---- text metrics -------------------------------------------------------- */

/*
 * The (slot, scale) pair, resolved against the bake.
 *
 * Nine tables, not three multiplied by a factor: an advance is the pen step of
 * a glyph that was BAKED at this size, so reading the 2x table is the only way
 * to measure what the 2x renderer will actually draw. The bake happens to make
 * the two agree exactly (fontbake scales every metric by the same integer),
 * and that is a property worth having rather than one worth assuming -- the
 * scaled-metrics test asserts it.
 */
static int const*
dbg_advance_table(int font_slot, int scale)
{
    assert(scale >= TORIDBG_SCALE_MIN);
    assert(scale <= TORIDBG_SCALE_MAX);
    switch( font_slot )
    {
    case TORIDBG_FONT_MENU:
        return scale == 3   ? ToriDbgFont_Menu3x_advance_px
               : scale == 2 ? ToriDbgFont_Menu2x_advance_px
                            : ToriDbgFont_Menu_advance_px;
    case TORIDBG_FONT_BODY:
        return scale == 3   ? ToriDbgFont_Body3x_advance_px
               : scale == 2 ? ToriDbgFont_Body2x_advance_px
                            : ToriDbgFont_Body_advance_px;
    default:
        return scale == 3   ? ToriDbgFont_Small3x_advance_px
               : scale == 2 ? ToriDbgFont_Small2x_advance_px
                            : ToriDbgFont_Small_advance_px;
    }
}

int
ToriRSChrome_FontLineHeight(int font_slot, int scale)
{
    assert(scale >= TORIDBG_SCALE_MIN);
    assert(scale <= TORIDBG_SCALE_MAX);
    switch( font_slot )
    {
    case TORIDBG_FONT_MENU:
        return scale == 3   ? ToriDbgFont_Menu3x_LINE_HEIGHT
               : scale == 2 ? ToriDbgFont_Menu2x_LINE_HEIGHT
                            : ToriDbgFont_Menu_LINE_HEIGHT;
    case TORIDBG_FONT_BODY:
        return scale == 3   ? ToriDbgFont_Body3x_LINE_HEIGHT
               : scale == 2 ? ToriDbgFont_Body2x_LINE_HEIGHT
                            : ToriDbgFont_Body_LINE_HEIGHT;
    default:
        return scale == 3   ? ToriDbgFont_Small3x_LINE_HEIGHT
               : scale == 2 ? ToriDbgFont_Small2x_LINE_HEIGHT
                            : ToriDbgFont_Small_LINE_HEIGHT;
    }
}

int
ToriRSChrome_FontLineBox(int font_slot, int scale)
{
    assert(scale >= TORIDBG_SCALE_MIN);
    assert(scale <= TORIDBG_SCALE_MAX);
    switch( font_slot )
    {
    case TORIDBG_FONT_MENU:
        return scale == 3   ? ToriDbgFont_Menu3x_LINE_BOX
               : scale == 2 ? ToriDbgFont_Menu2x_LINE_BOX
                            : ToriDbgFont_Menu_LINE_BOX;
    case TORIDBG_FONT_BODY:
        return scale == 3   ? ToriDbgFont_Body3x_LINE_BOX
               : scale == 2 ? ToriDbgFont_Body2x_LINE_BOX
                            : ToriDbgFont_Body_LINE_BOX;
    default:
        return scale == 3   ? ToriDbgFont_Small3x_LINE_BOX
               : scale == 2 ? ToriDbgFont_Small2x_LINE_BOX
                            : ToriDbgFont_Small_LINE_BOX;
    }
}

int
ToriRSChrome_MeasureText(int font_slot, int scale, char const* text)
{
    int const* adv = dbg_advance_table(font_slot, scale);
    int w = 0;
    unsigned char const* p;

    assert(text);
    for( p = (unsigned char const*)text; *p; p++ )
        w += adv[*p];
    return w;
}

/** Width of the first `len` bytes — what the caret position is measured from. */
static int
dbg_measure_prefix(int font_slot, int scale, char const* text, int len)
{
    int const* adv = dbg_advance_table(font_slot, scale);
    int w = 0;

    for( int i = 0; i < len && text[i]; i++ )
        w += adv[(unsigned char)text[i]];
    return w;
}

/* ---- small helpers ------------------------------------------------------- */

/* Defined with the dropdown machinery below; needed earlier by SetHidden. */
static void
dbg_dropdown_close(struct ToriRSChrome* ui);

/**
 * Offset from a labelled row's left edge to its control box.
 *
 * THE one answer to "where does the box start" -- widths, draws and the
 * dropdown popup all ask here, which is what keeps a table panel's popup
 * opening under the box it belongs to rather than under where the box would
 * have been without the table. In a table panel every row shares the panel's
 * label column; otherwise each row sits right after its own label.
 */
static int
dbg_row_box_offset(struct ToriRSChrome const* ui, struct ToriDbgWidget const* w)
{
    struct ToriDbgPanel const* p = &ui->panels[w->panel];
    int label_w;

    if( p->table && p->label_col > 0 )
        return p->label_col + DBG_CHECK_GAP;
    label_w = w->label[0] ? ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label) : 0;
    return label_w + (label_w > 0 ? DBG_CHECK_GAP : 0);
}


static int
dbg_max(int a, int b)
{
    return a > b ? a : b;
}

/** Is (x, y) inside `r`? The rect-taking form; dbg_point_in takes four ints. */
static int
dbg_point_in_rect(int x, int y, struct ToriDbgRect r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int
dbg_valid_panel(struct ToriRSChrome const* ui, int panel)
{
    return ui && panel >= 0 && panel < ui->panel_count;
}

static int
dbg_valid_widget(struct ToriRSChrome const* ui, int widget)
{
    return ui && widget >= 0 && widget < ui->widget_count &&
           ui->widgets[widget].kind != TORIDBG_W_FREE;
}

/**
 * Does this row take part in layout, drawing and hit testing?
 *
 * The one answer, asked by the measuring pass, the drawing pass and the hit
 * test alike. Skipping in only one of them is the bug this exists to prevent:
 * a row measured but not drawn leaves a hole, and a row drawn but not measured
 * runs off the bottom border -- both of which this file has had before, which
 * is why the comment at the drawing site says so.
 */
static int
dbg_widget_shown(struct ToriRSChrome const* ui, struct ToriDbgWidget const* w)
{
    if( w->hidden || w->kind == TORIDBG_W_FREE )
        return 0;
    /* A row belonging to no tab in particular shows on all of them; one with a
     * tab shows only on its own. The strip itself carries -1, so it survives
     * every switch -- a strip that hid itself could not be clicked back. */
    if( w->tab >= 0 && w->tab != ui->panels[w->panel].active_tab )
        return 0;
    return 1;
}

static void
dbg_copy(char* dst, int cap, char const* src)
{
    int i = 0;
    if( src )
        for( ; i < cap - 1 && src[i]; i++ )
            dst[i] = src[i];
    dst[i] = '\0';
}

/** Mark a panel for relayout. Also flags the model so Build stops being a no-op. */
static void
dbg_dirty_panel(struct ToriRSChrome* ui, int panel)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    ui->panels[panel].dirty = 1;
    ui->dirty = 1;
}

void
ToriRSChrome_PanelSetTable(struct ToriRSChrome* ui, int panel, int table)
{
    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( ui->panels[panel].table == (table ? 1 : 0) )
        return;
    ui->panels[panel].table = table ? 1 : 0;
    dbg_dirty_panel(ui, panel);
}

static void
dbg_dirty_widget(struct ToriRSChrome* ui, int widget)
{
    if( dbg_valid_widget(ui, widget) )
        dbg_dirty_panel(ui, ui->widgets[widget].panel);
}

/** Union `r` into the accumulated invalid region. Empty boxes contribute nothing. */
static void
dbg_damage_add(struct ToriRSChrome* ui, struct ToriDbgRect r)
{
    int x0;
    int y0;
    int x1;
    int y1;

    if( r.w <= 0 || r.h <= 0 )
        return;
    if( ui->damage.w <= 0 || ui->damage.h <= 0 )
    {
        ui->damage = r;
        return;
    }
    x0 = ui->damage.x < r.x ? ui->damage.x : r.x;
    y0 = ui->damage.y < r.y ? ui->damage.y : r.y;
    x1 = ui->damage.x + ui->damage.w > r.x + r.w ? ui->damage.x + ui->damage.w : r.x + r.w;
    y1 = ui->damage.y + ui->damage.h > r.y + r.h ? ui->damage.y + ui->damage.h : r.y + r.h;
    ui->damage.x = x0;
    ui->damage.y = y0;
    ui->damage.w = x1 - x0;
    ui->damage.h = y1 - y0;
}

/* ---- lifecycle ----------------------------------------------------------- */

void
ToriRSChrome_Init(struct ToriRSChrome* ui)
{
    assert(ui);
    memset(ui, 0, sizeof(*ui));
    /* The reference look is the default: this chrome is the editor's face, and
     * the editor is a tool for this game. A caller that wants the flat debug
     * palette assigns `toridbg_theme_default` over this -- no env read here,
     * because reading one would be this module's first dependency outside the
     * C library and the whole point of it is that it has none. */
    ui->theme = toridbg_theme_osrs;
    /* Native size. Zero (the memset) would multiply every metric to nothing,
     * which draws an empty chrome that looks like a failed boot. */
    ui->scale = 1;
    ui->focus = -1;
    ui->hover = -1;
    ui->press = -1;
    ui->activated = -1;
    ui->dropdown_open = -1;
    ui->drag_panel = -1;
    ui->resize_panel = -1;
    ui->free_widget = -1;
    ui->scroll_panel = -1;
    ui->caret_visible = 1;
}

void
ToriRSChrome_Reset(struct ToriRSChrome* ui)
{
    assert(ui);
    /* Everything on screen is going away, so all of it is invalid. */
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_damage_add(ui, ui->panels[i].last_rect);
    ui->panel_count = 0;
    ui->dropdown_open = -1;
    ui->dropdown_hover_row = -1;
    ui->dropdown_scroll_drag = 0;
    ui->widget_count = 0;
    /* The whole array is gone, so the free list that indexed into it is too --
     * leaving it would hand out slots above the new high-water mark. */
    ui->free_widget = -1;
    ui->prim_count = 0;
    ui->focus = -1;
    ui->hover = -1;
    ui->press = -1;
    ui->activated = -1;
    ui->drag_panel = -1;
    ui->resize_panel = -1;
    ui->scroll_panel = -1;
    ui->overflow = 0;
    ui->dirty = 1;
}

void
ToriRSChrome_SetScale(struct ToriRSChrome* ui, int scale)
{
    assert(ui);
    assert(scale >= TORIDBG_SCALE_MIN);
    assert(scale <= TORIDBG_SCALE_MAX);
    if( ui->scale == scale )
        return;
    /* Every panel is about to change size, so the boxes they occupy NOW are
     * stale as well as the ones they are about to occupy: damage the old
     * bounds before the relayout overwrites them. */
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_damage_add(ui, ui->panels[i].last_rect);
    ui->scale = scale;
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_dirty_panel(ui, i);
    ui->dirty = 1;
}

int
ToriRSChrome_Scale(struct ToriRSChrome const* ui)
{
    assert(ui);
    return ui->scale;
}

void
ToriRSChrome_SetTheme(struct ToriRSChrome* ui, struct ToriDbgTheme const* theme)
{
    assert(ui);
    assert(theme);
    if( memcmp(&ui->theme, theme, sizeof(*theme)) == 0 )
        return;
    ui->theme = *theme;
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_dirty_panel(ui, i);
}

/* ---- building ------------------------------------------------------------ */

int
ToriRSChrome_PanelAdd(struct ToriRSChrome* ui, int style, int x, int y, int fixed_w, char const* title)
{
    struct ToriDbgPanel* p;
    int const handle = ui ? ui->panel_count : -1;

    if( !ui || ui->panel_count >= TORIDBG_MAX_PANELS )
    {
        if( ui )
            ui->overflow = 1;
        return -1;
    }
    p = &ui->panels[handle];
    memset(p, 0, sizeof(*p));
    p->style = style;
    p->visible = 1;
    p->x = x;
    p->y = y;
    p->fixed_w = fixed_w;
    p->first_widget = -1;
    p->last_widget = -1;
    /* -1 is "every tab": a panel with no strip must not have its rows stamped
     * onto tab 0, or adding a strip later would hide all of them at once. */
    p->build_tab = -1;
    p->dirty = 1;
    dbg_copy(p->title, TORIDBG_LABEL_MAX, title);
    ui->panel_count++;
    ui->dirty = 1;
    return handle;
}

void
ToriRSChrome_PanelMove(struct ToriRSChrome* ui, int panel, int x, int y)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( ui->panels[panel].x == x && ui->panels[panel].y == y )
        return;
    ui->panels[panel].x = x;
    ui->panels[panel].y = y;
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelSetVisible(struct ToriRSChrome* ui, int panel, int visible)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    visible = visible ? 1 : 0;
    if( ui->panels[panel].visible == visible )
        return;
    ui->panels[panel].visible = visible;
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelSetResizable(struct ToriRSChrome* ui, int panel, int resizable)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    resizable = resizable ? 1 : 0;
    if( ui->panels[panel].resizable == resizable )
        return;
    ui->panels[panel].resizable = resizable;
    /* The grip is part of the panel's chrome, so turning it on or off is a
     * relayout of that panel and nothing else. */
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelSetFixedWidth(struct ToriRSChrome* ui, int panel, int width)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( width < 0 )
        width = 0;
    if( ui->panels[panel].fixed_w == width )
        return;
    /* The box it occupies now is about to be vacated on one side or the
     * other, so it is stale either way. */
    dbg_damage_add(ui, ui->panels[panel].last_rect);
    ui->panels[panel].fixed_w = width;
    dbg_dirty_panel(ui, panel);
}

struct ToriDbgRect
ToriRSChrome_PanelRect(struct ToriRSChrome const* ui, int panel)
{
    struct ToriDbgRect none = { 0, 0, 0, 0 };
    if( !dbg_valid_panel(ui, panel) )
        return none;
    return ui->panels[panel].last_rect;
}

static int
dbg_widget_add(struct ToriRSChrome* ui, int panel, int kind)
{
    struct ToriDbgWidget* w;
    int handle;

    if( !dbg_valid_panel(ui, panel) )
        return -1;
    /* A recycled slot before a fresh one, so a panel rebuilt every frame stays
     * inside the array instead of walking the high-water mark up to the cap. */
    if( ui->free_widget >= 0 )
    {
        handle = ui->free_widget;
        ui->free_widget = ui->widgets[handle].next;
    }
    else if( ui->widget_count < TORIDBG_MAX_WIDGETS )
    {
        handle = ui->widget_count++;
    }
    else
    {
        ui->overflow = 1;
        return -1;
    }
    w = &ui->widgets[handle];
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    w->panel = panel;
    w->next = -1;
    w->tab = ui->panels[panel].build_tab;

    if( ui->panels[panel].first_widget < 0 )
        ui->panels[panel].first_widget = handle;
    else
        ui->widgets[ui->panels[panel].last_widget].next = handle;
    ui->panels[panel].last_widget = handle;

    dbg_dirty_panel(ui, panel);
    return handle;
}

/** Drop every latch that names `widget`, so nothing outlives its target. */
static void
dbg_widget_forget(struct ToriRSChrome* ui, int widget)
{
    if( ui->focus == widget )
        ui->focus = -1;
    if( ui->hover == widget )
        ui->hover = -1;
    if( ui->press == widget )
        ui->press = -1;
    if( ui->activated == widget )
        ui->activated = -1;
    if( ui->dropdown_open == widget )
    {
        ui->dropdown_open = -1;
        ui->dropdown_hover_row = -1;
        ui->dropdown_scroll_drag = 0;
    }
}

void
ToriRSChrome_WidgetRemove(struct ToriRSChrome* ui, int widget)
{
    struct ToriDbgPanel* p;
    int prev;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) )
        return;
    p = &ui->panels[ui->widgets[widget].panel];

    /* Unlink from the panel's singly-linked row list. A walk rather than a back
     * pointer: rebuilds clear whole panels at a time (which never walks), and a
     * second link to keep in step is a second link to get wrong. */
    if( p->first_widget == widget )
    {
        p->first_widget = ui->widgets[widget].next;
    }
    else
    {
        for( prev = p->first_widget; prev >= 0; prev = ui->widgets[prev].next )
            if( ui->widgets[prev].next == widget )
            {
                ui->widgets[prev].next = ui->widgets[widget].next;
                break;
            }
    }
    if( p->last_widget == widget )
    {
        int last = -1;
        for( prev = p->first_widget; prev >= 0; prev = ui->widgets[prev].next )
            last = prev;
        p->last_widget = last;
    }

    dbg_widget_forget(ui, widget);
    dbg_dirty_panel(ui, ui->widgets[widget].panel);

    memset(&ui->widgets[widget], 0, sizeof(ui->widgets[widget]));
    ui->widgets[widget].kind = TORIDBG_W_FREE;
    ui->widgets[widget].next = ui->free_widget;
    ui->free_widget = widget;
}

void
ToriRSChrome_PanelClearWidgets(struct ToriRSChrome* ui, int panel)
{
    int widget;

    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;

    widget = ui->panels[panel].first_widget;
    while( widget >= 0 )
    {
        int const next = ui->widgets[widget].next;
        dbg_widget_forget(ui, widget);
        memset(&ui->widgets[widget], 0, sizeof(ui->widgets[widget]));
        ui->widgets[widget].kind = TORIDBG_W_FREE;
        ui->widgets[widget].next = ui->free_widget;
        ui->free_widget = widget;
        widget = next;
    }
    ui->panels[panel].first_widget = -1;
    ui->panels[panel].last_widget = -1;
    /* The panel is about to be rebuilt from the top, so the builder's tab
     * stamp goes back to "every tab" rather than whatever the last run left. */
    ui->panels[panel].build_tab = -1;
    ui->panels[panel].scroll_y = 0;
    dbg_dirty_panel(ui, panel);
}

int
ToriRSChrome_Label(struct ToriRSChrome* ui, int panel, char const* text)
{
    return ToriRSChrome_LabelColored(ui, panel, text, 0);
}

int
ToriRSChrome_ModelView(struct ToriRSChrome* ui, int panel, int w, int h)
{
    int const handle = dbg_widget_add(ui, panel, TORIDBG_W_MODELVIEW);
    if( handle < 0 )
        return -1;
    ui->widgets[handle].view_w = w > 0 ? w : 64;
    ui->widgets[handle].view_h = h > 0 ? h : 64;
    ui->widgets[handle].view_scene_id = 0;
    return handle;
}

void
ToriRSChrome_ModelViewSet(struct ToriRSChrome* ui, int widget, int scene_sprite_id)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) )
        return;
    if( ui->widgets[widget].view_scene_id == scene_sprite_id )
        return;
    ui->widgets[widget].view_scene_id = scene_sprite_id;
    dbg_dirty_widget(ui, widget);
}



int
ToriRSChrome_LabelColored(struct ToriRSChrome* ui, int panel, char const* text, uint32_t color)
{
    int const h = dbg_widget_add(ui, panel, TORIDBG_W_LABEL);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].text, TORIDBG_INPUT_MAX, text);
    ui->widgets[h].color = color;
    return h;
}

int
ToriRSChrome_Checkbox(struct ToriRSChrome* ui, int panel, char const* label, int checked)
{
    int const h = dbg_widget_add(ui, panel, TORIDBG_W_CHECKBOX);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIDBG_LABEL_MAX, label);
    ui->widgets[h].checked = checked ? 1 : 0;
    return h;
}

int
ToriRSChrome_TextInput(struct ToriRSChrome* ui, int panel, char const* label, char const* text)
{
    int const h = dbg_widget_add(ui, panel, TORIDBG_W_TEXTINPUT);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIDBG_LABEL_MAX, label);
    dbg_copy(ui->widgets[h].text, TORIDBG_INPUT_MAX, text);
    ui->widgets[h].caret = (int)strlen(ui->widgets[h].text);
    return h;
}

/** Keep `selected` inside the list and the scroll window over the selection. */
static void
dbg_dropdown_clamp(struct ToriDbgWidget* w)
{
    int rows;

    if( w->selected >= w->option_count )
        w->selected = w->option_count - 1;
    if( w->selected < -1 )
        w->selected = -1;

    rows = w->option_count < TORIDBG_DROPDOWN_ROWS ? w->option_count : TORIDBG_DROPDOWN_ROWS;
    if( w->scroll > w->option_count - rows )
        w->scroll = w->option_count - rows;
    if( w->scroll < 0 )
        w->scroll = 0;
}

int
ToriRSChrome_MenuDrop(
    struct ToriRSChrome* ui,
    int panel,
    char const* title,
    char const* const* options,
    int option_count)
{
    int const h = ToriRSChrome_Dropdown(ui, panel, "", options, option_count, -1);
    if( h < 0 )
        return -1;
    /* The title rides in `text`, like a MENUITEM's, because menu-mode draws it
     * as the whole closed state; `label` stays empty so the dropdown-rect
     * label offset is zero and the list opens flush under the title. */
    dbg_copy(ui->widgets[h].text, TORIDBG_INPUT_MAX, title);
    ui->widgets[h].menu_mode = 1;
    ui->widgets[h].selected = -1;
    return h;
}

void
ToriRSChrome_SetHidden(struct ToriRSChrome* ui, int widget, int hidden)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) )
        return;
    if( ui->widgets[widget].hidden == (hidden ? 1 : 0) )
        return;
    ui->widgets[widget].hidden = hidden ? 1 : 0;
    dbg_dirty_panel(ui, ui->widgets[widget].panel);
    /* Hiding the open dropdown takes its popup with it. */
    if( hidden && ui->dropdown_open == widget )
        dbg_dropdown_close(ui);
}

int
ToriRSChrome_Dropdown(
    struct ToriRSChrome* ui,
    int panel,
    char const* label,
    char const* const* options,
    int option_count,
    int selected)
{
    int const h = dbg_widget_add(ui, panel, TORIDBG_W_DROPDOWN);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIDBG_LABEL_MAX, label);
    ui->widgets[h].options = options;
    ui->widgets[h].option_count = options ? option_count : 0;
    ui->widgets[h].selected = selected;
    dbg_dropdown_clamp(&ui->widgets[h]);
    /* Open on the selection so a palette of hundreds does not open at the top
     * with the current choice off screen. */
    ui->widgets[h].scroll = ui->widgets[h].selected > 0 ? ui->widgets[h].selected : 0;
    dbg_dropdown_clamp(&ui->widgets[h]);
    return h;
}

void
ToriRSChrome_DropdownSetOptions(
    struct ToriRSChrome* ui,
    int widget,
    char const* const* options,
    int option_count,
    int selected)
{
    struct ToriDbgWidget* w;

    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    w->options = options;
    w->option_count = options ? option_count : 0;
    w->selected = selected;
    w->scroll = selected > 0 ? selected : 0;
    dbg_dropdown_clamp(w);
    /* The rows under an open list just changed identity; leaving it open would
     * let the next click select something the user never saw. */
    if( ui->dropdown_open == widget )
        ui->dropdown_open = -1;
    dbg_dirty_widget(ui, widget);
}

int
ToriRSChrome_DropdownSelected(struct ToriRSChrome const* ui, int widget)
{
    if( !dbg_valid_widget(ui, widget) )
        return -1;
    return ui->widgets[widget].selected;
}

void
ToriRSChrome_DropdownSetSelected(struct ToriRSChrome* ui, int widget, int selected)
{
    struct ToriDbgWidget* w;

    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    if( w->selected == selected )
        return;
    w->selected = selected;
    dbg_dropdown_clamp(w);
    dbg_dirty_widget(ui, widget);
}

int
ToriRSChrome_Separator(struct ToriRSChrome* ui, int panel)
{
    return dbg_widget_add(ui, panel, TORIDBG_W_SEPARATOR);
}

int
ToriRSChrome_MenuItem(struct ToriRSChrome* ui, int panel, char const* text)
{
    int const h = dbg_widget_add(ui, panel, TORIDBG_W_MENUITEM);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].text, TORIDBG_INPUT_MAX, text);
    return h;
}

int
ToriRSChrome_Button(struct ToriRSChrome* ui, int panel, char const* text)
{
    int const h = dbg_widget_add(ui, panel, TORIDBG_W_BUTTON);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].text, TORIDBG_INPUT_MAX, text);
    return h;
}

/* ---- tabs ---------------------------------------------------------------- */

int
ToriRSChrome_Tabs(
    struct ToriRSChrome* ui,
    int panel,
    char const* const* titles,
    int title_count,
    int selected)
{
    int h;

    assert(ui);
    assert(titles || title_count == 0);
    h = dbg_widget_add(ui, panel, TORIDBG_W_TABSTRIP);
    if( h < 0 )
        return -1;
    ui->widgets[h].options = titles;
    ui->widgets[h].option_count = title_count;
    /* The strip belongs to no tab: one that stamped itself with tab 0 would
     * vanish the moment tab 1 was chosen, taking the only way back with it. */
    ui->widgets[h].tab = -1;
    if( selected < 0 || selected >= title_count )
        selected = 0;
    ui->widgets[h].selected = selected;
    ui->panels[panel].active_tab = selected;
    return h;
}

void
ToriRSChrome_TabsSetTitles(
    struct ToriRSChrome* ui,
    int widget,
    char const* const* titles,
    int title_count)
{
    struct ToriDbgWidget* w;

    assert(ui);
    assert(titles || title_count == 0);
    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    if( w->options == titles && w->option_count == title_count )
        return;
    w->options = titles;
    w->option_count = title_count;
    if( w->selected >= title_count )
        w->selected = title_count > 0 ? title_count - 1 : 0;
    ui->panels[w->panel].active_tab = w->selected;
    dbg_dirty_widget(ui, widget);
}

int
ToriRSChrome_PanelActiveTab(struct ToriRSChrome const* ui, int panel)
{
    if( !dbg_valid_panel(ui, panel) )
        return 0;
    return ui->panels[panel].active_tab;
}

void
ToriRSChrome_PanelSetActiveTab(struct ToriRSChrome* ui, int panel, int tab)
{
    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( tab < 0 )
        tab = 0;
    if( ui->panels[panel].active_tab == tab )
        return;
    ui->panels[panel].active_tab = tab;
    /* The rows about to appear are a different set from the ones going away, so
     * a scroll offset measured against the old tab means nothing on the new
     * one -- and a tab that opened already scrolled reads as a broken panel. */
    ui->panels[panel].scroll_y = 0;
    /* Whatever was focused or hovered may be on the tab that just left. */
    if( ui->focus >= 0 && ui->widgets[ui->focus].panel == panel )
        ui->focus = -1;
    if( ui->hover >= 0 && ui->widgets[ui->hover].panel == panel )
        ui->hover = -1;
    if( ui->dropdown_open >= 0 && ui->widgets[ui->dropdown_open].panel == panel )
        dbg_dropdown_close(ui);
    for( int w = ui->panels[panel].first_widget; w >= 0; w = ui->widgets[w].next )
        if( ui->widgets[w].kind == TORIDBG_W_TABSTRIP )
        {
            ui->widgets[w].selected = tab;
            break;
        }
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelBeginTab(struct ToriRSChrome* ui, int panel, int tab)
{
    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    ui->panels[panel].build_tab = tab < 0 ? -1 : tab;
}

void
ToriRSChrome_PanelSetScrollable(struct ToriRSChrome* ui, int panel, int scrollable)
{
    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( ui->panels[panel].scrollable == (scrollable ? 1 : 0) )
        return;
    ui->panels[panel].scrollable = scrollable ? 1 : 0;
    if( !ui->panels[panel].scrollable )
        ui->panels[panel].scroll_y = 0;
    dbg_dirty_panel(ui, panel);
}

/* ---- mutation ------------------------------------------------------------ */

void
ToriRSChrome_SetText(struct ToriRSChrome* ui, int widget, char const* text)
{
    struct ToriDbgWidget* w;
    char buf[TORIDBG_INPUT_MAX];

    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    dbg_copy(buf, TORIDBG_INPUT_MAX, text);
    /* Compare-then-set is the whole point of retained mode: an app that
     * rewrites its frame counter every frame with the same string does no
     * work at all. */
    if( strcmp(w->text, buf) == 0 )
        return;
    memcpy(w->text, buf, sizeof(buf));
    if( w->caret > (int)strlen(w->text) )
        w->caret = (int)strlen(w->text);
    dbg_dirty_widget(ui, widget);
}

void
ToriRSChrome_SetLabel(struct ToriRSChrome* ui, int widget, char const* label)
{
    struct ToriDbgWidget* w;
    char buf[TORIDBG_LABEL_MAX];

    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    dbg_copy(buf, TORIDBG_LABEL_MAX, label);
    if( strcmp(w->label, buf) == 0 )
        return;
    memcpy(w->label, buf, sizeof(buf));
    dbg_dirty_widget(ui, widget);
}

void
ToriRSChrome_SetColor(struct ToriRSChrome* ui, int widget, uint32_t color)
{
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].color == color )
        return;
    ui->widgets[widget].color = color;
    dbg_dirty_widget(ui, widget);
}

void
ToriRSChrome_SetChecked(struct ToriRSChrome* ui, int widget, int checked)
{
    checked = checked ? 1 : 0;
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].checked == checked )
        return;
    ui->widgets[widget].checked = checked;
    dbg_dirty_widget(ui, widget);
}

int
ToriRSChrome_Checked(struct ToriRSChrome const* ui, int widget)
{
    return dbg_valid_widget(ui, widget) ? ui->widgets[widget].checked : 0;
}

char const*
ToriRSChrome_Text(struct ToriRSChrome const* ui, int widget)
{
    return dbg_valid_widget(ui, widget) ? ui->widgets[widget].text : "";
}

void
ToriRSChrome_SetCaretVisible(struct ToriRSChrome* ui, int visible)
{
    visible = visible ? 1 : 0;
    assert(ui);
    if( ui->caret_visible == visible )
        return;
    ui->caret_visible = visible;
    /* Only the panel holding the focused input repaints for a blink. */
    if( ui->focus >= 0 )
        dbg_dirty_widget(ui, ui->focus);
}

/* ---- layout -------------------------------------------------------------- */

/** Natural content width of one row, before the panel's padding. */
static int
dbg_widget_width(struct ToriRSChrome const* ui, struct ToriDbgWidget const* w)
{
    (void)ui;
    switch( w->kind )
    {
    case TORIDBG_W_LABEL:
        return w->text ? ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->text) : 0;
    case TORIDBG_W_CHECKBOX:
        return DBG_CHECK_SIZE + DBG_CHECK_GAP +
               (w->label ? ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label) : 0);
    case TORIDBG_W_TEXTINPUT:
    {
        int box_w = (w->text ? ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->text) : 0) +
                    2 * DBG_INPUT_PAD_X + 2 * DBG_RULE;
        if( box_w < DBG_INPUT_MIN_W )
            box_w = DBG_INPUT_MIN_W;
        return dbg_row_box_offset(ui, w) + box_w;
    }
    case TORIDBG_W_MENUITEM:
        return w->text ? ToriRSChrome_MeasureText(TORIDBG_FONT_MENU, ui->scale, w->text) : 0;
    case TORIDBG_W_DROPDOWN:
    {
        int box_w = DBG_INPUT_MIN_W;
        /* Sized to the widest option, so choosing one never resizes the panel
         * under the cursor. Palettes are built once, so this walk is not per
         * frame -- dbg_widget_width only runs on a dirty build. */
        for( int i = 0; i < w->option_count; i++ )
        {
            int const ow = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->options[i]);
            if( ow > box_w )
                box_w = ow;
        }
        box_w += 2 * DBG_INPUT_PAD_X + 2 * DBG_RULE + DBG_DROP_ARROW_W;
        return dbg_row_box_offset(ui, w) + box_w;
    }
    case TORIDBG_W_MODELVIEW:
        return w->view_w + 2 * DBG_RULE;
    case TORIDBG_W_BUTTON:
        return ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->text) +
               2 * DBG_BUTTON_PAD_X + 2 * DBG_RULE;
    case TORIDBG_W_TABSTRIP:
    {
        /* The natural width is every tab at its own caption width. A strip
         * wider than the panel is not clipped away, it is COMPRESSED at draw
         * time -- see dbg_tab_rect -- so this is the width that avoids that,
         * not a width the strip is guaranteed. */
        int total = 0;
        for( int i = 0; i < w->option_count; i++ )
            total += ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->options[i]) +
                     2 * DBG_TAB_PAD_X;
        return total;
    }
    case TORIDBG_W_SEPARATOR:
    default:
        return 0;
    }
}

/** Row height of one widget, excluding DBG_ROW_GAP. Takes the ToriRSChrome
 *  because row height follows the theme's row face, and a skin may pick a
 *  different one (see ToriDbgTheme::font_row). */
static int
dbg_widget_height(struct ToriRSChrome const* ui, struct ToriDbgWidget const* w)
{
    int const line = ToriRSChrome_FontLineBox(ui->theme.font_row, ui->scale);
    switch( w->kind )
    {
    case TORIDBG_W_CHECKBOX:
        return dbg_max(line, DBG_CHECK_SIZE);
    case TORIDBG_W_TEXTINPUT:
    case TORIDBG_W_DROPDOWN:
        return line + 2 * DBG_INPUT_PAD_Y + 2 * DBG_RULE;
    case TORIDBG_W_MODELVIEW:
        return w->view_h + 2 * DBG_RULE;
    case TORIDBG_W_BUTTON:
        return line + 2 * DBG_BUTTON_PAD_Y + 2 * DBG_RULE;
    case TORIDBG_W_TABSTRIP:
        return line + 2 * DBG_TAB_PAD_Y + DBG_RULE;
    case TORIDBG_W_SEPARATOR:
        return DBG_SEP_H;
    case TORIDBG_W_FREE:
        return 0;
    case TORIDBG_W_LABEL:
    default:
        return line;
    }
}

/* ---- display list -------------------------------------------------------- */

static struct ToriDbgPrim*
dbg_prim_push(struct ToriRSChrome* ui)
{
    struct ToriDbgPrim* p;
    if( ui->prim_count >= TORIDBG_MAX_PRIMS )
    {
        ui->overflow = 1;
        return NULL;
    }
    p = &ui->prims[ui->prim_count++];
    memset(p, 0, sizeof(*p));
    return p;
}

/** @param trans 0 opaque .. 255 invisible, the client's sense. */
static void
dbg_push_rect_trans(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int w,
    int h,
    uint32_t color,
    int filled,
    int trans,
    struct ToriDbgRect clip)
{
    struct ToriDbgPrim* p;
    if( w <= 0 || h <= 0 )
        return;
    /* Fully transparent is not a primitive. The reference builds invisible
     * rects because they still carry a click op; ours carry nothing, so one
     * here would be a display-list slot spent painting nothing. */
    if( trans >= 255 )
        return;
    p = dbg_prim_push(ui);
    if( !p )
        return;
    p->kind = TORIDBG_PRIM_RECT;
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
    p->color = color;
    p->filled = filled;
    p->trans = trans;
    p->clip = clip;
}

static void
dbg_push_rect(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int w,
    int h,
    uint32_t color,
    int filled,
    struct ToriDbgRect clip)
{
    dbg_push_rect_trans(ui, x, y, w, h, color, filled, 0, clip);
}

/** @param y the text baseline (ToriDraw2D_DrawString's y), not a box top. */
static void
dbg_push_text(
    struct ToriRSChrome* ui,
    int x,
    int y,
    char const* text,
    uint32_t color,
    int font_slot,
    int shadowed,
    struct ToriDbgRect clip)
{
    struct ToriDbgPrim* p;
    if( !text || !text[0] )
        return;
    p = dbg_prim_push(ui);
    if( !p )
        return;
    p->kind = TORIDBG_PRIM_TEXT;
    p->x = x;
    p->y = y;
    p->color = color;
    p->font_slot = font_slot;
    p->baseline = 1;
    /* OR, not assign: the theme flag can add the reference's drop shadow to
     * every string, but must never take one away from a call site that already
     * asked for it (the menu rows). */
    p->shadowed = shadowed || ui->theme.text_shadowed;
    p->text = text;
    p->clip = clip;
}

/** One baked image scaled into `w` x `h`; 0 x 0 means the image's own size. */
static void
dbg_push_sprite_box(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int w,
    int h,
    int slot,
    struct ToriDbgRect clip)
{
    struct ToriDbgPrim* p;

    /* 0 x 0 is the documented native-size request. A box with exactly one side
     * zero is a caller that computed a run and got it wrong, and silently
     * blitting the 16x5 grip middle at native size over a cap is not the
     * answer to that. */
    assert(w >= 0);
    assert(h >= 0);
    assert((w == 0) == (h == 0));
    p = dbg_prim_push(ui);
    if( !p )
        return;
    p->kind = TORIDBG_PRIM_SPRITE;
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
    p->sprite_slot = slot;
    p->clip = clip;
}

/** One baked image at its native size. */
static void
dbg_push_sprite(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int slot,
    struct ToriDbgRect clip)
{
    dbg_push_sprite_box(ui, x, y, 0, 0, slot, clip);
}

static int
dbg_skin_has(struct ToriRSChrome const* ui, int slot)
{
    return (ui->skin_avail & (1u << slot)) != 0;
}

/**
 * Fill a box by repeating a skin image across it, clipped to the box.
 *
 * The tile's own size is not known up here -- the drawer holds the pixels --
 * so this steps by a size the caller passes and lets the clip rect cut the
 * last row and column. Callers pass the size they uploaded; a wrong guess
 * costs overdraw at the edges, never a gap, because the step is what the
 * loop bounds use.
 */
static void
dbg_fill_tiled(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int w,
    int h,
    int slot,
    int tile_w,
    int tile_h,
    struct ToriDbgRect clip)
{
    if( tile_w <= 0 || tile_h <= 0 )
        return;
    for( int ty = y; ty < y + h; ty += tile_h )
        for( int tx = x; tx < x + w; tx += tile_w )
            dbg_push_sprite(ui, tx, ty, slot, clip);
}

/** The overlap of two boxes; w or h comes back 0 when they do not meet. */
static struct ToriDbgRect
dbg_rect_clip(struct ToriDbgRect a, struct ToriDbgRect b)
{
    struct ToriDbgRect r;
    int const x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int const y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;

    r.x = a.x > b.x ? a.x : b.x;
    r.y = a.y > b.y ? a.y : b.y;
    r.w = x1 > r.x ? x1 - r.x : 0;
    r.h = y1 > r.y ? y1 - r.y : 0;
    return r;
}

/* ---- scrollbars ----------------------------------------------------------
 *
 * The client's bar, both of the ways the client draws it.
 *
 * With the baked skin present it is assembled from the six cache sprites
 * ~script31 assembles it from -- two arrow buttons, a track, and a grip whose
 * middle stretches between two 5px caps -- which is why the skin bake carries
 * exactly those six. Without them it falls back to the flat IF1 form the
 * client's own scrollbar_v renderer draws: a dark track, a grip, a highlight
 * down its top and left and a shadow down its bottom and right.
 *
 * Both forms are 16 chrome pixels wide, because that is the sprite's width and
 * the width ~script31 gives the column.
 */

/** Where the grip sits inside a bar, and how tall it is. */
struct DbgScrollGeom
{
    /** Screen y of the track (below the up arrow) and its length. */
    int track_y;
    int track_h;
    int grip_y;
    int grip_h;
};

/**
 * Resolve a bar's grip from the content it scrolls.
 *
 * ~script31's arithmetic: the grip is the visible fraction of the track, never
 * shorter than 10, and its travel is the leftover track spread across the
 * scroll range. Returns 0 when there is nothing to scroll, which is also the
 * answer to "should a bar be drawn at all".
 *
 * Everything is in pixels rather than rows so the same function serves the
 * draw, the hit test and the drag -- a grip whose position is computed twice
 * is a grip that jumps under the cursor.
 */
static int
dbg_scroll_geom(
    struct ToriDbgRect bar,
    int content_px,
    int view_px,
    int offset_px,
    int arrow_h,
    int grip_min,
    struct DbgScrollGeom* out)
{
    int range;

    assert(out);
    out->track_y = bar.y + arrow_h;
    out->track_h = bar.h - 2 * arrow_h;
    out->grip_y = out->track_y;
    out->grip_h = 0;
    if( out->track_h <= 0 || content_px <= 0 || view_px <= 0 || content_px <= view_px )
        return 0;

    out->grip_h = out->track_h * view_px / content_px;
    if( out->grip_h < grip_min )
        out->grip_h = grip_min;
    if( out->grip_h > out->track_h )
        out->grip_h = out->track_h;
    range = content_px - view_px;
    out->grip_y = out->track_y + (out->track_h - out->grip_h) * offset_px / range;
    return 1;
}

/** One arrow button: the baked sprite, or a wedge of rects when there is none. */
static void
dbg_push_scroll_arrow(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int size,
    int slot,
    int down,
    struct ToriDbgRect clip)
{
    struct ToriDbgTheme const* th = &ui->theme;

    if( th->skin_dropdown && dbg_skin_has(ui, slot) )
    {
        dbg_push_sprite_box(ui, x, y, size, size, slot, clip);
        return;
    }
    /* The fallback wedge, drawn on a grip-coloured button so the arrow ends
     * still read as the two things you can press. Rects rather than a glyph:
     * the baked fonts carry no arrow, and the display list's only shape is a
     * rect anyway. */
    dbg_push_rect(ui, x, y, size, size, th->scroll_grip, 1, clip);
    dbg_push_rect(ui, x, y, size, size, th->scroll_grip_lo, 0, clip);
    for( int step = 0; step * 2 < size; step++ )
    {
        int const row = down ? y + size / 4 + step : y + (3 * size) / 4 - step;
        int const w = size - 2 * step - size / 2;
        if( w <= 0 )
            break;
        dbg_push_rect(ui, x + step + size / 4, row, w, DBG_RULE, th->scroll_grip_hi, 1, clip);
    }
}

/**
 * The whole bar, over `bar`, for a view of `view_px` into `content_px`.
 *
 * Draws nothing when the content fits: a bar with a full-length grip is a
 * control that cannot do anything, and the reference hides it the same way
 * (`if_setscrollsize(0, 0, ...)`).
 */
static void
dbg_push_scrollbar(
    struct ToriRSChrome* ui,
    struct ToriDbgRect bar,
    int content_px,
    int view_px,
    int offset_px,
    struct ToriDbgRect clip)
{
    struct ToriDbgTheme const* th = &ui->theme;
    struct DbgScrollGeom g;
    int const arrow = DBG_SCROLL_W;
    struct ToriDbgRect const inner = dbg_rect_clip(clip, bar);

    if( bar.w <= 0 || bar.h <= 0 )
        return;
    if( !dbg_scroll_geom(bar, content_px, view_px, offset_px, arrow, DBG_SCROLL_GRIP_MIN, &g) )
        return;

    /* Track first, then the grip over it, then the arrows -- draw order as
     * ~script31 creates them. */
    if( th->skin_dropdown && dbg_skin_has(ui, TORIDBG_SKIN_SCROLL_TRACK) )
        dbg_push_sprite_box(
            ui, bar.x, g.track_y, bar.w, g.track_h, TORIDBG_SKIN_SCROLL_TRACK, inner);
    else
        dbg_push_rect(ui, bar.x, g.track_y, bar.w, g.track_h, th->scroll_track, 1, inner);

    if( th->skin_dropdown && dbg_skin_has(ui, TORIDBG_SKIN_SCROLL_GRIP_MID) )
    {
        /* The middle stretched over the WHOLE grip and the caps laid on its
         * ends, not three pieces butted together. That is ~script31's order,
         * and it is what keeps a 10px grip -- shorter than its own two caps --
         * looking like a grip instead of like two overlapping stubs. */
        dbg_push_sprite_box(
            ui, bar.x, g.grip_y, bar.w, g.grip_h, TORIDBG_SKIN_SCROLL_GRIP_MID, inner);
        dbg_push_sprite_box(
            ui, bar.x, g.grip_y, bar.w, DBG_SCROLL_CAP_H, TORIDBG_SKIN_SCROLL_GRIP_TOP, inner);
        dbg_push_sprite_box(
            ui,
            bar.x,
            g.grip_y + g.grip_h - DBG_SCROLL_CAP_H,
            bar.w,
            DBG_SCROLL_CAP_H,
            TORIDBG_SKIN_SCROLL_GRIP_BOTTOM,
            inner);
    }
    else
    {
        /* torirs_frame.c's translate_scrollbar_v_step, steps 3..8: the body,
         * a 2px highlight down the top and the left, a shadow down the right
         * and the bottom. */
        dbg_push_rect(ui, bar.x, g.grip_y, bar.w, g.grip_h, th->scroll_grip, 1, inner);
        dbg_push_rect(ui, bar.x, g.grip_y, 2 * DBG_RULE, g.grip_h, th->scroll_grip_hi, 1, inner);
        dbg_push_rect(ui, bar.x, g.grip_y, bar.w, 2 * DBG_RULE, th->scroll_grip_hi, 1, inner);
        dbg_push_rect(
            ui, bar.x + bar.w - DBG_RULE, g.grip_y, DBG_RULE, g.grip_h, th->scroll_grip_lo, 1,
            inner);
        dbg_push_rect(
            ui,
            bar.x,
            g.grip_y + g.grip_h - DBG_RULE,
            bar.w,
            DBG_RULE,
            th->scroll_grip_lo,
            1,
            inner);
    }

    dbg_push_scroll_arrow(ui, bar.x, bar.y, arrow, TORIDBG_SKIN_SCROLL_UP, 0, inner);
    dbg_push_scroll_arrow(
        ui, bar.x, bar.y + bar.h - arrow, arrow, TORIDBG_SKIN_SCROLL_DOWN, 1, inner);
}

/* ---- tab strips ----------------------------------------------------------
 *
 * Tabs are laid out by PREFIX SUM rather than by width-per-tab: the left edge
 * of tab i is the scaled prefix of the captions before it, and its width is the
 * next edge minus that one. Widths computed independently and then scaled
 * accumulate one rounding error per tab, which shows up as a strip that stops a
 * few pixels short of its panel; edges from a common prefix always meet, and
 * the last one lands exactly on the end.
 *
 * Compression rather than clipping when the captions do not fit, because a tab
 * strip is a set of destinations: a tab scrolled off the end of its own strip
 * cannot be reached, where a tab squeezed to its first few letters still can.
 */

/** Natural width of one tab: its caption plus the air either side. */
static int
dbg_tab_natural(struct ToriRSChrome const* ui, struct ToriDbgWidget const* w, int index)
{
    return ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->options[index]) +
           2 * DBG_TAB_PAD_X;
}

/** Sum of the natural widths of the tabs before `count`. */
static int
dbg_tab_prefix(struct ToriRSChrome const* ui, struct ToriDbgWidget const* w, int count)
{
    int sum = 0;
    for( int i = 0; i < count && i < w->option_count; i++ )
        sum += dbg_tab_natural(ui, w, i);
    return sum;
}

/**
 * Box of one tab, in absolute screen pixels. Empty (w 0) for an out-of-range
 * index or a strip with no room.
 *
 * THE one answer to "where is tab i", asked by the draw and by the hit test.
 */
static struct ToriDbgRect
dbg_tab_rect(struct ToriRSChrome const* ui, struct ToriDbgWidget const* w, int index)
{
    struct ToriDbgRect r = { 0, 0, 0, 0 };
    int total;
    int x0;
    int x1;

    if( index < 0 || index >= w->option_count || w->w <= 0 )
        return r;
    total = dbg_tab_prefix(ui, w, w->option_count);
    if( total <= 0 )
        return r;

    if( total <= w->w )
    {
        x0 = dbg_tab_prefix(ui, w, index);
        x1 = x0 + dbg_tab_natural(ui, w, index);
    }
    else
    {
        x0 = dbg_tab_prefix(ui, w, index) * w->w / total;
        x1 = dbg_tab_prefix(ui, w, index + 1) * w->w / total;
        if( x1 - x0 < DBG_TAB_MIN_W )
            x1 = x0 + DBG_TAB_MIN_W;
    }
    r.x = w->x + x0;
    r.y = w->y;
    r.w = x1 - x0;
    r.h = w->h;
    return r;
}

/** Tab under a point, or -1. */
static int
dbg_tab_at(struct ToriRSChrome const* ui, struct ToriDbgWidget const* w, int x, int y)
{
    for( int i = 0; i < w->option_count; i++ )
    {
        struct ToriDbgRect const r = dbg_tab_rect(ui, w, i);
        if( r.w > 0 && dbg_point_in_rect(x, y, r) )
            return i;
    }
    return -1;
}

/**
 * The strip: a rule along the bottom, and a raised box per tab.
 *
 * The SELECTED tab has no bottom rule -- that gap is what joins it to the
 * content beneath, and it is the whole of what makes a tab strip read as tabs
 * rather than as a row of buttons.
 */
static void
dbg_push_tabstrip(
    struct ToriRSChrome* ui,
    struct ToriDbgWidget const* w,
    int active,
    int hover_tab,
    struct ToriDbgRect clip)
{
    struct ToriDbgTheme const* th = &ui->theme;
    int const base_y = w->y + w->h - DBG_RULE;
    int const text_y = w->y + DBG_TAB_PAD_Y +
                       ToriRSChrome_FontLineHeight(ui->theme.font_row, ui->scale);

    dbg_push_rect(ui, w->x, base_y, w->w, DBG_RULE, th->panel_border, 1, clip);

    for( int i = 0; i < w->option_count; i++ )
    {
        struct ToriDbgRect const r = dbg_tab_rect(ui, w, i);
        int const on = (i == active);
        struct ToriDbgRect tab_clip;
        int caption_w;
        int text_x;

        if( r.w <= 0 )
            continue;

        /* An unselected tab is a veil over the panel body, so the tabs read as
         * part of the same surface; the selected one is the body itself,
         * unveiled, continuing into the content below. */
        if( !on )
            dbg_push_rect_trans(
                ui, r.x, r.y, r.w, r.h, th->dropdown_veil, 1,
                i == hover_tab ? th->dropdown_row_trans_hover : th->dropdown_band_trans, clip);

        dbg_push_rect(ui, r.x, r.y, DBG_RULE, r.h, th->panel_border, 1, clip);
        dbg_push_rect(ui, r.x, r.y, r.w, DBG_RULE, th->panel_border, 1, clip);
        if( i == w->option_count - 1 )
            dbg_push_rect(ui, r.x + r.w - DBG_RULE, r.y, DBG_RULE, r.h, th->panel_border, 1, clip);
        /* Erase the base rule under the selected tab -- see the note above. */
        if( on )
            dbg_push_rect(
                ui, r.x + DBG_RULE, base_y, r.w - 2 * DBG_RULE, DBG_RULE, th->panel_body, 1, clip);

        /* Captions are clipped to their own tab so a compressed strip cuts the
         * text at the tab's edge instead of running it under its neighbour. */
        tab_clip = dbg_rect_clip(clip, r);
        caption_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->options[i]);
        text_x = r.w > caption_w ? r.x + (r.w - caption_w) / 2 : r.x + DBG_RULE;
        dbg_push_text(
            ui,
            text_x,
            text_y,
            w->options[i],
            on ? th->text : (i == hover_tab ? th->accent : th->text_dim),
            ui->theme.font_row,
            0,
            tab_clip);
    }
}

/*
 * The closed dropdown button, as script_3850 builds it.
 *
 * Five pieces in this order: the panel tile, a near-black frame, a grey inset
 * one pixel inside it, the arrow sprite on the LEFT, and the current value
 * centred in what is left. The arrow points down while the list is shut and up
 * while it is open, which in the reference is literally the same two sprites
 * the scrollbar's ends wear -- so the skin carries one pair, not two.
 *
 * The left-hand arrow is the detail that makes it read as the game's control
 * rather than as a generic combo box; it used to sit on the right.
 */
static void
dbg_push_dropdown_button(
    struct ToriRSChrome* ui,
    struct ToriDbgRect box,
    char const* text,
    int open,
    int hovered,
    struct ToriDbgRect clip)
{
    struct ToriDbgTheme const* th = &ui->theme;
    struct ToriDbgRect const inside = dbg_rect_clip(clip, box);
    int const arrow = DBG_DROP_ARROW_W;
    int const arrow_slot = open ? TORIDBG_SKIN_SCROLL_UP : TORIDBG_SKIN_SCROLL_DOWN;
    int const line_box = ToriRSChrome_FontLineBox(th->font_row, ui->scale);
    int const text_top = box.y + (box.h - line_box) / 2;
    struct ToriDbgRect text_clip;
    int text_x;
    int text_w;
    int shown_w;

    assert(text);
    if( box.w <= 0 || box.h <= 0 )
        return;

    /* Flat fill first either way: the tile carries transparent pixels at its
     * edges, and tiling it straight onto the panel would show through them. */
    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->input_bg, 1, clip);
    if( th->skin_dropdown && dbg_skin_has(ui, TORIDBG_SKIN_PANEL_BODY) )
        dbg_fill_tiled(
            ui, box.x, box.y, box.w, box.h, TORIDBG_SKIN_PANEL_BODY, ui->skin_tile_w,
            ui->skin_tile_h, inside);

    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->dropdown_border, 0, clip);
    dbg_push_rect(
        ui,
        box.x + DBG_RULE,
        box.y + DBG_RULE,
        box.w - 2 * DBG_RULE,
        box.h - 2 * DBG_RULE,
        th->dropdown_border_inner,
        0,
        clip);

    dbg_push_scroll_arrow(
        ui,
        box.x + 2 * DBG_RULE,
        box.y + (box.h - arrow) / 2,
        arrow,
        arrow_slot,
        !open,
        inside);

    /* The value gets the strip right of the arrow, centred in it when it fits
     * and left-aligned when it does not -- the reference sizes its button to
     * the text and so only ever has the first case, and a centred string that
     * is being clipped at both ends is unreadable. */
    text_x = box.x + 2 * DBG_RULE + arrow;
    text_w = box.x + box.w - DBG_RULE - text_x;
    shown_w = ToriRSChrome_MeasureText(th->font_row, ui->scale, text);
    if( shown_w < text_w )
        text_x += (text_w - shown_w) / 2;
    text_clip = inside;
    if( text_clip.x < box.x + 2 * DBG_RULE + arrow )
    {
        int const right = text_clip.x + text_clip.w;
        text_clip.x = box.x + 2 * DBG_RULE + arrow;
        text_clip.w = right > text_clip.x ? right - text_clip.x : 0;
    }
    dbg_push_text(
        ui,
        text_x,
        text_top + ToriRSChrome_FontLineHeight(th->font_row, ui->scale),
        text,
        th->dropdown_text,
        th->font_row,
        0,
        text_clip);

    /* The hover veil goes on last, over the text, because that is where
     * script_3850 creates it -- it is a translucent black rect that starts
     * invisible and drops to trans 220 on mouseover. */
    if( hovered && !open )
        dbg_push_rect_trans(
            ui,
            box.x + DBG_RULE,
            box.y + DBG_RULE,
            box.w - 2 * DBG_RULE,
            box.h - 2 * DBG_RULE,
            th->dropdown_veil,
            1,
            th->dropdown_hover_trans,
            clip);
}

/**
 * Grab box of a panel's resize grip: the bottom-right corner, inside the edge.
 *
 * One function, used by the draw and by the hit test, so the pixels that show
 * a grip and the pixels that take one cannot drift apart. Callers check
 * `resizable` first -- this answers for any panel, because a rect is not a
 * permission.
 */
static struct ToriDbgRect
dbg_grip_rect(struct ToriRSChrome const* ui, struct ToriDbgRect panel)
{
    struct ToriDbgRect r;
    r.x = panel.x + panel.w - DBG_RULE - DBG_GRIP_HIT;
    r.y = panel.y + panel.h - DBG_RULE - DBG_GRIP_HIT;
    r.w = DBG_GRIP_HIT;
    r.h = DBG_GRIP_HIT;
    return r;
}

/*
 * The resize grip: three diagonal carets nested into the bottom-right corner,
 * the corner both edges it drags meet at.
 *
 * Dots rather than solid diagonals because a diagonal line, in a display list
 * whose only shape is a rect, costs one primitive per pixel -- three full
 * eight-pixel lines would be 24 prims on every panel, against a 512-prim
 * budget shared with everything else on screen. Six 2x2 dots read as the same
 * chevrons and cost six.
 */
static void
dbg_push_grip(struct ToriRSChrome* ui, struct ToriDbgPanel const* p, struct ToriDbgRect clip)
{
    struct ToriDbgRect const box = { p->x, p->y, p->w, p->h };
    struct ToriDbgRect const g = dbg_grip_rect(ui, box);
    /* Inside the rail and above the bottom rule: the grab box has a pixel of
     * slop on both, and the dots must not paint over either. */
    int const x1 = g.x + g.w - 2 * DBG_RULE;
    int const y1 = g.y + g.h - 2 * DBG_RULE;

    for( int j = 0; j < DBG_GRIP_LINES; j++ )
        for( int i = 0; i + j < DBG_GRIP_LINES; i++ )
            dbg_push_rect(
                ui,
                x1 - DBG_GRIP_DOT + DBG_RULE - i * DBG_GRIP_PITCH,
                y1 - DBG_GRIP_DOT + DBG_RULE - j * DBG_GRIP_PITCH,
                DBG_GRIP_DOT,
                DBG_GRIP_DOT,
                ui->theme.panel_border,
                1,
                clip);
}

/*
 * A window panel: the minimenu's chrome, body fill, an optional title bar in
 * the menu face, then one row per widget in the theme's row face.
 */
static void
dbg_build_window(struct ToriRSChrome* ui, struct ToriDbgPanel* p)
{
    struct ToriDbgTheme const* th = &ui->theme;
    struct DbgMenuLayout const l = dbg_menu_layout(ToriRSChrome_FontLineBox(TORIDBG_FONT_MENU, ui->scale), ui->scale);
    /* Distance from the panel's top edge to the first content row, borders
     * included: the black bar, the body gap under it and the separator rule --
     * the minimenu's own header block. A titleless panel has just its top
     * border. */
    int const head_h = p->title[0] ? l.separator_y + DBG_RULE : DBG_RULE;
    /* Bottom padding. A resizable panel reserves the grip's full grab box
     * instead of the usual pad, so the carets get a strip of their own: at
     * DBG_PAD_Y the grip reaches up into the last row and the two draw over
     * each other, which reads as a rendering fault rather than as a corner you
     * can pull. */
    int const foot_h = p->resizable ? DBG_GRIP_HIT : DBG_PAD_Y;
    struct ToriDbgRect clip;
    int content_w = 0;
    int content_h = 0;
    int content_top;
    int content_bot;
    int overflow;
    int bar_w;
    int row_y;
    int widget;

    /* The label column first: every width below depends on it in a table
     * panel, and only VISIBLE labelled rows vote -- a hidden tool's wide
     * label must not hold the column open for rows that are on screen. */
    p->label_col = 0;
    if( p->table )
        for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
        {
            struct ToriDbgWidget const* lw = &ui->widgets[widget];
            int label_w;
            if( !dbg_widget_shown(ui, lw) || !lw->label[0] )
                continue;
            if( lw->kind != TORIDBG_W_TEXTINPUT && lw->kind != TORIDBG_W_DROPDOWN )
                continue;
            label_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, lw->label);
            if( label_w > p->label_col )
                p->label_col = label_w;
        }

    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        int w;
        if( !dbg_widget_shown(ui, &ui->widgets[widget]) )
            continue;
        w = dbg_widget_width(ui, &ui->widgets[widget]);
        if( w > content_w )
            content_w = w;
        content_h += dbg_widget_height(ui, &ui->widgets[widget]) + DBG_ROW_GAP;
    }
    if( content_h > 0 )
        content_h -= DBG_ROW_GAP;
    if( p->title[0] )
    {
        int const tw = ToriRSChrome_MeasureText(TORIDBG_FONT_MENU, ui->scale, p->title);
        if( tw > content_w )
            content_w = tw;
    }
    if( content_w < DBG_MIN_CONTENT_W )
        content_w = DBG_MIN_CONTENT_W;

    p->w = p->fixed_w > 0 ? p->fixed_w : content_w + 2 * DBG_PAD_X + 2 * DBG_RULE;
    /* content_h is 0 for an empty panel, so this is also the empty case: the
     * header block, the pads and the bottom border. */
    p->h = p->fixed_h > 0 ? p->fixed_h : head_h + DBG_PAD_Y + content_h + foot_h + DBG_RULE;

    /*
     * The scroll window, and whether there is anything to scroll.
     *
     * Held on the panel because the bar's draw, its hit test and its drag all
     * read them, and a view height recomputed per caller is a grip that lands
     * somewhere different depending on who asked. A content-sized panel is
     * exactly as tall as its rows, so `overflow` there is always false --
     * scrolling only ever engages under a hand-set or dragged height.
     */
    p->content_h = content_h;
    p->view_h = p->h - head_h - DBG_PAD_Y - foot_h - DBG_RULE;
    if( p->view_h < 0 )
        p->view_h = 0;
    overflow = p->scrollable && content_h > p->view_h;
    if( !overflow )
    {
        p->scroll_y = 0;
    }
    else
    {
        /* Clamped here rather than at the wheel, so a panel that SHRANK -- by a
         * grip drag, a tab switch, a row being removed -- cannot be left
         * scrolled past content that is no longer there. */
        int const max_scroll = content_h - p->view_h;
        if( p->scroll_y > max_scroll )
            p->scroll_y = max_scroll;
        if( p->scroll_y < 0 )
            p->scroll_y = 0;
    }
    bar_w = overflow ? DBG_SCROLL_W : 0;

    clip.x = p->x;
    clip.y = p->y;
    clip.w = p->w;
    clip.h = p->h;
    /* Flat fill first either way: the parchment carries transparent pixels at
     * its edges, and tiling it straight onto whatever was behind the panel
     * would show the world through them. */
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->panel_body, 1, clip);
    if( th->skin_panel_body && dbg_skin_has(ui, TORIDBG_SKIN_PANEL_BODY) )
        dbg_fill_tiled(
            ui, p->x, p->y, p->w, p->h, TORIDBG_SKIN_PANEL_BODY, ui->skin_tile_w,
            ui->skin_tile_h, clip);

    /*
     * The minimenu's chrome, on a window panel.
     *
     * Not "structurally the same" -- the same boxes at the same offsets, taken
     * from the same dbg_menu_layout dbg_build_menu uses (and emit_minimenu
     * before it): header bar, the body gap and separator rule under it, a
     * bottom rule, and a rail down each side starting below the separator. A
     * panel and a real game menu on screen together have to read as one
     * widget, and the two former deviations -- rails run the full height, and
     * a 1px outline around the outside -- were both visible as an edge the
     * minimenu does not have.
     *
     * The palette stays the window's own (`panel_*`) rather than the menu's:
     * the keys are separately themable so the flat developer look can keep a
     * legible grey title, and it is only the osrs theme that sets them to the
     * menu's values.
     */
    if( p->title[0] )
    {
        dbg_push_rect(
            ui,
            p->x + DBG_RULE,
            p->y + DBG_RULE,
            p->w - 2 * DBG_RULE,
            l.header_bar_h,
            th->panel_title_bg,
            1,
            clip);
        dbg_push_text(
            ui,
            p->x + DBG_PX(3),
            p->y + DBG_PX(2) + ToriRSChrome_FontLineHeight(TORIDBG_FONT_MENU, ui->scale),
            p->title,
            th->panel_title_text,
            TORIDBG_FONT_MENU,
            0,
            clip);
        /* The rule closing the header, where the minimenu's separator sits. */
        dbg_push_rect(
            ui,
            p->x + DBG_RULE,
            p->y + l.separator_y,
            p->w - 2 * DBG_RULE,
            DBG_RULE,
            th->panel_border,
            1,
            clip);
    }
    /* Bottom rule and the two side rails: inset a pixel, and running from the
     * separator down, exactly as dbg_build_menu draws them. A titleless panel
     * has no separator to start at, so its rails start at the top border
     * instead -- the menu never has that case, having always a title. */
    {
        int const rail_y = p->title[0] ? l.separator_y : DBG_RULE;
        int const rail_h = p->h - rail_y - DBG_RULE;

        dbg_push_rect(
            ui,
            p->x + DBG_RULE,
            p->y + p->h - 2 * DBG_RULE,
            p->w - 2 * DBG_RULE,
            DBG_RULE,
            th->panel_border,
            1,
            clip);
        dbg_push_rect(
            ui, p->x + DBG_RULE, p->y + rail_y, DBG_RULE, rail_h, th->panel_border, 1, clip);
        dbg_push_rect(
            ui,
            p->x + p->w - 2 * DBG_RULE,
            p->y + rail_y,
            DBG_RULE,
            rail_h,
            th->panel_border,
            1,
            clip);
    }
    if( p->resizable )
        dbg_push_grip(ui, p, clip);

    content_top = p->y + head_h + DBG_PAD_Y;
    content_bot = p->y + p->h - foot_h - DBG_RULE;

    /* The bar sits in the content column's right-hand edge, above the footer,
     * and is drawn before the rows so a row's clip can exclude its column. */
    if( overflow )
    {
        struct ToriDbgRect bar;
        bar.x = p->x + p->w - DBG_RULE - DBG_PAD_X - bar_w;
        bar.y = content_top;
        bar.w = bar_w;
        bar.h = content_bot - content_top;
        if( bar.h > 0 )
            dbg_push_scrollbar(ui, bar, p->content_h, p->view_h, p->scroll_y, clip);
    }

    /* Rows are clipped to the content column, so an over-long label is cut at
     * the border instead of painting over it. A scrolling panel tightens that
     * to the scroll window itself: a row half off the top would otherwise paint
     * up into the header's padding, and one half off the bottom over the grip. */
    clip.x = p->x + DBG_RULE + DBG_PAD_X;
    clip.y = overflow ? content_top : p->y + head_h;
    clip.w = p->w - 2 * DBG_RULE - 2 * DBG_PAD_X - bar_w;
    clip.h = overflow ? content_bot - content_top : p->h - head_h - DBG_RULE;
    if( clip.w < 0 )
        clip.w = 0;
    if( clip.h < 0 )
        clip.h = 0;

    row_y = content_top - p->scroll_y;
    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        struct ToriDbgWidget* w = &ui->widgets[widget];
        int row_h;
        int row_x;
        int hovered;

        /* Not shown: no space, no draw -- and no stale hit box, or the widget
         * would keep taking clicks meant for the row drawn where it was. The
         * measuring loop above skipped it too; skipping in only one of the
         * two draws every later row across the bottom border. */
        if( !dbg_widget_shown(ui, w) )
        {
            w->x = 0;
            w->y = 0;
            w->w = 0;
            w->h = 0;
            continue;
        }
        row_h = dbg_widget_height(ui, w);
        row_x = p->x + DBG_RULE + DBG_PAD_X;
        hovered = ui->hover == widget;

        w->x = row_x;
        w->y = row_y;
        w->w = p->w - 2 * DBG_RULE - 2 * DBG_PAD_X - bar_w;
        w->h = row_h;

        /*
         * A scrolled panel steps PAST rows outside the window rather than
         * stopping at them: the rows below one that is off the top are the
         * ones the user scrolled down to see, so `break` would empty the panel.
         * The hit box is zeroed for the same reason the dropped-row case
         * zeroes it -- an invisible row must not take clicks -- and the loop
         * still advances row_y so the rows that ARE in view land correctly.
         */
        if( overflow && (row_y + row_h <= content_top || row_y >= content_bot) )
        {
            w->w = 0;
            w->h = 0;
            row_y += row_h + DBG_ROW_GAP;
            continue;
        }

        /*
         * A row past the bottom of a hand-sized panel is GONE, not merely
         * clipped.
         *
         * The clip rect alone would stop it being drawn and leave its hit box
         * where it was, so the panel would keep taking clicks on rows nobody
         * can see -- the worst version of this, because it is invisible until
         * someone toggles a checkbox they did not know was under the cursor.
         * Zeroing the box takes the row out of the hit test too, and `break`
         * because rows only ever go downwards from here.
         *
         * Measured against the footer rather than the clip rect so a surviving
         * row cannot run under the grip. On a content-sized panel the last row
         * ends exactly on this line, so nothing is ever dropped there -- it is
         * only a hand-sized height that can cut rows off.
         */
        if( !overflow && row_y + row_h > content_bot )
        {
            for( ; widget >= 0; widget = ui->widgets[widget].next )
            {
                ui->widgets[widget].w = 0;
                ui->widgets[widget].h = 0;
            }
            break;
        }

        switch( w->kind )
        {
        case TORIDBG_W_LABEL:
            dbg_push_text(
                ui,
                row_x,
                row_y + ToriRSChrome_FontLineHeight(ui->theme.font_row, ui->scale),
                w->text,
                w->color ? w->color : th->text,
                ui->theme.font_row,
                0,
                clip);
            break;

        case TORIDBG_W_SEPARATOR:
            dbg_push_rect(
                ui, row_x, row_y + DBG_SEP_H / 2, w->w, DBG_RULE, th->separator, 1, clip);
            break;

        case TORIDBG_W_MODELVIEW:
        {
            /* The box always draws, sprite or not: an empty bordered well says
             * "preview goes here", where nothing at all reads as a layout
             * hole. The sprite prim carries the scene id itself; the frame
             * translator uses it in place of the skin mapping. */
            dbg_push_rect(
                ui, row_x, row_y, w->view_w + 2 * DBG_RULE, w->view_h + 2 * DBG_RULE,
                th->input_bg, 1, clip);
            dbg_push_rect(
                ui, row_x, row_y, w->view_w + 2 * DBG_RULE, w->view_h + 2 * DBG_RULE,
                ui->focus == widget ? th->input_border_focus : th->input_border, 0, clip);
            if( w->view_scene_id > 0 )
            {
                struct ToriDbgPrim* p2 = dbg_prim_push(ui);
                if( p2 )
                {
                    p2->kind = TORIDBG_PRIM_SPRITE;
                    p2->x = row_x + DBG_RULE;
                    p2->y = row_y + DBG_RULE;
                    p2->sprite_scene_id = w->view_scene_id;
                    /* Clip to the well, not the panel: the host's render may
                     * be larger than the box. */
                    p2->clip.x = row_x + DBG_RULE;
                    p2->clip.y = row_y + DBG_RULE;
                    p2->clip.w = w->view_w;
                    p2->clip.h = w->view_h;
                }
            }
            break;
        }

        case TORIDBG_W_CHECKBOX:
        {
            int const box_y = row_y + (row_h - DBG_CHECK_SIZE) / 2;
            dbg_push_rect(
                ui, row_x, box_y, DBG_CHECK_SIZE, DBG_CHECK_SIZE, th->input_bg, 1, clip);
            dbg_push_rect(
                ui,
                row_x,
                box_y,
                DBG_CHECK_SIZE,
                DBG_CHECK_SIZE,
                hovered ? th->accent : th->check_box,
                0,
                clip);
            if( w->checked )
                dbg_push_rect(
                    ui,
                    row_x + DBG_PX(2),
                    box_y + DBG_PX(2),
                    DBG_CHECK_SIZE - DBG_PX(4),
                    DBG_CHECK_SIZE - DBG_PX(4),
                    th->check_mark,
                    1,
                    clip);
            dbg_push_text(
                ui,
                row_x + DBG_CHECK_SIZE + DBG_CHECK_GAP,
                row_y + (row_h - ToriRSChrome_FontLineBox(ui->theme.font_row, ui->scale)) / 2 + ToriRSChrome_FontLineHeight(ui->theme.font_row, ui->scale),
                w->label,
                hovered ? th->accent : (w->color ? w->color : th->text),
                ui->theme.font_row,
                0,
                clip);
            break;
        }

        /* A clickable row inside a window panel.
         *
         * MENUITEM used to be drawn only by the menu-style panel, so one placed
         * in a window laid out a row and then painted nothing into it — an
         * action the user could click but could not see. A window panel that
         * can hold checkboxes and dropdowns should be able to hold a command
         * too, so it draws one: accent-coloured on hover, which is the whole of
         * what makes a row read as pressable. */
        case TORIDBG_W_MENUITEM:
            dbg_push_text(
                ui,
                row_x,
                row_y + ToriRSChrome_FontLineHeight(ui->theme.font_row, ui->scale),
                w->text,
                hovered ? th->accent : (w->color ? w->color : th->text),
                ui->theme.font_row,
                0,
                clip);
            break;

        case TORIDBG_W_BUTTON:
        {
            int const caption_w =
                ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->text);
            /* Sized to its caption and left-aligned in the row, not stretched
             * across it: a full-width button in a column of labelled rows reads
             * as a banner, and two of them side by side (Save / Revert) is the
             * shape this exists for. */
            int const box_w = caption_w + 2 * DBG_BUTTON_PAD_X + 2 * DBG_RULE;
            int const pressed = ui->press == widget && hovered;

            dbg_push_rect(ui, row_x, row_y, box_w, row_h, th->input_bg, 1, clip);
            dbg_push_rect(
                ui, row_x, row_y, box_w, row_h,
                hovered ? th->accent : th->input_border, 0, clip);
            dbg_push_text(
                ui,
                /* A pressed button shifts its caption a pixel down and right --
                 * the whole of what makes the press read as a press. */
                row_x + DBG_RULE + DBG_BUTTON_PAD_X + (pressed ? DBG_RULE : 0),
                row_y + DBG_RULE + DBG_BUTTON_PAD_Y + (pressed ? DBG_RULE : 0) +
                    ToriRSChrome_FontLineHeight(ui->theme.font_row, ui->scale),
                w->text,
                hovered ? th->accent : (w->color ? w->color : th->text),
                ui->theme.font_row,
                0,
                clip);
            /* The hit box is the drawn box, not the whole row: the empty strip
             * beside a button must not press it. */
            w->w = box_w;
            break;
        }

        case TORIDBG_W_TABSTRIP:
            dbg_push_tabstrip(
                ui, w, p->active_tab, hovered ? dbg_tab_at(ui, w, ui->hover_x, ui->hover_y) : -1,
                clip);
            break;

        case TORIDBG_W_DROPDOWN:
        {
            int const label_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label);
            int const box_x = row_x + dbg_row_box_offset(ui, w);
            int const box_w = row_x + w->w - box_x;
            int const open = ui->dropdown_open == widget;
            char const* shown = (w->selected >= 0 && w->selected < w->option_count)
                                    ? w->options[w->selected]
                                    : "";
            struct ToriDbgRect box;

            if( label_w > 0 )
                dbg_push_text(
                    ui,
                    row_x,
                    row_y + DBG_INPUT_PAD_Y + DBG_RULE +
                        ToriRSChrome_FontLineHeight(ui->theme.font_row, ui->scale),
                    w->label,
                    w->color ? w->color : th->text_dim,
                    ui->theme.font_row,
                    0,
                    clip);
            if( box_w <= 0 )
                break;

            box.x = box_x;
            box.y = row_y;
            box.w = box_w;
            box.h = row_h;
            dbg_push_dropdown_button(ui, box, shown, open, hovered, clip);
            break;
        }

        case TORIDBG_W_TEXTINPUT:
        {
            int const label_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label);
            int const box_x = row_x + dbg_row_box_offset(ui, w);
            int const box_w = row_x + w->w - box_x;
            int const focused = ui->focus == widget;
            struct ToriDbgRect inner;
            int caret_px;
            int scroll = 0;
            int text_x;

            if( label_w > 0 )
                dbg_push_text(
                    ui,
                    row_x,
                    row_y + DBG_INPUT_PAD_Y + DBG_RULE +
                        ToriRSChrome_FontLineHeight(ui->theme.font_row, ui->scale),
                    w->label,
                    w->color ? w->color : th->text_dim,
                    ui->theme.font_row,
                    0,
                    clip);
            if( box_w <= 0 )
                break;

            dbg_push_rect(ui, box_x, row_y, box_w, row_h, th->input_bg, 1, clip);
            dbg_push_rect(
                ui,
                box_x,
                row_y,
                box_w,
                row_h,
                focused ? th->input_border_focus : th->input_border,
                0,
                clip);

            /* Scroll the content so the caret stays inside the box — the
             * classic single-line edit behaviour. */
            inner.x = box_x + DBG_RULE + DBG_INPUT_PAD_X;
            inner.y = row_y + DBG_RULE;
            inner.w = box_w - 2 * DBG_RULE - 2 * DBG_INPUT_PAD_X;
            inner.h = row_h - 2 * DBG_RULE;
            if( inner.w < 0 )
                inner.w = 0;
            caret_px = dbg_measure_prefix(ui->theme.font_row, ui->scale, w->text, w->caret);
            if( caret_px > inner.w )
                scroll = caret_px - inner.w;
            text_x = inner.x - scroll;

            dbg_push_text(
                ui,
                text_x,
                row_y + DBG_INPUT_PAD_Y + DBG_RULE +
                    ToriRSChrome_FontLineHeight(ui->theme.font_row, ui->scale),
                w->text,
                th->input_text,
                ui->theme.font_row,
                0,
                inner);
            if( focused && ui->caret_visible )
                dbg_push_rect(
                    ui,
                    text_x + caret_px,
                    row_y + DBG_PX(2),
                    DBG_RULE,
                    row_h - DBG_PX(4),
                    th->input_text,
                    1,
                    inner);
            break;
        }

        default:
            break;
        }

        /*
         * Trim a straddling row's hit box to the part of it you can see.
         *
         * Only the scrolling case can produce one: the row is drawn clipped, so
         * without this its clickable area would extend past the fold and a
         * click on the footer strip -- or on the resize grip -- would land on
         * the half-row above or below it.
         */
        if( overflow )
        {
            int const top = w->y < content_top ? content_top : w->y;
            int const bot = w->y + w->h > content_bot ? content_bot : w->y + w->h;
            w->y = top;
            w->h = bot - top > 0 ? bot - top : 0;
        }
        row_y += row_h + DBG_ROW_GAP;
    }
}

/*
 * A menu panel: the minimenu's chrome and geometry (see dbg_menu_layout),
 * rows top-to-bottom.
 */
static void
dbg_build_menu(struct ToriRSChrome* ui, struct ToriDbgPanel* p)
{
    struct ToriDbgTheme const* th = &ui->theme;
    struct DbgMenuLayout const l = dbg_menu_layout(ToriRSChrome_FontLineBox(TORIDBG_FONT_MENU, ui->scale), ui->scale);
    struct ToriDbgRect clip;
    int content_w = 0;
    int rows = 0;
    int widget;
    int row;

    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        int const w = ToriRSChrome_MeasureText(TORIDBG_FONT_MENU, ui->scale, ui->widgets[widget].text);
        if( w > content_w )
            content_w = w;
        rows++;
    }
    if( p->title[0] )
    {
        int const tw = ToriRSChrome_MeasureText(TORIDBG_FONT_MENU, ui->scale, p->title);
        if( tw > content_w )
            content_w = tw;
    }

    p->w = p->fixed_w > 0 ? p->fixed_w : content_w + l.width_pad;
    p->h = rows * l.row_stride + l.chrome_h;

    clip.x = p->x;
    clip.y = p->y;
    clip.w = p->w;
    clip.h = p->h;

    /* Body, black title bar, separator, bottom rule, side rails — the same
     * six boxes emit_minimenu draws, in the same order. */
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->menu_body, 1, clip);
    dbg_push_rect(
        ui,
        p->x + DBG_RULE,
        p->y + DBG_RULE,
        p->w - 2 * DBG_RULE,
        l.header_bar_h,
        th->menu_chrome,
        1,
        clip);
    dbg_push_rect(
        ui,
        p->x + DBG_RULE,
        p->y + l.separator_y,
        p->w - 2 * DBG_RULE,
        DBG_RULE,
        th->menu_chrome,
        1,
        clip);
    dbg_push_rect(
        ui,
        p->x + DBG_RULE,
        p->y + p->h - 2 * DBG_RULE,
        p->w - 2 * DBG_RULE,
        DBG_RULE,
        th->menu_chrome,
        1,
        clip);
    dbg_push_rect(
        ui,
        p->x + DBG_RULE,
        p->y + l.separator_y,
        DBG_RULE,
        p->h - l.border_inset,
        th->menu_chrome,
        1,
        clip);
    dbg_push_rect(
        ui,
        p->x + p->w - 2 * DBG_RULE,
        p->y + l.separator_y,
        DBG_RULE,
        p->h - l.border_inset,
        th->menu_chrome,
        1,
        clip);
    dbg_push_text(
        ui,
        p->x + DBG_PX(3),
        p->y + DBG_PX(2) + ToriRSChrome_FontLineHeight(TORIDBG_FONT_MENU, ui->scale),
        p->title,
        th->menu_body,
        TORIDBG_FONT_MENU,
        0,
        clip);

    row = 0;
    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        struct ToriDbgWidget* w = &ui->widgets[widget];
        int const baseline = p->y + row * l.row_stride + l.option_base_y;
        int const hovered = ui->hover == widget;

        /* The hit box the reference minimenu uses, so a row picked here is the
         * row drawn here. */
        w->x = p->x;
        w->y = baseline - l.hover_above;
        w->w = p->w;
        w->h = l.hover_above + l.hover_below;

        dbg_push_text(
            ui,
            p->x + DBG_PX(3),
            baseline,
            w->text,
            hovered ? th->menu_hover_text : (w->color ? w->color : th->menu_text),
            TORIDBG_FONT_MENU,
            1,
            clip);
        row++;
    }
}

static void
dbg_build_dropdown_list(struct ToriRSChrome* ui);
static struct ToriDbgRect
dbg_dropdown_rect(struct ToriRSChrome const* ui);

/*
 * The File/Edit bar: one horizontal row of menu titles across the top.
 *
 * Chrome-wise it is the minimenu header stretched across the screen -- black
 * strip, menu-face titles, accent on hover -- and each title is a menu-mode
 * dropdown whose option list opens beneath it, reusing the popup machinery
 * wholesale rather than growing a second popup implementation.
 */
static void
dbg_build_menubar(struct ToriRSChrome* ui, struct ToriDbgPanel* p)
{
    struct ToriDbgTheme const* th = &ui->theme;
    int const line = ToriRSChrome_FontLineBox(TORIDBG_FONT_MENU, ui->scale);
    struct ToriDbgRect clip;
    int pen_x;
    int widget;
    int content_w = 0;

    p->h = line + DBG_PX(4);
    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        struct ToriDbgWidget const* w = &ui->widgets[widget];
        if( w->hidden )
            continue;
        content_w += ToriRSChrome_MeasureText(TORIDBG_FONT_MENU, ui->scale, w->text) + 2 * DBG_PAD_X;
    }
    p->w = p->fixed_w > 0 ? p->fixed_w : content_w + 2 * DBG_RULE;

    clip.x = p->x;
    clip.y = p->y;
    clip.w = p->w;
    clip.h = p->h;
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->menu_chrome, 1, clip);
    /* The border. In the body colour, not panel_border: the bar's fill is the
     * chrome black, and in the osrs theme panel_border is black too -- a
     * black-on-black outline is the "missing border" this line exists to fix.
     * The body brown is the one colour the minimenu already pairs with its
     * black header, so the bar reads as the same chrome family. */
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->menu_body, 0, clip);

    pen_x = p->x + DBG_PAD_X;
    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        struct ToriDbgWidget* w = &ui->widgets[widget];
        int tw;
        int const hovered = ui->hover == widget || ui->dropdown_open == widget;

        if( w->hidden )
        {
            w->x = 0;
            w->y = 0;
            w->w = 0;
            w->h = 0;
            continue;
        }
        tw = ToriRSChrome_MeasureText(TORIDBG_FONT_MENU, ui->scale, w->text);
        w->x = pen_x - DBG_PAD_X / 2;
        w->y = p->y;
        w->w = tw + DBG_PAD_X;
        w->h = p->h;
        dbg_push_text(
            ui,
            pen_x,
            p->y + DBG_PX(2) + ToriRSChrome_FontLineHeight(TORIDBG_FONT_MENU, ui->scale),
            w->text,
            hovered ? th->accent : th->panel_title_text,
            TORIDBG_FONT_MENU,
            1,
            clip);
        pen_x += tw + 2 * DBG_PAD_X;
    }
}

int
ToriRSChrome_Build(struct ToriRSChrome* ui)
{
    assert(ui);
    /* The retained-mode payoff: a frame in which nothing moved does no
     * measurement, no layout and no display-list work at all. */
    if( !ui->dirty )
        return 0;

    ui->prim_count = 0;
    ui->overflow = 0;

    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct ToriDbgPanel* p = &ui->panels[i];
        struct ToriDbgRect rect;

        if( !p->visible )
        {
            if( p->dirty )
            {
                /* It was on screen last frame; that area has to be repainted. */
                dbg_damage_add(ui, p->last_rect);
                p->last_rect.w = 0;
                p->last_rect.h = 0;
                p->dirty = 0;
            }
            continue;
        }

        if( p->style == TORIDBG_PANEL_MENU )
            dbg_build_menu(ui, p);
        else if( p->style == TORIDBG_PANEL_MENUBAR )
            dbg_build_menubar(ui, p);
        else
            dbg_build_window(ui, p);

        rect.x = p->x;
        rect.y = p->y;
        rect.w = p->w;
        rect.h = p->h;
        if( p->dirty )
        {
            /* Old bounds ∪ new bounds: a panel that shrank or moved leaves the
             * vacated pixels invalid too. */
            dbg_damage_add(ui, p->last_rect);
            dbg_damage_add(ui, rect);
            p->dirty = 0;
        }
        p->last_rect = rect;
    }

    /* After every panel, so the open list is never buried under one drawn
     * later. This is the whole reason the list is overlay state rather than a
     * child of the panel that owns the dropdown. */
    dbg_build_dropdown_list(ui);

    ui->dirty = 0;
    return 1;
}

/** Screen rect of the open list, or a zero rect when none is open. */
static struct ToriDbgRect
dbg_dropdown_rect(struct ToriRSChrome const* ui)
{
    struct ToriDbgRect rect = { 0, 0, 0, 0 };
    struct ToriDbgWidget const* w;
    int rows;

    if( ui->dropdown_open < 0 )
        return rect;

    w = &ui->widgets[ui->dropdown_open];
    rows = w->option_count < TORIDBG_DROPDOWN_ROWS ? w->option_count : TORIDBG_DROPDOWN_ROWS;
    if( rows <= 0 )
        return rect;

    if( w->menu_mode )
    {
        /* A menu list is sized to its rows, not to the title above it. */
        int widest = 0;
        for( int i = 0; i < w->option_count; i++ )
        {
            int const ow = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->options[i]);
            if( ow > widest )
                widest = ow;
        }
        rect.x = w->x;
        rect.w = widest + 2 * DBG_INPUT_PAD_X + DBG_PX(6);
    }
    else
    {
        rect.x = w->x + dbg_row_box_offset(ui, w);
        rect.w = w->x + w->w - rect.x;
    }
    rect.y = w->y + w->h;
    /* Rows plus a pad above and below, which is script_9114's `$int26 + 4`. */
    rect.h = rows * DBG_DROP_ROW_H + 2 * DBG_DROP_LIST_PAD;
    return rect;
}

/** Rows the open list shows at once. */
static int
dbg_dropdown_rows(struct ToriRSChrome const* ui)
{
    struct ToriDbgWidget const* w;

    assert(ui->dropdown_open >= 0);
    w = &ui->widgets[ui->dropdown_open];
    return w->option_count < TORIDBG_DROPDOWN_ROWS ? w->option_count : TORIDBG_DROPDOWN_ROWS;
}

/**
 * The open list's scrollbar column, or a zero rect when the list does not
 * scroll.
 *
 * Inside the list rather than beside it, which is where the cache puts it --
 * interface 134's floating list is 100 wide and holds an 80-wide row column
 * next to a 16-wide bar. So the rows lose the width the bar takes, and a long
 * option is clipped at the bar instead of running under it.
 */
static struct ToriDbgRect
dbg_dropdown_scrollbar_rect(struct ToriRSChrome const* ui)
{
    struct ToriDbgRect bar = { 0, 0, 0, 0 };
    struct ToriDbgRect rect;
    struct ToriDbgWidget const* w;

    if( ui->dropdown_open < 0 )
        return bar;
    w = &ui->widgets[ui->dropdown_open];
    if( w->option_count <= dbg_dropdown_rows(ui) )
        return bar;

    rect = dbg_dropdown_rect(ui);
    if( rect.w <= 2 * DBG_DROP_LIST_PAD + DBG_SCROLL_W )
        return bar;
    bar.x = rect.x + rect.w - DBG_DROP_LIST_PAD - DBG_SCROLL_W;
    bar.y = rect.y + DBG_DROP_LIST_PAD;
    bar.w = DBG_SCROLL_W;
    bar.h = rect.h - 2 * DBG_DROP_LIST_PAD;
    return bar;
}

/** The open list's scroll position, its window and its content, in pixels --
 *  the units dbg_scroll_geom works in. */
static void
dbg_dropdown_scroll_px(struct ToriRSChrome const* ui, int* content, int* view, int* offset)
{
    struct ToriDbgWidget const* w = &ui->widgets[ui->dropdown_open];

    assert(content);
    assert(view);
    assert(offset);
    *content = w->option_count * DBG_DROP_ROW_H;
    *view = dbg_dropdown_rows(ui) * DBG_DROP_ROW_H;
    *offset = w->scroll * DBG_DROP_ROW_H;
}

static void
dbg_build_dropdown_list(struct ToriRSChrome* ui)
{
    struct ToriDbgTheme const* th = &ui->theme;
    struct ToriDbgWidget const* w;
    struct ToriDbgRect rect;
    struct ToriDbgRect clip;
    struct ToriDbgRect row_clip;
    struct ToriDbgRect bar;
    int rows;
    int row_x;
    int row_w;

    if( ui->dropdown_open < 0 )
        return;

    w = &ui->widgets[ui->dropdown_open];
    rect = dbg_dropdown_rect(ui);
    if( rect.w <= 0 || rect.h <= 0 )
        return;

    rows = dbg_dropdown_rows(ui);
    clip = rect;
    bar = dbg_dropdown_scrollbar_rect(ui);
    row_x = rect.x + DBG_DROP_LIST_PAD;
    row_w = rect.w - 2 * DBG_DROP_LIST_PAD - (bar.w > 0 ? bar.w : 0);
    row_clip = clip;
    if( row_clip.w > row_x + row_w - row_clip.x )
        row_clip.w = row_x + row_w - row_clip.x;
    if( row_clip.w < 0 )
        row_clip.w = 0;

    /*
     * The body, in whichever of the two the list is.
     *
     * A value list is the cache's dropdown: graphic_1040 tiled, a *lighter*
     * parchment than the graphic_297 a panel and a closed button wear. A menu
     * list is the minimenu, and keeps the minimenu's flat brown -- the two are
     * different widgets in the game and the split is here rather than in the
     * theme because no palette can turn one into the other.
     *
     * Flat fill first either way: the tile has transparent pixels at its edges.
     */
    dbg_push_rect(ui, rect.x, rect.y, rect.w, rect.h, th->menu_body, 1, clip);
    if( !w->menu_mode && th->skin_dropdown && dbg_skin_has(ui, TORIDBG_SKIN_DROPDOWN_BODY) )
        dbg_fill_tiled(
            ui, rect.x, rect.y, rect.w, rect.h, TORIDBG_SKIN_DROPDOWN_BODY, ui->skin_tile_w,
            ui->skin_tile_h, clip);
    dbg_push_rect(ui, rect.x, rect.y, rect.w, rect.h, th->menu_chrome, 0, clip);

    for( int row = 0; row < rows; row++ )
    {
        int const index = w->scroll + row;
        int const y = rect.y + DBG_DROP_LIST_PAD + row * DBG_DROP_ROW_H;
        int const chosen = index == w->selected;
        int const hovered = ui->dropdown_hover_row == row;
        int const baseline =
            y + (DBG_DROP_ROW_H - ToriRSChrome_FontLineBox(th->font_row, ui->scale)) / 2 +
            ToriRSChrome_FontLineHeight(th->font_row, ui->scale);
        int text_x = row_x + DBG_INPUT_PAD_X;

        if( index < 0 || index >= w->option_count )
            break;

        if( w->menu_mode )
        {
            /* The minimenu's row: left-aligned, and the cursor picks it out by
             * turning the text yellow rather than by lighting the row. */
            dbg_push_text(
                ui,
                text_x,
                baseline,
                w->options[index],
                hovered ? th->menu_hover_text : th->menu_text,
                th->font_row,
                0,
                row_clip);
            continue;
        }

        {
            /*
             * The bands alternate on the OPTION index, not on the screen row,
             * so scrolling slides the stripes with the text instead of leaving
             * them pinned to the window and strobing under it.
             */
            int trans = (index & 1) ? th->dropdown_band_trans_alt : th->dropdown_band_trans;
            int const shown_w = ToriRSChrome_MeasureText(th->font_row, ui->scale, w->options[index]);

            if( hovered )
                trans = th->dropdown_row_trans_hover;
            dbg_push_rect_trans(
                ui, row_x, y, row_w, DBG_DROP_ROW_H, th->dropdown_veil, 1, trans, clip);

            /* Centred, as `cc_settextalign(1, 1, 14)` centres every row of the
             * reference's list -- and left-aligned instead when the option is
             * too wide for the column, where centring would clip both ends. */
            if( shown_w < row_w - 2 * DBG_INPUT_PAD_X )
                text_x = row_x + (row_w - shown_w) / 2;
            dbg_push_text(
                ui,
                text_x,
                baseline,
                w->options[index],
                chosen ? th->accent : th->dropdown_text,
                th->font_row,
                0,
                row_clip);
        }
    }

    /* The bar the game draws beside every list that overflows. A long palette
     * with no bar gives no clue how far down it goes -- and no way to get
     * there but the wheel. */
    if( bar.w > 0 )
    {
        int content;
        int view;
        int offset;
        dbg_dropdown_scroll_px(ui, &content, &view, &offset);
        dbg_push_scrollbar(ui, bar, content, view, offset, clip);
    }
}

struct ToriDbgPrim const*
ToriRSChrome_Prims(struct ToriRSChrome const* ui, int* out_count)
{
    if( out_count )
        *out_count = ui ? ui->prim_count : 0;
    return ui ? ui->prims : NULL;
}

int
ToriRSChrome_Damage(struct ToriRSChrome const* ui, struct ToriDbgRect* out)
{
    assert(ui);
    if( ui->damage.w <= 0 || ui->damage.h <= 0 )
        return 0;
    if( out )
        *out = ui->damage;
    return 1;
}

void
ToriRSChrome_DamageClear(struct ToriRSChrome* ui)
{
    assert(ui);
    ui->damage.x = 0;
    ui->damage.y = 0;
    ui->damage.w = 0;
    ui->damage.h = 0;
}

/* ---- input --------------------------------------------------------------- */

static int
dbg_point_in(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/** Topmost panel under the point, or -1. Later panels are drawn later, so they win. */
static int
dbg_panel_at(struct ToriRSChrome const* ui, int x, int y)
{
    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct ToriDbgPanel const* p = &ui->panels[i];
        if( !p->visible || p->last_rect.w <= 0 )
            continue;
        if( dbg_point_in(x, y, p->last_rect.x, p->last_rect.y, p->last_rect.w, p->last_rect.h) )
            return i;
    }
    return -1;
}

/**
 * Panel whose HEADER BAR is under the point, or -1.
 *
 * The header is the drag handle, so this is deliberately narrower than
 * dbg_panel_at: only the title strip, and only on a panel that has a title to
 * strip. A menu-style panel is not draggable -- it is a popup that appears
 * where it was asked for and closes on the next click, so a handle on it would
 * be a handle on something with no persistent position to move.
 */
static int
dbg_panel_header_at(struct ToriRSChrome const* ui, int x, int y)
{
    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct ToriDbgPanel const* p = &ui->panels[i];
        int title_h;

        if( !p->visible || p->last_rect.w <= 0 || p->style != TORIDBG_PANEL_WINDOW )
            continue;
        if( !p->title[0] )
            continue;
        title_h = ToriRSChrome_FontLineBox(TORIDBG_FONT_MENU, ui->scale);
        if( dbg_point_in(
                x,
                y,
                p->last_rect.x + DBG_RULE,
                p->last_rect.y + DBG_RULE,
                p->last_rect.w - 2 * DBG_RULE,
                title_h) )
            return i;
    }
    return -1;
}

/**
 * Panel whose RESIZE GRIP is under the point, or -1.
 *
 * Hit off `last_rect`, not the live x/y: mid-drag the panel's own fields have
 * already moved and the grip that was grabbed is the one that was drawn.
 */
static int
dbg_panel_grip_at(struct ToriRSChrome const* ui, int x, int y)
{
    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct ToriDbgPanel const* p = &ui->panels[i];
        struct ToriDbgRect g;

        if( !p->visible || p->last_rect.w <= 0 || !p->resizable )
            continue;
        if( p->style != TORIDBG_PANEL_WINDOW )
            continue;
        g = dbg_grip_rect(ui, p->last_rect);
        if( dbg_point_in(x, y, g.x, g.y, g.w, g.h) )
            return i;
    }
    return -1;
}

/**
 * Put a panel's bottom-right corner at (`right`, `bottom`).
 *
 * The origin does not move -- that is the whole reason the grip is in this
 * corner rather than another: a bottom-right drag is two independent clamps on
 * the size and nothing else, where any other corner also has to walk x or y
 * and can drift the edge the user is not touching.
 *
 * Both `fixed_w` and `fixed_h` are written, which is what pins the result
 * against the next Build's content sizing.
 */
static void
dbg_panel_resize_to(struct ToriRSChrome* ui, int panel, int right, int bottom)
{
    struct ToriDbgPanel* p = &ui->panels[panel];
    int w = right - p->x;
    int h = bottom - p->y;

    if( w < DBG_MIN_PANEL_W )
        w = DBG_MIN_PANEL_W;
    if( h < DBG_MIN_PANEL_H )
        h = DBG_MIN_PANEL_H;
    if( p->fixed_w == w && p->fixed_h == h )
        return;
    /* Same reason the move damages: the strip a shrinking panel just uncovered
     * is stale, and only damage tells the host to repaint it. */
    dbg_damage_add(ui, p->last_rect);
    p->fixed_w = w;
    p->fixed_h = h;
    dbg_dirty_panel(ui, panel);
}

/** Move a panel, invalidating both the box it left and the one it now covers. */
static void
dbg_panel_move_to(struct ToriRSChrome* ui, int panel, int x, int y)
{
    struct ToriDbgPanel* p = &ui->panels[panel];

    if( p->x == x && p->y == y )
        return;
    /* The vacated box has to repaint too, or the panel smears across the
     * screen on every drag frame -- damage is the only thing telling the host
     * that those pixels are now stale. */
    dbg_damage_add(ui, p->last_rect);
    p->x = x;
    p->y = y;
    dbg_dirty_panel(ui, panel);
}

/* ---- panel scrolling ------------------------------------------------------
 *
 * The bar is geometry, not a widget: it belongs to the panel the way the resize
 * grip does, so it is hit off `last_rect` for the same reason the grip is --
 * mid-drag the panel's live fields may already have moved, and what was grabbed
 * is what was drawn.
 */

/** Does this panel currently have a bar? Answered from the last Build. */
static int
dbg_panel_scrolls(struct ToriDbgPanel const* p)
{
    return p->scrollable && p->view_h > 0 && p->content_h > p->view_h;
}

/** The panel's scrollbar box, or an empty rect when it has none. */
static struct ToriDbgRect
dbg_panel_scrollbar_rect(struct ToriRSChrome const* ui, int panel)
{
    struct ToriDbgRect r = { 0, 0, 0, 0 };
    struct ToriDbgPanel const* p = &ui->panels[panel];
    struct DbgMenuLayout const l =
        dbg_menu_layout(ToriRSChrome_FontLineBox(TORIDBG_FONT_MENU, ui->scale), ui->scale);
    int const head_h = p->title[0] ? l.separator_y + DBG_RULE : DBG_RULE;
    int const foot_h = p->resizable ? DBG_GRIP_HIT : DBG_PAD_Y;

    if( !p->visible || p->last_rect.w <= 0 || !dbg_panel_scrolls(p) )
        return r;
    r.x = p->last_rect.x + p->last_rect.w - DBG_RULE - DBG_PAD_X - DBG_SCROLL_W;
    r.y = p->last_rect.y + head_h + DBG_PAD_Y;
    r.w = DBG_SCROLL_W;
    r.h = p->last_rect.y + p->last_rect.h - foot_h - DBG_RULE - r.y;
    if( r.h < 0 )
        r.h = 0;
    return r;
}

/** Move a panel's scroll to `scroll` px, clamped. @return 1 if it moved. */
static int
dbg_panel_scroll_to(struct ToriRSChrome* ui, int panel, int scroll)
{
    struct ToriDbgPanel* p = &ui->panels[panel];
    int const max_scroll = p->content_h - p->view_h;

    if( scroll > max_scroll )
        scroll = max_scroll;
    if( scroll < 0 )
        scroll = 0;
    if( p->scroll_y == scroll )
        return 0;
    p->scroll_y = scroll;
    dbg_dirty_panel(ui, panel);
    return 1;
}

/**
 * Press on a panel's bar: an arrow steps, the track pages, the grip starts a
 * drag. @return 1 when the press belonged to a bar.
 */
static int
dbg_panel_scroll_press(struct ToriRSChrome* ui, int x, int y)
{
    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct ToriDbgRect const bar = dbg_panel_scrollbar_rect(ui, i);
        struct ToriDbgPanel* p = &ui->panels[i];
        struct DbgScrollGeom g;
        int const step = ToriRSChrome_FontLineBox(ui->theme.font_row, ui->scale);

        if( bar.w <= 0 || !dbg_point_in_rect(x, y, bar) )
            continue;

        if( y < bar.y + DBG_SCROLL_W )
        {
            dbg_panel_scroll_to(ui, i, p->scroll_y - step);
            return 1;
        }
        if( y >= bar.y + bar.h - DBG_SCROLL_W )
        {
            dbg_panel_scroll_to(ui, i, p->scroll_y + step);
            return 1;
        }
        if( !dbg_scroll_geom(
                bar, p->content_h, p->view_h, p->scroll_y, DBG_SCROLL_W, DBG_SCROLL_GRIP_MIN, &g) )
            return 1;
        if( y >= g.grip_y && y < g.grip_y + g.grip_h )
        {
            ui->scroll_panel = i;
            ui->scroll_grab = y - g.grip_y;
            return 1;
        }
        /* Above or below the grip: page by a viewful, the desktop convention. */
        dbg_panel_scroll_to(
            ui, i, y < g.grip_y ? p->scroll_y - p->view_h : p->scroll_y + p->view_h);
        return 1;
    }
    return 0;
}

/** Continue a panel grip drag: put the grip's top under the held grab point. */
static void
dbg_panel_scroll_drag_to(struct ToriRSChrome* ui, int y)
{
    int const panel = ui->scroll_panel;
    struct ToriDbgPanel* p;
    struct ToriDbgRect bar;
    struct DbgScrollGeom g;
    int travel;

    if( !dbg_valid_panel(ui, panel) )
        return;
    p = &ui->panels[panel];
    bar = dbg_panel_scrollbar_rect(ui, panel);
    if( bar.w <= 0 )
        return;
    if( !dbg_scroll_geom(
            bar, p->content_h, p->view_h, p->scroll_y, DBG_SCROLL_W, DBG_SCROLL_GRIP_MIN, &g) )
        return;
    travel = g.track_h - g.grip_h;
    if( travel <= 0 )
        return;
    dbg_panel_scroll_to(
        ui, panel, (y - ui->scroll_grab - g.track_y) * (p->content_h - p->view_h) / travel);
}

int
ToriRSChrome_HitTest(struct ToriRSChrome const* ui, int x, int y)
{
    int panel;

    assert(ui);
    panel = dbg_panel_at(ui, x, y);
    if( panel < 0 )
        return -1;
    for( int w = ui->panels[panel].first_widget; w >= 0; w = ui->widgets[w].next )
    {
        struct ToriDbgWidget const* wd = &ui->widgets[w];
        if( !dbg_widget_shown(ui, wd) || wd->kind == TORIDBG_W_SEPARATOR ||
            wd->kind == TORIDBG_W_LABEL )
            continue;
        if( dbg_point_in(x, y, wd->x, wd->y, wd->w, wd->h) )
            return w;
    }
    return -1;
}


/**
 * Row of the open dropdown list under a point, or -1.
 *
 * The list is not made of widgets, so it is not part of the widget hit test —
 * it is a view on the borrowed `options` array, and its rows are addressed by
 * index. Every mouse entry point asks this FIRST, because an open list sits
 * over whatever panel is beneath it and must take the click.
 */
static int
dbg_dropdown_row_at(struct ToriRSChrome const* ui, int x, int y)
{
    struct ToriDbgRect rect;
    struct ToriDbgRect bar;
    struct ToriDbgWidget const* w;
    int row;
    int rows;

    if( ui->dropdown_open < 0 )
        return -1;
    rect = dbg_dropdown_rect(ui);
    if( rect.w <= 0 || !dbg_point_in(x, y, rect.x, rect.y, rect.w, rect.h) )
        return -1;
    /* The bar is inside the list, so its column is not a row. Without this a
     * press on the grip would also be a press on whatever option it covers,
     * and releasing the drag would pick that option. */
    bar = dbg_dropdown_scrollbar_rect(ui);
    if( bar.w > 0 && dbg_point_in(x, y, bar.x, bar.y, bar.w, bar.h) )
        return -1;

    w = &ui->widgets[ui->dropdown_open];
    rows = dbg_dropdown_rows(ui);
    row = (y - rect.y - DBG_DROP_LIST_PAD) / DBG_DROP_ROW_H;
    if( row < 0 || row >= rows || w->scroll + row >= w->option_count )
        return -1;
    return row;
}

/** Move the open list to `scroll`, damaging it if that changed anything. */
static int
dbg_dropdown_scroll_to(struct ToriRSChrome* ui, int scroll)
{
    struct ToriDbgWidget* w = &ui->widgets[ui->dropdown_open];
    int const before = w->scroll;

    w->scroll = scroll;
    dbg_dropdown_clamp(w);
    if( w->scroll == before )
        return 0;
    ui->dirty = 1;
    dbg_damage_add(ui, dbg_dropdown_rect(ui));
    /* The hovered row is a WINDOW index: the same row number now shows a
     * different option, so whatever was highlighted has moved out from under
     * the cursor. Recomputed by the next move; cleared now so the highlight
     * does not lag a scroll by one event. */
    ui->dropdown_hover_row = -1;
    return 1;
}

/**
 * Take a press on the open list's scrollbar.
 *
 * Returns 0 when the point is not on a bar, so the caller falls through to the
 * rows. The three regions are the ones the reference wires up in ~script31: an
 * arrow at each end steps a row (`script32`/`script33`), the grip drags
 * (`script35`), and the track pages (`script34`).
 */
static int
dbg_dropdown_scroll_press(struct ToriRSChrome* ui, int x, int y)
{
    struct ToriDbgRect bar;
    struct DbgScrollGeom g;
    struct ToriDbgWidget const* w;
    int content;
    int view;
    int offset;

    if( ui->dropdown_open < 0 )
        return 0;
    bar = dbg_dropdown_scrollbar_rect(ui);
    if( bar.w <= 0 || !dbg_point_in(x, y, bar.x, bar.y, bar.w, bar.h) )
        return 0;

    w = &ui->widgets[ui->dropdown_open];
    dbg_dropdown_scroll_px(ui, &content, &view, &offset);
    if( !dbg_scroll_geom(bar, content, view, offset, DBG_SCROLL_W, DBG_SCROLL_GRIP_MIN, &g) )
        return 1;

    if( y < g.track_y )
        dbg_dropdown_scroll_to(ui, w->scroll - 1);
    else if( y >= bar.y + bar.h - DBG_SCROLL_W )
        dbg_dropdown_scroll_to(ui, w->scroll + 1);
    else if( y < g.grip_y )
        dbg_dropdown_scroll_to(ui, w->scroll - dbg_dropdown_rows(ui));
    else if( y >= g.grip_y + g.grip_h )
        dbg_dropdown_scroll_to(ui, w->scroll + dbg_dropdown_rows(ui));
    else
    {
        ui->dropdown_scroll_drag = 1;
        ui->dropdown_scroll_grab = y - g.grip_y;
    }
    return 1;
}

/** Follow a grip drag: put the grabbed point of the grip back under the cursor. */
static void
dbg_dropdown_scroll_drag_to(struct ToriRSChrome* ui, int y)
{
    struct ToriDbgRect bar;
    struct DbgScrollGeom g;
    int content;
    int view;
    int offset;
    int travel;
    int range;

    assert(ui->dropdown_open >= 0);
    bar = dbg_dropdown_scrollbar_rect(ui);
    dbg_dropdown_scroll_px(ui, &content, &view, &offset);
    if( bar.w <= 0 ||
        !dbg_scroll_geom(bar, content, view, offset, DBG_SCROLL_W, DBG_SCROLL_GRIP_MIN, &g) )
    {
        ui->dropdown_scroll_drag = 0;
        return;
    }

    /* Invert dbg_scroll_geom: the grip's top travels (track_h - grip_h) over a
     * scroll range of (content - view), so a wanted top maps back to a wanted
     * offset. Rounded to the nearest row on the way out, because this list
     * scrolls by rows -- a pixel offset it cannot hold would make the grip
     * drift away from the cursor as the drag went on. */
    travel = g.track_h - g.grip_h;
    range = content - view;
    if( travel <= 0 || range <= 0 )
        return;
    offset = (y - ui->dropdown_scroll_grab - g.track_y) * range / travel;
    dbg_dropdown_scroll_to(
        ui, (offset + DBG_DROP_ROW_H / 2) / (DBG_DROP_ROW_H > 0 ? DBG_DROP_ROW_H : 1));
}

/** Close the open list, damaging the area it occupied. */
static void
dbg_dropdown_close(struct ToriRSChrome* ui)
{
    if( ui->dropdown_open < 0 )
        return;
    dbg_damage_add(ui, dbg_dropdown_rect(ui));
    dbg_dirty_widget(ui, ui->dropdown_open);
    ui->dropdown_open = -1;
    ui->dropdown_hover_row = -1;
    /* The grip cannot outlive the list it scrolls: a drag left armed here
     * would move the NEXT list the moment one opened. */
    ui->dropdown_scroll_drag = 0;
    ui->dirty = 1;
}

int
ToriRSChrome_MouseMove(struct ToriRSChrome* ui, int x, int y)
{
    int hit;

    assert(ui);

    /* Recorded before any early return: a strip's hovered TAB is resolved from
     * the position at draw time, and a move that a drag consumes still moved
     * the cursor. */
    ui->hover_x = x;
    ui->hover_y = y;

    /* A panel's own grip, held for as long as the button is: same rule as the
     * dropdown's below it, and checked first for the same reason. */
    if( ui->scroll_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->scroll_panel) )
        {
            dbg_panel_scroll_drag_to(ui, y);
            return 1;
        }
        ui->scroll_panel = -1;
    }

    /* Ahead of every other claim on the pointer, including the panel drags
     * below: a scrollbar grip is held until it is let go, and the cursor
     * leaving the bar (or the list) does not release it. */
    if( ui->dropdown_scroll_drag )
    {
        if( ui->dropdown_open < 0 )
            ui->dropdown_scroll_drag = 0;
        else
        {
            dbg_dropdown_scroll_drag_to(ui, y);
            return 1;
        }
    }

    /* Ahead of everything, for the same reason the drag is: the pointer
     * belongs to the grip until it is let go. */
    if( ui->resize_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->resize_panel) )
            dbg_panel_resize_to(
                ui, ui->resize_panel, x - ui->resize_grab_x, y - ui->resize_grab_y);
        else
            ui->resize_panel = -1;
        return 1;
    }

    /* Ahead of everything: while a panel is held, the pointer belongs to it
     * wherever it has got to, including outside the panel's own box. Hover
     * must not follow the cursor mid-drag or rows light up under a panel the
     * user is carrying. */
    if( ui->drag_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->drag_panel) )
            dbg_panel_move_to(ui, ui->drag_panel, x - ui->drag_grab_x, y - ui->drag_grab_y);
        else
            ui->drag_panel = -1;
        return 1;
    }

    if( ui->dropdown_open >= 0 )
    {
        int const row = dbg_dropdown_row_at(ui, x, y);
        if( row != ui->dropdown_hover_row )
        {
            ui->dropdown_hover_row = row;
            ui->dirty = 1;
            dbg_damage_add(ui, dbg_dropdown_rect(ui));
        }
        if( row >= 0 )
            return 1;
    }

    hit = ToriRSChrome_HitTest(ui, x, y);
    if( hit != ui->hover )
    {
        /* Both rows repaint: the one losing the highlight and the one gaining it. */
        dbg_dirty_widget(ui, ui->hover);
        ui->hover = hit;
        dbg_dirty_widget(ui, hit);
    }
    else if( hit >= 0 && ui->widgets[hit].kind == TORIDBG_W_TABSTRIP )
    {
        /* Moving between two tabs of one strip does not change the hovered
         * WIDGET, so the highlight would stick to the tab first entered. The
         * strip is the one widget whose hover lives below handle granularity. */
        dbg_dirty_widget(ui, hit);
    }
    return dbg_panel_at(ui, x, y) >= 0;
}

int
ToriRSChrome_MouseDown(struct ToriRSChrome* ui, int x, int y)
{
    int hit;

    assert(ui);

    /* A press inside the open list belongs to the list; MouseUp turns it into
     * a selection. A press anywhere else closes it, which is what makes
     * clicking away dismiss rather than select. */
    if( ui->dropdown_open >= 0 )
    {
        /* The bar first: it is inside the list, and a press on it must not be
         * the press that dismisses the list. */
        if( dbg_dropdown_scroll_press(ui, x, y) )
        {
            ui->press = -1;
            return 1;
        }
        if( dbg_dropdown_row_at(ui, x, y) >= 0 )
        {
            ui->press = -1;
            return 1;
        }
        dbg_dropdown_close(ui);
    }

    /* A panel's own bar, before its rows: the bar's column is inside the
     * content area, so a press there would otherwise land on whatever row it
     * happens to sit beside. */
    if( dbg_panel_scroll_press(ui, x, y) )
    {
        ui->press = -1;
        return 1;
    }

    /* The grip is tested before the widgets, not after them as the header is.
     * The header strip is empty by construction, so a control found there is a
     * control the caller deliberately put there and it should win; the grip
     * sits in the bottom-left of the content column, where the last row's
     * padding can legitimately overlap it, and a grip that loses to whatever
     * row happens to end near it is a grip that works on some panels and not
     * others. */
    {
        int const grip = dbg_panel_grip_at(ui, x, y);
        if( grip >= 0 )
        {
            /* Offsets from the corner being dragged, not from the origin, so
             * the panel keeps the exact point it was picked up by instead of
             * snapping its corner to the cursor on the first move. */
            ui->resize_grab_x =
                x - (ui->panels[grip].last_rect.x + ui->panels[grip].last_rect.w);
            ui->resize_grab_y =
                y - (ui->panels[grip].last_rect.y + ui->panels[grip].last_rect.h);
            ui->resize_panel = grip;
            ui->press = -1;
            if( ui->focus >= 0 )
            {
                dbg_dirty_widget(ui, ui->focus);
                ui->focus = -1;
            }
            return 1;
        }
    }

    hit = ToriRSChrome_HitTest(ui, x, y);

    /* A press on the header with no widget under it starts a drag. Widgets win
     * -- a control that happens to sit in the title strip is still a control --
     * which is why this is tested after the hit test rather than before it. */
    if( hit < 0 )
    {
        int const header = dbg_panel_header_at(ui, x, y);
        if( header >= 0 )
        {
            ui->drag_panel = header;
            ui->drag_grab_x = x - ui->panels[header].x;
            ui->drag_grab_y = y - ui->panels[header].y;
            ui->press = -1;
            if( ui->focus >= 0 )
            {
                dbg_dirty_widget(ui, ui->focus);
                ui->focus = -1;
            }
            return 1;
        }
    }

    ui->press = hit;
    if( hit >= 0 && ui->widgets[hit].kind == TORIDBG_W_MODELVIEW )
    {
        /* Focusable so the host can route WASD/zoom keys at it -- the same
         * focus slot text inputs use, so clicking anywhere else releases it
         * through the existing paths. */
        if( ui->focus != hit )
        {
            dbg_dirty_widget(ui, ui->focus);
            ui->focus = hit;
            dbg_dirty_widget(ui, hit);
        }
        return 1;
    }
    if( hit >= 0 && ui->widgets[hit].kind == TORIDBG_W_TEXTINPUT )
    {
        if( ui->focus != hit )
        {
            dbg_dirty_widget(ui, ui->focus);
            ui->focus = hit;
            dbg_dirty_widget(ui, hit);
        }
        ui->widgets[hit].caret = (int)strlen(ui->widgets[hit].text);
    }
    else if( ui->focus >= 0 )
    {
        dbg_dirty_widget(ui, ui->focus);
        ui->focus = -1;
    }
    return dbg_panel_at(ui, x, y) >= 0;
}

int
ToriRSChrome_MouseUp(struct ToriRSChrome* ui, int x, int y)
{
    int hit;

    assert(ui);

    /* The release that ends a panel's grip drag, ahead of the row logic for the
     * same reason the dropdown's is: letting go over a row must not click it. */
    if( ui->scroll_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->scroll_panel) )
            dbg_panel_scroll_drag_to(ui, y);
        ui->scroll_panel = -1;
        ui->press = -1;
        return 1;
    }

    /* The release that ends a grip drag. It reports consumed and returns
     * before the row logic below, so letting go of the grip over a row does
     * not also choose that row. */
    if( ui->dropdown_scroll_drag )
    {
        if( ui->dropdown_open >= 0 )
            dbg_dropdown_scroll_drag_to(ui, y);
        ui->dropdown_scroll_drag = 0;
        ui->press = -1;
        return 1;
    }

    /* The release that ends a resize, before the one that ends a drag: the two
     * are exclusive, and this one also reports consumed so the click that set
     * the width is not delivered to the world underneath. */
    if( ui->resize_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->resize_panel) )
            dbg_panel_resize_to(
                ui, ui->resize_panel, x - ui->resize_grab_x, y - ui->resize_grab_y);
        ui->resize_panel = -1;
        ui->press = -1;
        return 1;
    }

    /* The release that ends a drag. It lands the panel at the cursor one last
     * time and retires the grab -- and reports consumed, so the click that
     * dropped the panel is not also delivered to whatever is underneath it. */
    if( ui->drag_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->drag_panel) )
            dbg_panel_move_to(ui, ui->drag_panel, x - ui->drag_grab_x, y - ui->drag_grab_y);
        ui->drag_panel = -1;
        ui->press = -1;
        return 1;
    }

    if( ui->dropdown_open >= 0 )
    {
        int const row = dbg_dropdown_row_at(ui, x, y);
        if( row >= 0 )
        {
            struct ToriDbgWidget* dd = &ui->widgets[ui->dropdown_open];
            int const chosen = ui->dropdown_open;
            dd->selected = dd->scroll + row;
            dbg_dropdown_close(ui);
            /* Reported like any other activation, so a caller drains choices
             * and clicks through one TakeActivated loop. */
            ui->activated = chosen;
            ui->press = -1;
            return 1;
        }
    }

    hit = ToriRSChrome_HitTest(ui, x, y);
    /* Press and release must land on the same widget, so a drag off a checkbox
     * cancels rather than toggles. */
    if( hit >= 0 && hit == ui->press )
    {
        struct ToriDbgWidget* w = &ui->widgets[hit];
        if( w->kind == TORIDBG_W_CHECKBOX )
        {
            w->checked = !w->checked;
            ui->activated = hit;
            dbg_dirty_widget(ui, hit);
        }
        else if( w->kind == TORIDBG_W_MENUITEM || w->kind == TORIDBG_W_BUTTON )
        {
            ui->activated = hit;
            /* The button repaints because its pressed state just ended. */
            if( w->kind == TORIDBG_W_BUTTON )
                dbg_dirty_widget(ui, hit);
        }
        else if( w->kind == TORIDBG_W_TABSTRIP )
        {
            int const tab = dbg_tab_at(ui, w, x, y);
            /* A click on the tab already showing is not a no-op to report: the
             * caller may be using the strip to re-run a page's build. It just
             * does not need the switch work, which PanelSetActiveTab skips on
             * its own. */
            if( tab >= 0 )
            {
                ToriRSChrome_PanelSetActiveTab(ui, w->panel, tab);
                ui->activated = hit;
            }
        }
        else if( w->kind == TORIDBG_W_DROPDOWN )
        {
            /* Toggle: a second click on the closed row shuts it again. */
            if( ui->dropdown_open == hit )
            {
                dbg_dropdown_close(ui);
            }
            else if( w->option_count > 0 )
            {
                /* A menu has no persistent value: clearing on open keeps the
                 * list from highlighting last time's command, and makes
                 * choosing the same row twice two activations. */
                if( w->menu_mode )
                    w->selected = -1;
                ui->dropdown_open = hit;
                ui->dropdown_hover_row = -1;
                dbg_dropdown_clamp(w);
                dbg_dirty_widget(ui, hit);
            }
        }
    }
    ui->press = -1;
    return dbg_panel_at(ui, x, y) >= 0;
}


int
ToriRSChrome_MouseWheel(struct ToriRSChrome* ui, int x, int y, int delta)
{
    struct ToriDbgWidget* w;
    int before;

    assert(ui);

    /* No open list: the wheel may still belong to a widget under the pointer.
     * A CLOSED dropdown steps its selection -- the desktop convention, and
     * what makes a palette of hundreds usable without opening it -- except in
     * menu mode, where the rows are commands and a wheel must never run one.
     * Anything else on a panel consumes the event into nothing, so the camera
     * behind the panel stays still. */
    if( ui->dropdown_open < 0 )
    {
        int const hit = ToriRSChrome_HitTest(ui, x, y);
        if( hit >= 0 && ui->widgets[hit].kind == TORIDBG_W_DROPDOWN &&
            !ui->widgets[hit].menu_mode && ui->widgets[hit].option_count > 0 )
        {
            struct ToriDbgWidget* dd = &ui->widgets[hit];
            int const was = dd->selected;
            /* Wheel down (negative delta) moves DOWN the list. */
            dd->selected -= delta;
            if( dd->selected < 0 )
                dd->selected = 0;
            if( dd->selected >= dd->option_count )
                dd->selected = dd->option_count - 1;
            if( dd->selected != was )
            {
                /* Reported like a chosen row, so the caller's TakeActivated
                 * loop reacts to a wheel exactly as it would to a click. */
                ui->activated = hit;
                dbg_dirty_widget(ui, hit);
            }
            return 1;
        }
        /* Otherwise the panel under the pointer scrolls, if it can. Tested
         * after the closed-dropdown case so a wheel over a picker still steps
         * the picker rather than scrolling the page out from under it. */
        {
            int const panel = dbg_panel_at(ui, x, y);
            if( panel >= 0 )
            {
                if( dbg_panel_scrolls(&ui->panels[panel]) )
                {
                    int const step = ToriRSChrome_FontLineBox(ui->theme.font_row, ui->scale);
                    /* Wheel down (negative delta) moves the content UP. */
                    dbg_panel_scroll_to(ui, panel, ui->panels[panel].scroll_y - delta * step);
                }
                return 1;
            }
            return 0;
        }
    }

    {
        struct ToriDbgRect const rect = dbg_dropdown_rect(ui);
        if( rect.w <= 0 || !dbg_point_in(x, y, rect.x, rect.y, rect.w, rect.h) )
            return dbg_panel_at(ui, x, y) >= 0;
    }

    w = &ui->widgets[ui->dropdown_open];
    before = w->scroll;
    w->scroll += delta;
    dbg_dropdown_clamp(w);
    if( w->scroll != before )
    {
        /* The list is not a panel, so panel dirtying would not repaint it. */
        ui->dirty = 1;
        dbg_damage_add(ui, dbg_dropdown_rect(ui));
    }
    /* Consumed either way: a wheel over an open list must never also zoom the
     * camera behind it, including at the ends of the list. */
    return 1;
}

int
ToriRSChrome_WantsWheel(struct ToriRSChrome const* ui, int x, int y)
{
    assert(ui);
    if( ui->dropdown_open >= 0 )
    {
        struct ToriDbgRect const rect = dbg_dropdown_rect(ui);
        if( rect.w > 0 && dbg_point_in(x, y, rect.x, rect.y, rect.w, rect.h) )
            return 1;
    }
    return dbg_panel_at(ui, x, y) >= 0;
}

int
ToriRSChrome_KeyChar(struct ToriRSChrome* ui, int ch)
{
    struct ToriDbgWidget* w;
    int len;

    assert(ui);
    if( ui->focus < 0 )
        return 0;
    /* Typing edits TEXT INPUTS only. A model view holds focus so the HOST can
     * route its own keys at it; characters falling through into its unused
     * text field would be silent state nobody can see. */
    if( ui->widgets[ui->focus].kind != TORIDBG_W_TEXTINPUT )
        return 0;
    if( ch < 0x20 || ch > 0x7E )
        return 0;
    w = &ui->widgets[ui->focus];
    len = (int)strlen(w->text);
    if( len >= TORIDBG_INPUT_MAX - 1 )
        return 1;
    if( w->caret < 0 )
        w->caret = 0;
    if( w->caret > len )
        w->caret = len;
    memmove(w->text + w->caret + 1, w->text + w->caret, (size_t)(len - w->caret) + 1);
    w->text[w->caret] = (char)ch;
    w->caret++;
    dbg_dirty_widget(ui, ui->focus);
    return 1;
}

int
ToriRSChrome_KeyEdit(struct ToriRSChrome* ui, int key)
{
    /* Same rule as KeyChar: editing keys belong to text inputs alone. */
    if( ui && ui->focus >= 0 && ui->widgets[ui->focus].kind != TORIDBG_W_TEXTINPUT )
        return 0;
    struct ToriDbgWidget* w;
    int len;

    assert(ui);
    if( ui->focus < 0 )
        return 0;
    w = &ui->widgets[ui->focus];
    len = (int)strlen(w->text);
    if( w->caret < 0 )
        w->caret = 0;
    if( w->caret > len )
        w->caret = len;

    switch( key )
    {
    case TORIDBG_KEY_BACKSPACE:
        if( w->caret > 0 )
        {
            memmove(w->text + w->caret - 1, w->text + w->caret, (size_t)(len - w->caret) + 1);
            w->caret--;
            dbg_dirty_widget(ui, ui->focus);
        }
        return 1;
    case TORIDBG_KEY_DELETE:
        if( w->caret < len )
        {
            memmove(w->text + w->caret, w->text + w->caret + 1, (size_t)(len - w->caret));
            dbg_dirty_widget(ui, ui->focus);
        }
        return 1;
    case TORIDBG_KEY_LEFT:
        if( w->caret > 0 )
        {
            w->caret--;
            dbg_dirty_widget(ui, ui->focus);
        }
        return 1;
    case TORIDBG_KEY_RIGHT:
        if( w->caret < len )
        {
            w->caret++;
            dbg_dirty_widget(ui, ui->focus);
        }
        return 1;
    case TORIDBG_KEY_HOME:
        if( w->caret != 0 )
        {
            w->caret = 0;
            dbg_dirty_widget(ui, ui->focus);
        }
        return 1;
    case TORIDBG_KEY_END:
        if( w->caret != len )
        {
            w->caret = len;
            dbg_dirty_widget(ui, ui->focus);
        }
        return 1;
    case TORIDBG_KEY_ENTER:
        ui->activated = ui->focus;
        return 1;
    case TORIDBG_KEY_ESCAPE:
        dbg_dirty_widget(ui, ui->focus);
        ui->focus = -1;
        return 1;
    default:
        return 0;
    }
}

int
ToriRSChrome_TakeActivated(struct ToriRSChrome* ui)
{
    int const fired = ui ? ui->activated : -1;
    if( ui )
        ui->activated = -1;
    return fired;
}

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
 */

/** Inner left/right padding of a window panel's content column. */
#define DBG_PAD_X 5
/** Inner top/bottom padding of a window panel's content column. */
#define DBG_PAD_Y 4
/** Extra pixels between consecutive rows. */
#define DBG_ROW_GAP 2
/** Checkbox square edge. */
#define DBG_CHECK_SIZE 9
/** Gap between a checkbox/label and what follows it. */
#define DBG_CHECK_GAP 5
/** Horizontal padding inside a text input's box. */
#define DBG_INPUT_PAD_X 3
/** Vertical padding inside a text input's box. */
#define DBG_INPUT_PAD_Y 2
/** A text input never lays out narrower than this, even when empty. */
#define DBG_INPUT_MIN_W 60
/** Width of the dropdown's arrow gutter, right-aligned inside the box. */
#define DBG_DROP_ARROW_W 9
/** Row pitch inside the open dropdown list. */
#define DBG_DROP_ROW_H (ToriDbgFont_Small_LINE_BOX)
/** Height of a separator row: a 1px rule with air above and below. */
#define DBG_SEP_H 5
/** Narrowest a panel may be, before borders. */
#define DBG_MIN_CONTENT_W 16

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
dbg_menu_layout(int line_box)
{
    int const box = line_box > 0 ? line_box : ToriDbgFont_Menu_LINE_BOX;
    struct DbgMenuLayout l;
    l.line_height = box - 2;
    l.row_stride = box - 1;
    l.header_bar_h = box;
    l.separator_y = box + 2;
    l.option_base_y = 2 * box - 1;
    l.chrome_h = box + 5;
    l.hover_above = box - 3;
    l.hover_below = 3;
    l.width_pad = 8;
    l.border_inset = box + 3;
    return l;
}

/* ---- text metrics -------------------------------------------------------- */

static int const*
dbg_advance_table(int font_slot)
{
    return font_slot == TORIDBG_FONT_MENU ? ToriDbgFont_Menu_advance_px
                                          : ToriDbgFont_Small_advance_px;
}

int
ToriDbgUI_FontLineHeight(int font_slot)
{
    return font_slot == TORIDBG_FONT_MENU ? ToriDbgFont_Menu_LINE_HEIGHT
                                          : ToriDbgFont_Small_LINE_HEIGHT;
}

int
ToriDbgUI_FontLineBox(int font_slot)
{
    return font_slot == TORIDBG_FONT_MENU ? ToriDbgFont_Menu_LINE_BOX
                                          : ToriDbgFont_Small_LINE_BOX;
}

int
ToriDbgUI_MeasureText(int font_slot, char const* text)
{
    int const* adv = dbg_advance_table(font_slot);
    int w = 0;
    unsigned char const* p;

    assert(text);
    for( p = (unsigned char const*)text; *p; p++ )
        w += adv[*p];
    return w;
}

/** Width of the first `len` bytes — what the caret position is measured from. */
static int
dbg_measure_prefix(int font_slot, char const* text, int len)
{
    int const* adv = dbg_advance_table(font_slot);
    int w = 0;

    for( int i = 0; i < len && text[i]; i++ )
        w += adv[(unsigned char)text[i]];
    return w;
}

/* ---- small helpers ------------------------------------------------------- */

static int
dbg_max(int a, int b)
{
    return a > b ? a : b;
}

static int
dbg_valid_panel(struct ToriDbgUI const* ui, int panel)
{
    return ui && panel >= 0 && panel < ui->panel_count;
}

static int
dbg_valid_widget(struct ToriDbgUI const* ui, int widget)
{
    return ui && widget >= 0 && widget < ui->widget_count;
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
dbg_dirty_panel(struct ToriDbgUI* ui, int panel)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    ui->panels[panel].dirty = 1;
    ui->dirty = 1;
}

static void
dbg_dirty_widget(struct ToriDbgUI* ui, int widget)
{
    if( dbg_valid_widget(ui, widget) )
        dbg_dirty_panel(ui, ui->widgets[widget].panel);
}

/** Union `r` into the accumulated invalid region. Empty boxes contribute nothing. */
static void
dbg_damage_add(struct ToriDbgUI* ui, struct ToriDbgRect r)
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
ToriDbgUI_Init(struct ToriDbgUI* ui)
{
    assert(ui);
    memset(ui, 0, sizeof(*ui));
    ui->theme = toridbg_theme_default;
    ui->focus = -1;
    ui->hover = -1;
    ui->press = -1;
    ui->activated = -1;
    ui->dropdown_open = -1;
    ui->caret_visible = 1;
}

void
ToriDbgUI_Reset(struct ToriDbgUI* ui)
{
    assert(ui);
    /* Everything on screen is going away, so all of it is invalid. */
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_damage_add(ui, ui->panels[i].last_rect);
    ui->panel_count = 0;
    ui->dropdown_open = -1;
    ui->dropdown_hover_row = -1;
    ui->widget_count = 0;
    ui->prim_count = 0;
    ui->focus = -1;
    ui->hover = -1;
    ui->press = -1;
    ui->activated = -1;
    ui->overflow = 0;
    ui->dirty = 1;
}

void
ToriDbgUI_SetTheme(struct ToriDbgUI* ui, struct ToriDbgTheme const* theme)
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
ToriDbgUI_PanelAdd(struct ToriDbgUI* ui, int style, int x, int y, int fixed_w, char const* title)
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
    p->dirty = 1;
    dbg_copy(p->title, TORIDBG_LABEL_MAX, title);
    ui->panel_count++;
    ui->dirty = 1;
    return handle;
}

void
ToriDbgUI_PanelMove(struct ToriDbgUI* ui, int panel, int x, int y)
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
ToriDbgUI_PanelSetVisible(struct ToriDbgUI* ui, int panel, int visible)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    visible = visible ? 1 : 0;
    if( ui->panels[panel].visible == visible )
        return;
    ui->panels[panel].visible = visible;
    dbg_dirty_panel(ui, panel);
}

struct ToriDbgRect
ToriDbgUI_PanelRect(struct ToriDbgUI const* ui, int panel)
{
    struct ToriDbgRect none = { 0, 0, 0, 0 };
    if( !dbg_valid_panel(ui, panel) )
        return none;
    return ui->panels[panel].last_rect;
}

static int
dbg_widget_add(struct ToriDbgUI* ui, int panel, int kind)
{
    struct ToriDbgWidget* w;
    int handle;

    if( !dbg_valid_panel(ui, panel) || ui->widget_count >= TORIDBG_MAX_WIDGETS )
    {
        if( ui )
            ui->overflow = 1;
        return -1;
    }
    handle = ui->widget_count++;
    w = &ui->widgets[handle];
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    w->panel = panel;
    w->next = -1;

    if( ui->panels[panel].first_widget < 0 )
        ui->panels[panel].first_widget = handle;
    else
        ui->widgets[ui->panels[panel].last_widget].next = handle;
    ui->panels[panel].last_widget = handle;

    dbg_dirty_panel(ui, panel);
    return handle;
}

int
ToriDbgUI_Label(struct ToriDbgUI* ui, int panel, char const* text)
{
    return ToriDbgUI_LabelColored(ui, panel, text, 0);
}

int
ToriDbgUI_LabelColored(struct ToriDbgUI* ui, int panel, char const* text, uint32_t color)
{
    int const h = dbg_widget_add(ui, panel, TORIDBG_W_LABEL);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].text, TORIDBG_INPUT_MAX, text);
    ui->widgets[h].color = color;
    return h;
}

int
ToriDbgUI_Checkbox(struct ToriDbgUI* ui, int panel, char const* label, int checked)
{
    int const h = dbg_widget_add(ui, panel, TORIDBG_W_CHECKBOX);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIDBG_LABEL_MAX, label);
    ui->widgets[h].checked = checked ? 1 : 0;
    return h;
}

int
ToriDbgUI_TextInput(struct ToriDbgUI* ui, int panel, char const* label, char const* text)
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
ToriDbgUI_Dropdown(
    struct ToriDbgUI* ui,
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
ToriDbgUI_DropdownSetOptions(
    struct ToriDbgUI* ui,
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
ToriDbgUI_DropdownSelected(struct ToriDbgUI const* ui, int widget)
{
    if( !dbg_valid_widget(ui, widget) )
        return -1;
    return ui->widgets[widget].selected;
}

void
ToriDbgUI_DropdownSetSelected(struct ToriDbgUI* ui, int widget, int selected)
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
ToriDbgUI_Separator(struct ToriDbgUI* ui, int panel)
{
    return dbg_widget_add(ui, panel, TORIDBG_W_SEPARATOR);
}

int
ToriDbgUI_MenuItem(struct ToriDbgUI* ui, int panel, char const* text)
{
    int const h = dbg_widget_add(ui, panel, TORIDBG_W_MENUITEM);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].text, TORIDBG_INPUT_MAX, text);
    return h;
}

/* ---- mutation ------------------------------------------------------------ */

void
ToriDbgUI_SetText(struct ToriDbgUI* ui, int widget, char const* text)
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
ToriDbgUI_SetLabel(struct ToriDbgUI* ui, int widget, char const* label)
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
ToriDbgUI_SetColor(struct ToriDbgUI* ui, int widget, uint32_t color)
{
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].color == color )
        return;
    ui->widgets[widget].color = color;
    dbg_dirty_widget(ui, widget);
}

void
ToriDbgUI_SetChecked(struct ToriDbgUI* ui, int widget, int checked)
{
    checked = checked ? 1 : 0;
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].checked == checked )
        return;
    ui->widgets[widget].checked = checked;
    dbg_dirty_widget(ui, widget);
}

int
ToriDbgUI_Checked(struct ToriDbgUI const* ui, int widget)
{
    return dbg_valid_widget(ui, widget) ? ui->widgets[widget].checked : 0;
}

char const*
ToriDbgUI_Text(struct ToriDbgUI const* ui, int widget)
{
    return dbg_valid_widget(ui, widget) ? ui->widgets[widget].text : "";
}

void
ToriDbgUI_SetCaretVisible(struct ToriDbgUI* ui, int visible)
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
dbg_widget_width(struct ToriDbgUI const* ui, struct ToriDbgWidget const* w)
{
    (void)ui;
    switch( w->kind )
    {
    case TORIDBG_W_LABEL:
        return w->text ? ToriDbgUI_MeasureText(TORIDBG_FONT_SMALL, w->text) : 0;
    case TORIDBG_W_CHECKBOX:
        return DBG_CHECK_SIZE + DBG_CHECK_GAP +
               (w->label ? ToriDbgUI_MeasureText(TORIDBG_FONT_SMALL, w->label) : 0);
    case TORIDBG_W_TEXTINPUT:
    {
        int const label_w =
            w->label ? ToriDbgUI_MeasureText(TORIDBG_FONT_SMALL, w->label) : 0;
        int box_w = (w->text ? ToriDbgUI_MeasureText(TORIDBG_FONT_SMALL, w->text) : 0) +
                    2 * DBG_INPUT_PAD_X + 2;
        if( box_w < DBG_INPUT_MIN_W )
            box_w = DBG_INPUT_MIN_W;
        return label_w + (label_w > 0 ? DBG_CHECK_GAP : 0) + box_w;
    }
    case TORIDBG_W_MENUITEM:
        return w->text ? ToriDbgUI_MeasureText(TORIDBG_FONT_MENU, w->text) : 0;
    case TORIDBG_W_DROPDOWN:
    {
        int const label_w = w->label[0] ? ToriDbgUI_MeasureText(TORIDBG_FONT_SMALL, w->label) : 0;
        int box_w = DBG_INPUT_MIN_W;
        /* Sized to the widest option, so choosing one never resizes the panel
         * under the cursor. Palettes are built once, so this walk is not per
         * frame -- dbg_widget_width only runs on a dirty build. */
        for( int i = 0; i < w->option_count; i++ )
        {
            int const ow = ToriDbgUI_MeasureText(TORIDBG_FONT_SMALL, w->options[i]);
            if( ow > box_w )
                box_w = ow;
        }
        box_w += 2 * DBG_INPUT_PAD_X + 2 + DBG_DROP_ARROW_W;
        return label_w + (label_w > 0 ? DBG_CHECK_GAP : 0) + box_w;
    }
    case TORIDBG_W_SEPARATOR:
    default:
        return 0;
    }
}

/** Row height of one widget, excluding DBG_ROW_GAP. */
static int
dbg_widget_height(struct ToriDbgWidget const* w)
{
    int const line = ToriDbgFont_Small_LINE_BOX;
    switch( w->kind )
    {
    case TORIDBG_W_CHECKBOX:
        return dbg_max(line, DBG_CHECK_SIZE);
    case TORIDBG_W_TEXTINPUT:
    case TORIDBG_W_DROPDOWN:
        return line + 2 * DBG_INPUT_PAD_Y + 2;
    case TORIDBG_W_SEPARATOR:
        return DBG_SEP_H;
    case TORIDBG_W_LABEL:
    default:
        return line;
    }
}

/* ---- display list -------------------------------------------------------- */

static struct ToriDbgPrim*
dbg_prim_push(struct ToriDbgUI* ui)
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

static void
dbg_push_rect(
    struct ToriDbgUI* ui,
    int x,
    int y,
    int w,
    int h,
    uint32_t color,
    int filled,
    struct ToriDbgRect clip)
{
    struct ToriDbgPrim* p;
    if( w <= 0 || h <= 0 )
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
    p->clip = clip;
}

/** @param y the text baseline (ToriDraw2D_DrawString's y), not a box top. */
static void
dbg_push_text(
    struct ToriDbgUI* ui,
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
    p->shadowed = shadowed;
    p->text = text;
    p->clip = clip;
}

/*
 * A window panel: 1px border, body fill, an optional title bar in the menu
 * face, then one row per widget in the small face.
 */
static void
dbg_build_window(struct ToriDbgUI* ui, struct ToriDbgPanel* p)
{
    struct ToriDbgTheme const* th = &ui->theme;
    int const title_h = p->title[0] ? ToriDbgFont_Menu_LINE_BOX : 0;
    struct ToriDbgRect clip;
    int content_w = 0;
    int content_h = 0;
    int row_y;
    int widget;

    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        int const w = dbg_widget_width(ui, &ui->widgets[widget]);
        if( w > content_w )
            content_w = w;
        content_h += dbg_widget_height(&ui->widgets[widget]) + DBG_ROW_GAP;
    }
    if( content_h > 0 )
        content_h -= DBG_ROW_GAP;
    if( p->title[0] )
    {
        int const tw = ToriDbgUI_MeasureText(TORIDBG_FONT_MENU, p->title);
        if( tw > content_w )
            content_w = tw;
    }
    if( content_w < DBG_MIN_CONTENT_W )
        content_w = DBG_MIN_CONTENT_W;

    p->w = p->fixed_w > 0 ? p->fixed_w : content_w + 2 * DBG_PAD_X + 2;
    p->h = title_h + content_h + 2 * DBG_PAD_Y + 2;
    if( content_h <= 0 )
        p->h = title_h + 2 * DBG_PAD_Y + 2;

    /* Body, then the border on top of it so the outline is never overdrawn. */
    clip.x = p->x;
    clip.y = p->y;
    clip.w = p->w;
    clip.h = p->h;
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->panel_body, 1, clip);
    if( title_h > 0 )
    {
        dbg_push_rect(ui, p->x + 1, p->y + 1, p->w - 2, title_h, th->panel_title_bg, 1, clip);
        dbg_push_text(
            ui,
            p->x + 1 + DBG_PAD_X,
            p->y + 1 + ToriDbgFont_Menu_LINE_HEIGHT,
            p->title,
            th->panel_title_text,
            TORIDBG_FONT_MENU,
            0,
            clip);
    }
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->panel_border, 0, clip);

    /* Rows are clipped to the content column, so an over-long label is cut at
     * the border instead of painting over it. */
    clip.x = p->x + 1 + DBG_PAD_X;
    clip.y = p->y + 1 + title_h;
    clip.w = p->w - 2 - 2 * DBG_PAD_X;
    clip.h = p->h - 2 - title_h;
    if( clip.w < 0 )
        clip.w = 0;
    if( clip.h < 0 )
        clip.h = 0;

    row_y = p->y + 1 + title_h + DBG_PAD_Y;
    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        struct ToriDbgWidget* w = &ui->widgets[widget];
        int const row_h = dbg_widget_height(w);
        int const row_x = p->x + 1 + DBG_PAD_X;
        int const hovered = ui->hover == widget;

        w->x = row_x;
        w->y = row_y;
        w->w = p->w - 2 - 2 * DBG_PAD_X;
        w->h = row_h;

        switch( w->kind )
        {
        case TORIDBG_W_LABEL:
            dbg_push_text(
                ui,
                row_x,
                row_y + ToriDbgFont_Small_LINE_HEIGHT,
                w->text,
                w->color ? w->color : th->text,
                TORIDBG_FONT_SMALL,
                0,
                clip);
            break;

        case TORIDBG_W_SEPARATOR:
            dbg_push_rect(ui, row_x, row_y + DBG_SEP_H / 2, w->w, 1, th->separator, 1, clip);
            break;

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
                    row_x + 2,
                    box_y + 2,
                    DBG_CHECK_SIZE - 4,
                    DBG_CHECK_SIZE - 4,
                    th->check_mark,
                    1,
                    clip);
            dbg_push_text(
                ui,
                row_x + DBG_CHECK_SIZE + DBG_CHECK_GAP,
                row_y + (row_h - ToriDbgFont_Small_LINE_BOX) / 2 + ToriDbgFont_Small_LINE_HEIGHT,
                w->label,
                hovered ? th->accent : (w->color ? w->color : th->text),
                TORIDBG_FONT_SMALL,
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
                row_y + ToriDbgFont_Small_LINE_HEIGHT,
                w->text,
                hovered ? th->accent : (w->color ? w->color : th->text),
                TORIDBG_FONT_SMALL,
                0,
                clip);
            break;

        case TORIDBG_W_DROPDOWN:
        {
            int const label_w = ToriDbgUI_MeasureText(TORIDBG_FONT_SMALL, w->label);
            int const box_x = row_x + label_w + (label_w > 0 ? DBG_CHECK_GAP : 0);
            int const box_w = row_x + w->w - box_x;
            int const open = ui->dropdown_open == widget;
            char const* shown = (w->selected >= 0 && w->selected < w->option_count)
                                    ? w->options[w->selected]
                                    : "";
            struct ToriDbgRect inner;
            int arrow_x;

            if( label_w > 0 )
                dbg_push_text(
                    ui,
                    row_x,
                    row_y + DBG_INPUT_PAD_Y + 1 + ToriDbgFont_Small_LINE_HEIGHT,
                    w->label,
                    w->color ? w->color : th->text_dim,
                    TORIDBG_FONT_SMALL,
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
                (open || hovered) ? th->input_border_focus : th->input_border,
                0,
                clip);

            /* The chosen option is clipped to the box rather than to the row,
             * so a long name is cut at the arrow instead of running under it. */
            inner = clip;
            if( inner.x < box_x + 1 )
                inner.x = box_x + 1;
            {
                int const right = box_x + box_w - 1 - DBG_DROP_ARROW_W;
                if( inner.x + inner.w > right )
                    inner.w = right - inner.x;
                if( inner.w < 0 )
                    inner.w = 0;
            }
            dbg_push_text(
                ui,
                box_x + DBG_INPUT_PAD_X + 1,
                row_y + DBG_INPUT_PAD_Y + 1 + ToriDbgFont_Small_LINE_HEIGHT,
                shown,
                th->input_text,
                TORIDBG_FONT_SMALL,
                0,
                inner);

            /* A three-step wedge rather than a glyph: the baked fonts carry no
             * arrow, and three rects cost less than teaching the font bake one. */
            arrow_x = box_x + box_w - DBG_DROP_ARROW_W;
            for( int step = 0; step < 3; step++ )
                dbg_push_rect(
                    ui,
                    arrow_x + step,
                    row_y + row_h / 2 - 1 + (open ? 2 - step : step),
                    DBG_DROP_ARROW_W - 2 * step - 2,
                    1,
                    (open || hovered) ? th->accent : th->text_dim,
                    1,
                    clip);
            break;
        }

        case TORIDBG_W_TEXTINPUT:
        {
            int const label_w = ToriDbgUI_MeasureText(TORIDBG_FONT_SMALL, w->label);
            int const box_x = row_x + label_w + (label_w > 0 ? DBG_CHECK_GAP : 0);
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
                    row_y + DBG_INPUT_PAD_Y + 1 + ToriDbgFont_Small_LINE_HEIGHT,
                    w->label,
                    w->color ? w->color : th->text_dim,
                    TORIDBG_FONT_SMALL,
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
            inner.x = box_x + 1 + DBG_INPUT_PAD_X;
            inner.y = row_y + 1;
            inner.w = box_w - 2 - 2 * DBG_INPUT_PAD_X;
            inner.h = row_h - 2;
            if( inner.w < 0 )
                inner.w = 0;
            caret_px = dbg_measure_prefix(TORIDBG_FONT_SMALL, w->text, w->caret);
            if( caret_px > inner.w )
                scroll = caret_px - inner.w;
            text_x = inner.x - scroll;

            dbg_push_text(
                ui,
                text_x,
                row_y + DBG_INPUT_PAD_Y + 1 + ToriDbgFont_Small_LINE_HEIGHT,
                w->text,
                th->input_text,
                TORIDBG_FONT_SMALL,
                0,
                inner);
            if( focused && ui->caret_visible )
                dbg_push_rect(
                    ui,
                    text_x + caret_px,
                    row_y + 2,
                    1,
                    row_h - 4,
                    th->input_text,
                    1,
                    inner);
            break;
        }

        default:
            break;
        }
        row_y += row_h + DBG_ROW_GAP;
    }
}

/*
 * A menu panel: the minimenu's chrome and geometry (see dbg_menu_layout),
 * rows top-to-bottom.
 */
static void
dbg_build_menu(struct ToriDbgUI* ui, struct ToriDbgPanel* p)
{
    struct ToriDbgTheme const* th = &ui->theme;
    struct DbgMenuLayout const l = dbg_menu_layout(ToriDbgFont_Menu_LINE_BOX);
    struct ToriDbgRect clip;
    int content_w = 0;
    int rows = 0;
    int widget;
    int row;

    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        int const w = ToriDbgUI_MeasureText(TORIDBG_FONT_MENU, ui->widgets[widget].text);
        if( w > content_w )
            content_w = w;
        rows++;
    }
    if( p->title[0] )
    {
        int const tw = ToriDbgUI_MeasureText(TORIDBG_FONT_MENU, p->title);
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
    dbg_push_rect(ui, p->x + 1, p->y + 1, p->w - 2, l.header_bar_h, th->menu_chrome, 1, clip);
    dbg_push_rect(ui, p->x + 1, p->y + l.separator_y, p->w - 2, 1, th->menu_chrome, 1, clip);
    dbg_push_rect(ui, p->x + 1, p->y + p->h - 2, p->w - 2, 1, th->menu_chrome, 1, clip);
    dbg_push_rect(
        ui, p->x + 1, p->y + l.separator_y, 1, p->h - l.border_inset, th->menu_chrome, 1, clip);
    dbg_push_rect(
        ui,
        p->x + p->w - 2,
        p->y + l.separator_y,
        1,
        p->h - l.border_inset,
        th->menu_chrome,
        1,
        clip);
    dbg_push_text(
        ui,
        p->x + 3,
        p->y + 2 + ToriDbgFont_Menu_LINE_HEIGHT,
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
            p->x + 3,
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
dbg_build_dropdown_list(struct ToriDbgUI* ui);
static struct ToriDbgRect
dbg_dropdown_rect(struct ToriDbgUI const* ui);

int
ToriDbgUI_Build(struct ToriDbgUI* ui)
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
dbg_dropdown_rect(struct ToriDbgUI const* ui)
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

    {
        int const label_w = ToriDbgUI_MeasureText(TORIDBG_FONT_SMALL, w->label);
        rect.x = w->x + label_w + (label_w > 0 ? DBG_CHECK_GAP : 0);
        rect.w = w->x + w->w - rect.x;
    }
    rect.y = w->y + w->h;
    rect.h = rows * DBG_DROP_ROW_H + 2;
    return rect;
}

static void
dbg_build_dropdown_list(struct ToriDbgUI* ui)
{
    struct ToriDbgTheme const* th = &ui->theme;
    struct ToriDbgWidget const* w;
    struct ToriDbgRect rect;
    struct ToriDbgRect clip;
    int rows;

    if( ui->dropdown_open < 0 )
        return;

    w = &ui->widgets[ui->dropdown_open];
    rect = dbg_dropdown_rect(ui);
    if( rect.w <= 0 || rect.h <= 0 )
        return;

    rows = w->option_count < TORIDBG_DROPDOWN_ROWS ? w->option_count : TORIDBG_DROPDOWN_ROWS;
    clip = rect;

    dbg_push_rect(ui, rect.x, rect.y, rect.w, rect.h, th->menu_body, 1, clip);
    dbg_push_rect(ui, rect.x, rect.y, rect.w, rect.h, th->menu_chrome, 0, clip);

    for( int row = 0; row < rows; row++ )
    {
        int const index = w->scroll + row;
        int const y = rect.y + 1 + row * DBG_DROP_ROW_H;
        int const chosen = index == w->selected;
        int const hovered = ui->dropdown_hover_row == row;

        if( index < 0 || index >= w->option_count )
            break;

        if( hovered )
            dbg_push_rect(ui, rect.x + 1, y, rect.w - 2, DBG_DROP_ROW_H, th->panel_title_bg, 1, clip);
        dbg_push_text(
            ui,
            rect.x + 1 + DBG_INPUT_PAD_X,
            y + ToriDbgFont_Small_LINE_HEIGHT,
            w->options[index],
            hovered ? th->menu_hover_text : (chosen ? th->accent : th->menu_text),
            TORIDBG_FONT_SMALL,
            0,
            clip);
    }

    /* A scroll thumb only when the list does not fit, sized to the fraction
     * shown -- otherwise a long palette gives no clue how far down it goes. */
    if( w->option_count > rows )
    {
        int const track_h = rect.h - 2;
        int thumb_h = track_h * rows / w->option_count;
        int thumb_y;

        if( thumb_h < 4 )
            thumb_h = 4;
        thumb_y = rect.y + 1;
        if( w->option_count > rows )
            thumb_y += (track_h - thumb_h) * w->scroll / (w->option_count - rows);
        dbg_push_rect(ui, rect.x + rect.w - 3, thumb_y, 2, thumb_h, th->accent, 1, clip);
    }
}

struct ToriDbgPrim const*
ToriDbgUI_Prims(struct ToriDbgUI const* ui, int* out_count)
{
    if( out_count )
        *out_count = ui ? ui->prim_count : 0;
    return ui ? ui->prims : NULL;
}

int
ToriDbgUI_Damage(struct ToriDbgUI const* ui, struct ToriDbgRect* out)
{
    assert(ui);
    if( ui->damage.w <= 0 || ui->damage.h <= 0 )
        return 0;
    if( out )
        *out = ui->damage;
    return 1;
}

void
ToriDbgUI_DamageClear(struct ToriDbgUI* ui)
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
dbg_panel_at(struct ToriDbgUI const* ui, int x, int y)
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

int
ToriDbgUI_HitTest(struct ToriDbgUI const* ui, int x, int y)
{
    int panel;

    assert(ui);
    panel = dbg_panel_at(ui, x, y);
    if( panel < 0 )
        return -1;
    for( int w = ui->panels[panel].first_widget; w >= 0; w = ui->widgets[w].next )
    {
        struct ToriDbgWidget const* wd = &ui->widgets[w];
        if( wd->kind == TORIDBG_W_SEPARATOR || wd->kind == TORIDBG_W_LABEL )
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
dbg_dropdown_row_at(struct ToriDbgUI const* ui, int x, int y)
{
    struct ToriDbgRect rect;
    struct ToriDbgWidget const* w;
    int row;
    int rows;

    if( ui->dropdown_open < 0 )
        return -1;
    rect = dbg_dropdown_rect(ui);
    if( rect.w <= 0 || !dbg_point_in(x, y, rect.x, rect.y, rect.w, rect.h) )
        return -1;

    w = &ui->widgets[ui->dropdown_open];
    rows = w->option_count < TORIDBG_DROPDOWN_ROWS ? w->option_count : TORIDBG_DROPDOWN_ROWS;
    row = (y - rect.y - 1) / DBG_DROP_ROW_H;
    if( row < 0 || row >= rows || w->scroll + row >= w->option_count )
        return -1;
    return row;
}

/** Close the open list, damaging the area it occupied. */
static void
dbg_dropdown_close(struct ToriDbgUI* ui)
{
    if( ui->dropdown_open < 0 )
        return;
    dbg_damage_add(ui, dbg_dropdown_rect(ui));
    dbg_dirty_widget(ui, ui->dropdown_open);
    ui->dropdown_open = -1;
    ui->dropdown_hover_row = -1;
    ui->dirty = 1;
}

int
ToriDbgUI_MouseMove(struct ToriDbgUI* ui, int x, int y)
{
    int hit;

    assert(ui);

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

    hit = ToriDbgUI_HitTest(ui, x, y);
    if( hit != ui->hover )
    {
        /* Both rows repaint: the one losing the highlight and the one gaining it. */
        dbg_dirty_widget(ui, ui->hover);
        ui->hover = hit;
        dbg_dirty_widget(ui, hit);
    }
    return dbg_panel_at(ui, x, y) >= 0;
}

int
ToriDbgUI_MouseDown(struct ToriDbgUI* ui, int x, int y)
{
    int hit;

    assert(ui);

    /* A press inside the open list belongs to the list; MouseUp turns it into
     * a selection. A press anywhere else closes it, which is what makes
     * clicking away dismiss rather than select. */
    if( ui->dropdown_open >= 0 )
    {
        if( dbg_dropdown_row_at(ui, x, y) >= 0 )
        {
            ui->press = -1;
            return 1;
        }
        dbg_dropdown_close(ui);
    }

    hit = ToriDbgUI_HitTest(ui, x, y);
    ui->press = hit;
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
ToriDbgUI_MouseUp(struct ToriDbgUI* ui, int x, int y)
{
    int hit;

    assert(ui);

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

    hit = ToriDbgUI_HitTest(ui, x, y);
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
        else if( w->kind == TORIDBG_W_MENUITEM )
        {
            ui->activated = hit;
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
ToriDbgUI_MouseWheel(struct ToriDbgUI* ui, int x, int y, int delta)
{
    struct ToriDbgWidget* w;
    int before;

    assert(ui);
    if( ui->dropdown_open < 0 )
        return 0;

    {
        struct ToriDbgRect const rect = dbg_dropdown_rect(ui);
        if( rect.w <= 0 || !dbg_point_in(x, y, rect.x, rect.y, rect.w, rect.h) )
            return 0;
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
ToriDbgUI_KeyChar(struct ToriDbgUI* ui, int ch)
{
    struct ToriDbgWidget* w;
    int len;

    assert(ui);
    if( ui->focus < 0 )
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
ToriDbgUI_KeyEdit(struct ToriDbgUI* ui, int key)
{
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
ToriDbgUI_TakeActivated(struct ToriDbgUI* ui)
{
    int const fired = ui ? ui->activated : -1;
    if( ui )
        ui->activated = -1;
    return fired;
}

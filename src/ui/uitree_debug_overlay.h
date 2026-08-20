#ifndef SRC_UITREE_DEBUG_OVERLAY_H
#define SRC_UITREE_DEBUG_OVERLAY_H

/*
 * ToriRSChrome — the developer debug overlay's widget model and display list.
 *
 * This is client chrome, not content: it has no counterpart in the server
 * tree, the same way uitree_minimenu.c and uitree_hovertext.c are chrome. It
 * carries no game-facing strings, ids or config-shaped constants — every
 * string is caller-supplied and every widget is addressed by a handle this
 * module hands back.
 *
 * RETAINED, not immediate. The overlay's cost is dominated by text
 * measurement and layout, and both change only when the app changes a widget
 * — not once per frame. So the model persists, mutators compare-then-set, and
 * ToriRSChrome_Build is a no-op on a frame where nothing moved. The display list
 * it produces is a flat array of ToriDbgPrim that the emit layer hands to the
 * renderer by pointer: on a clean frame the whole overlay costs one pointer
 * copy. An immediate-mode overlay would re-measure every label every frame,
 * which is exactly the work this avoids. See src/ui/README_DEBUG_OVERLAY.md.
 *
 * DAMAGE RECTANGLES, the XP-era half. Every mutation marks its panel dirty;
 * Build unions the panel's *old* and *new* bounds into ToriRSChrome_Damage. That
 * is the invalid region in the classic WM_PAINT sense — the smallest box that
 * has to be repainted for the frame to be correct. Callers that can present a
 * partial frame (or want to skip presenting at all) read it; callers that
 * always repaint the whole canvas can ignore it and still get the layout skip.
 *
 * NO DEPENDENCIES. The whole module is this header, one .c, and the generated
 * uitree_debug_font_metrics.h — which is `static const int` tables and nothing
 * else, no includes of its own. It measures its own text from those tables, so
 * it does not link a font implementation, a renderer, a scene, or the rest of
 * ui/. Outside the C library (<string.h>, <stdint.h>) it depends on nothing.
 * That is what baking the fonts bought: layout needs glyph advances, and
 * advances that are compiled in need no cache, no decoder and no init.
 *
 * No allocation either: struct ToriRSChrome is a fixed-size POD (54272 bytes on
 * the i686 lane — heap or static, not a stack local). Nothing here calls
 * malloc, so it is safe to bring up before any cache is open and cheap to tear
 * down.
 *
 * The one thing it does not do is rasterise. Primitives name a font *slot*,
 * not a font; whoever draws them maps the two slots onto the baked
 * struct ToriDraw_Font it registered (src/engine/torirs_debug_font_baked.h).
 * Both files come out of the same fontbake run over the same archives, so the
 * advances laid out here and the glyphs drawn there cannot disagree.
 */

#include <stdint.h>

/** Panels the overlay can hold at once. */
#define TORIDBG_MAX_PANELS 16
/** Widgets across all panels. */
#define TORIDBG_MAX_WIDGETS 128
/** Primitives in the display list. Build stops early and sets `overflow`. */
#define TORIDBG_MAX_PRIMS 512
/** Label / title bytes, including the terminator. */
#define TORIDBG_LABEL_MAX 64
/** Text-input content bytes, including the terminator. */
#define TORIDBG_INPUT_MAX 64

/**
 * Which baked font a primitive draws in. The overlay never names a font id:
 * whoever draws the display list maps these two slots onto whatever scene ids
 * it registered the baked fonts under (src/engine/torirs_debug_font_baked.h).
 */
enum ToriDbgFontSlot
{
    /** The small debug face: body text, labels, input contents. */
    TORIDBG_FONT_SMALL = 0,
    /** The menu face: panel titles and menu rows, matching the minimenu. */
    TORIDBG_FONT_MENU = 1,
    TORIDBG_FONT_SLOT_COUNT = 2
};

/** Pixel width of a NUL-terminated string in `font_slot`, from the baked
 *  advance tables. Plain bytes only — no markup tokens. */
int
ToriRSChrome_MeasureText(int font_slot, char const* text);

/** Baseline offset from the top of a line box (the font's ascent). */
int
ToriRSChrome_FontLineHeight(int font_slot);

/** Row pitch for `font_slot` (the tallest glyph's bottom edge). */
int
ToriRSChrome_FontLineBox(int font_slot);

enum ToriDbgPrimKind
{
    TORIDBG_PRIM_RECT = 0,
    TORIDBG_PRIM_TEXT,
};

struct ToriDbgRect
{
    int x;
    int y;
    int w;
    int h;
};

/**
 * One entry of the display list. Deliberately flat and POD: the emit layer
 * passes the array through by pointer and the render layer walks it one step
 * at a time, so nothing here may own memory. `text` points into the widget
 * that produced it, which lives as long as the ToriRSChrome does.
 */
struct ToriDbgPrim
{
    /** enum ToriDbgPrimKind. */
    int kind;
    int x;
    int y;
    int w;
    int h;
    /** 0xRRGGBB. */
    uint32_t color;
    /** RECT: 1 = filled, 0 = a 1px outline of the same box. */
    int filled;
    /** enum ToriDbgFontSlot. TEXT only. */
    int font_slot;
    /** TEXT: `y` is the baseline (reference PixFont.drawString), not a box top. */
    int baseline;
    /** TEXT: draw a 1px black drop shadow, as the minimenu rows do. */
    int shadowed;
    /** TEXT: NUL-terminated, owned by the widget that produced this prim. */
    char const* text;
    /** Scissor box. Panel content is clipped to the panel's inner rect. */
    struct ToriDbgRect clip;
};

/** Chrome colours. All 0xRRGGBB; see toridbg_theme_default. */
struct ToriDbgTheme
{
    uint32_t panel_body;
    uint32_t panel_border;
    uint32_t panel_title_bg;
    uint32_t panel_title_text;
    uint32_t text;
    uint32_t text_dim;
    uint32_t accent;
    uint32_t input_bg;
    uint32_t input_border;
    uint32_t input_border_focus;
    uint32_t input_text;
    uint32_t check_box;
    uint32_t check_mark;
    uint32_t menu_body;
    uint32_t menu_chrome;
    uint32_t menu_text;
    uint32_t menu_hover_text;
    uint32_t separator;
};

extern struct ToriDbgTheme const toridbg_theme_default;

enum ToriDbgPanelStyle
{
    /** Bordered background with a title bar. */
    TORIDBG_PANEL_WINDOW = 0,
    /**
     * The minimenu's chrome: body fill, black title bar, black separator and
     * side/bottom border strips, shadowed rows that go accent-coloured on
     * hover. Geometry comes from UIMinimenu_LayoutFromLineBox so the two
     * cannot drift apart. Rows read top-to-bottom (the reference minimenu
     * draws bottom-to-top because its option list is built in reverse; a
     * debug menu is authored in the order it is read).
     */
    TORIDBG_PANEL_MENU = 1,
};

enum ToriDbgWidgetKind
{
    TORIDBG_W_LABEL = 0,
    TORIDBG_W_CHECKBOX,
    TORIDBG_W_TEXTINPUT,
    TORIDBG_W_SEPARATOR,
    TORIDBG_W_MENUITEM,
    /**
     * A closed row showing the current choice; clicking it opens the shared
     * popup list. See `dropdown_open` on struct ToriRSChrome for why the list is
     * shared rather than per-widget.
     */
    TORIDBG_W_DROPDOWN,
};

/** Rows the open dropdown list shows at once; longer lists scroll. Chosen so a
 *  palette of several hundred entries stays inside TORIDBG_MAX_PRIMS. */
#define TORIDBG_DROPDOWN_ROWS 10

/** Editing keys ToriRSChrome_KeyEdit understands. Printable input goes through
 *  ToriRSChrome_KeyChar so ui/ never has to own a keymap. */
enum ToriDbgKey
{
    TORIDBG_KEY_NONE = 0,
    TORIDBG_KEY_BACKSPACE,
    TORIDBG_KEY_DELETE,
    TORIDBG_KEY_LEFT,
    TORIDBG_KEY_RIGHT,
    TORIDBG_KEY_HOME,
    TORIDBG_KEY_END,
    TORIDBG_KEY_ENTER,
    TORIDBG_KEY_ESCAPE,
};

struct ToriDbgWidget
{
    int kind;
    int panel;
    int next;
    /** 0 = use the theme colour for this widget kind. */
    uint32_t color;
    int checked;
    int caret;
    /** Resolved by Build; absolute screen pixels. */
    int x;
    int y;
    int w;
    int h;
    char label[TORIDBG_LABEL_MAX];
    char text[TORIDBG_INPUT_MAX];

    /**
     * DROPDOWN options. BORROWED, not copied — the array and the strings it
     * points at must outlive the widget.
     *
     * Borrowed rather than copied because the lists this exists for are
     * palettes: every underlay in the cache, every loc name in a search. Those
     * are hundreds of entries the caller already holds, and copying them into a
     * fixed-size POD would either cap the palette or make this module allocate,
     * and it deliberately never allocates.
     */
    char const* const* options;
    int option_count;
    /** Index into `options`, or -1 for "nothing chosen". */
    int selected;
    /** First visible row while the list is open. */
    int scroll;
};

struct ToriDbgPanel
{
    int style;
    int visible;
    int x;
    int y;
    /** 0 = size to content. */
    int fixed_w;
    /** Resolved by Build. */
    int w;
    int h;
    int dirty;
    /** Bounds as of the last Build, for the damage union. */
    struct ToriDbgRect last_rect;
    int first_widget;
    int last_widget;
    char title[TORIDBG_LABEL_MAX];
};

struct ToriRSChrome
{
    struct ToriDbgTheme theme;
    struct ToriDbgPanel panels[TORIDBG_MAX_PANELS];
    int panel_count;
    struct ToriDbgWidget widgets[TORIDBG_MAX_WIDGETS];
    int widget_count;
    struct ToriDbgPrim prims[TORIDBG_MAX_PRIMS];
    int prim_count;

    /** Any panel needs relayout. Build clears it. */
    int dirty;
    /** Caret phase for the focused text input; the app drives the blink. */
    int caret_visible;
    /** Widget handles, -1 for none. */
    int focus;
    int hover;
    int press;
    /** Latched by input, drained by ToriRSChrome_TakeActivated. -1 = none. */
    int activated;
    /**
     * The dropdown whose list is open, or -1.
     *
     * ONE list for the whole overlay, not one per dropdown — the same shape the
     * cache's own settings panel uses, where interface 134 owns a single list
     * component that every dropdown borrows and repositions rather than each
     * shipping its own. It costs nothing per widget, it makes "only one list
     * can be open" true by construction instead of by bookkeeping, and the open
     * list draws after every panel so it is never buried under one.
     */
    int dropdown_open;
    /** Row of the open list under the pointer, or -1. List-local, not a widget
     *  handle: the rows are not widgets, they are a view on `options`. */
    int dropdown_hover_row;
    /** Set by Build when a capacity limit truncated the display list. */
    int overflow;
    /** Union of what changed since the last DamageClear. w/h 0 = nothing. */
    struct ToriDbgRect damage;
};

/* ---- lifecycle ---------------------------------------------------------- */

/** Zero the model and install toridbg_theme_default. No allocation. */
void
ToriRSChrome_Init(struct ToriRSChrome* ui);

/** Drop every panel and widget. The theme survives; the vacated area is damaged. */
void
ToriRSChrome_Reset(struct ToriRSChrome* ui);

void
ToriRSChrome_SetTheme(struct ToriRSChrome* ui, struct ToriDbgTheme const* theme);

/* ---- building ----------------------------------------------------------- */

/**
 * Add a panel. @param fixed_w 0 sizes it to its widest row.
 * @return panel handle, or -1 when full.
 */
int
ToriRSChrome_PanelAdd(struct ToriRSChrome* ui, int style, int x, int y, int fixed_w, char const* title);

void
ToriRSChrome_PanelMove(struct ToriRSChrome* ui, int panel, int x, int y);

void
ToriRSChrome_PanelSetVisible(struct ToriRSChrome* ui, int panel, int visible);

/** Resolved bounds of a panel as of the last Build. 0 when unknown. */
struct ToriDbgRect
ToriRSChrome_PanelRect(struct ToriRSChrome const* ui, int panel);

/** @return widget handle, or -1 when full / panel invalid. */
int
ToriRSChrome_Label(struct ToriRSChrome* ui, int panel, char const* text);

/** As ToriRSChrome_Label with an explicit colour (0 = theme). */
int
ToriRSChrome_LabelColored(struct ToriRSChrome* ui, int panel, char const* text, uint32_t color);

int
ToriRSChrome_Checkbox(struct ToriRSChrome* ui, int panel, char const* label, int checked);

int
ToriRSChrome_TextInput(struct ToriRSChrome* ui, int panel, char const* label, char const* text);

/**
 * A dropdown over `options`, which is BORROWED and must outlive the widget.
 *
 * @param selected index into `options`, or -1 for none.
 * @return widget handle, or -1 when full / panel invalid.
 */
int
ToriRSChrome_Dropdown(
    struct ToriRSChrome* ui,
    int panel,
    char const* label,
    char const* const* options,
    int option_count,
    int selected);

/** Point a dropdown at a different list. Clamps the selection and closes the
 *  list if this widget's was open, since the rows under it just changed. */
void
ToriRSChrome_DropdownSetOptions(
    struct ToriRSChrome* ui,
    int widget,
    char const* const* options,
    int option_count,
    int selected);

/** Selected index, or -1. */
int
ToriRSChrome_DropdownSelected(struct ToriRSChrome const* ui, int widget);

void
ToriRSChrome_DropdownSetSelected(struct ToriRSChrome* ui, int widget, int selected);

int
ToriRSChrome_Separator(struct ToriRSChrome* ui, int panel);

int
ToriRSChrome_MenuItem(struct ToriRSChrome* ui, int panel, char const* text);

/* ---- mutation (compare-then-set; a no-op change does not dirty) ---------- */

void
ToriRSChrome_SetText(struct ToriRSChrome* ui, int widget, char const* text);

void
ToriRSChrome_SetLabel(struct ToriRSChrome* ui, int widget, char const* label);

void
ToriRSChrome_SetColor(struct ToriRSChrome* ui, int widget, uint32_t color);

void
ToriRSChrome_SetChecked(struct ToriRSChrome* ui, int widget, int checked);

int
ToriRSChrome_Checked(struct ToriRSChrome const* ui, int widget);

/** Live text of a TEXTINPUT / LABEL / MENUITEM. Never NULL. */
char const*
ToriRSChrome_Text(struct ToriRSChrome const* ui, int widget);

/** Show/hide the caret of the focused input. App-driven so ui/ owns no clock. */
void
ToriRSChrome_SetCaretVisible(struct ToriRSChrome* ui, int visible);

/* ---- input -------------------------------------------------------------- */

/** @return widget handle under (x,y), or -1. Only hits visible panels. */
int
ToriRSChrome_HitTest(struct ToriRSChrome const* ui, int x, int y);

/** @return 1 when the overlay consumed the event (pointer was over a panel). */
int
ToriRSChrome_MouseMove(struct ToriRSChrome* ui, int x, int y);

int
ToriRSChrome_MouseDown(struct ToriRSChrome* ui, int x, int y);

/** Fires checkbox toggles and menu activations. @see ToriRSChrome_TakeActivated. */
int
ToriRSChrome_MouseUp(struct ToriRSChrome* ui, int x, int y);

/**
 * Scroll the open dropdown list. @param delta rows, negative = up.
 * @return 1 when the overlay consumed it (a list was open under the pointer),
 * so the caller can leave the camera zoom alone.
 */
int
ToriRSChrome_MouseWheel(struct ToriRSChrome* ui, int x, int y, int delta);

/** Insert a printable byte into the focused input. @return 1 if consumed. */
int
ToriRSChrome_KeyChar(struct ToriRSChrome* ui, int ch);

/** @param key enum ToriDbgKey. @return 1 if consumed. */
int
ToriRSChrome_KeyEdit(struct ToriRSChrome* ui, int key);

/**
 * Handle of the widget activated since the last call (menu item clicked,
 * checkbox toggled, input committed with Enter), then clears it. -1 = none.
 */
int
ToriRSChrome_TakeActivated(struct ToriRSChrome* ui);

/* ---- display list ------------------------------------------------------- */

/**
 * Relayout every visible panel and rebuild the display list, but only if
 * something changed. @return 1 when it rebuilt, 0 when the cached list stood.
 */
int
ToriRSChrome_Build(struct ToriRSChrome* ui);

/** The display list. Valid until the next Build. */
struct ToriDbgPrim const*
ToriRSChrome_Prims(struct ToriRSChrome const* ui, int* out_count);

/** @return 1 and writes the invalid region when there is one. */
int
ToriRSChrome_Damage(struct ToriRSChrome const* ui, struct ToriDbgRect* out);

void
ToriRSChrome_DamageClear(struct ToriRSChrome* ui);

#endif

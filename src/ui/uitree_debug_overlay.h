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
    /**
     * The game's plain body face (p12), the bold menu face's twin.
     *
     * Same line box as MENU, so swapping a row between the two reflows
     * nothing -- which is the point: it lets a skin choose plain-vs-bold per
     * row without the layout moving underneath it.
     */
    TORIDBG_FONT_BODY = 2,
    TORIDBG_FONT_SLOT_COUNT = 3
};

/**
 * Integer chrome zoom. 1 is the baked fonts' native size; higher scales
 * multiply every metric and glyph, so the layout reflows rather than blurs.
 *
 * The upper bound is what the BAKE carries, not a taste: fontbake wrote
 * Small/Body/Menu at @2 and @3 (src/engine/torirs_debug_font_baked.h), and a
 * scale with no font behind it would lay rows out at a size nothing can draw.
 * Raising it means baking that size first, in the one fontbake run that writes
 * both generated files.
 *
 * Integer only, and that is the renderer's property rather than a shortcut:
 * the glyph blitter tests a mask byte for non-zero instead of blending it
 * (toridraw_font.c), so there is no half-covered pixel for a 1.5x glyph to
 * resample onto. A display at 1.5x picks the nearer baked size; nothing is
 * ever stretched, which is the whole point of authoring the sizes.
 */
#define TORIDBG_SCALE_MIN 1
#define TORIDBG_SCALE_MAX 3

/** Pixel width of a NUL-terminated string in `font_slot` at `scale`, from the
 *  baked advance tables. Plain bytes only — no markup tokens. */
int
ToriRSChrome_MeasureText(int font_slot, int scale, char const* text);

/** Baseline offset from the top of a line box (the font's ascent). */
int
ToriRSChrome_FontLineHeight(int font_slot, int scale);

/** Row pitch for `font_slot` (the tallest glyph's bottom edge). */
int
ToriRSChrome_FontLineBox(int font_slot, int scale);

enum ToriDbgPrimKind
{
    TORIDBG_PRIM_RECT = 0,
    TORIDBG_PRIM_TEXT,
    /**
     * One blit of a baked skin image, into `w` x `h` or at its native size
     * when those are 0.
     *
     * Tiling is still a loop up here rather than a repeat mode down in the
     * renderer -- a parchment is a handful of native-size copies, and the clip
     * rect bounds the count. The destination box exists for the other case:
     * chrome authored at 1x and drawn at 2x or 3x, and the scrollbar grip,
     * whose middle piece is one 16x5 image stretched over the run between its
     * two caps exactly as ~script31 stretches it.
     */
    TORIDBG_PRIM_SPRITE,
};

/**
 * Which baked image a sprite primitive draws.
 *
 * Semantic slots, not archive ids: the chrome names *what the image is for*
 * and whoever draws the display list maps that onto whatever it uploaded, the
 * same indirection the font slots already use. That is what lets the skin be
 * re-baked from a different cache, or be absent entirely, without this module
 * knowing.
 */
enum ToriDbgSkinSlot
{
    /** Tiled behind a window panel's content. */
    TORIDBG_SKIN_PANEL_BODY = 0,
    /**
     * The six pieces of the reference scrollbar, in the order ~script31 builds
     * them: two 16x16 arrow buttons, a track, and a three-part grip whose
     * middle stretches between two fixed caps.
     *
     * Six sprites and not one nine-slice because that is how the cache ships
     * it -- the bar the game draws beside every CS2 list is exactly these
     * images at exactly these sizes, so a bar assembled from them is the same
     * bar rather than a drawing of one.
     */
    TORIDBG_SKIN_SCROLL_UP,
    TORIDBG_SKIN_SCROLL_DOWN,
    TORIDBG_SKIN_SCROLL_TRACK,
    TORIDBG_SKIN_SCROLL_GRIP_TOP,
    TORIDBG_SKIN_SCROLL_GRIP_MID,
    TORIDBG_SKIN_SCROLL_GRIP_BOTTOM,
    /** Tiled behind an open dropdown list. A *different* tile from the panel
     *  body -- the cache backs the floating list with its own, lighter one. */
    TORIDBG_SKIN_DROPDOWN_BODY,
    TORIDBG_SKIN_SLOT_COUNT
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
    /** enum ToriDbgSkinSlot. SPRITE only. */
    int sprite_slot;
    /** SPRITE: a scene sprite id to draw INSTEAD of the skin slot -- the
     *  model-view widget's rendered preview. 0 = use the skin mapping. */
    int sprite_scene_id;
    /**
     * RECT/SPRITE: the client's transparency, 0 opaque .. 255 invisible.
     *
     * The reference's sense, not an alpha, because the values here are lifted
     * from the scripts that draw the real widget -- the dropdown's row bands
     * are `cc_settrans(220)` and `cc_settrans(200)` -- and a field that reads
     * the other way round would have every one of them written backwards.
     */
    int trans;
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

    /* ---- dropdowns, as the cache's own CS2 dropdown draws them -----------
     *
     * Every value below is read off script_3850 (the closed button) and
     * script_9114 (the open list), the two scripts that build every dropdown
     * on the settings page. They are theme keys rather than constants so the
     * flat developer look can stay flat, but the osrs theme is a transcript:
     * a tiled body under a black outline and a grey inset, an arrow sprite on
     * the LEFT, orange shadowed text, and rows that are black veils at
     * alternating transparency with a lighter veil under the cursor.
     */

    /** Outer 1px frame of the closed button (`cc_setcolour(0x0e0e0c)`). */
    uint32_t dropdown_border;
    /** The 1px inset inside it (`cc_setcolour(0x474745)`), which is what makes
     *  the button read as sunk rather than as a plain outlined box. */
    uint32_t dropdown_border_inner;
    /** Button and row text (`0xff981f`). */
    uint32_t dropdown_text;
    /** The colour every veil is drawn in: the row bands, and the one the
     *  closed button wears while hovered. Black in the reference. */
    uint32_t dropdown_veil;
    /**
     * Transparency of the two alternating row bands, and of a hovered row.
     *
     * 220 / 200 / 240 in the reference -- note the hover value is the LARGEST,
     * so the row under the cursor is the one wearing the *thinnest* veil. The
     * highlight is the background showing through, not a colour laid on top.
     */
    int dropdown_band_trans;
    int dropdown_band_trans_alt;
    int dropdown_row_trans_hover;
    /** Veil over the closed button while the cursor is on it (220). */
    int dropdown_hover_trans;

    /* ---- scrollbars -----------------------------------------------------
     *
     * The four colours the client fills an IF1 scrollbar with -- the same
     * values as UITREE_SCROLLBAR_TRACK/GRIP/GRIP_HI/GRIP_LO_ARGB, which this
     * module cannot include (it has no dependencies; see the header note).
     * They are the bar's *flat* form: with the baked skin present the bar is
     * assembled from the CS2 sprites instead and these are not read at all.
     * Both themes carry the client's values, because a scrollbar is chrome
     * rather than palette -- the flat look is a different panel, not a
     * different scrollbar.
     */
    uint32_t scroll_track;
    uint32_t scroll_grip;
    uint32_t scroll_grip_hi;
    uint32_t scroll_grip_lo;

    /**
     * Draw every string with the 1px black drop shadow, not just the menu rows
     * that ask for it per-call.
     *
     * A theme-level flag rather than an edit to each dbg_push_text call site,
     * because "does this skin shadow its text" is a property of the skin: the
     * reference interface shadows all of it, the flat debug look shadows none
     * of it, and a call site that hard-codes 1 could not express either. The
     * per-call argument still wins where it is already 1 (the minimenu rows),
     * so turning this on never *removes* a shadow.
     */
    int text_shadowed;

    /**
     * enum ToriDbgFontSlot: the face window-panel rows lay out and draw in.
     *
     * Layout follows it, not just colour -- row height and every measured
     * width come from this slot -- so a skin that picks the game's p12 body
     * face gets game-sized rows rather than debug-sized rows with game-sized
     * glyphs spilling out of them. Panel titles are not affected; those are
     * always the bold menu face, which is what the reference titles with.
     */
    int font_row;

    /**
     * Draw window-panel bodies as the tiled skin image instead of a flat fill.
     *
     * Off is not a degraded mode, it is the flat look -- and it is also the
     * automatic fallback: whoever draws the list reports which skin slots it
     * actually has, and a panel whose body image never uploaded falls back to
     * `panel_body` on its own. A build with the baked skin module stubbed out
     * therefore still renders, which is the property that lets the skin be
     * optional rather than load-bearing.
     */
    int skin_panel_body;

    /**
     * Draw dropdowns and their scrollbars from the baked skin rather than as
     * flat boxes.
     *
     * Separate from skin_panel_body because the two fail independently: a bake
     * carrying the panel parchment and no scrollbar pieces must still draw a
     * usable bar. As with skin_panel_body, off is the flat look and also the
     * automatic fallback -- each draw checks the slot it is about to use.
     */
    int skin_dropdown;
};

/** The flat developer look: grey chrome, no shadows, no sprites. */
extern struct ToriDbgTheme const toridbg_theme_default;

/**
 * The reference interface palette.
 *
 * Every value here is a colour the game itself uses -- the minimenu's body
 * brown, its black chrome strips, its yellow hover and the brown-on-black it
 * titles with -- so a panel drawn in this theme and a real game widget on
 * screen together read as one system rather than two. Paired
 * with a baked skin (struct ToriDbgSkin) it gets the sprite art as well; on its
 * own it is already the right colours in the right places, which is most of
 * what makes chrome look like it belongs.
 */
extern struct ToriDbgTheme const toridbg_theme_osrs;

enum ToriDbgPanelStyle
{
    /** Bordered background with a title bar. */
    TORIDBG_PANEL_WINDOW = 0,
    /**
     * A horizontal strip of menu titles across the top of the screen -- the
     * File/Edit bar. Its widgets lay out left to right in one row instead of
     * stacking, and the intended child is a menu-mode dropdown
     * (ToriRSChrome_MenuDrop): the title is the whole closed state, and the
     * option list opens beneath it as the menu.
     */
    TORIDBG_PANEL_MENUBAR = 2,
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
    /**
     * A rendered model preview: draws one scene sprite the HOST rendered and
     * registered (a model rasterised through the same pipeline as inventory
     * icons). The chrome stays a non-rasteriser -- it draws a handle, and
     * which pixels that handle holds is the host's business.
     */
    TORIDBG_W_MODELVIEW,
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
    /** MODELVIEW: the box size and the host-rendered scene sprite (0 none). */
    int view_w;
    int view_h;
    int view_scene_id;
    /**
     * Dropdown-as-menu: the closed state is just the label, opening clears
     * `selected` so choosing the same row twice still reads as a fresh choice,
     * and the caller treats each activation as a command rather than a value.
     */
    int menu_mode;
    /** Skipped by layout, drawing and hit testing. For panels whose row set
     *  depends on state -- the tool panel shows only the active tool's
     *  inputs -- so the alternative to this flag is rebuilding the panel's
     *  widgets every switch, which invalidates every held handle. */
    int hidden;
    /** First visible row while the list is open. */
    int scroll;
};

struct ToriDbgPanel
{
    int style;
    int visible;
    /**
     * Draw a resize grip in the bottom-right corner and let it be dragged.
     *
     * Both axes. The origin stays put and the dragged corner follows the
     * cursor, so a resize never moves the panel.
     *
     * A hand-set height overrides content sizing, and there is no scrolling
     * here: rows that fall past the bottom are DROPPED -- not drawn, and not
     * clickable either (dbg_build_window zeroes their hit boxes, so a panel
     * cannot take clicks on rows nobody can see). Shrinking a panel therefore
     * hides controls until it is grown back, which is the honest behaviour
     * available without a scroll offset, and the thing to revisit if these
     * panels ever get one.
     */
    int resizable;
    /**
     * Table layout: every labelled control in this panel starts its box at one
     * shared column, sized to the panel's widest label. Off, each row's box
     * starts right after its own label, which reads as a ragged pile the
     * moment two labels differ in width -- "Tool" and "LocRot" gave every
     * dropdown its own left edge.
     */
    int table;
    /** Resolved by Build: the shared label column width, 0 when not a table
     *  or no row carries a label. */
    int label_col;
    int x;
    int y;
    /** 0 = size to content. A grip drag writes the width it lands on here. */
    int fixed_w;
    /** 0 = size to content. A grip drag writes the height it lands on here. */
    int fixed_h;
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
    /** Integer chrome zoom, TORIDBG_SCALE_MIN..MAX. Every layout metric and
     *  glyph multiplies by it; Init sets 1. */
    int scale;
    struct ToriDbgPanel panels[TORIDBG_MAX_PANELS];
    int panel_count;
    struct ToriDbgWidget widgets[TORIDBG_MAX_WIDGETS];
    int widget_count;
    struct ToriDbgPrim prims[TORIDBG_MAX_PRIMS];
    int prim_count;

    /**
     * Bit per enum ToriDbgSkinSlot the drawer actually has an image for.
     *
     * The host sets this after it uploads the baked skin; 0 (the Init default)
     * means "no skin", and every skinned draw falls back to its flat form. So
     * the fallback is the state the module starts in rather than an error path
     * that has to be remembered -- a build with no skin module, or a host that
     * never uploads one, renders the flat chrome without anything special.
     */
    uint32_t skin_avail;
    /** Native size of the PANEL_BODY image, so the tiling loop can step by it.
     *  Set alongside skin_avail; ignored when that bit is clear. */
    int skin_tile_w;
    int skin_tile_h;

    /** Any panel needs relayout. Build clears it. */
    int dirty;
    /** Caret phase for the focused text input; the app drives the blink. */
    int caret_visible;
    /**
     * Panel being dragged by its header, or -1.
     *
     * Held as a panel handle rather than a pointer because the panel array is
     * the stable thing here -- and the grab offset is stored, not recomputed,
     * so the panel keeps the exact point it was picked up by instead of
     * snapping its corner to the cursor on the first move.
     */
    int drag_panel;
    int drag_grab_x;
    int drag_grab_y;

    /**
     * Panel being resized by its grip, or -1.
     *
     * The two grabs are offsets from the CORNER being dragged, not from the
     * origin -- held, not recomputed, for the same reason the drag holds its
     * offset: recomputing snaps the corner to the cursor on the first move.
     */
    int resize_panel;
    int resize_grab_x;
    int resize_grab_y;

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
    /**
     * Non-zero while the open list's scrollbar grip is being dragged.
     *
     * Held with the grab offset -- the pixels from the top of the grip to the
     * point it was picked up by -- for the same reason the panel drag holds
     * one: recomputing it every move snaps the grip's top to the cursor, which
     * reads as the list jumping the moment you touch the bar.
     */
    int dropdown_scroll_drag;
    int dropdown_scroll_grab;
    /** Set by Build when a capacity limit truncated the display list. */
    int overflow;
    /** Union of what changed since the last DamageClear. w/h 0 = nothing. */
    struct ToriDbgRect damage;
};

/* ---- lifecycle ---------------------------------------------------------- */

/** Zero the model, install the default theme and set scale 1. No allocation. */
void
ToriRSChrome_Init(struct ToriRSChrome* ui);

/**
 * Set the device pixels per chrome pixel: TORIDBG_SCALE_MIN..TORIDBG_SCALE_MAX.
 *
 * A relayout, not a transform: every panel is remeasured against the fonts
 * baked at that size, so rows grow with the text in them instead of text
 * overflowing boxes sized for a smaller face. Panel ORIGINS are left alone --
 * whoever placed a panel knows whether it meant a device pixel or a chrome
 * one, and this module does not.
 *
 * The host must map the font slots onto the same scale's baked fonts or the
 * layout and the glyphs disagree; UITreeSceneBridge_EnsureDebugFont takes the
 * scale for exactly that reason.
 */
void
ToriRSChrome_SetScale(struct ToriRSChrome* ui, int scale);

/** Device pixels per chrome pixel. */
int
ToriRSChrome_Scale(struct ToriRSChrome const* ui);

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

/**
 * Give a window panel a resize grip in its bottom-right corner.
 *
 * Window panels only -- a menu panel is a popup sized to its rows, with
 * nothing a hand-set size would mean. @see ToriDbgPanel::resizable for what
 * happens to rows a shrunk panel no longer has room for.
 */
void
ToriRSChrome_PanelSetResizable(struct ToriRSChrome* ui, int panel, int resizable);

/**
 * Set a panel's hand-picked width, or 0 to go back to sizing from content.
 *
 * The counterpart to the `fixed_w` PanelAdd takes: a width authored in chrome
 * pixels stops being right the moment the scale changes, and a panel that
 * cannot be re-widened keeps a 1x column around a 2x row -- which draws as
 * text cut off mid-word, the exact symptom of a UI that was scaled halfway.
 */
void
ToriRSChrome_PanelSetFixedWidth(struct ToriRSChrome* ui, int panel, int width);

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

/**
 * A menu title for a MENUBAR panel: a dropdown in menu mode.
 *
 * Activation fires when a row of the opened list is chosen;
 * ToriRSChrome_DropdownSelected then says which row, and the caller runs the
 * command it maps to. The selection is not a value -- it is cleared on every
 * open -- which is the difference between a menu and a picker.
 */
int
ToriRSChrome_MenuDrop(
    struct ToriRSChrome* ui,
    int panel,
    char const* title,
    char const* const* options,
    int option_count);

/** Show or hide one widget. Hidden widgets take no space, draw nothing and hit
 *  nothing; the handle stays valid throughout. */
void
ToriRSChrome_SetHidden(struct ToriRSChrome* ui, int widget, int hidden);

/** Turn table layout on or off for a panel; see ToriDbgPanel::table. */
void
ToriRSChrome_PanelSetTable(struct ToriRSChrome* ui, int panel, int table);

/** A model-view box of w x h chrome pixels. Draws nothing until the host
 *  hands it a rendered sprite via ToriRSChrome_ModelViewSet. */
int
ToriRSChrome_ModelView(struct ToriRSChrome* ui, int panel, int w, int h);

/** Point a model view at a scene sprite id the host registered (0 = clear).
 *  The widget draws the sprite at its native size, centred in the box. */
void
ToriRSChrome_ModelViewSet(struct ToriRSChrome* ui, int widget, int scene_sprite_id);

/**
 * Would a wheel event at (x, y) belong to the chrome?
 *
 * True over any visible panel and over the open dropdown popup (which extends
 * beyond its panel). Const and side-effect free, so the CALLER's wheel gate
 * can ask it: the camera-zoom path runs long after the chrome handled input
 * this frame, and this probe is what keeps a wheel over a panel from also
 * zooming the world behind it -- the chrome may consume the event and do
 * nothing with it, and "consumed into nothing" over a panel is correct where
 * "fell through to the camera" is not.
 */
int
ToriRSChrome_WantsWheel(struct ToriRSChrome const* ui, int x, int y);

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

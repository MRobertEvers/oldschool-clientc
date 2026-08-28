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
 * it produces is a flat array of ToriRSChromePrim that the emit layer hands to the
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
 * The colour picker keeps that true the same way the fonts do: the HSL16
 * palette is arithmetic over a baked gamma ramp rather than a call into the
 * rasteriser's table, so a swatch is drawable before anything has initialised
 * a renderer. See ToriRSChrome_Hsl16ToRgb.
 * That is what baking the fonts bought: layout needs glyph advances, and
 * advances that are compiled in need no cache, no decoder and no init.
 *
 * No allocation either: struct ToriRSChrome is a fixed-size POD — a couple of
 * hundred KB at the capacities above, so heap or static, never a stack local.
 * Nothing here calls malloc, so it is safe to bring up before any cache is open
 * and cheap to tear down.
 *
 * The one thing it does not do is rasterise. Primitives name a font *slot*,
 * not a font; whoever draws them maps the two slots onto the baked
 * struct ToriDraw_Font it registered (src/engine/torirs_debug_font_baked.h).
 * Both files come out of the same fontbake run over the same archives, so the
 * advances laid out here and the glyphs drawn there cannot disagree.
 */

#include <stdint.h>

/*
 * Capacities.
 *
 * Sized for the widest consumer rather than the average one: a plugin window
 * hosting a tab per plugin, each with its own settings rows, is a different
 * order of magnitude from the four-row developer readout this started as. They
 * are per-INSTANCE and the instance is a POD, so the cost is one fixed block
 * per chrome rather than growth over a session -- which is the property worth
 * keeping, not the smallness of any particular number.
 *
 * Scrolling is what lets MAX_PRIMS stay far below MAX_WIDGETS: a panel only
 * ever emits the rows inside its own view, so a 40-row tab costs the same
 * display list as the 12 rows of it you can see.
 */
/** Panels the overlay can hold at once. */
#define TORIRS_CHROME_MAX_PANELS 24
/** Widgets across all panels. */
#define TORIRS_CHROME_MAX_WIDGETS 384
/** Primitives in the display list. Build stops early and sets `overflow`. */
#define TORIRS_CHROME_MAX_PRIMS 1536
/**
 * Bytes of wrapped-line scratch one build may hand out. @see
 * ToriRSChrome::wrap_pool.
 *
 * A multiline field costs at most its whole value plus one terminator per
 * visible line -- about 208 bytes at TORIRS_CHROME_INPUT_MAX -- and only for
 * the lines actually on screen, so this is room for ten of them at once on one
 * page. A settings page with more than that draws the first ten and raises
 * `overflow`, which is the same answer running out of prims gives.
 */
#define TORIRS_CHROME_WRAP_POOL 2048
/** Label / title bytes, including the terminator. */
#define TORIRS_CHROME_LABEL_MAX 64
/**
 * Text-input content bytes, including the terminator.
 *
 * 192, which is TORIRS_PLUGIN_CONFIG_VALUE_MAX -- the store these fields edit.
 * It was 64, and a plugin key longer than 63 bytes was silently truncated on
 * its way INTO the panel, so opening the settings page and pressing Save
 * shortened the value. A multiline field makes that unmissable rather than
 * merely wrong: the ground-items highlight list the reference ships is a
 * comma-separated run of item names, and 63 bytes is about four of them.
 *
 * The cost is one flat block per widget slot, in the model and in the sync's
 * shadow -- the same trade every other fixed-size field here makes.
 */
#define TORIRS_CHROME_INPUT_MAX 192

/**
 * Which baked font a primitive draws in. The overlay never names a font id:
 * whoever draws the display list maps these two slots onto whatever scene ids
 * it registered the baked fonts under (src/engine/torirs_debug_font_baked.h).
 */
enum ToriRSChromeFontSlot
{
    /** The small debug face: body text, labels, input contents. */
    TORIRS_CHROME_FONT_SMALL = 0,
    /** The menu face: panel titles and menu rows, matching the minimenu. */
    TORIRS_CHROME_FONT_MENU = 1,
    /**
     * The game's plain body face (p12), the bold menu face's twin.
     *
     * Same line box as MENU, so swapping a row between the two reflows
     * nothing -- which is the point: it lets a skin choose plain-vs-bold per
     * row without the layout moving underneath it.
     */
    TORIRS_CHROME_FONT_BODY = 2,
    TORIRS_CHROME_FONT_SLOT_COUNT = 3
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
#define TORIRS_CHROME_SCALE_MIN 1
#define TORIRS_CHROME_SCALE_MAX 3

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

enum ToriRSChromePrimKind
{
    TORIRS_CHROME_PRIM_RECT = 0,
    TORIRS_CHROME_PRIM_TEXT,
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
    TORIRS_CHROME_PRIM_SPRITE,
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
enum ToriRSChromeSkinSlot
{
    /** Tiled behind a window panel's content. */
    TORIRS_CHROME_SKIN_PANEL_BODY = 0,
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
    TORIRS_CHROME_SKIN_SCROLL_UP,
    TORIRS_CHROME_SKIN_SCROLL_DOWN,
    TORIRS_CHROME_SKIN_SCROLL_TRACK,
    TORIRS_CHROME_SKIN_SCROLL_GRIP_TOP,
    TORIRS_CHROME_SKIN_SCROLL_GRIP_MID,
    TORIRS_CHROME_SKIN_SCROLL_GRIP_BOTTOM,
    /** Tiled behind an open dropdown list. A *different* tile from the panel
     *  body -- the cache backs the floating list with its own, lighter one. */
    TORIRS_CHROME_SKIN_DROPDOWN_BODY,
    /**
     * The wrench that launches the plugin window from the gameframe's strip.
     *
     * Not chrome the model draws -- nothing here emits it -- but baked
     * alongside the rest because it is the same kind of thing and wants the
     * same guarantee: the launcher must not depend on a cache archive id.
     * Sprite 785 is the OSRS wrench; on any other cache that id is some
     * unrelated image, so resolving it at runtime gives the wrong picture
     * rather than no picture.
     */
    TORIRS_CHROME_SKIN_PLUGIN_ICON,
    /**
     * The on/off pair the interfaces use for every boolean setting: a green
     * tick in a circle, a red cross in a circle, both 17x17.
     *
     * A checkbox is a SPRITE in this game, not a drawn box -- open any
     * settings panel and there is no square with a mark in it anywhere. The
     * flat box-and-blob is still here as the fallback for a build with no
     * skin, but where the skin is present these are what a checkbox is.
     */
    TORIRS_CHROME_SKIN_CHECK_ON,
    TORIRS_CHROME_SKIN_CHECK_OFF,
    /**
     * The interfaces' own panel frame, as an EIGHT-piece nine-slice: four 32x32
     * corners carrying an L of rail along their outer edges, and four 6px edges
     * stretched along their runs.
     *
     * This is the border the gameframe's popout strip (interface 728) draws
     * around the panels that mount in it -- literally the same sprites, read
     * off the live strip rather than guessed at. An earlier bake took the thin
     * black nine-slice sitting beside them in the cache (5814-5822); those
     * components are `hidden=1` on every frame the strip draws, which is
     * exactly the kind of thing a screenshot cannot tell you and a component
     * dump can.
     *
     * The corners are ROUNDED -- their outer pixel is transparent -- so the
     * pieces are blitted at their baked size rather than stretched through one
     * box, and the order below is the order dbg_push_frame draws them in.
     *
     * NO CENTRE. The cache authors one and the strip never shows it: the
     * panel's own tradebacking is already under the frame, and painting a flat
     * brown over the parchment would be the frame erasing the surface it
     * frames. A slot for an image nothing draws is a slot every consumer has to
     * carry, so it is not baked at all.
     *
     * @see TORIRS_CHROME_M_FRAME / _M_FRAME_CORNER for the two thicknesses, and
     * why an edge is baked cropped out of its 32x32 tile.
     */
    TORIRS_CHROME_SKIN_FRAME_TOP_LEFT,
    TORIRS_CHROME_SKIN_FRAME_TOP,
    TORIRS_CHROME_SKIN_FRAME_TOP_RIGHT,
    TORIRS_CHROME_SKIN_FRAME_LEFT,
    TORIRS_CHROME_SKIN_FRAME_RIGHT,
    TORIRS_CHROME_SKIN_FRAME_BOTTOM_LEFT,
    TORIRS_CHROME_SKIN_FRAME_BOTTOM,
    TORIRS_CHROME_SKIN_FRAME_BOTTOM_RIGHT,
    /**
     * The interfaces' own window X, and the same button with its bevel
     * inverted -- which is what this game's buttons do under the cursor.
     *
     * A CLOSE BUTTON IS NOT A DISMISS MARK. The red cross this replaces
     * (`CHECK_OFF`) is the game's *no* answer -- the other half of the tick, as
     * it appears against every boolean setting -- and using it to shut a window
     * read as "reject" rather than "close". Sprites 831/832 are the button the
     * interfaces actually put in a title bar.
     *
     * The pair is one image twice, lit from opposite corners: the glyph pixels
     * are identical and only the bevel flips, so the resting button reads as
     * raised and the hovered one as pressed in. That is the whole hover effect
     * and it is why the accent outline every other control wears is *not* drawn
     * over this one -- the art already says it.
     *
     * 16x16, matching the scrollbar arrows rather than the 24x24 pair beside
     * them in the cache (799/800, the same button drawn larger). The title
     * bar's button box is 14 at 1x chrome scale, and the bake has no scale
     * variants: the larger art would land there as a 0.58x downscale, which is
     * the speckling this chrome already avoids everywhere else.
     */
    TORIRS_CHROME_SKIN_CLOSE,
    TORIRS_CHROME_SKIN_CLOSE_OVER,
    /**
     * Pop out into a window of its own, and put it back -- each with the same
     * hover half the close button has.
     *
     * SYNTHESIZED, and the only art here that is. There is no pop-out button
     * anywhere in the cache: the game has no window that leaves its frame, so
     * there is no archive to name. What the bake does instead is borrow the
     * close button's PLATE -- its frame, bevel, face, ink and hover treatment,
     * all of it the cache's -- and stamp an arrow where the X was
     * (spritebake's `--stamp`). Only the eight by eight pixels in the middle
     * are ours, so a re-bake against another revision carries these along with
     * the real buttons rather than leaving them at last year's palette.
     *
     * A text glyph beside them was the alternative, and what shipped first: an
     * arrow from the system font, at the system font's weight, next to a
     * button drawn in 2005. The two read as furniture from two programs.
     *
     * Only the web presentation puts a window in a tab, so only it draws
     * these. They are in the shared enum anyway because the bake is shared --
     * a slot the page can ask for has to exist on the client that answers.
     */
    TORIRS_CHROME_SKIN_POPOUT,
    TORIRS_CHROME_SKIN_POPOUT_OVER,
    TORIRS_CHROME_SKIN_DOCK,
    TORIRS_CHROME_SKIN_DOCK_OVER,
    /**
     * The OTHER boolean the interfaces draw: a bordered 18x18 well with a
     * green tick in it, and the same well empty.
     *
     * Archives 2847/2848, and the second answer to "what is a checkbox in this
     * game". The tick/cross pair above is the settings page's; this one is the
     * bordered box a quest journal, a make-x list or a slayer task filter puts
     * beside a row, and it is what most people picture when they hear
     * checkbox. Which of the two a chrome wears is a choice rather than a
     * fallback -- @see enum ToriRSChromeCheckStyle -- so BOTH are baked and
     * nothing here decides.
     *
     * 18x18 and not 17x17, which is the whole reason the box size is a
     * function of the style rather than one constant: the art is drawn at its
     * baked size or it speckles. @see TORIRS_CHROME_M_BOX_SQUARE.
     *
     * OFF IS AN EMPTY WELL, not a red cross -- the pair is one control in two
     * states rather than two answers, which is exactly the difference between
     * this style and the other one.
     */
    TORIRS_CHROME_SKIN_CHECK_BOX_ON,
    TORIRS_CHROME_SKIN_CHECK_BOX_OFF,
    /*
     * The interfaces' own wide stone BUTTON, in three pieces: two 36x36 caps
     * with the rounded corners on their outer edges, and a 20x36 tile that
     * repeats to fill whatever is between them. It is the button the logout
     * interface's "Logout" and "World Switcher" rows are drawn with.
     *
     * Baked here rather than named by cache id, because it is drawn by
     * CHROME -- the client's own furniture, which has to look the same on a
     * cache that does not contain it, on a cache that failed to open, and on
     * the frames of 2004 that have no such art at all. That is the same reason
     * every other slot above is baked, and the reason a `[component:]` can now
     * ask for one by `sprite=chrome:<slot>`.
     */
    TORIRS_CHROME_SKIN_BUTTON_LEFT,
    TORIRS_CHROME_SKIN_BUTTON_MID,
    TORIRS_CHROME_SKIN_BUTTON_RIGHT,
    TORIRS_CHROME_SKIN_SLOT_COUNT
};

/**
 * Which of the interfaces' two booleans a checkbox wears.
 *
 * Both are the cache's own art, so this is a preference and not a quality
 * ladder: TICK is the settings page's green tick / red cross, BOX is the
 * bordered well with a tick in it that the journals and filter lists use.
 * Default TICK, because that is what every panel in this chrome was built
 * against and a default that silently redraws every existing panel is not a
 * default.
 *
 * It rides on the MODEL rather than on the theme, beside `scale`: a theme is
 * a palette that a host swaps wholesale (TORIRS_CHROME_THEME=flat), and a
 * user's choice of checkbox has no business being reset by one.
 */
enum ToriRSChromeCheckStyle
{
    /** 8380/8379: a green tick, and a red cross for off. */
    TORIRS_CHROME_CHECK_STYLE_TICK = 0,
    /** 2848/2847: a tick in a bordered well, and the empty well for off. */
    TORIRS_CHROME_CHECK_STYLE_BOX = 1,
};

/**
 * The skin slot one style's on/off state is drawn from.
 *
 * Inline and shared rather than a ternary at each draw, because there are five
 * presentations of this chrome and the pairing is the one thing all of them
 * have to agree on: a checkbox showing the tick while its neighbour in another
 * window shows the well is the failure the executor seam exists to prevent.
 *
 * An unknown style draws the TICK pair. That is not a contract violation being
 * swallowed: the value crosses a command stream from a client that may be
 * newer than the executor reading it, and an executor that met a style it does
 * not know is entitled to draw the one it does.
 */
static inline int
ToriRSChrome_CheckSlot(int style, int on)
{
    if( style == TORIRS_CHROME_CHECK_STYLE_BOX )
        return on ? TORIRS_CHROME_SKIN_CHECK_BOX_ON : TORIRS_CHROME_SKIN_CHECK_BOX_OFF;
    return on ? TORIRS_CHROME_SKIN_CHECK_ON : TORIRS_CHROME_SKIN_CHECK_OFF;
}

struct ToriRSChromeRect
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
struct ToriRSChromePrim
{
    /** enum ToriRSChromePrimKind. */
    int kind;
    int x;
    int y;
    int w;
    int h;
    /** 0xRRGGBB. */
    uint32_t color;
    /** RECT: 1 = filled, 0 = a 1px outline of the same box. */
    int filled;
    /** enum ToriRSChromeFontSlot. TEXT only. */
    int font_slot;
    /** TEXT: `y` is the baseline (reference PixFont.drawString), not a box top. */
    int baseline;
    /** TEXT: draw a 1px black drop shadow, as the minimenu rows do. */
    int shadowed;
    /** TEXT: NUL-terminated, owned by the widget that produced this prim. */
    char const* text;
    /** enum ToriRSChromeSkinSlot. SPRITE only. */
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
    struct ToriRSChromeRect clip;
};

/** Chrome colours. All 0xRRGGBB; see torirs_chrome_theme_default. */
struct ToriRSChromeTheme
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
    /**
     * Body fill of a MULTILINE field (TORIRS_CHROME_W_TEXTAREA).
     *
     * A key of its own rather than `input_bg`, because the reference gives the
     * two different colours and the reason is visual rather than arbitrary: a
     * one-line field is mostly full and reads fine at black, and a four-line
     * box mostly is not -- at `input_bg` it reads as a hole cut in the panel.
     * `~script7210` fills it 0x372e22, a shade off the body brown.
     */
    uint32_t textarea_bg;
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
     * enum ToriRSChromeFontSlot: the face window-panel rows lay out and draw in.
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
extern struct ToriRSChromeTheme const torirs_chrome_theme_default;

/**
 * The reference interface palette.
 *
 * Every value here is a colour the game itself uses -- the minimenu's body
 * brown, its black chrome strips, its yellow hover and the brown-on-black it
 * titles with -- so a panel drawn in this theme and a real game widget on
 * screen together read as one system rather than two. Paired
 * with a baked skin (struct ToriRSChromeSkin) it gets the sprite art as well; on its
 * own it is already the right colours in the right places, which is most of
 * what makes chrome look like it belongs.
 */
extern struct ToriRSChromeTheme const torirs_chrome_theme_osrs;

enum ToriRSChromePanelStyle
{
    /** Bordered background with a title bar. */
    TORIRS_CHROME_PANEL_WINDOW = 0,
    /**
     * A horizontal strip of menu titles across the top of the screen -- the
     * File/Edit bar. Its widgets lay out left to right in one row instead of
     * stacking, and the intended child is a menu-mode dropdown
     * (ToriRSChrome_MenuDrop): the title is the whole closed state, and the
     * option list opens beneath it as the menu.
     */
    TORIRS_CHROME_PANEL_MENUBAR = 2,
    /**
     * The minimenu's chrome: body fill, black title bar, black separator and
     * side/bottom border strips, shadowed rows that go accent-coloured on
     * hover. Geometry comes from UIMinimenu_LayoutFromLineBox so the two
     * cannot drift apart. Rows read top-to-bottom (the reference minimenu
     * draws bottom-to-top because its option list is built in reverse; a
     * debug menu is authored in the order it is read).
     */
    TORIRS_CHROME_PANEL_MENU = 1,
};

enum ToriRSChromeWidgetKind
{
    TORIRS_CHROME_W_LABEL = 0,
    TORIRS_CHROME_W_CHECKBOX,
    TORIRS_CHROME_W_TEXTINPUT,
    TORIRS_CHROME_W_SEPARATOR,
    TORIRS_CHROME_W_MENUITEM,
    /**
     * A closed row showing the current choice; clicking it opens the shared
     * popup list. See `dropdown_open` on struct ToriRSChrome for why the list is
     * shared rather than per-widget.
     */
    TORIRS_CHROME_W_DROPDOWN,
    /**
     * A rendered model preview: draws one scene sprite the HOST rendered and
     * registered (a model rasterised through the same pipeline as inventory
     * icons). The chrome stays a non-rasteriser -- it draws a handle, and
     * which pixels that handle holds is the host's business.
     */
    TORIRS_CHROME_W_MODELVIEW,
    /**
     * A pressable box with a centred caption. Activates on release inside it,
     * like every other clickable row.
     *
     * Distinct from MENUITEM, which is a line of text that happens to be
     * clickable: a button LOOKS pressable when nothing else on the row says so.
     * A settings panel needs that -- "Save" as a bare text row beside a column
     * of labels reads as another label, and the one control that commits the
     * user's edits is the last one that should have to be discovered.
     */
    TORIRS_CHROME_W_BUTTON,
    /**
     * A strip of tab titles across one row; the selected one names which of the
     * panel's other widgets lay out at all (see ToriRSChromeWidget::tab).
     *
     * ONE widget holding N titles rather than one widget per tab, because the
     * layout engine stacks rows vertically: N sibling widgets would be N rows,
     * and a horizontal strip would need a second layout mode to exist. Titles
     * are BORROWED exactly as a dropdown's options are, and for the same
     * reason -- the caller already holds them.
     */
    TORIRS_CHROME_W_TABSTRIP,
    /**
     * A roster row: a name, an optional settings affordance, and a switch --
     * the shape RuneLite's plugin list uses, and the reason this is a kind of
     * its own rather than a checkbox with extras.
     *
     * A CHECKBOX is one control: box, label, one hit zone. This is a row of
     * THREE zones with two different outcomes -- flip the switch, or open this
     * entry's own page -- and a list of them is a NAVIGATION surface rather
     * than a form. Tabs were the alternative and they do not scale: one tab
     * per plugin fits four titles across a panel and then starts compressing
     * captions to initials, where a list scrolls to any length.
     *
     * The action zone reports through `ToriRSChrome_ActivationWasAction`
     * beside the ordinary activation latch, so a host drains both from the one
     * loop it already has.
     */
    TORIRS_CHROME_W_LISTROW,
    /**
     * An HSL16 colour field: a swatch, an editable "#RRGGBB", and a popup of
     * three bars -- hue, saturation, lightness -- that are the game's own
     * colour axes rather than a rendering of RGB.
     *
     * A MODEL FACE IS NOT RGB. It is a 6-bit hue / 3-bit saturation / 7-bit
     * lightness index into the revision's palette (ToriRSChrome_Hsl16ToRgb),
     * and every colour a plugin hands the engine is quantised onto one of
     * those 32768 entries whether it asked to be or not. A picker that offered
     * 24-bit RGB would therefore be lying about its own precision: two hexes
     * the user can tell apart in the field land on one palette entry on
     * screen. Picking ON the axes makes every reachable value a value the
     * renderer can actually produce, and makes the quantisation visible where
     * it happens instead of at the far end of the pipeline.
     *
     * The value lives in `selected` (packed HSL16) and `text` mirrors it as
     * "#RRGGBB" -- editable, because a hex out of a wiki or another client is
     * how a colour usually arrives, and because the config store this feeds is
     * textual. `checked` says whether the popup is open, which is what lets a
     * NATIVE-WIDGET executor draw its own bars without a command of its own.
     */
    TORIRS_CHROME_W_COLORPICK,
    /**
     * A multiline text field: a header line, then a box `rows` lines tall that
     * the value word-wraps down.
     *
     * NOT a TEXTINPUT that happens to be taller. The two differ in what they
     * are FOR, and every difference below follows from it:
     *
     *   - The caption sits ABOVE the box, full width, instead of in the
     *     104px label column. That column is what makes a page of one-line
     *     settings line up; against a four-line list it is a third of the
     *     width taken from the one control that needs it.
     *   - Enter INSERTS a newline instead of committing, and Home/End address
     *     the LINE rather than the value -- which is what makes it editable at
     *     all once there is more than one line to be on.
     *   - The value wraps. A single-line field scrolls its content sideways
     *     under a fixed caret; this one reflows, so where the caret is depends
     *     on the box's width and has to be recomputed from it.
     *
     * The shape is the cache's own. `~script7213` (interface 650, the
     * ground-items settings page) builds the highlight and filter lists as a
     * 0x372e22 rect under a type-12 input with the settings frame around it;
     * see the note in torirs_chrome_metrics.h for the transcript. Both of
     * those hold a comma-separated list of item names, which is exactly the
     * kind of value a one-line field cannot show enough of to edit.
     */
    TORIRS_CHROME_W_TEXTAREA,
    /**
     * A removed widget's slot, waiting on the free list.
     *
     * A kind rather than a flag so a stale handle is inert everywhere at once:
     * layout, drawing and hit testing all switch on kind already, and
     * dbg_valid_widget rejects it, so a caller that kept a handle across a
     * ToriRSChrome_PanelClearWidgets addresses nothing instead of addressing
     * whatever was recycled into the slot.
     */
    TORIRS_CHROME_W_FREE,
};

/** Rows the open dropdown list shows at once; longer lists scroll. Chosen so a
 *  palette of several hundred entries stays inside TORIRS_CHROME_MAX_PRIMS. */
#define TORIRS_CHROME_DROPDOWN_ROWS 10

/** Editing keys ToriRSChrome_KeyEdit understands. Printable input goes through
 *  ToriRSChrome_KeyChar so ui/ never has to own a keymap. */
enum ToriRSChromeKey
{
    TORIRS_CHROME_KEY_NONE = 0,
    TORIRS_CHROME_KEY_BACKSPACE,
    TORIRS_CHROME_KEY_DELETE,
    TORIRS_CHROME_KEY_LEFT,
    TORIRS_CHROME_KEY_RIGHT,
    TORIRS_CHROME_KEY_HOME,
    TORIRS_CHROME_KEY_END,
    TORIRS_CHROME_KEY_ENTER,
    TORIRS_CHROME_KEY_ESCAPE,
    /**
     * Up and down a line. Meaningful only in a TEXTAREA -- a one-line field
     * has nowhere to go -- but reported unconditionally by the platform, so
     * the two kinds do not need separate key routing.
     *
     * Appended rather than slotted beside LEFT/RIGHT: enum ToriRSChromeAuxKey's
     * twin in platform/platform_sdl2.h is pinned to this one value for value
     * (see the _Static_asserts in torirs_chrome_exec_sdl.c), and inserting in
     * the middle would renumber every key the platform already reports.
     */
    TORIRS_CHROME_KEY_UP,
    TORIRS_CHROME_KEY_DOWN,
};

struct ToriRSChromeWidget
{
    int kind;
    int panel;
    int next;
    /** 0 = use the theme colour for this widget kind. */
    uint32_t color;
    int checked;
    int caret;
    /** LISTROW: draw and hit-test the settings affordance. A row without one
     *  is a name and a switch, and its whole width toggles. */
    int row_action;
    /** LISTROW: this row has no switch at all -- the thing it names has one
     *  state. The toggle column is not drawn and not reserved, so the name
     *  runs out to where the switch would have been, and every part of the row
     *  opens it. @see ToriRSChrome_ListRowLocked. */
    int row_locked;
    /** TEXTAREA: visible lines of the box, before it scrolls. Part of the
     *  widget's SHAPE, so it rides the ADD command and never changes after. */
    int rows;
    /** Resolved by Build; absolute screen pixels. */
    int x;
    int y;
    int w;
    int h;
    char label[TORIRS_CHROME_LABEL_MAX];
    char text[TORIRS_CHROME_INPUT_MAX];

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
    /** Index into `options`, or -1 for "nothing chosen". COLORPICK: the packed
     *  HSL16 value instead -- a colour IS a selection out of the revision's
     *  palette, so it rides the field the seam already diffs and announces
     *  (WIDGET_SELECTED / INTENT_PICK) rather than adding a second one. */
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
    /**
     * Which tab of the owning panel this row belongs to, or -1 for "every tab".
     *
     * Separate from `hidden` rather than folded into it because the two answer
     * to different owners: `hidden` is the CALLER's decision about one widget,
     * `tab` is the panel's decision about a whole group. A row that is on an
     * inactive tab AND hidden by its owner must come back hidden when its tab
     * is selected again, which a single flag cannot express.
     *
     * -1 (the "every tab" case) is also what a panel with no tab strip gives
     * every row, so the untabbed panel needs no special case anywhere.
     */
    int tab;
    /**
     * Monotonic id, distinct for every widget this instance has ever created.
     *
     * A HANDLE is not an identity: the free list recycles them, so handle 5
     * removed and handle 5 added are two different widgets wearing one number.
     * Anything downstream that diffs by handle -- the executor seam's shadow --
     * then sees "widget 5, still a checkbox, still on panel 0" and concludes
     * nothing changed, when in fact the row it names was replaced. The visible
     * symptom is a rebuilt panel whose rows come out in the order they were
     * FIRST created rather than the order they are in now.
     *
     * Never reused, never reset by a panel clear: it only ever has to be
     * comparable and distinct.
     */
    int serial;
    /** First visible row while the list is open. */
    int scroll;
};

struct ToriRSChromePanel
{
    int style;
    int visible;
    /**
     * The panel carries its own Close button in its title bar.
     *
     * OPT-IN, and that is the point. Most panels here are developer tools
     * toggled by a hotkey, and a close box on one is chrome that duplicates a
     * key nobody has trouble finding. The plugin window is the opposite case:
     * it is the one panel a PLAYER uses, it is reached by a sidebar button
     * rather than a key, and in the CS2 presentation -- where it is a column
     * of game components with no window furniture of its own -- there was no
     * way to shut it at all. It opened and stayed open.
     *
     * ONE button, and it does one thing. There was an Ok beside it that
     * committed the page's Save row on the way out; it is gone, along with the
     * `confirm_widget` that told it what to fire. A window's title bar is where
     * a user looks for the way OUT, not for a second copy of a control the page
     * already carries -- and a page that stages edits carries Save and Revert
     * as rows, where they are labelled and can be found.
     */
    int closable;
    /**
     * Draw a resize grip in the bottom-right corner and let it be dragged.
     *
     * Both axes. The origin stays put and the dragged corner follows the
     * cursor, so a resize never moves the panel.
     *
     * A hand-set height overrides content sizing. Rows that fall past the
     * bottom are DROPPED unless the panel is scrollable -- not drawn, and not
     * clickable either (dbg_build_window zeroes their hit boxes, so a panel
     * cannot take clicks on rows nobody can see). @see ToriRSChromePanel::scrollable
     * for the other answer.
     */
    int resizable;
    /**
     * Wear the interfaces' nine-slice border instead of the minimenu's rails.
     *
     * The frame the gameframe's popout strip draws around the panels mounted
     * in it (TORIRS_CHROME_SKIN_FRAME_TOP_LEFT and its eight siblings). A
     * framed panel keeps its title bar -- inset by the frame -- and drops the
     * bottom rule and the two side rails, because those ARE the border it now
     * has one of.
     *
     * Opt-in rather than a property of the window style, and both of the
     * reasons are live in this tree at once: a floating developer panel wants
     * the minimenu's rails so it reads as a menu beside a real minimenu, and a
     * panel mounted inside something that already draws a frame (the strip)
     * must not draw a second one inside the first.
     *
     * Ignored on a build whose skin carries no frame -- see
     * dbg_panel_is_framed, which is what keeps the LAYOUT honest too: a panel
     * that reserved three pixels for a border it cannot draw would sit its
     * content in from an edge that is not there.
     */
    int framed;
    /**
     * The panel IS its surface: origin 0,0, exactly the surface's size.
     *
     * For a presentation that owns a WINDOW OF ITS OWN there is nothing behind
     * the panel for it to float over, so a box parked at (8,72) leaves three
     * bands of empty background around it and reads as a window inside a
     * window -- and one that never grows when the OS window is dragged wider.
     * @see ToriRSChromeSync_FillSurface, which is what sets this.
     *
     * Filling takes the drag and the grip away with it. Both write geometry
     * that the next fill overwrites, so leaving them is an affordance that
     * lies; the OS window's own frame is what moves and resizes this now.
     */
    int filled;
    /**
     * Rows past the bottom scroll into view instead of being dropped.
     *
     * The panel grows a bar down its right edge whenever its content is taller
     * than its view, and the wheel over the panel moves it. Off is still the
     * default and still drops: a menu popup sized to its own rows has no
     * overflow to scroll, and a bar on it would be chrome that can never do
     * anything.
     *
     * Only meaningful with a hand-set or dragged height -- a content-sized
     * panel is exactly as tall as its rows, so there is never anything below
     * the fold to reach.
     */
    int scrollable;
    /** Rows scrolled past, in pixels. Clamped by Build to the content that
     *  actually overflows, so shrinking a panel can never strand it. */
    int scroll_y;
    /**
     * Resolved by Build: where the scroll window starts on screen, the total
     * height of the rows inside it, and the height of the window onto them.
     *
     * Held rather than recomputed because the bar's draw, its hit test and its
     * drag all need the same three numbers, and deriving them separately from
     * the header/footer/strip metrics is how a grip ends up landing somewhere
     * different depending on who asked.
     */
    int content_y;
    int content_h;
    int view_h;
    /**
     * Which tab of this panel's strip is showing. 0 until something selects
     * another, which is also the right answer for a panel with no strip.
     */
    int active_tab;
    /**
     * Tab that ToriRSChrome_PanelBeginTab stamps onto every widget added next,
     * or -1 for "every tab".
     *
     * Builder state rather than an argument on all nine widget constructors:
     * a tab's contents are added as a run, and threading the same constant
     * through every call in that run is where the one typo puts a row on the
     * wrong tab. Reset by PanelClearWidgets, since a cleared panel is about to
     * be rebuilt from the top.
     */
    int build_tab;
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
    struct ToriRSChromeRect last_rect;
    int first_widget;
    int last_widget;
    char title[TORIRS_CHROME_LABEL_MAX];
};

struct ToriRSChrome
{
    struct ToriRSChromeTheme theme;
    /** Integer chrome zoom, TORIRS_CHROME_SCALE_MIN..MAX. Every layout metric and
     *  glyph multiplies by it; Init sets 1. */
    int scale;
    /**
     * enum ToriRSChromeCheckStyle: which boolean art every checkbox and roster
     * switch in this instance wears. Init sets TICK.
     *
     * A LAYOUT input and not only a palette one -- the two arts are 17 and 18
     * wide -- so changing it dirties every panel, the same way SetScale does.
     */
    int check_style;
    struct ToriRSChromePanel panels[TORIRS_CHROME_MAX_PANELS];
    int panel_count;
    struct ToriRSChromeWidget widgets[TORIRS_CHROME_MAX_WIDGETS];
    /** High-water mark of the widget array, NOT the number of live widgets:
     *  removed slots below it are on the free list. */
    int widget_count;
    /** Next ToriRSChromeWidget::serial to hand out. */
    int next_serial;
    /**
     * Head of the removed-slot list, chained through ToriRSChromeWidget::next, or -1.
     *
     * A free list rather than compaction because handles are the caller's
     * addresses: compacting would renumber every widget above the hole and
     * silently repoint every handle anyone still holds. Recycling a slot can
     * only ever surprise a caller that kept a handle to something it removed,
     * and TORIRS_CHROME_W_FREE plus dbg_valid_widget catch that case until the slot
     * is actually reused.
     */
    int free_widget;
    struct ToriRSChromePrim prims[TORIRS_CHROME_MAX_PRIMS];
    int prim_count;

    /**
     * Where a multiline field's wrapped lines live for the life of a build.
     *
     * A TEXT prim BORROWS its string (dbg_push_text stores the pointer), and a
     * wrapped line is not a string that exists anywhere: it is a SLICE of the
     * widget's value with no terminator of its own, and the byte after it
     * belongs to the next line. So each visible line is copied here,
     * NUL-terminated, and the prim points at the copy.
     *
     * A bump arena reset by Build rather than a buffer per widget, because
     * only the lines actually ON SCREEN are ever copied -- a 40-line list in a
     * four-line box costs four lines of scratch, the same way scrolling keeps
     * TORIRS_CHROME_MAX_PRIMS far below MAX_WIDGETS. Exhausting it stops the
     * copying and raises `overflow`, exactly as running out of prims does.
     */
    char wrap_pool[TORIRS_CHROME_WRAP_POOL];
    int wrap_used;

    /**
     * Bit per enum ToriRSChromeSkinSlot the drawer actually has an image for.
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
    /**
     * Last pointer position seen by MouseMove.
     *
     * Kept because `hover` names a WIDGET, and a tab strip is one widget made
     * of several targets: "which tab is under the cursor" cannot be answered
     * from a handle. Build reads it rather than taking a position argument, so
     * the hover highlight survives a rebuild triggered by anything else.
     */
    int hover_x;
    int hover_y;
    /**
     * The panel whose Close button is under the cursor, or -1.
     *
     * Held rather than derived at draw time, because it is what a repaint has
     * to be triggered BY. The button is not a widget -- it belongs to the
     * panel's chrome, so `hover` never names it -- and a build that reads the
     * pointer position but is never told the pointer moved simply does not run:
     * the pressed art would appear only on a frame something else had already
     * dirtied.
     */
    int hover_close_panel;
    /** Latched by input, drained by ToriRSChrome_TakeActivated. -1 = none. */
    int activated;
    /** Set beside `activated` when a LISTROW's ACTION zone was what fired it,
     *  rather than its switch. */
    int activated_action;
    /** `activated_action` as it stood at the last TakeActivated, so a host can
     *  ask which kind of activation it just drained -- the drain clears the
     *  live flag, and asking after the drain is the only order a caller has. */
    int taken_action;
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
    /**
     * The COLORPICK whose bar popup is open, or -1.
     *
     * Its own latch and not `dropdown_open`, even though only one popup can be
     * up at a time and sharing one would enforce that for free: the two popups
     * are different shapes with different hit tests, and a single latch would
     * make every `dropdown_open >= 0` site -- the scroll drag, the row hover,
     * the wheel, the dismiss-on-press -- have to ask which kind it was holding
     * first. Opening either closes the other, which is the part that actually
     * had to be true.
     */
    int colorpick_open;
    /**
     * Which bar of the open picker the pointer is dragging, or -1.
     *
     * A colour is picked by SWEEPING, not by clicking a cell: the whole point
     * of laying the three axes out as bars is that the preview follows the
     * pointer, and a press-move-release that only reported its endpoints would
     * make the bars a row of 128 tiny buttons instead. @see enum ToriRSChromeColorBar.
     */
    int colorpick_drag_bar;
    /**
     * Panel whose own scrollbar grip is being dragged, or -1.
     *
     * Its own state and not the dropdown's, because the two bars can be live in
     * the same instance -- a dropdown inside a scrolled panel -- and sharing one
     * drag latch would have a grip release cancel the other bar's drag. The grab
     * offset is held for the reason every other drag here holds one: recomputing
     * it snaps the grip's top to the cursor on the first move.
     */
    int scroll_panel;
    int scroll_grab;
    /**
     * Incremented every time Build actually rebuilt the display list.
     *
     * A host holding the prim array by pointer -- which is the whole retained
     * contract -- has no other way to ask "is what I copied last still what
     * this says". The prim COUNT is not that answer: an edit that replaces one
     * string with another of the same shape leaves the count identical and the
     * contents different, which is exactly the case a count-compare misses and
     * a serial does not.
     */
    int build_serial;
    /** Set by Build when a capacity limit truncated the display list. */
    int overflow;
    /** Union of what changed since the last DamageClear. w/h 0 = nothing. */
    struct ToriRSChromeRect damage;
};

/* ---- lifecycle ---------------------------------------------------------- */

/** Zero the model, install the default theme and set scale 1. No allocation. */
void
ToriRSChrome_Init(struct ToriRSChrome* ui);

/**
 * Set the device pixels per chrome pixel: TORIRS_CHROME_SCALE_MIN..TORIRS_CHROME_SCALE_MAX.
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

/**
 * Choose which of the interfaces' two booleans every checkbox wears.
 *
 * A relayout, like SetScale and for the same reason: the two arts are
 * different sizes, so the row a checkbox sits in is a different width
 * afterwards. Every panel is dirtied and the whole chrome damaged.
 *
 * @param style enum ToriRSChromeCheckStyle. An unknown value is kept as it
 *        arrived and drawn as TICK -- see ToriRSChrome_CheckSlot for why the
 *        drawers are lenient about a style they have not heard of.
 */
void
ToriRSChrome_SetCheckStyle(struct ToriRSChrome* ui, int style);

/** enum ToriRSChromeCheckStyle, as SetCheckStyle left it. */
int
ToriRSChrome_CheckStyle(struct ToriRSChrome const* ui);

/** Edge of the box a checkbox reserves for its art, at 1x -- the size of the
 *  sprite this style wears. @see TORIRS_CHROME_M_BOX_SQUARE. */
int
ToriRSChrome_CheckBoxMetric(int style);

/** Drop every panel and widget. The theme survives; the vacated area is damaged. */
void
ToriRSChrome_Reset(struct ToriRSChrome* ui);

void
ToriRSChrome_SetTheme(struct ToriRSChrome* ui, struct ToriRSChromeTheme const* theme);

/* ---- building ----------------------------------------------------------- */

/**
 * Add a panel. @param fixed_w 0 sizes it to its widest row.
 * @return panel handle, or -1 when full.
 */
int
ToriRSChrome_PanelAdd(
    struct ToriRSChrome* ui,
    int style,
    int x,
    int y,
    int fixed_w,
    char const* title);

void
ToriRSChrome_PanelMove(struct ToriRSChrome* ui, int panel, int x, int y);

void
ToriRSChrome_PanelSetVisible(struct ToriRSChrome* ui, int panel, int visible);

/**
 * Give a window panel a resize grip in its bottom-right corner.
 *
 * Window panels only -- a menu panel is a popup sized to its rows, with
 * nothing a hand-set size would mean. @see ToriRSChromePanel::resizable for what
 * happens to rows a shrunk panel no longer has room for.
 */
void
ToriRSChrome_PanelSetResizable(struct ToriRSChrome* ui, int panel, int resizable);

/**
 * Give a panel the interfaces' nine-slice border.
 *
 * @see ToriRSChromePanel::framed for what it replaces and why it is asked for
 * rather than implied by the panel's style. A build with no frame in its baked
 * skin keeps the rails, layout included.
 */
void
ToriRSChrome_PanelSetFramed(struct ToriRSChrome* ui, int panel, int framed);

/**
 * Give a panel a Close button in its title bar: the interfaces' own window X.
 *
 * Closing DISCARDS, which is what closing has always meant here -- staged rows
 * live in the chrome and nothing writes them but their own Save. There is no
 * commit-on-the-way-out beside it: a page that stages edits carries Save and
 * Revert as labelled rows, and a second, unlabelled copy of Save in the title
 * bar is a control a user has to already know about to use.
 */
void
ToriRSChrome_PanelSetClosable(struct ToriRSChrome* ui, int panel, int closable);

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

/**
 * Stretch a panel over a whole surface: origin 0,0, exactly `w` x `h`.
 *
 * @see ToriRSChromePanel::filled for what it costs the panel, and
 * ToriRSChromeSync_FillSurface for the caller that knows the size -- a host
 * asks the executor how big its window is rather than deciding here, because
 * ui/ has no idea what a window is.
 *
 * Idempotent, which is what lets it run every frame: a repeat of the size the
 * panel already fills marks nothing dirty and rebuilds nothing.
 */
void
ToriRSChrome_PanelFill(struct ToriRSChrome* ui, int panel, int w, int h);

/** Resolved bounds of a panel as of the last Build. 0 when unknown. */
struct ToriRSChromeRect
ToriRSChrome_PanelRect(struct ToriRSChrome const* ui, int panel);

/** @return widget handle, or -1 when full / panel invalid. */
int
ToriRSChrome_Label(struct ToriRSChrome* ui, int panel, char const* text);

/** As ToriRSChrome_Label with an explicit colour (0 = theme). */
int
ToriRSChrome_LabelColored(struct ToriRSChrome* ui, int panel, char const* text, uint32_t color);

int
ToriRSChrome_Checkbox(struct ToriRSChrome* ui, int panel, char const* label, int checked);

/**
 * A roster row: `label`, a switch showing `checked`, and -- when `has_action`
 * -- a settings affordance that reports through
 * ToriRSChrome_ActivationWasAction instead of flipping the switch.
 *
 * @see TORIRS_CHROME_W_LISTROW for why this is not a checkbox.
 */
int
ToriRSChrome_ListRow(
    struct ToriRSChrome* ui, int panel, char const* label, int checked, int has_action);

/**
 * A roster row for something that cannot be switched off: `label` and the
 * settings affordance, and no switch.
 *
 * Not `ListRow(..., checked = 1, ...)` with the toggle ignored, because a
 * switch drawn on is a switch that says it can be turned off -- clicking it
 * would be the one control on the page that does nothing, which reads as a
 * broken row rather than as a fixed one. The column is not merely disabled,
 * it is not there, and the name takes the space.
 */
int
ToriRSChrome_ListRowLocked(struct ToriRSChrome* ui, int panel, char const* label);

int
ToriRSChrome_TextInput(struct ToriRSChrome* ui, int panel, char const* label, char const* text);

/**
 * A multiline field: `label` on its own line, then a box `rows` lines tall.
 *
 * `rows` <= 0 asks for TORIRS_CHROME_M_TEXTAREA_ROWS; anything past
 * TORIRS_CHROME_M_TEXTAREA_ROWS_MAX is clamped to it, because the number comes
 * from a plugin manifest and a box taller than the panel is a page with no way
 * to reach the rows under it.
 *
 * @see TORIRS_CHROME_W_TEXTAREA for why this is a kind of its own.
 */
int
ToriRSChrome_TextArea(
    struct ToriRSChrome* ui, int panel, char const* label, char const* text, int rows);

/**
 * Break `text` into display lines that fit `width` pixels, at `font_slot`.
 *
 * Writes each line's byte OFFSET into `out_start` and its length into
 * `out_len` (both optional), and returns how many lines there are -- at least
 * one, even for an empty string, because an empty box still has a caret on a
 * first line. A line ends at a '\n' (which is not part of it) or where the
 * next word would not fit; a single word longer than the box is broken mid-word
 * rather than run off the edge.
 *
 * Public because more than one presentation wraps the same string for the same
 * box and they all have to break it in the same places. It measures with
 * the baked advance tables, which are a pure function of the slot -- no chrome
 * instance, no cache, nothing to be uninitialised.
 */
int
ToriRSChrome_WrapText(
    int font_slot,
    int scale,
    char const* text,
    int width,
    int* out_start,
    int* out_len,
    int max_lines);

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

/* ---- HSL16 colour ------------------------------------------------------
 *
 * The unit a model face is actually coloured in: 6-bit hue, 3-bit saturation,
 * 7-bit lightness, packed `(h << 10) | (s << 7) | l`. Everything a plugin
 * hands the engine as a colour ends up here, so a picker that works in this
 * space is a picker whose every value survives the trip.
 *
 * Computed rather than pulled from the rasteriser's g_hsl16_to_rgb_table, and
 * that is the module's no-dependency rule (see the header note) rather than a
 * preference: ui/ links no renderer, so the chrome would draw grey swatches in
 * every test and in any build whose palette had not been initialised yet. It
 * is a PORT of pix3d_init_palette at brightness 0.8 -- the same arithmetic on
 * the same constants -- with the one pow() replaced by a baked 256-entry gamma
 * ramp. That the two agree for all 32768 entries is asserted by
 * test-debug-overlay-visual, which links both; if the client ever changes its
 * palette brightness, that test is what says so.
 */

/** Packed HSL16 -> 0xRRGGBB. Out-of-range input is masked, never rejected:
 *  the low 16 bits are the whole domain. */
uint32_t
ToriRSChrome_Hsl16ToRgb(int hsl16);

/**
 * 0xRRGGBB -> a packed HSL16, quantised exactly as the reference's rgbToHSL
 * does -- ceilings, `% 63` and all.
 *
 * This is what the game's own toolchain used to turn authored art into palette
 * indices, so it is what makes a chosen colour land on the entry the cache's
 * models already use, and it is what the plugin api exposes as `hsl_from_rgb`.
 *
 * It is NOT an inverse of ToRgb and cannot be made into one: the palette puts
 * hue h at (h + 0.5) / 64 and this recovers it with `ceil(hue * 64) % 63`, so
 * a round trip moves the hue by one and the saturation by more. Use
 * ToriRSChrome_Hsl16NearestRgb wherever the round trip has to hold still.
 */
int
ToriRSChrome_Hsl16FromRgb(uint32_t rgb);

/**
 * 0xRRGGBB -> the palette entry that is actually CLOSEST to it.
 *
 * The conversion a picker needs, and a different question from the one above.
 * A colour row's value is a palette entry and the config store holds its hex,
 * so every open reads a colour back through this -- and with the reference
 * quantiser that read moved the colour, by one hue step and two saturation
 * steps, EVERY TIME. Sixty-three thousand of the sixty-five thousand entries
 * failed to survive one round trip, so a saved marker colour drifted a shade
 * per session and nothing anywhere said why.
 *
 * Exact, by search: an entry whose RGB matches is at distance zero, so
 * `Nearest(ToRgb(h))` always yields an entry with ToRgb(h)'s exact colour and
 * the round trip is stable by construction. The search is the full 32768-entry
 * palette in the sum-of-squares sense; it costs about a millisecond, and it
 * runs when a panel is BUILT rather than when it is drawn.
 */
int
ToriRSChrome_Hsl16NearestRgb(uint32_t rgb);

/** Split a packed HSL16 into its three axes. Any of the outs may be NULL. */
void
ToriRSChrome_Hsl16Split(int hsl16, int* hue, int* sat, int* lum);

/** Repack three axes, each clamped to its own field width. */
int
ToriRSChrome_Hsl16Pack(int hue, int sat, int lum);

/**
 * "#RRGGBB" (or "RRGGBB", or "0xRRGGBB") -> 0xRRGGBB.
 * @return 0 when `text` is not a colour, leaving `*out` alone.
 *
 * Tolerant of the spellings a config file and a wiki page actually carry,
 * strict about the digit count: a half-typed "#00FF" is a value the user is
 * still in the middle of, and guessing at it would snap the swatch to a colour
 * they never asked for while they are still typing.
 */
int
ToriRSChrome_ParseHexRgb(char const* text, uint32_t* out);

/** Which bar of the open picker popup a gesture is on. */
enum ToriRSChromeColorBar
{
    TORIRS_CHROME_COLORBAR_NONE = -1,
    TORIRS_CHROME_COLORBAR_HUE = 0,
    TORIRS_CHROME_COLORBAR_SAT,
    TORIRS_CHROME_COLORBAR_LUM,
    TORIRS_CHROME_COLORBAR_COUNT
};

/** How many distinct values each bar spans -- the field widths of HSL16. */
#define TORIRS_CHROME_COLOR_HUE_STEPS 64
#define TORIRS_CHROME_COLOR_SAT_STEPS 8
#define TORIRS_CHROME_COLOR_LUM_STEPS 128

/**
 * An HSL16 colour row: `label`, a swatch, an editable hex, and a popup of the
 * three axes behind the swatch.
 *
 * @param hsl16 the initial value; see ToriRSChrome_Hsl16FromRgb to come from
 *        an RGB. @return widget handle, or -1 when full / panel invalid.
 */
int
ToriRSChrome_ColorPick(struct ToriRSChrome* ui, int panel, char const* label, int hsl16);

/** The packed HSL16 a picker is showing, or -1 when `widget` is not one. */
int
ToriRSChrome_ColorPickValue(struct ToriRSChrome const* ui, int widget);

/** Set a picker's value and re-derive the hex it shows. Masks to 16 bits. */
void
ToriRSChrome_ColorPickSet(struct ToriRSChrome* ui, int widget, int hsl16);

/**
 * Take the hex currently in the field as the value, quantising it.
 *
 * Called when an edit is committed rather than on every keystroke: half a hex
 * is not a colour, and a swatch that jumped through six wrong colours while
 * the user typed the right one would read as a fault. @return 1 when the text
 * parsed and the value moved.
 */
int
ToriRSChrome_ColorPickCommitText(struct ToriRSChrome* ui, int widget);

/** Is the picker's bar popup open? */
int
ToriRSChrome_ColorPickIsOpen(struct ToriRSChrome const* ui, int widget);

/** Open or close it. Opening closes any other popup, including a dropdown's
 *  list -- only one thing floats over the chrome at a time. */
void
ToriRSChrome_ColorPickSetOpen(struct ToriRSChrome* ui, int widget, int open);

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

/** A pressable box captioned `text`. @see TORIRS_CHROME_W_BUTTON. */
int
ToriRSChrome_Button(struct ToriRSChrome* ui, int panel, char const* text);

/**
 * A tab strip over `titles`, which is BORROWED and must outlive the widget.
 *
 * One per panel. Selecting a tab shows the widgets stamped with its index and
 * hides the rest; @see ToriRSChrome_PanelBeginTab for how rows get stamped.
 * Activation fires on every change, so a caller can react to the switch.
 *
 * @return widget handle, or -1 when full / panel invalid.
 */
int
ToriRSChrome_Tabs(
    struct ToriRSChrome* ui,
    int panel,
    char const* const* titles,
    int title_count,
    int selected);

/** Point a tab strip at a different list of titles, clamping the selection.
 *  Widgets stamped past the new end become unreachable, so a caller that
 *  shortens a strip normally clears the panel and rebuilds it. */
void
ToriRSChrome_TabsSetTitles(
    struct ToriRSChrome* ui,
    int widget,
    char const* const* titles,
    int title_count);

/** Which tab of `panel` is showing. 0 for a panel with no strip. */
int
ToriRSChrome_PanelActiveTab(struct ToriRSChrome const* ui, int panel);

void
ToriRSChrome_PanelSetActiveTab(struct ToriRSChrome* ui, int panel, int tab);

/**
 * Stamp every widget added to `panel` from here on with `tab`; -1 for rows that
 * belong to every tab. @see ToriRSChromePanel::build_tab.
 */
void
ToriRSChrome_PanelBeginTab(struct ToriRSChrome* ui, int panel, int tab);

/** Let rows past the bottom scroll into view. @see ToriRSChromePanel::scrollable. */
void
ToriRSChrome_PanelSetScrollable(struct ToriRSChrome* ui, int panel, int scrollable);

/** Retitle a panel. The title is where a paged window says which page is up,
 *  so it changes at runtime rather than only at PanelAdd. */
void
ToriRSChrome_PanelSetTitle(struct ToriRSChrome* ui, int panel, char const* title);

/**
 * Remove one widget, returning its slot to the free list.
 *
 * The handle is dead afterwards and every latch that pointed at it (focus,
 * hover, press, the pending activation, an open dropdown list) is cleared, so a
 * removal can never leave the chrome holding a widget that is no longer there.
 */
void
ToriRSChrome_WidgetRemove(struct ToriRSChrome* ui, int widget);

/**
 * Remove every widget of one panel, leaving the panel itself in place.
 *
 * This is what makes a panel rebuildable. The alternative before it existed was
 * ToriRSChrome_Reset, which takes down every OTHER panel too -- so a consumer
 * that shared an instance could only ever append, and a plugin whose settings
 * changed could not have its rows replaced at all.
 */
void
ToriRSChrome_PanelClearWidgets(struct ToriRSChrome* ui, int panel);

/** Show or hide one widget. Hidden widgets take no space, draw nothing and hit
 *  nothing; the handle stays valid throughout. */
void
ToriRSChrome_SetHidden(struct ToriRSChrome* ui, int widget, int hidden);

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

/**
 * Is any panel of this instance on screen?
 *
 * The gate a host routes input on. Asking the chrome rather than tracking it
 * caller-side is what lets a new panel work the day it is added: the four
 * consumers that share the developer instance each had their own visibility
 * flag, the input gate named only two of them, and the panels the gate did not
 * name received no clicks at all -- inert controls, which reads as a broken
 * panel rather than an unrouted one.
 */
int
ToriRSChrome_HasVisiblePanel(struct ToriRSChrome const* ui);

/** @return widget handle under (x,y), or -1. Only hits visible panels. */
int
ToriRSChrome_HitTest(struct ToriRSChrome const* ui, int x, int y);

/* ---- moving the OS window by chrome that is drawn in it ------------------- */

/**
 * Boxes that drag the WINDOW, and boxes inside them that do not.
 *
 * WHY A PUBLISHED REGION AND NOT A QUERY. The point test this feeds runs inside
 * the platform's event pump -- the window manager asks while it is deciding
 * what a mouse press even IS -- and this model is the frame thread's, laid out
 * and mutated there. A callback that walked panels and widgets would be reading
 * a tree the frame it interrupted is halfway through rebuilding. A dozen
 * rectangles copied out once a frame is a snapshot the pump can test against
 * for nothing, and it is also the whole of what the pump needs.
 *
 * WHY HOLES. A draggable region SWALLOWS the press that starts the drag: the
 * application is never told about a mouse-down there. So every control inside a
 * handle has to be punched back out of it, or it silently stops being
 * clickable -- and "the tabs of the tab strip I am dragging the window by" is
 * exactly that case.
 *
 * The tab run is ONE hole rather than one per tab because tabs are laid out
 * contiguously from the strip's left edge: their union is the run, and what is
 * left draggable is the empty tail behind it. A strip whose tabs have been
 * compressed to fill its width therefore offers no handle at all, correctly --
 * there is no pixel of it that is not a tab. The panel's title bar is the
 * handle that is always there.
 */
#define TORIRS_CHROME_DRAG_HANDLES_MAX 2
#define TORIRS_CHROME_DRAG_HOLES_MAX 6

struct ToriRSChromeDragRegion
{
    struct ToriRSChromeRect handles[TORIRS_CHROME_DRAG_HANDLES_MAX];
    int handle_count;
    struct ToriRSChromeRect holes[TORIRS_CHROME_DRAG_HOLES_MAX];
    int hole_count;
};

/**
 * This frame's window-move handles for `panel`, in the surface's own pixels.
 * @return 1 when there is a region; 0 -- with `out` cleared -- when there is
 * none, which a caller must still publish rather than skip.
 *
 * Only a FILLED window panel has one. A floating panel is dragged INSIDE the
 * canvas by its own header (dbg_panel_header_at), and moving the OS window from
 * that header would take the game out from under the pointer; a filled panel is
 * the whole of its window, so its title bar and the tail of its tab strip are
 * the only chrome in it with no other job.
 */
int
ToriRSChrome_WindowDragRegion(
    struct ToriRSChrome const* ui, int panel, struct ToriRSChromeDragRegion* out);

/**
 * Is (x,y) on a handle and in no hole?
 *
 * Pure, and takes no model: this is the half that runs in the event pump,
 * against the copy that was published to it.
 */
int
ToriRSChromeDragRegion_Contains(struct ToriRSChromeDragRegion const* region, int x, int y);

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

/** @param key enum ToriRSChromeKey. @return 1 if consumed. */
int
ToriRSChrome_KeyEdit(struct ToriRSChrome* ui, int key);

/**
 * Handle of the widget activated since the last call (menu item clicked,
 * checkbox toggled, input committed with Enter), then clears it. -1 = none.
 */
int
ToriRSChrome_TakeActivated(struct ToriRSChrome* ui);

/**
 * Was the activation just drained a LISTROW's ACTION -- its settings
 * affordance -- rather than its switch? Valid immediately after
 * ToriRSChrome_TakeActivated, and 0 for every other widget kind.
 */
int
ToriRSChrome_ActivationWasAction(struct ToriRSChrome const* ui);

/* ---- display list ------------------------------------------------------- */

/**
 * Relayout every visible panel and rebuild the display list, but only if
 * something changed. @return 1 when it rebuilt, 0 when the cached list stood.
 */
int
ToriRSChrome_Build(struct ToriRSChrome* ui);

/** The display list. Valid until the next Build. */
struct ToriRSChromePrim const*
ToriRSChrome_Prims(struct ToriRSChrome const* ui, int* out_count);

/** @return 1 and writes the invalid region when there is one. */
int
ToriRSChrome_Damage(struct ToriRSChrome const* ui, struct ToriRSChromeRect* out);

void
ToriRSChrome_DamageClear(struct ToriRSChrome* ui);

#endif

/*
 * The CS2 chrome executor: the plugin window as a game interface.
 *
 * A NATIVE-WIDGET executor (see the two kinds in torirs_chrome_exec.h) whose
 * "native" is the game's own interface tree. Every row becomes real components
 * -- a RECT for a checkbox's box, a TEXT for its label -- built through the
 * same UITree_PushBuildComponent the client uses for any programmatic panel,
 * and clicked through the same hit test as any cache-authored interface.
 *
 * WHY THIS ONE IS DIFFERENT. The other executors reach for a foreign toolkit;
 * this one reaches for the toolkit the game already is. That buys the one thing
 * none of the others can: the plugin window looks like the game, scales with
 * the game, and is drawn by the same pass in the same order -- no second
 * window, no second theme, no second thing to keep in step when the gameframe
 * changes. It also needs no new platform code at all, which is why it is the
 * only executor that works on every lane at once.
 *
 * COMPONENT IDS ARE ALLOCATED FROM A PRIVATE RANGE. A component id is how a
 * click gets home: the tree reports `clicked_com_id`, the host recognises the
 * range as this executor's and turns it into an intent. The range is high and
 * fixed (TORIRS_CHROME_CS2_ID_BASE), well clear of anything a cache ships, so
 * the recognition is a bounds test rather than a registry.
 *
 * The tree and its mounting point are INJECTED, not reached for: ui/ owns the
 * tree type but not the App that holds one, and which slot a panel mounts in is
 * a gameframe question this file has no business answering.
 */

#include "torirs_chrome_exec.h"
#include "torirs_chrome_metrics.h"
#include "torirs_chrome_mirror.h"
#include "uitree.h"
#include "uitree_build.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Layout, in interface pixels. The gameframe's own scale applies on top.
 *
 * From torirs_chrome_metrics.h, NOT from numbers of this file's own: the
 * in-canvas chrome draws the same panel from the same table, and the two
 * presentations agreeing is the whole point of that header. A number that
 * belongs only to this executor -- the tab caption's approximated advance,
 * the component-id blocks -- still lives here.
 */
#define CS2_PAD TORIRS_CHROME_M_PAD
#define CS2_ROW_H TORIRS_CHROME_M_ROW_H
#define CS2_ROW_GAP TORIRS_CHROME_M_ROW_GAP
#define CS2_LABEL_W TORIRS_CHROME_M_LABEL_W
#define CS2_TAB_H TORIRS_CHROME_M_TAB_H
/** Air either side of a tab caption, and the caption-width approximation the
 *  strip lays out with: this executor cannot measure text (advances live in
 *  the scene's font), so a p12-ish average per character serves both the
 *  proportional widths and the truncation. */
#define CS2_TAB_PAD_X TORIRS_CHROME_M_TAB_PAD_X
#define CS2_TAB_CHAR_W 7
/** Checkbox edge -- the baked on/off pair's own 17x17, so it draws unscaled. */
#define CS2_BOX TORIRS_CHROME_M_BOX
#define CS2_PANEL_W 280
#define CS2_PANEL_H 240

/* The reference interface palette, the same values torirs_chrome_theme_osrs carries:
 * a window built here and a cache-authored one on screen together have to read
 * as one system. The frame pair and the orange are script_3850 verbatim: a
 * settings field is graphic_297 tiled, framed in 0x0e0e0c with a 0x474745
 * inset, its label and value set in 0xff981f. */
#define CS2_COL_BODY TORIRS_CHROME_C_BODY
#define CS2_COL_CHROME TORIRS_CHROME_C_CHROME
#define CS2_COL_TEXT TORIRS_CHROME_C_TEXT
#define CS2_COL_DIM 0xC8C8C8
#define CS2_COL_LABEL TORIRS_CHROME_C_LABEL
#define CS2_COL_ACCENT TORIRS_CHROME_C_ACCENT
#define CS2_COL_ON TORIRS_CHROME_C_ON
#define CS2_COL_FIELD_BG TORIRS_CHROME_C_FIELD_BG
#define CS2_COL_FRAME TORIRS_CHROME_C_FRAME
#define CS2_COL_FRAME_INSET TORIRS_CHROME_C_FRAME_INSET
/* Flat scrollbar fallback: torirs_frame.c's translate_scrollbar_v_step colours. */
#define CS2_COL_SCROLL_TRACK TORIRS_CHROME_C_SCROLL_TRACK
#define CS2_COL_SCROLL_GRIP TORIRS_CHROME_C_SCROLL_GRIP
#define CS2_COL_SCROLL_GRIP_HI TORIRS_CHROME_C_SCROLL_GRIP_HI
#define CS2_COL_SCROLL_GRIP_LO TORIRS_CHROME_C_SCROLL_GRIP_LO
/* script_9114's band veil: an unselected tab is the body seen through a
 * translucent black rect at client transparency 220 (0 opaque, 255 invisible). */
#define CS2_TRANS_TAB 220
/* The same veil down the open list's rows, alternating between script_9114's
 * own two values so a long list reads as rows. Both are lifted from the
 * `cc_settrans` calls in the script that draws the real list, which is where
 * the in-canvas chrome's theme takes them from too. */
#define CS2_TRANS_DROP_BAND 220
#define CS2_TRANS_DROP_BAND_ALT 200

/*
 * The furniture comes from the BAKED skin, not from the cache.
 *
 * These are the same seven images either way -- tradebacking behind panels and
 * fields, and the six pieces ~script31 builds a scrollbar out of -- but taking
 * them from `torirs_chrome_skin_baked.h` rather than from archives 297/773/788/
 * 792/789/790/791 buys three things. There is nothing to wait for, so no
 * half-drawn first frames and no rebuild-when-it-lands probe. There is nothing
 * to be wrong, since those archive ids mean unrelated images on any cache but
 * the OSRS one they were baked from. And there is no cache requirement at all,
 * which is what lets this presentation work on a lane whose cache failed to
 * open -- the same guarantee the in-canvas chrome already has, reached the same
 * way.
 *
 * The skin is ONE multi-frame scene sprite, so a slot is an atlas index into
 * it; `skin_scene_id` is the sprite and these are the frames.
 */
#define CS2_SPR_FIELD_TILE TORIRS_CHROME_SKIN_PANEL_BODY
/** The open dropdown list's own tile -- graphic_1040, a lighter parchment than
 *  the tradebacking a panel and a closed button wear. */
#define CS2_SPR_LIST_TILE TORIRS_CHROME_SKIN_DROPDOWN_BODY
#define CS2_SPR_SCROLL_UP TORIRS_CHROME_SKIN_SCROLL_UP
#define CS2_SPR_SCROLL_DOWN TORIRS_CHROME_SKIN_SCROLL_DOWN
#define CS2_SPR_SCROLL_TRACK TORIRS_CHROME_SKIN_SCROLL_TRACK
#define CS2_SPR_GRIP_TOP TORIRS_CHROME_SKIN_SCROLL_GRIP_TOP
#define CS2_SPR_GRIP_MID TORIRS_CHROME_SKIN_SCROLL_GRIP_MID
#define CS2_SPR_GRIP_BOTTOM TORIRS_CHROME_SKIN_SCROLL_GRIP_BOTTOM
/** The grip caps' baked height. */
#define CS2_SCROLL_CAP_H TORIRS_CHROME_M_SCROLL_CAP_H
/** Shortest grip ~script31 draws. */
#define CS2_SCROLL_GRIP_MIN TORIRS_CHROME_M_SCROLL_GRIP_MIN

/* The two scroll arrows' component ids, in the executor's own range. */
#define CS2_ID_SCROLL_UP (TORIRS_CHROME_CS2_ID_BASE + 0x20)
#define CS2_ID_SCROLL_DOWN (TORIRS_CHROME_CS2_ID_BASE + 0x21)

/* ---- the open dropdown list ----------------------------------------------
 *
 * The geometry is the shared table's, so this list and the in-canvas one are
 * the same list: as wide as the button it hangs off, starting at its bottom
 * edge, its rows a little taller than a settings row.
 */
#define CS2_DROP_LIST_PAD TORIRS_CHROME_M_DROP_LIST_PAD
#define CS2_DROP_ROW_H TORIRS_CHROME_M_DROP_LIST_ROW_H
#define CS2_DROP_ROWS TORIRS_CHROME_M_DROP_LIST_ROWS
/** The list's two scroll arrows. Separate ids from the panel's own bar: both
 *  can be on screen at once, and one pair serving both would scroll whichever
 *  the handler guessed. */
#define CS2_ID_DROP_UP (TORIRS_CHROME_CS2_ID_BASE + 0x24)
#define CS2_ID_DROP_DOWN (TORIRS_CHROME_CS2_ID_BASE + 0x25)
/**
 * One id per VISIBLE row of the open list, not per option.
 *
 * The same trade the colour picker's cells make: only one list is open at a
 * time and it shows at most CS2_DROP_ROWS rows, so the block is indexed by
 * screen row and the option it names is `drop_scroll + row`. A block per
 * widget would be one per option -- a search palette can hold thousands.
 */
#define CS2_ID_DROP_ROW_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x2000)
/**
 * Widget ids, and the parallel block a LISTROW's ACTION zone uses.
 *
 * Two ids for one widget because a row has two outcomes and a component id is
 * how a click gets home: the switch answers on the widget's own id and the
 * settings affordance on the same handle in the action block, so the click
 * handler recovers both the handle and which zone from arithmetic alone.
 */
#define CS2_ID_WIDGET_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x100)
#define CS2_ID_ACTION_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x400)
/**
 * A COLORPICK's swatch, and the cells of its open axis popup.
 *
 * The swatch gets a block of its own for the same reason a LISTROW's action
 * zone does: one widget, two outcomes, and a component id is how a click gets
 * home. The CELLS are not per widget at all -- only one picker is open at a
 * time, so the block is indexed by cell number and the handler recovers which
 * picker from `colorpick_open`.
 */
/**
 * Close, at the top of the panel.
 *
 * This presentation has no window furniture of its own -- it is a column of
 * components inside the gameframe's popout strip -- so there was nothing to
 * shut it with: the window opened and stayed open. The in-canvas chrome grew
 * the same button in its title bar (ToriRSChrome_PanelSetClosable); here it is
 * a component at the top right, and the intent it sends is the model's, so both
 * presentations mean the same thing by it.
 */
#define CS2_ID_CLOSE (TORIRS_CHROME_CS2_ID_BASE + 0x23)
/** Side of that button. */
#define CS2_BTN 14

#define CS2_ID_SWATCH_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x800)
#define CS2_ID_CELL_BASE (TORIRS_CHROME_CS2_ID_BASE + 0x1000)
/** Cells one bar is cut into. Not the axis's own step count: hue has 64 values
 *  and lightness 128, and one component per value would be two hundred nodes
 *  in the interface tree for one open popup. The cell a click lands in is
 *  mapped back onto the full range, so every value is still reachable -- what
 *  is coarser is the POINTER, not the axis. */
#define CS2_COLOR_CELLS 32
/** Bars, in enum ToriRSChromeColorBar order. */
#define CS2_COLOR_BARS 3
#define CS2_COLORBAR_H TORIRS_CHROME_M_COLORBAR_H
#define CS2_COLORBAR_GAP TORIRS_CHROME_M_COLORBAR_GAP
/** Width of the swatch inside a colour field. */
#define CS2_SWATCH TORIRS_CHROME_M_SWATCH
/** Width of the scroll column: the sprite's own width, and the width
 *  ~script31 gives the column. Always reserved -- see cs2_rebuild. */
#define CS2_SCROLL_W TORIRS_CHROME_M_SCROLL_W

struct ChromeCs2
{
    struct UITree* tree;
    /** Node index of the container everything mounts under, from the host;
     *  -1 means "its own root". Re-resolved from mount_com_id at every
     *  rebuild: a gameframe rebuild renumbers the whole tree, and an index
     *  held across one silently names a different node. */
    int32_t mount;
    /** Component id of the mount, taken at bind time; 0 when mount is -1. */
    int mount_com_id;
    /** The panel box the last rebuild laid rows out in, so a SYNC_END can see
     *  the strip's layout settle (or the window resize) and rebuild to fit.
     *  The mount's size is resolved by a layout pass that runs AFTER the panel
     *  first builds, so the first build is always at the fallback size. */
    int built_w;
    int built_h;
    /** Scene font id every TEXT component draws in. Zero draws nothing, which
     *  is exactly what the first version of this did. */
    int font_id;
    /** Cache font id of the face row text SHOULD be set in -- the gameframe's
     *  own p12, resolved through resolve_font_cb. -1 = draw in font_id. The
     *  debug face above stays as the fallback for the frames before the cache
     *  face lands, so text is never invisible while a load is in flight. */
    int cache_font_id;
    /** The baked skin's scene sprite id, or -1 when this build baked none.
     *  Every piece of furniture is a frame of it. */
    int skin_scene_id;
    /** Cache font id -> scene font id, requesting the load on a miss; NULL or
     *  a -1 answer means "set the rows in the baked fallback face". Injected
     *  because the provider and the bridge are the App's, not ui/'s. */
    int (*resolve_font_cb)(void*, int);
    void* resolve_cb_ud;
    /** How many furniture assets (the baked skin + the cache face) resolved at
     *  the last rebuild. A SYNC_END that sees the count grow rebuilds, which is
     *  how the panel upgrades to the real face the frame the requested load
     *  lands. */
    int assets_ok;
    /** Node index of the panel layer this executor owns, or -1. */
    int32_t panel_node;
    int open;
    int dirty;
    struct ToriRSChromeMirror mirror;

    /**
     * Rows scrolled past, in whole rows.
     *
     * Rows rather than pixels because every row here is the same height, so a
     * row index is the honest unit and there is no partial row to clip. The
     * in-canvas chrome scrolls by pixels because its rows are NOT uniform -- a
     * model view, a separator and a text input are three different heights.
     */
    int scroll_row;
    /** Which panel owns the strip, and its titles. One window, one strip. */
    int tab_panel;
    int tab_strip_widget;
    char tabs[TORIRS_CHROME_CS2_TABS_MAX][48];
    int tab_count;

    /* Per widget, the strings the tree needs. The tree BORROWS text pointers,
     * so unlike the other executors this one must hold the strings itself --
     * a DOM node and an EDIT control own their text; a TEXT component points
     * at yours. */
    char label[TORIRS_CHROME_MAX_WIDGETS][TORIRS_CHROME_LABEL_MAX];
    char text[TORIRS_CHROME_MAX_WIDGETS][TORIRS_CHROME_TEXT_MAX];
    int checked[TORIRS_CHROME_MAX_WIDGETS];
    /** 0xRRGGBB per widget, 0 meaning "the palette's" -- how a faulted
     *  plugin's note stays red here too. */
    uint32_t wcolor[TORIRS_CHROME_MAX_WIDGETS];
    /** A dropdown's selection, and how many options it has. */
    int selected[TORIRS_CHROME_MAX_WIDGETS];
    int option_count[TORIRS_CHROME_MAX_WIDGETS];
    /**
     * The option strings themselves, owned -- `option_count` pointers, each
     * strdup'd from the WIDGET_OPTION that announced it, or NULL while the
     * list has not been stated.
     *
     * This executor used to hold only the count, because a click CYCLED the
     * selection and the shown value arrived with the WIDGET_SELECTED that
     * answered it. A list that opens has to draw the options it is offering,
     * and the tree BORROWS the text it is handed (see cs2_text_box), so they
     * have to live somewhere that outlives the build -- which is here.
     *
     * Allocated rather than an array of fixed rows: a palette dropdown is
     * every loc name in the cache, and 384 widgets' worth of that reserved up
     * front would be megabytes for a window that mostly holds checkboxes.
     */
    char** options[TORIRS_CHROME_MAX_WIDGETS];
    /**
     * Which dropdown's list is open, or -1, and how far down it is scrolled.
     *
     * Held HERE and not mirrored from the model, unlike the colour picker's
     * popup: the model's own open list is prims at the in-canvas window's
     * floating position, and this presentation is neither. What the model has
     * to hear is the PICK a row makes, which is the same intent the cycling
     * click sent before.
     */
    int drop_open;
    int drop_scroll;
    /**
     * The box the last build put the open dropdown's BUTTON in, so the list
     * can be built after every row rather than inside the row loop -- a list
     * built where the row is would be drawn under the rows that follow it.
     * Zero width means the row was not drawn at all this build (scrolled out
     * of the panel, or its tab is not up), which closes the list.
     */
    int drop_x;
    int drop_y;
    int drop_w;
    /** LISTROW: whether the row carries a settings affordance. Arrives on the
     *  ADD as `w`, the one command that carries a widget's shape. */
    int wrow_action[TORIRS_CHROME_MAX_WIDGETS];
    /**
     * Does the model's keyboard focus rest on this row?
     *
     * A component tree has no focus of its own -- the host routes keys at the
     * MODEL's focused widget -- so without being told, this executor draws
     * every field identically and clicking one reads as having done nothing.
     * That was the bug: the rows took typing perfectly well and gave no sign
     * of it, so they looked read-only.
     */
    int focused[TORIRS_CHROME_MAX_WIDGETS];
    /** Which COLORPICK's axis bars are on screen, or -1. Held here as well as
     *  in `checked` because a bar CELL's component id names the cell and not
     *  the row, so the click handler has to be able to ask. */
    int colorpick_open;
};

static struct ChromeCs2 g_chrome_cs2;

/* ---- a dropdown's option list --------------------------------------------
 *
 * Owned strings, because the tree borrows the text it is handed and a build
 * outlives the command that carried it. Three calls, so the free-then-allocate
 * order is written once: a restate that allocated first and freed after would
 * leak the old list on the path where the new one is empty.
 */

/** Drop `widget`'s options. Idempotent -- a widget that never had a list is
 *  the common case, and its slot is already NULL. */
static void
cs2_options_clear(struct ChromeCs2* s, int widget)
{
    assert(s);
    assert(widget >= 0);
    assert(widget < TORIRS_CHROME_MAX_WIDGETS);

    if( s->options[widget] )
    {
        for( int i = 0; i < s->option_count[widget]; i++ )
            free(s->options[widget][i]);
        free(s->options[widget]);
        s->options[widget] = NULL;
    }
    s->option_count[widget] = 0;
}

/** Make room for a restated list of `count`. The entries arrive one command
 *  each and are NULL until they do, which is what a build has to tolerate. */
static void
cs2_options_reset(struct ChromeCs2* s, int widget, int count)
{
    assert(s);
    assert(widget >= 0);
    assert(widget < TORIRS_CHROME_MAX_WIDGETS);

    cs2_options_clear(s, widget);
    if( count <= 0 )
        return;
    s->options[widget] = calloc((size_t)count, sizeof(*s->options[widget]));
    assert(s->options[widget]);
    s->option_count[widget] = count;
}

/** One entry of the list just announced. An index outside the announced count
 *  is dropped: the two commands are a pair and a stray entry means the pair
 *  was torn, not that the list should grow under the build. */
static void
cs2_options_set(struct ChromeCs2* s, int widget, int index, char const* text)
{
    assert(s);
    assert(widget >= 0);
    assert(widget < TORIRS_CHROME_MAX_WIDGETS);
    assert(text);

    if( !s->options[widget] || index < 0 || index >= s->option_count[widget] )
        return;
    free(s->options[widget][index]);
    s->options[widget][index] = strdup(text);
    assert(s->options[widget][index]);
}

/** Option `index` of `widget`, or "" for one that has not arrived -- an empty
 *  row draws as an empty row, where a NULL through the builder is a crash. */
static char const*
cs2_option_text(struct ChromeCs2 const* s, int widget, int index)
{
    assert(s);
    assert(widget >= 0);
    assert(widget < TORIRS_CHROME_MAX_WIDGETS);

    if( !s->options[widget] || index < 0 || index >= s->option_count[widget] )
        return "";
    return s->options[widget][index] ? s->options[widget][index] : "";
}

/** Every list this executor holds, on the way out. */
static void
cs2_options_free_all(struct ChromeCs2* s)
{
    assert(s);
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        cs2_options_clear(s, i);
}

/** Shut the open list, if one is open. @return 1 when something changed, so
 *  the caller can decide whether that is worth a rebuild. */
static int
cs2_dropdown_close(struct ChromeCs2* s)
{
    assert(s);
    if( s->drop_open < 0 )
        return 0;
    s->drop_open = -1;
    s->drop_scroll = 0;
    return 1;
}

int
ToriRSChrome_TreeAcceptsChrome(struct UITree const* tree)
{
    int group;

    if( !tree || tree->root_index < 0 )
        return 0;
    if( (uint32_t)tree->root_index >= tree->component_count )
        return 0;
    group = (tree->components[tree->root_index].component_id >> 16) & 0xffff;
    /* Our own group already sitting first means chrome got in ahead of the
     * gameframe -- the state this exists to keep out of. */
    return group != TORIRS_CHROME_CS2_GROUP;
}

/* ---- building the interface ---------------------------------------------- */

static int
cs2_resolve_font(void* ud, int font)
{
    struct ChromeCs2* s = ud;

    assert(s);
    /* The row face is a CACHE font id and resolves through the host; anything
     * else handed to a component here is already a scene font id (the injected
     * fallback face). A miss answers with the fallback rather than -1, because
     * the builder's own fallback is the unresolved cache id -- which is not a
     * scene font, and text set in it is simply invisible. */
    if( s->resolve_font_cb && s->cache_font_id >= 0 && font == s->cache_font_id )
    {
        int const scene = s->resolve_font_cb(s->resolve_cb_ud, font);
        return scene >= 0 ? scene : s->font_id;
    }
    return font;
}

/** The font id row text is authored in; cs2_resolve_font turns it into a
 *  scene id at build time. */
static int
cs2_row_font(struct ChromeCs2 const* s)
{
    return s->cache_font_id >= 0 ? s->cache_font_id : s->font_id;
}

/** Is the furniture drawable? One question for all seven pieces now: they
 *  arrive together or not at all. */
static int
cs2_sprite_ok(struct ChromeCs2 const* s, int slot)
{
    (void)slot;
    return s->skin_scene_id > 0;
}

/**
 * How many of the assets the furniture wants are drawable right now.
 *
 * Asked at every rebuild and at every SYNC_END while short: the font probe
 * also REQUESTS the load, and a count that grew is the signal to rebuild with
 * what arrived.
 *
 * Only the row FACE can still be short. The seven sprites are baked and were
 * resolved once at construction, so they are either all here or this build
 * baked no skin at all -- which is why the whole sprite half of this collapsed
 * to one term.
 */
static int
cs2_assets_avail(struct ChromeCs2* s)
{
    int n = 0;

    if( s->skin_scene_id > 0 )
        n++;
    if( s->resolve_font_cb && s->cache_font_id >= 0 &&
        s->resolve_font_cb(s->resolve_cb_ud, s->cache_font_id) >= 0 )
        n++;
    return n;
}

/** A filled or outlined rectangle, optionally translucent (client convention:
 *  0 opaque, 255 invisible). */
static int32_t
cs2_rect_trans(
    struct ChromeCs2* s, int32_t parent, int com_id, int x, int y, int w, int h,
    int color, int filled, int trans)
{
    struct UIBuildComponent comp;

    memset(&comp, 0, sizeof(comp));
    comp.id = com_id;
    comp.type = UIBUILD_RECT;
    comp.parent_id = -1;
    comp.base_x = x;
    comp.base_y = y;
    comp.base_width = w;
    comp.base_height = h;
    comp.if3 = 1;
    comp.graphic = -1;
    comp.graphic_active = -1;
    comp.model_active_id = -1;
    comp.model_seq_id = -1;
    comp.color = color;
    comp.filled = filled;
    comp.transparency = trans;
    return UITree_PushBuildComponent(
        s->tree, parent, &comp, NULL, cs2_resolve_font, s);
}

/** A filled or outlined rectangle. */
static int32_t
cs2_rect(
    struct ChromeCs2* s, int32_t parent, int com_id, int x, int y, int w, int h,
    int color, int filled)
{
    return cs2_rect_trans(s, parent, com_id, x, y, w, h, color, filled, 0);
}

/** A baked skin frame, stretched into its box, or repeated across it. */
static int32_t
cs2_graphic(
    struct ChromeCs2* s, int32_t parent, int com_id, int x, int y, int w, int h,
    int slot, int tiled)
{
    struct UIBuildComponent comp;

    memset(&comp, 0, sizeof(comp));
    comp.id = com_id;
    comp.type = UIBUILD_GRAPHIC;
    comp.parent_id = -1;
    comp.base_x = x;
    comp.base_y = y;
    comp.base_width = w;
    comp.base_height = h;
    comp.if3 = 1;
    comp.graphic = -1;
    comp.graphic_scene_id = s->skin_scene_id;
    comp.graphic_atlas_index = slot;
    comp.graphic_active = -1;
    comp.model_active_id = -1;
    comp.model_seq_id = -1;
    comp.tiled = tiled;
    return UITree_PushBuildComponent(
        s->tree, parent, &comp, NULL, cs2_resolve_font, s);
}

/**
 * A line of text.
 *
 * The string is handed to the BUILDER, not written into the component
 * afterwards: the tree OWNS its text (UITree_PushBuildComponent strdups it and
 * the node teardown frees it), so assigning a pointer of ours into the field
 * would have the tree free memory it never allocated the next time the panel
 * was rebuilt. That is a heap corruption rather than a leak, and it is exactly
 * what the first version of this did.
 */
static int32_t
cs2_text_box(
    struct ChromeCs2* s, int32_t parent, int com_id, int x, int y, int w, int h,
    char const* str, int color, int centred)
{
    struct UIBuildComponent comp;

    memset(&comp, 0, sizeof(comp));
    comp.id = com_id;
    comp.type = UIBUILD_TEXT;
    comp.parent_id = -1;
    comp.base_x = x;
    comp.base_y = y;
    comp.base_width = w;
    comp.base_height = h;
    comp.if3 = 1;
    comp.graphic = -1;
    comp.graphic_active = -1;
    comp.model_active_id = -1;
    comp.model_seq_id = -1;
    comp.text = str;
    comp.color = color;
    comp.shadowed = 1;
    comp.font_id = cs2_row_font(s);
    comp.text_h_align = centred;
    /* Centred vertically in its own row box. A TEXT component's y is the box
     * TOP and the glyphs hang from a baseline inside it, so without this every
     * label sits high in its row and a row beside a checkbox looks misaligned
     * with the box it labels. */
    comp.text_v_align = 1;
    return UITree_PushBuildComponent(
        s->tree, parent, &comp, NULL, cs2_resolve_font, s);
}

static int32_t
cs2_text(
    struct ChromeCs2* s, int32_t parent, int com_id, int x, int y, int w,
    char const* str, int color)
{
    return cs2_text_box(s, parent, com_id, x, y, w, CS2_ROW_H, str, color, 0);
}

/* ---- the colour picker's three axes ---------------------------------------
 *
 * Small mirrors of the model's own bar arithmetic. Duplicated rather than
 * exported because they are three lines each over constants the seam header
 * already carries, and the alternative -- a bar-geometry call the model
 * exports -- would have to speak in the MODEL's pixels, which are the one
 * thing this presentation does not share with it.
 */

static int
cs2_colorbar_steps(int bar)
{
    switch( bar )
    {
    case TORIRS_CHROME_COLORBAR_HUE:
        return TORIRS_CHROME_COLOR_HUE_STEPS;
    case TORIRS_CHROME_COLORBAR_SAT:
        return TORIRS_CHROME_COLOR_SAT_STEPS;
    default:
        return TORIRS_CHROME_COLOR_LUM_STEPS;
    }
}

static int
cs2_colorbar_value(int hsl16, int bar)
{
    int hue;
    int sat;
    int lum;
    ToriRSChrome_Hsl16Split(hsl16, &hue, &sat, &lum);
    if( bar == TORIRS_CHROME_COLORBAR_HUE )
        return hue;
    if( bar == TORIRS_CHROME_COLORBAR_SAT )
        return sat;
    return lum;
}

static int
cs2_colorbar_with(int hsl16, int bar, int value)
{
    int hue;
    int sat;
    int lum;
    ToriRSChrome_Hsl16Split(hsl16, &hue, &sat, &lum);
    if( bar == TORIRS_CHROME_COLORBAR_HUE )
        hue = value;
    else if( bar == TORIRS_CHROME_COLORBAR_SAT )
        sat = value;
    else
        lum = value;
    return ToriRSChrome_Hsl16Pack(hue, sat, lum);
}

/*
 * The chrome a settings field wears -- script_3850 verbatim, the same box the
 * in-canvas chrome's dbg_push_field_chrome draws: tiled tradebacking under a
 * near-black frame with a grey inset one pixel inside it. A dropdown button, a
 * text input and a button here are all this box, differing only in contents,
 * because the reference shares it the same way.
 *
 * The flat fill goes first either way: the tile carries transparent pixels at
 * its edges, and tiling it straight onto the strip would show through them.
 * The com_id rides the fill, so the whole box is the click target; decoration
 * over it carries -1 and stays inert.
 */
static void
cs2_field_chrome(
    struct ChromeCs2* s, int32_t panel, int com_id, int x, int y, int w, int h)
{
    int32_t const box = cs2_rect(s, panel, com_id, x, y, w, h, CS2_COL_FIELD_BG, 1);

    if( box >= 0 && com_id >= 0 )
        UITree_ApplyClickMask(s->tree, com_id, 1);
    if( cs2_sprite_ok(s, CS2_SPR_FIELD_TILE) )
        cs2_graphic(s, panel, -1, x, y, w, h, CS2_SPR_FIELD_TILE, 1);
    cs2_rect(s, panel, -1, x, y, w, h, CS2_COL_FRAME, 0);
    cs2_rect(s, panel, -1, x + 1, y + 1, w - 2, h - 2, CS2_COL_FRAME_INSET, 0);
}

/*
 * The interfaces' nine-slice panel frame, built as components.
 *
 * The in-canvas chrome's dbg_push_frame, in this presentation's vocabulary --
 * corners at their baked 32x32, edges stretched along their runs at the rail's
 * 6, and no centre because the panel's tile is already under it.
 *
 * Only the STANDALONE panel draws one. Mounted in the popout strip we are
 * inside a frame the gameframe already drew, and a second one inside the first
 * is the box-in-a-box the fill-parent modes exist to avoid.
 */
static void
cs2_push_frame(struct ChromeCs2* s, int32_t panel, int32_t* com, int w, int h)
{
    int const rail = TORIRS_CHROME_M_FRAME;
    int const c = TORIRS_CHROME_M_FRAME_CORNER;
    int const mid_w = w - 2 * c;
    int const mid_h = h - 2 * c;
    int const corner_r = w - c;
    int const corner_b = h - c;
    int const rail_r = w - rail;
    int const rail_b = h - rail;

    if( !cs2_sprite_ok(s, TORIRS_CHROME_SKIN_FRAME_TOP_LEFT) )
        return;
    cs2_graphic(s, panel, (*com)++, 0, 0, c, c, TORIRS_CHROME_SKIN_FRAME_TOP_LEFT, 0);
    cs2_graphic(s, panel, (*com)++, corner_r, 0, c, c, TORIRS_CHROME_SKIN_FRAME_TOP_RIGHT, 0);
    cs2_graphic(s, panel, (*com)++, 0, corner_b, c, c, TORIRS_CHROME_SKIN_FRAME_BOTTOM_LEFT, 0);
    cs2_graphic(
        s, panel, (*com)++, corner_r, corner_b, c, c, TORIRS_CHROME_SKIN_FRAME_BOTTOM_RIGHT, 0);
    if( mid_w > 0 )
    {
        cs2_graphic(s, panel, (*com)++, c, 0, mid_w, rail, TORIRS_CHROME_SKIN_FRAME_TOP, 0);
        cs2_graphic(
            s, panel, (*com)++, c, rail_b, mid_w, rail, TORIRS_CHROME_SKIN_FRAME_BOTTOM, 0);
    }
    if( mid_h > 0 )
    {
        cs2_graphic(s, panel, (*com)++, 0, c, rail, mid_h, TORIRS_CHROME_SKIN_FRAME_LEFT, 0);
        cs2_graphic(
            s, panel, (*com)++, rail_r, c, rail, mid_h, TORIRS_CHROME_SKIN_FRAME_RIGHT, 0);
    }
}

/**
 * Where a labelled row's field box starts, measured from the panel's left pad.
 *
 * The fixed label column -- and NO column at all when the row carries no
 * label. Reserving one unconditionally was the first version, and it makes a
 * lone unlabelled dropdown in a narrow panel half the width of the panel
 * holding it for the sake of a caption that is never drawn. The in-canvas
 * chrome's dbg_row_box_offset answers the same question the same way.
 */
static int
cs2_row_box_offset(char const* label)
{
    return label && label[0] ? CS2_LABEL_W : 0;
}

/**
 * The scrollbar, assembled the way ~script31 assembles it: track stretched
 * between two arrow buttons, the grip's middle stretched over its whole run
 * and a cap laid on each end. Falls back to the flat IF1 form (a dark track,
 * a grip with a highlight down its top-left and a shadow down its
 * bottom-right) for any piece whose sprite has not landed.
 *
 * The grip is drawn from ROW arithmetic -- content is `visible` rows, the view
 * is `drawn` rows, the offset is `scroll_row` -- because rows are this
 * executor's honest scroll unit (see scroll_row above).
 */
static void
cs2_push_scrollbar(
    struct ChromeCs2* s, int32_t panel, int x, int top, int bottom, int visible,
    int drawn, int scroll_row, int id_up, int id_down)
{
    int const track_y = top + CS2_SCROLL_W;
    int const track_h = bottom - top - 2 * CS2_SCROLL_W;
    int grip_h;
    int grip_y;
    int32_t box;

    if( track_h <= 0 || visible <= 0 || drawn <= 0 || visible <= drawn )
        return;

    grip_h = track_h * drawn / visible;
    if( grip_h < CS2_SCROLL_GRIP_MIN )
        grip_h = CS2_SCROLL_GRIP_MIN;
    if( grip_h > track_h )
        grip_h = track_h;
    grip_y = track_y + (track_h - grip_h) * scroll_row / (visible - drawn);

    if( cs2_sprite_ok(s, CS2_SPR_SCROLL_TRACK) )
        cs2_graphic(
            s, panel, -1, x, track_y, CS2_SCROLL_W, track_h, CS2_SPR_SCROLL_TRACK, 0);
    else
        cs2_rect(s, panel, -1, x, track_y, CS2_SCROLL_W, track_h, CS2_COL_SCROLL_TRACK, 1);

    if( cs2_sprite_ok(s, CS2_SPR_GRIP_MID) )
    {
        /* Middle over the WHOLE grip, caps on its ends -- ~script31's order,
         * which is what keeps a grip shorter than its own two caps looking
         * like a grip instead of two overlapping stubs. */
        cs2_graphic(s, panel, -1, x, grip_y, CS2_SCROLL_W, grip_h, CS2_SPR_GRIP_MID, 0);
        cs2_graphic(
            s, panel, -1, x, grip_y, CS2_SCROLL_W, CS2_SCROLL_CAP_H, CS2_SPR_GRIP_TOP, 0);
        cs2_graphic(
            s, panel, -1, x, grip_y + grip_h - CS2_SCROLL_CAP_H, CS2_SCROLL_W,
            CS2_SCROLL_CAP_H, CS2_SPR_GRIP_BOTTOM, 0);
    }
    else
    {
        cs2_rect(s, panel, -1, x, grip_y, CS2_SCROLL_W, grip_h, CS2_COL_SCROLL_GRIP, 1);
        cs2_rect(s, panel, -1, x, grip_y, 2, grip_h, CS2_COL_SCROLL_GRIP_HI, 1);
        cs2_rect(s, panel, -1, x, grip_y, CS2_SCROLL_W, 2, CS2_COL_SCROLL_GRIP_HI, 1);
        cs2_rect(
            s, panel, -1, x + CS2_SCROLL_W - 1, grip_y, 1, grip_h, CS2_COL_SCROLL_GRIP_LO, 1);
        cs2_rect(
            s, panel, -1, x, grip_y + grip_h - 1, CS2_SCROLL_W, 1, CS2_COL_SCROLL_GRIP_LO, 1);
    }

    /* The arrows are the affordance that scrolls (this toolkit has no drag),
     * so they carry the component ids whatever form the rest of the bar took.
     * WHICH ids is the caller's: the panel's own bar and an open dropdown's
     * can be on screen together, and one pair serving both would scroll
     * whichever the click handler guessed. */
    if( cs2_sprite_ok(s, CS2_SPR_SCROLL_UP) )
        box = cs2_graphic(
            s, panel, id_up, x, top, CS2_SCROLL_W, CS2_SCROLL_W,
            CS2_SPR_SCROLL_UP, 0);
    else
    {
        box = cs2_rect(
            s, panel, id_up, x, top, CS2_SCROLL_W, CS2_SCROLL_W,
            CS2_COL_SCROLL_GRIP, 1);
        cs2_text_box(
            s, panel, -1, x, top, CS2_SCROLL_W, CS2_SCROLL_W, "^",
            scroll_row > 0 ? CS2_COL_ACCENT : CS2_COL_DIM, 1);
    }
    if( box >= 0 )
        UITree_ApplyClickMask(s->tree, id_up, 1);

    if( cs2_sprite_ok(s, CS2_SPR_SCROLL_DOWN) )
        box = cs2_graphic(
            s, panel, id_down, x, bottom - CS2_SCROLL_W, CS2_SCROLL_W,
            CS2_SCROLL_W, CS2_SPR_SCROLL_DOWN, 0);
    else
    {
        box = cs2_rect(
            s, panel, id_down, x, bottom - CS2_SCROLL_W, CS2_SCROLL_W,
            CS2_SCROLL_W, CS2_COL_SCROLL_GRIP, 1);
        cs2_text_box(
            s, panel, -1, x, bottom - CS2_SCROLL_W, CS2_SCROLL_W, CS2_SCROLL_W, "v",
            scroll_row + drawn < visible ? CS2_COL_ACCENT : CS2_COL_DIM, 1);
    }
    if( box >= 0 )
        UITree_ApplyClickMask(s->tree, id_down, 1);
}

/**
 * One row of the open list: the band, which is also the row's hit box.
 *
 * script_9114's row is the list's own tile seen through a translucent black
 * rect, and the rects alternate between two transparencies so a long list
 * reads as rows rather than as one field of text. The band is the CLICKABLE
 * component -- a transparent hit rect over it would be a second component per
 * row for nothing, since the band already covers exactly the row.
 *
 * `over_color` is what gives the row a cursor: the same rect drawn in white
 * while the pointer is on it, at its own transparency, which lightens the row
 * the way the reference's thinner veil does. There is no hover state in this
 * executor to hold, and none is wanted -- the tree does it per frame.
 */
static void
cs2_push_drop_band(
    struct ChromeCs2* s, int32_t panel, int com_id, int x, int y, int w, int h, int trans)
{
    struct UIBuildComponent comp;

    memset(&comp, 0, sizeof(comp));
    comp.id = com_id;
    comp.type = UIBUILD_RECT;
    comp.parent_id = -1;
    comp.base_x = x;
    comp.base_y = y;
    comp.base_width = w;
    comp.base_height = h;
    comp.if3 = 1;
    comp.graphic = -1;
    comp.graphic_active = -1;
    comp.model_active_id = -1;
    comp.model_seq_id = -1;
    comp.color = CS2_COL_CHROME;
    comp.over_color = CS2_COL_TEXT;
    comp.filled = 1;
    comp.transparency = trans;
    if( UITree_PushBuildComponent(s->tree, panel, &comp, NULL, cs2_resolve_font, s) >= 0 &&
        com_id >= 0 )
        UITree_ApplyClickMask(s->tree, com_id, 1);
}

/*
 * The open dropdown's list.
 *
 * Built AFTER every row, not inside the row loop, for the one reason a popup
 * exists at all: children draw in the order they are pushed, so a list built
 * where its row is would be painted over by every row below it.
 *
 * It is the in-canvas chrome's list, in this presentation's vocabulary -- the
 * shared table's width (the button's), its 2px pad, its 20px rows -- so the
 * two presentations open the same list. What is NOT shared is the hover: there
 * it is a thinner veil drawn from the model's own hover row, here it is the
 * band's `over_color` and the tree finds the row itself.
 *
 * OPENS UPWARD when there is more room above than below. A list clipped to
 * three of its ten rows at the bottom of the strip is a control that stops
 * working the further down the panel it is, which is what a fixed downward
 * list would be here: the strip is short and a settings tab fills it.
 */
static void
cs2_push_dropdown_list(struct ChromeCs2* s, int32_t panel, int panel_h)
{
    int const handle = s->drop_open;
    int const pad = CS2_DROP_LIST_PAD;
    int count;
    int rows;
    int fits;
    int upward = 0;
    int room;
    int list_h;
    int list_y;
    int row_x;
    int row_w;
    int bar_w = 0;

    if( handle < 0 )
        return;
    /* The row is not on screen this build -- scrolled out of the panel, or on
     * a tab that is no longer up. A list hanging off a button that is not
     * there is a menu floating in the middle of the window. */
    if( s->drop_w <= 0 )
    {
        cs2_dropdown_close(s);
        return;
    }
    count = s->option_count[handle];
    if( count <= 0 )
    {
        /* Dirty as well as shut: the button above was drawn this pass with its
         * arrow pointing UP, and leaving that on screen over no list is the
         * one state a viewer cannot make sense of. */
        cs2_dropdown_close(s);
        s->dirty = 1;
        return;
    }

    rows = count < CS2_DROP_ROWS ? count : CS2_DROP_ROWS;
    {
        int const below = panel_h - CS2_PAD - (s->drop_y + CS2_ROW_H);
        int const above = s->drop_y - CS2_PAD;
        int const want = rows * CS2_DROP_ROW_H + 2 * pad;

        room = below;
        if( want > below && above > below )
        {
            upward = 1;
            room = above;
        }
    }
    fits = (room - 2 * pad) / CS2_DROP_ROW_H;
    if( fits < rows )
        rows = fits;
    /* Not one row's worth of room in either direction. Nothing is drawn and
     * the list shuts, rather than leaving an open state with no list under it
     * -- which would swallow the next click on the button. */
    if( rows < 1 )
    {
        cs2_dropdown_close(s);
        s->dirty = 1;
        return;
    }

    /* Clamped here rather than at the arrows, for the same reason the panel's
     * own offset is: the list can be restated shorter under a scrolled one. */
    if( s->drop_scroll > count - rows )
        s->drop_scroll = count - rows;
    if( s->drop_scroll < 0 )
        s->drop_scroll = 0;

    list_h = rows * CS2_DROP_ROW_H + 2 * pad;
    list_y = upward ? s->drop_y - list_h : s->drop_y + CS2_ROW_H;

    /* The body: the list's own lighter tile over the flat brown the tile's
     * transparent edges would otherwise show the panel through, edged in the
     * same frame the button wears -- the two are one control seen open. */
    cs2_rect(s, panel, -1, s->drop_x, list_y, s->drop_w, list_h, CS2_COL_BODY, 1);
    if( cs2_sprite_ok(s, CS2_SPR_LIST_TILE) )
        cs2_graphic(s, panel, -1, s->drop_x, list_y, s->drop_w, list_h, CS2_SPR_LIST_TILE, 1);
    cs2_rect(s, panel, -1, s->drop_x, list_y, s->drop_w, list_h, CS2_COL_FRAME, 0);
    cs2_rect(
        s, panel, -1, s->drop_x + 1, list_y + 1, s->drop_w - 2, list_h - 2,
        CS2_COL_FRAME_INSET, 0);

    /* The bar goes INSIDE the list, as the cache puts it: the rows lose the
     * width it takes rather than running under it. */
    if( count > rows && s->drop_w > 2 * pad + CS2_SCROLL_W )
        bar_w = CS2_SCROLL_W;
    row_x = s->drop_x + pad;
    row_w = s->drop_w - 2 * pad - bar_w;

    for( int row = 0; row < rows; row++ )
    {
        int const index = s->drop_scroll + row;
        int const y = list_y + pad + row * CS2_DROP_ROW_H;
        /* The bands alternate on the OPTION index, not on the screen row, so
         * scrolling slides the stripes with the text instead of leaving them
         * pinned to the window and strobing under it. */
        int const trans = (index & 1) ? CS2_TRANS_DROP_BAND_ALT : CS2_TRANS_DROP_BAND;

        if( index >= count )
            break;
        cs2_push_drop_band(
            s, panel, CS2_ID_DROP_ROW_BASE + row, row_x, y, row_w, CS2_DROP_ROW_H, trans);
        /* Every row in the same orange, the chosen one included: the row the
         * pointer is on is the only one the reference picks out, and it picks
         * it out by its band. The chosen option is already stated by the
         * button above the list. */
        cs2_text_box(
            s, panel, -1, row_x, y, row_w, CS2_DROP_ROW_H, cs2_option_text(s, handle, index),
            CS2_COL_LABEL, 1);
    }

    if( bar_w > 0 )
        cs2_push_scrollbar(
            s, panel, s->drop_x + s->drop_w - pad - bar_w, list_y + pad,
            list_y + list_h - pad, count, rows, s->drop_scroll, CS2_ID_DROP_UP,
            CS2_ID_DROP_DOWN);
}

/*
 * Is the panel we built still in the tree?
 *
 * It is not enough to remember the node index. The client rebuilds the whole
 * interface tree whenever the gameframe changes -- a resize, a revision's
 * onload, an interface opening -- and every node in it goes, this executor's
 * root included. Nothing tells us: the index stays in range and simply names
 * something else now, so the panel silently stops existing while the executor
 * believes it is up. Checking the COMPONENT ID rather than the index is what
 * catches that, because a rebuilt tree will not have put ours back.
 *
 * Same failure as cache-authored hooks being dropped by a rebuild: the fix is
 * to notice and re-declare, not to hold an index tighter.
 */
static int
cs2_panel_alive(struct ChromeCs2 const* s)
{
    if( !s->tree || s->panel_node < 0 )
        return 0;
    if( (uint32_t)s->panel_node >= s->tree->component_count )
        return 0;
    return s->tree->components[s->panel_node].component_id == TORIRS_CHROME_CS2_ID_BASE;
}

/*
 * Rebuild the whole panel.
 *
 * Wholesale, unlike every other executor: the tree has no cheap "move this one
 * component" for a panel whose row set just changed, and a rebuild of a
 * hundred components is a handful of microseconds that runs only when the
 * window's SHAPE moved. Property changes (a label, a checkbox) write into the
 * component that already exists and do not come through here at all -- which is
 * what keeps this off the per-frame path.
 */
static void
cs2_rebuild(struct ChromeCs2* s)
{
    int32_t panel;
    int y;
    int panel_w;
    int panel_h;
    int com = TORIRS_CHROME_CS2_ID_BASE;

    if( !s->tree || !ToriRSChrome_TreeAcceptsChrome(s->tree) )
        return;

    /* What could be drawn THIS build; a SYNC_END that sees the count grow
     * rebuilds with the pieces that arrived. Asked before anything draws, so
     * the count and the build agree. */
    s->assets_ok = cs2_assets_avail(s);

    /* The mount by COMPONENT ID, not the index bind handed over: the client
     * rebuilds the whole tree whenever the gameframe changes and every index
     * in it renumbers -- the same reason cs2_panel_alive checks the id. Not
     * back yet (mid-rebuild) means build nothing this frame; SYNC_END retries. */
    if( s->mount_com_id )
    {
        s->mount = UITree_FindByComponentId(s->tree, s->mount_com_id);
        if( s->mount < 0 )
        {
            if( getenv("TORIRS_CHROME_DEBUG") )
                fprintf(stderr, "chrome-cs2: mount %x not in tree, waiting\n", s->mount_com_id);
            return;
        }
    }

    if( cs2_panel_alive(s) )
        UITree_ClearChildren(s->tree, s->panel_node);
    else
    {
        /* Gone with a tree rebuild, or never made. Either way the index we
         * hold means nothing now, so a fresh one rather than a clear. */
        struct UIBuildComponent layer;
        s->panel_node = -1;
        memset(&layer, 0, sizeof(layer));
        layer.id = com++;
        layer.type = UIBUILD_LAYER;
        layer.parent_id = -1;
        layer.if3 = 1;
        layer.graphic = -1;
        layer.graphic_active = -1;
        layer.model_active_id = -1;
        layer.model_seq_id = -1;
        if( s->mount >= 0 )
        {
            /*
             * Pure fill-parent, exactly as xptracker, loottools and hiscores
             * are authored: their roots carry no chrome because the strip's
             * nine-slice frame is a SIBLING under popout:frame. A panel that
             * placed and sized itself here would sit inside that frame at its
             * own size, which is a box in a box.
             */
            layer.x_mode = 1;
            layer.y_mode = 1;
            layer.width_mode = 1;
            layer.height_mode = 1;
        }
        else
        {
            layer.base_x = 8;
            layer.base_y = 8;
            layer.base_width = CS2_PANEL_W;
            layer.base_height = CS2_PANEL_H;
        }
        s->panel_node = UITree_PushBuildComponent(
            s->tree, s->mount, &layer, NULL, cs2_resolve_font, s);
        if( s->panel_node < 0 )
            return;
    }
    panel = s->panel_node;

    /*
     * The panel's own box.
     *
     * Mounted in the strip we take the slot's resolved size and draw NO body
     * and NO border: popout:frame already draws both around us, and a second
     * set inside it is the box-in-a-box the fill-parent modes above exist to
     * avoid. Standalone we are the whole window and have to draw them.
     */
    if( s->mount >= 0 )
    {
        /* The MOUNT's resolved box (abs_w/abs_h -- what layout computed), not
         * our layer's spec: a fill-parent layer's spec width is 0, which read
         * as "no size" here and pinned the panel at the fallback box no matter
         * how large the slot it filled was. */
        struct UITreeComponent const* box = &s->tree->components[s->mount];
        panel_w = box->position.abs_w > 0 ? box->position.abs_w : CS2_PANEL_W;
        panel_h = box->position.abs_h > 0 ? box->position.abs_h : CS2_PANEL_H;
    }
    else
    {
        panel_w = CS2_PANEL_W;
        panel_h = CS2_PANEL_H;
        /* The window panel the in-canvas chrome draws: body brown under the
         * tiled tradebacking, edged in the interfaces' own nine-slice frame --
         * the same border ToriRSChrome_PanelSetFramed puts on the in-canvas
         * one, so the standalone window is the same window either way. The
         * flat brown stays under the tile for the same reason the field box
         * keeps its fill: the tile's edges are transparent.
         *
         * A build that baked no frame falls back to the plain black outline,
         * which is what every interface edge in this game is anyway. */
        cs2_rect(s, panel, com++, 0, 0, panel_w, panel_h, CS2_COL_BODY, 1);
        if( cs2_sprite_ok(s, CS2_SPR_FIELD_TILE) )
            cs2_graphic(s, panel, com++, 0, 0, panel_w, panel_h, CS2_SPR_FIELD_TILE, 1);
        if( cs2_sprite_ok(s, TORIRS_CHROME_SKIN_FRAME_TOP_LEFT) )
            cs2_push_frame(s, panel, &com, panel_w, panel_h);
        else
            cs2_rect(s, panel, com++, 0, 0, panel_w, panel_h, CS2_COL_CHROME, 0);
    }
    s->built_w = panel_w;
    s->built_h = panel_h;
    if( getenv("TORIRS_CHROME_DEBUG") )
        fprintf(
            stderr, "chrome-cs2: rebuild mount=%d box=%dx%d assets=%d\n", s->mount, panel_w,
            panel_h, s->assets_ok);

    /* The tab strip, pinned above the rows -- the same rule the in-canvas
     * chrome follows, and for the same reason. Its look is the in-canvas
     * strip's: an unselected tab is the body seen through a veil, the selected
     * one is the body itself with a gap in the base rule joining it to the
     * content below -- which is the whole of what makes a strip read as tabs
     * rather than as a row of buttons. */
    y = CS2_PAD;

    /*
     * The way out, before anything else claims the top row.
     *
     * The interfaces' own window X -- the same slot the in-canvas chrome draws,
     * because this is the same window by another means (see dbg_build_window,
     * and torirs_chrome_metrics.h on why the two presentations may not each
     * carry their own answer).
     *
     * ONE button. There was an Ok beside it wearing the checkbox tick, which
     * committed the page's Save row on the way out; both it and the confirm
     * intent behind it are gone. A green tick is the game's answer to a
     * question, not a way out of a window, and this presentation already draws
     * Save as a labelled row a few pixels below.
     */
    {
        int const btn_y = y;
        int const close_x = panel_w - CS2_PAD - CS2_BTN;
        int32_t hit;

        if( s->skin_scene_id > 0 )
            cs2_graphic(
                s, panel, -1, close_x, btn_y, CS2_BTN, CS2_BTN, TORIRS_CHROME_SKIN_CLOSE, 0);
        else
        {
            cs2_rect(s, panel, -1, close_x, btn_y, CS2_BTN, CS2_BTN, CS2_COL_FIELD_BG, 1);
            cs2_rect(
                s, panel, -1, close_x + 2, btn_y + 2, CS2_BTN - 4, CS2_BTN - 4,
                CS2_COL_ACCENT, 1);
        }
        /* The click target over the art, not the art itself: a graphic
         * component sized to the hit box would stretch the sprite -- the
         * same trade the checkboxes make. */
        hit = cs2_rect_trans(
            s, panel, CS2_ID_CLOSE, close_x, btn_y, CS2_BTN, CS2_BTN, CS2_COL_FIELD_BG, 1,
            255);
        if( hit >= 0 )
            UITree_ApplyClickMask(s->tree, CS2_ID_CLOSE, 1);

        /* The rows start below it, so the strip and the first row cannot be
         * drawn under the button. */
        y += CS2_BTN + CS2_ROW_GAP;
    }

    if( s->tab_count > 1 )
    {
        int const base_y = y + CS2_TAB_H - 1;
        int const avail = panel_w - 2 * CS2_PAD;
        /*
         * Tab edges from a PREFIX SUM of caption-proportional widths, the
         * in-canvas strip's own layout: widths computed independently and then
         * scaled accumulate one rounding error per tab, where edges from a
         * common prefix always meet. Proportional (approximating the p12
         * advance as CS2_TAB_CHAR_W per character) rather than equal shares,
         * so "lua" does not get the room "entity-highlighter" needs.
         *
         * Compressed to fit rather than clipped when they cannot all have
         * their natural width -- a tab scrolled off the end of its own strip
         * cannot be reached, where one squeezed to its first letters still can
         * -- and each caption is TRUNCATED to its own box, because a TEXT
         * component centres its full string over the box edges and eight
         * captions printed over one another was the result.
         */
        int natural[TORIRS_CHROME_CS2_TABS_MAX + 1];
        int total = 0;

        natural[0] = 0;
        for( int t = 0; t < s->tab_count; t++ )
        {
            total += 2 * CS2_TAB_PAD_X + CS2_TAB_CHAR_W * (int)strlen(s->tabs[t]);
            natural[t + 1] = total;
        }

        for( int t = 0; t < s->tab_count; t++ )
        {
            int x0 = natural[t];
            int x1 = natural[t + 1];
            int x;
            int tab_w;
            int on;
            int32_t box;
            char cap[48];
            int max_chars;

            if( total > avail && total > 0 )
            {
                x0 = x0 * avail / total;
                x1 = x1 * avail / total;
            }
            x = CS2_PAD + x0;
            tab_w = x1 - x0;
            if( tab_w < 2 * CS2_TAB_PAD_X )
                tab_w = 2 * CS2_TAB_PAD_X;
            on = (t == s->mirror.panels[s->tab_panel].active_tab);

            if( on )
                /* The base rule stops at this tab's edges; a bare frame with
                 * an open bottom is the joint. The id rides the frame -- an
                 * outlined rect's box is still its hit box. */
                box = cs2_rect(
                    s, panel, TORIRS_CHROME_CS2_ID_TAB_BASE + t, x, y, tab_w,
                    CS2_TAB_H, CS2_COL_CHROME, 0);
            else
            {
                box = cs2_rect_trans(
                    s, panel, TORIRS_CHROME_CS2_ID_TAB_BASE + t, x, y, tab_w,
                    CS2_TAB_H, CS2_COL_CHROME, 1, CS2_TRANS_TAB);
                cs2_rect(s, panel, -1, x, y, tab_w, 1, CS2_COL_CHROME, 1);
                cs2_rect(s, panel, -1, x, y, 1, CS2_TAB_H, CS2_COL_CHROME, 1);
                if( t == s->tab_count - 1 )
                    cs2_rect(
                        s, panel, -1, x + tab_w - 1, y, 1, CS2_TAB_H, CS2_COL_CHROME, 1);
                cs2_rect(s, panel, -1, x, base_y, tab_w, 1, CS2_COL_CHROME, 1);
            }
            /* By COMPONENT ID, not node index: ApplyClickMask looks the
             * component up, and handing it an index silently masks whatever
             * component happens to carry that number -- or nothing at all,
             * which is a control that draws and cannot be clicked. */
            if( box >= 0 )
                UITree_ApplyClickMask(s->tree, TORIRS_CHROME_CS2_ID_TAB_BASE + t, 1);

            snprintf(cap, sizeof(cap), "%s", s->tabs[t]);
            max_chars = (tab_w - 2) / CS2_TAB_CHAR_W;
            if( max_chars < 1 )
                max_chars = 1;
            if( max_chars < (int)strlen(cap) )
                cap[max_chars] = '\0';
            cs2_text_box(
                s, panel, -1, x + 1, y, tab_w - 2, CS2_TAB_H, cap,
                on ? CS2_COL_TEXT : CS2_COL_LABEL, 1);
        }
        /* The rule continues to the panel's edge past the last tab. */
        if( total < avail )
            cs2_rect(
                s, panel, -1, CS2_PAD + total, base_y, avail - total, 1, CS2_COL_CHROME, 1);
        y += CS2_TAB_H + CS2_ROW_GAP;
    }

    /* In ROW order, not handle order: the free list recycles handles, so a
     * rebuilt panel walked by index puts Save above the settings it commits. */
    {
    int order[TORIRS_CHROME_MAX_WIDGETS];
    int const shown_count = ToriRSChromeMirror_Order(&s->mirror, order, TORIRS_CHROME_MAX_WIDGETS);
    /*
     * The scroll column is reserved up front, not once an overflow is found.
     *
     * A column that appeared only when rows overflowed would reflow every row
     * the moment one was added, and a settings tab that jumps sideways as it
     * fills reads as a bug rather than as a scrollbar arriving.
     */
    int const row_w = panel_w - 2 * CS2_PAD - CS2_SCROLL_W;
    int visible = 0;
    int drawn = 0;
    int skipped = 0;
    int extra = 0;

    /* Zeroed before the walk, so a list whose button is NOT drawn this build
     * sees a zero-width box and shuts itself rather than hanging off wherever
     * the button was last time. */
    s->drop_w = 0;

    for( int oi = 0; oi < shown_count; oi++ )
    {
        int const i = order[oi];
        if( i == s->tab_strip_widget )
            continue;
        if( ToriRSChromeMirror_Shown(&s->mirror, i) )
            visible++;
    }
    /* Clamped here rather than at the arrows: a tab switch or a reload can
     * shorten the list under a scroll offset, and an offset past the end shows
     * an empty panel with no way back. */
    if( s->scroll_row > visible - 1 )
        s->scroll_row = visible - 1;
    if( s->scroll_row < 0 )
        s->scroll_row = 0;

    for( int oi = 0; oi < shown_count; oi++ )
    {
        int const i = order[oi];
        struct ToriRSChromeMirrorWidget* w = ToriRSChromeMirror_Widget(&s->mirror, i);
        int const id = CS2_ID_WIDGET_BASE + i;

        if( !w || !ToriRSChromeMirror_Shown(&s->mirror, i) )
            continue;
        if( i == s->tab_strip_widget )
            continue;
        /* Scrolled past: skipped, and NOT counted as placed. */
        if( skipped < s->scroll_row )
        {
            skipped++;
            continue;
        }
        /* Out of room. The break comes BEFORE the count, so the row that did
         * not fit is not counted as one that did -- counting it made
         * `placed + skipped == visible` and the arrows never appeared. */
        if( y + CS2_ROW_H > panel_h - CS2_PAD )
            break;
        drawn++;

        /* Rows are one CS2_ROW_H tall except when one grows its own furniture
         * -- an open colour picker's axis bars. Carried as an addend rather
         * than by making the popup a row of its own, because the model has no
         * such row: it is the same widget, taller while it is open. */
        extra = 0;

        switch( w->kind )
        {
        case TORIRS_CHROME_W_CHECKBOX:
        {
            /*
             * The interfaces' own on/off pair: green tick, red cross, 17x17.
             *
             * There is no drawn checkbox anywhere in this game to imitate --
             * every boolean setting in every panel is one of these two
             * sprites, so a box with a mark in it reads as foreign no matter
             * how carefully it is coloured. Baked, so it costs no load.
             *
             * The clickable component is a transparent rect over the sprite
             * rather than the sprite itself: the hit box wants the row's
             * height, and a graphic component sized to the row would stretch
             * the art to match.
             */
            int const box_y = y + (CS2_ROW_H - CS2_BOX) / 2;
            int const mark =
                s->checked[i] ? TORIRS_CHROME_SKIN_CHECK_ON : TORIRS_CHROME_SKIN_CHECK_OFF;
            int32_t box;

            if( s->skin_scene_id > 0 )
                cs2_graphic(s, panel, -1, CS2_PAD, box_y, CS2_BOX, CS2_BOX, mark, 0);
            else
            {
                cs2_rect(
                    s, panel, -1, CS2_PAD, box_y, CS2_BOX, CS2_BOX, CS2_COL_FIELD_BG, 1);
                cs2_rect(
                    s, panel, -1, CS2_PAD, box_y, CS2_BOX, CS2_BOX, CS2_COL_FRAME_INSET, 0);
                if( s->checked[i] )
                    cs2_rect(
                        s, panel, -1, CS2_PAD + 3, box_y + 3, CS2_BOX - 6, CS2_BOX - 6,
                        CS2_COL_ON, 1);
            }
            box = cs2_rect_trans(
                s, panel, id, CS2_PAD, box_y, CS2_BOX, CS2_BOX, CS2_COL_FIELD_BG, 1, 255);
            if( box >= 0 )
                UITree_ApplyClickMask(s->tree, id, 1);
            cs2_text(
                s, panel, -1, CS2_PAD + CS2_BOX + TORIRS_CHROME_M_CHECK_GAP, y,
                row_w - CS2_BOX - TORIRS_CHROME_M_CHECK_GAP, s->label[i],
                s->wcolor[i] ? (int)s->wcolor[i] : CS2_COL_TEXT);
            break;
        }

        case TORIRS_CHROME_W_LISTROW:
        {
            /* The roster row, in game chrome: the name at the left, a settings
             * affordance and a switch pinned to the right so a column of them
             * lines up. Two component ids -- see CS2_ID_ACTION_BASE. */
            int const tog_w = TORIRS_CHROME_M_TOGGLE_W;
            int const tog_h = TORIRS_CHROME_M_TOGGLE_H;
            int const icon = TORIRS_CHROME_M_ROW_ICON;
            int const tog_x = CS2_PAD + row_w - tog_w;
            int const tog_y = y + (CS2_ROW_H - tog_h) / 2;
            int const icon_x = tog_x - TORIRS_CHROME_M_ROW_ICON_GAP - icon;
            int const icon_y = y + (CS2_ROW_H - icon) / 2;
            int const name_w =
                (s->wrow_action[i] ? icon_x : tog_x) - CS2_PAD - TORIRS_CHROME_M_ROW_NAME_GAP;
            int32_t box;

            cs2_text(
                s, panel, -1, CS2_PAD, y, name_w > 0 ? name_w : 1, s->label[i],
                s->wcolor[i] ? (int)s->wcolor[i] : CS2_COL_TEXT);

            if( s->wrow_action[i] )
            {
                int const aid = CS2_ID_ACTION_BASE + i;
                cs2_field_chrome(s, panel, aid, icon_x, icon_y, icon, icon);
                for( int d = 0; d < 3; d++ )
                    cs2_rect(
                        s, panel, -1,
                        icon_x + TORIRS_CHROME_M_DOT_INSET + d * TORIRS_CHROME_M_DOT_PITCH,
                        icon_y + icon / 2 - 1, TORIRS_CHROME_M_DOT, TORIRS_CHROME_M_DOT,
                        CS2_COL_LABEL, 1);
            }

            /* The same tick/cross a checkbox wears -- see the note there. A
             * sliding switch is an idiom this game does not have. */
            if( s->skin_scene_id > 0 )
            {
                int const tog_mark =
                    s->checked[i] ? TORIRS_CHROME_SKIN_CHECK_ON : TORIRS_CHROME_SKIN_CHECK_OFF;
                cs2_graphic(
                    s, panel, -1, tog_x + tog_w - CS2_BOX, y + (CS2_ROW_H - CS2_BOX) / 2,
                    CS2_BOX, CS2_BOX, tog_mark, 0);
            }
            else
            {
                cs2_rect(
                    s, panel, -1, tog_x, tog_y, tog_w, tog_h, CS2_COL_FIELD_BG, 1);
                cs2_rect(s, panel, -1, tog_x, tog_y, tog_w, tog_h, CS2_COL_FRAME_INSET, 0);
                cs2_rect(
                    s, panel, -1,
                    s->checked[i] ? tog_x + tog_w - tog_h + 1 : tog_x + 1, tog_y + 1,
                    tog_h - 2, tog_h - 2,
                    s->checked[i] ? CS2_COL_ON : CS2_COL_SCROLL_GRIP, 1);
            }
            box = cs2_rect_trans(
                s, panel, id, tog_x, tog_y, tog_w, tog_h, CS2_COL_FIELD_BG, 1, 255);
            if( box >= 0 )
                UITree_ApplyClickMask(s->tree, id, 1);
            break;
        }

        case TORIRS_CHROME_W_TEXTINPUT:
        case TORIRS_CHROME_W_DROPDOWN:
        {
            int const bx = CS2_PAD + cs2_row_box_offset(s->label[i]);
            int const bw = CS2_PAD + row_w - bx;

            /* script_3850's row: the label in the settings orange, the value
             * in its field box. */
            cs2_text(s, panel, -1, CS2_PAD, y, CS2_LABEL_W, s->label[i], CS2_COL_LABEL);
            cs2_field_chrome(s, panel, id, bx, y, bw, CS2_ROW_H);
            if( w->kind == TORIRS_CHROME_W_DROPDOWN )
            {
                /* The closed button: the arrow on the RIGHT (the scrollbar's
                 * own down-arrow sprite, exactly as the reference reuses it),
                 * the value centred in what is left, set in the same orange as
                 * the label. script_3850 places it with x-mode 2, which
                 * measures from the far edge -- see the note on the in-canvas
                 * chrome's dbg_push_dropdown_button. */
                int const arrow = TORIRS_CHROME_M_DROP_ARROW;
                int const open_here = s->drop_open == i;
                /* Down while the list is shut, up while it is open -- which in
                 * the reference is literally the same two sprites the
                 * scrollbar's ends wear, so the skin carries one pair. */
                int const slot = open_here ? CS2_SPR_SCROLL_UP : CS2_SPR_SCROLL_DOWN;
                int text_x = bx + TORIRS_CHROME_M_FIELD_INSET;
                int text_w = bw - 2 * TORIRS_CHROME_M_FIELD_INSET;
                if( cs2_sprite_ok(s, slot) )
                {
                    cs2_graphic(
                        s, panel, -1, bx + bw - TORIRS_CHROME_M_FIELD_INSET - arrow,
                        y + TORIRS_CHROME_M_FIELD_INSET, arrow, arrow, slot, 0);
                    text_w -= arrow;
                }
                /* Where the list hangs from, for the pass that builds it after
                 * every row. Recorded rather than rebuilt from the widget
                 * later, because `y` here is the scroll's answer and nothing
                 * outside this loop can work it out again. */
                if( open_here )
                {
                    s->drop_x = bx;
                    s->drop_y = y;
                    s->drop_w = bw;
                }
                cs2_text_box(
                    s, panel, -1, text_x, y, text_w, CS2_ROW_H, s->text[i],
                    CS2_COL_LABEL, 1);
            }
            else
                cs2_text(
                    s, panel, -1, bx + TORIRS_CHROME_M_FIELD_PAD_X, y,
                    bw - 2 * TORIRS_CHROME_M_FIELD_PAD_X, s->text[i], CS2_COL_TEXT);
            if( s->focused[i] )
                cs2_rect(s, panel, -1, bx + 1, y + 1, bw - 2, CS2_ROW_H - 2, CS2_COL_ACCENT, 0);
            break;
        }

        case TORIRS_CHROME_W_COLORPICK:
        {
            /*
             * The same labelled field a text row wears, with a swatch inside
             * its box at the left -- and, when the model says the picker is
             * open, the three HSL16 axis bars laid out underneath it.
             *
             * The bars are BUILT HERE rather than mirrored from the model's
             * own popup: that popup is prims, at the in-canvas window's
             * floating position, and this presentation is neither. What
             * crosses the seam is the fact that it is open (WIDGET_CHECKED),
             * which is all a presentation needs to draw its own.
             */
            int const bx = CS2_PAD + cs2_row_box_offset(s->label[i]);
            int const bw = CS2_PAD + row_w - bx;
            int const sw_x = bx + 3;
            int const sw_y = y + (CS2_ROW_H - CS2_SWATCH) / 2;
            int32_t swatch;

            cs2_text(s, panel, -1, CS2_PAD, y, CS2_LABEL_W, s->label[i], CS2_COL_LABEL);
            cs2_field_chrome(s, panel, id, bx, y, bw, CS2_ROW_H);
            cs2_text(
                s, panel, -1, sw_x + CS2_SWATCH + TORIRS_CHROME_M_SWATCH_GAP, y,
                bx + bw - TORIRS_CHROME_M_FIELD_PAD_X -
                    (sw_x + CS2_SWATCH + TORIRS_CHROME_M_SWATCH_GAP),
                s->text[i], CS2_COL_TEXT);
            if( s->focused[i] )
                cs2_rect(s, panel, -1, bx + 1, y + 1, bw - 2, CS2_ROW_H - 2, CS2_COL_ACCENT, 0);

            /* The sample itself, then the click target OVER it: a graphic or a
             * fill that also carries the id would have to be one node doing
             * two jobs, and the transparent rect on top is how every other
             * two-layer control in this file takes its clicks. */
            cs2_rect(
                s, panel, -1, sw_x, sw_y, CS2_SWATCH, CS2_SWATCH,
                (int)ToriRSChrome_Hsl16ToRgb(s->selected[i]), 1);
            cs2_rect(
                s, panel, -1, sw_x, sw_y, CS2_SWATCH, CS2_SWATCH,
                s->checked[i] ? CS2_COL_ACCENT : CS2_COL_FRAME_INSET, 0);
            swatch = cs2_rect_trans(
                s, panel, CS2_ID_SWATCH_BASE + i, sw_x, sw_y, CS2_SWATCH, CS2_SWATCH,
                CS2_COL_FIELD_BG, 1, 255);
            if( swatch >= 0 )
                UITree_ApplyClickMask(s->tree, CS2_ID_SWATCH_BASE + i, 1);

            if( s->checked[i] )
            {
                int const bar_x = bx + TORIRS_CHROME_M_COLORBAR_INSET;
                int const bar_w = bw - 2 * TORIRS_CHROME_M_COLORBAR_INSET;
                int by = y + CS2_ROW_H + CS2_ROW_GAP;

                for( int bar = 0; bar < CS2_COLOR_BARS; bar++ )
                {
                    int const steps = cs2_colorbar_steps(bar);
                    int const chosen = cs2_colorbar_value(s->selected[i], bar);
                    int const mark_cell = steps > 0 ? chosen * CS2_COLOR_CELLS / steps : 0;

                    for( int c = 0; c < CS2_COLOR_CELLS; c++ )
                    {
                        int const x0 = bar_x + bar_w * c / CS2_COLOR_CELLS;
                        int const x1 = bar_x + bar_w * (c + 1) / CS2_COLOR_CELLS;
                        int const value = steps * c / CS2_COLOR_CELLS;
                        int const cell_id =
                            CS2_ID_CELL_BASE + bar * CS2_COLOR_CELLS + c;
                        int32_t hit;

                        if( x1 <= x0 )
                            continue;
                        cs2_rect(
                            s, panel, -1, x0, by, x1 - x0, CS2_COLORBAR_H,
                            (int)ToriRSChrome_Hsl16ToRgb(
                                cs2_colorbar_with(s->selected[i], bar, value)),
                            1);
                        if( c == mark_cell )
                            cs2_rect(
                                s, panel, -1, x0, by, x1 - x0, CS2_COLORBAR_H,
                                CS2_COL_ACCENT, 0);
                        hit = cs2_rect_trans(
                            s, panel, cell_id, x0, by, x1 - x0, CS2_COLORBAR_H,
                            CS2_COL_FIELD_BG, 1, 255);
                        if( hit >= 0 )
                            UITree_ApplyClickMask(s->tree, cell_id, 1);
                    }
                    by += CS2_COLORBAR_H + CS2_COLORBAR_GAP;
                }
                /* The bars are part of this row as far as the layout is
                 * concerned, so the row that follows starts below them rather
                 * than under them. */
                extra = CS2_COLOR_BARS * (CS2_COLORBAR_H + CS2_COLORBAR_GAP);
            }
            break;
        }

        case TORIRS_CHROME_W_BUTTON:
        case TORIRS_CHROME_W_MENUITEM:
        {
            /* A button wears the same field box a setting does, its caption
             * centred -- which is how the reference draws a pressable row
             * (script_3850's own Save is exactly this shape). */
            char const* caption = s->text[i][0] ? s->text[i] : s->label[i];
            int const bw = CS2_LABEL_W;
            cs2_field_chrome(s, panel, id, CS2_PAD, y, bw, CS2_ROW_H);
            cs2_text_box(
                s, panel, -1, CS2_PAD + TORIRS_CHROME_M_FIELD_INSET, y,
                bw - 2 * TORIRS_CHROME_M_FIELD_INSET, CS2_ROW_H, caption, CS2_COL_TEXT, 1);
            break;
        }

        case TORIRS_CHROME_W_SEPARATOR:
            cs2_rect(
                s, panel, -1, CS2_PAD, y + CS2_ROW_H / 2, row_w, 1, CS2_COL_CHROME, 1);
            break;

        case TORIRS_CHROME_W_LABEL:
        default:
            cs2_text(
                s, panel, -1, CS2_PAD, y, row_w,
                s->text[i][0] ? s->text[i] : s->label[i],
                s->wcolor[i] ? (int)s->wcolor[i] : CS2_COL_TEXT);
            break;
        }
        y += CS2_ROW_H + CS2_ROW_GAP + extra;
    }

    /*
     * The bar looks like ~script31's -- track, grip, arrow sprites -- but only
     * the ARROWS take a click: a grip drag is a press held across frames, which
     * this toolkit has no way to carry. The grip still moves, because it is
     * drawn from scroll_row, so the bar reads (and scrolls) like the game's.
     */
    if( s->scroll_row > 0 || skipped + drawn < visible )
        cs2_push_scrollbar(
            s,
            panel,
            panel_w - CS2_PAD - CS2_SCROLL_W,
            CS2_PAD + (s->tab_count > 1 ? CS2_TAB_H + CS2_ROW_GAP : 0),
            panel_h - CS2_PAD,
            visible,
            drawn,
            s->scroll_row,
            CS2_ID_SCROLL_UP,
            CS2_ID_SCROLL_DOWN);

    /* Last, so it is over every row and over the panel's own bar: this is the
     * whole of what makes a popup a popup here. */
    cs2_push_dropdown_list(s, panel, panel_h);
    }

    UITree_MarkAllDirty(s->tree);
}

/* ---- the executor -------------------------------------------------------- */

static int
chrome_cs2_begin(void* user)
{
    struct ChromeCs2* s = user;

    assert(s);
    if( !s->tree )
    {
        fprintf(stderr, "chrome: no interface tree to build the plugin window in\n");
        return 0;
    }
    ToriRSChromeMirror_Init(&s->mirror);
    s->panel_node = -1;
    cs2_dropdown_close(s);
    s->tab_panel = -1;
    s->tab_strip_widget = -1;
    s->tab_count = 0;
    s->open = 1;
    return 1;
}

static void
chrome_cs2_end(void* user)
{
    struct ChromeCs2* s = user;

    assert(s);
    if( !s->open )
        return;
    if( s->tree && s->panel_node >= 0 )
    {
        UITree_ClearChildren(s->tree, s->panel_node);
        UITree_MarkAllDirty(s->tree);
    }
    s->panel_node = -1;
    cs2_dropdown_close(s);
    /* The strings were held only so a list could draw them; the window closing
     * is the end of that, and a palette's worth of them is not memory to sit
     * on until the next open restates it. */
    cs2_options_free_all(s);
    s->open = 0;
}

static void
chrome_cs2_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct ChromeCs2* s = user;
    int shape;

    assert(s);
    assert(cmd);
    if( !s->open )
        return;

    shape = ToriRSChromeMirror_Apply(&s->mirror, cmd);

    switch( cmd->kind )
    {
    case TORIRS_CHROME_CMD_SYNC_END:
        /* One rebuild per frame at most, and only when something moved -- or
         * when the tree threw our panel away, or a furniture asset whose load
         * an earlier build requested has landed since, or the slot the panel
         * fills was resized under it (the strip's layout pass runs AFTER the
         * first build, so the first build is always at the fallback box). A
         * rebuild per command would tear the panel down and put it back up
         * several times for a single tab switch. */
        {
            int resized = 0;
            if( !s->dirty && s->mount >= 0 &&
                (uint32_t)s->mount < s->tree->component_count && cs2_panel_alive(s) )
            {
                struct UITreeComponent const* box = &s->tree->components[s->mount];
                if( (box->position.abs_w > 0 && box->position.abs_w != s->built_w) ||
                    (box->position.abs_h > 0 && box->position.abs_h != s->built_h) )
                    resized = 1;
            }
            if( s->dirty || resized || !cs2_panel_alive(s) ||
                (s->panel_node >= 0 && cs2_assets_avail(s) > s->assets_ok) )
            {
                cs2_rebuild(s);
                s->dirty = 0;
            }
        }
        return;

    case TORIRS_CHROME_CMD_WIDGET_ADD:
        if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
        {
            snprintf(
                s->label[cmd->widget], sizeof(s->label[0]), "%s", cmd->label);
            snprintf(s->text[cmd->widget], sizeof(s->text[0]), "%s", cmd->text);
            s->checked[cmd->widget] = 0;
            s->wcolor[cmd->widget] = cmd->color;
            s->selected[cmd->widget] = -1;
            /* Handles come off a free list, so this slot may be a widget that
             * has just gone away -- its options with it. */
            cs2_options_clear(s, cmd->widget);
            if( s->drop_open == cmd->widget )
                cs2_dropdown_close(s);
            /* The ADD is the one command carrying a widget's shape; `w` is a
             * LISTROW's action affordance. */
            s->wrow_action[cmd->widget] = cmd->w;
        }
        if( cmd->value == TORIRS_CHROME_W_TABSTRIP )
        {
            s->tab_panel = cmd->panel;
            s->tab_strip_widget = cmd->widget;
        }
        break;

    /* Both string changes rebuild: the tree OWNS its text (the builder strdups
     * it), so a rewrite of this executor's copy alone changes nothing on
     * screen. Typing pays one rebuild per keystroke, which is microseconds. */
    case TORIRS_CHROME_CMD_WIDGET_LABEL:
        if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
            snprintf(s->label[cmd->widget], sizeof(s->label[0]), "%s", cmd->label);
        s->dirty = 1;
        break;

    case TORIRS_CHROME_CMD_WIDGET_TEXT:
        if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
            snprintf(s->text[cmd->widget], sizeof(s->text[0]), "%s", cmd->text);
        s->dirty = 1;
        break;

    case TORIRS_CHROME_CMD_WIDGET_CHECKED:
        if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
        {
            struct ToriRSChromeMirrorWidget const* mw =
                ToriRSChromeMirror_Widget(&s->mirror, cmd->widget);
            s->checked[cmd->widget] = cmd->value;
            /* A COLORPICK's `checked` IS "the axis popup is open" -- see the
             * kind's own note. The click handler set this optimistically so the
             * cells it just built are addressable in the same frame; this is
             * the authoritative statement, and it is also what closes the popup
             * when something OTHER than the swatch did (a click elsewhere, a
             * tab switch, the row going away). */
            if( mw && mw->kind == TORIRS_CHROME_W_COLORPICK )
            {
                if( cmd->value )
                    s->colorpick_open = cmd->widget;
                else if( s->colorpick_open == cmd->widget )
                    s->colorpick_open = -1;
            }
        }
        /* The tick is its own component, so this one DOES change the shape. */
        s->dirty = 1;
        break;

    case TORIRS_CHROME_CMD_WIDGET_COLOR:
        if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
            s->wcolor[cmd->widget] = cmd->color;
        /* Colour is baked into the TEXT component at build time, so a change
         * needs the rebuild; rare (a fault note appearing) and cheap. */
        s->dirty = 1;
        break;

    case TORIRS_CHROME_CMD_WIDGET_SELECTED:
        /* A dropdown's shown value: the command carries the chosen option's
         * text beside its index, exactly so this executor does not need its
         * own copy of the list. DROPDOWNS ONLY: the sync restates a selection
         * for every widget it announces (the shadow is seeded to force it),
         * so a button or a text input hears one too -- with empty text, which
         * wrote over every caption and value in the window. */
        if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS &&
            cmd->widget != s->tab_strip_widget )
        {
            struct ToriRSChromeMirrorWidget const* mw =
                ToriRSChromeMirror_Widget(&s->mirror, cmd->widget);
            if( mw && mw->kind == TORIRS_CHROME_W_DROPDOWN )
            {
                s->selected[cmd->widget] = cmd->value;
                snprintf(s->text[cmd->widget], sizeof(s->text[0]), "%s", cmd->text);
                s->dirty = 1;
            }
            /* A COLORPICK's selection is its packed HSL16, and the swatch is
             * drawn from it -- so this one takes the value and leaves the text
             * alone, which the WIDGET_TEXT beside it already carries as the
             * hex the field shows. */
            else if( mw && mw->kind == TORIRS_CHROME_W_COLORPICK )
            {
                s->selected[cmd->widget] = cmd->value;
                s->dirty = 1;
            }
        }
        break;

    case TORIRS_CHROME_CMD_WIDGET_FOCUS:
        if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
            s->focused[cmd->widget] = cmd->value;
        /* The focus ring is a component, so this changes the shape. */
        s->dirty = 1;
        break;

    case TORIRS_CHROME_CMD_WIDGET_OPTION:
        if( cmd->widget == s->tab_strip_widget && cmd->value >= 0 &&
            cmd->value < TORIRS_CHROME_CS2_TABS_MAX )
        {
            snprintf(s->tabs[cmd->value], sizeof(s->tabs[0]), "%s", cmd->text);
            if( cmd->value + 1 > s->tab_count )
                s->tab_count = cmd->value + 1;
            s->dirty = 1;
        }
        else if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
        {
            cs2_options_set(s, cmd->widget, cmd->value, cmd->text);
            /* Only the OPEN list is on screen, so only that one is worth a
             * rebuild -- a palette restating two thousand rows behind a shut
             * button would otherwise ask for two thousand of them. */
            if( s->drop_open == cmd->widget )
                s->dirty = 1;
        }
        break;

    case TORIRS_CHROME_CMD_WIDGET_OPTIONS:
        if( cmd->widget == s->tab_strip_widget )
        {
            s->tab_count = 0;
            memset(s->tabs, 0, sizeof(s->tabs));
            s->dirty = 1;
        }
        else if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
        {
            cs2_options_reset(s, cmd->widget, cmd->value);
            /* A list that is being restated is not the list the open popup is
             * showing rows out of, and an offset into the old one means
             * nothing against the new. */
            if( s->drop_open == cmd->widget )
            {
                cs2_dropdown_close(s);
                s->dirty = 1;
            }
        }
        break;

    case TORIRS_CHROME_CMD_WIDGET_REMOVE:
        if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
        {
            cs2_options_clear(s, cmd->widget);
            if( s->drop_open == cmd->widget )
                cs2_dropdown_close(s);
        }
        break;

    case TORIRS_CHROME_CMD_WIDGET_HIDDEN:
        /* A hidden dropdown takes its list with it. The build would shut it
         * anyway (the button is not drawn, so the box comes back zero-width),
         * but a list left open over the rows for the frame in between is
         * exactly the flicker this is cheap to avoid. */
        if( cmd->widget == s->drop_open && cmd->value )
            cs2_dropdown_close(s);
        break;

    default:
        break;
    }

    if( shape )
        s->dirty = 1;
}

static int
chrome_cs2_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct ChromeCs2* s = user;

    assert(s);
    assert(out);
    if( !s->open )
        return 0;
    /* The clicks themselves arrive through ToriRSChromeExecCs2_Click, called by
     * the host's interface dispatch when it recognises a component id in this
     * executor's range. Nothing is pumped here. */
    return ToriRSChromeMirror_Poll(&s->mirror, out, max);
}

int
ToriRSChromeExecCs2_Click(int component_id)
{
    struct ChromeCs2* s = &g_chrome_cs2;

    if( !s->open || component_id < TORIRS_CHROME_CS2_ID_BASE )
        return 0;

    /*
     * Anything that is not the open list dismisses it, before that anything is
     * acted on.
     *
     * A dropdown list is modal in every client that has one: it shuts on the
     * next click whatever the click was for. Without this the only way to shut
     * one is to choose a row or press the button again, so a list opened by
     * mistake sits over the rows underneath and takes their clicks.
     *
     * Its own BUTTON is excluded, or the toggle below would find it already
     * closed and open it straight back up.
     */
    if( s->drop_open >= 0 )
    {
        int const own_button = CS2_ID_WIDGET_BASE + s->drop_open;
        int const in_list = component_id >= CS2_ID_DROP_ROW_BASE &&
                            component_id < CS2_ID_DROP_ROW_BASE + CS2_DROP_ROWS;
        int const on_bar = component_id == CS2_ID_DROP_UP || component_id == CS2_ID_DROP_DOWN;

        if( !in_list && !on_bar && component_id != own_button )
        {
            cs2_dropdown_close(s);
            s->dirty = 1;
        }
    }

    if( component_id == CS2_ID_CLOSE )
    {
        struct ToriRSChromeIntent intent;
        memset(&intent, 0, sizeof(intent));
        intent.kind = TORIRS_CHROME_INTENT_CLOSE;
        /* The panel the window IS. Taken from the tab strip's owner where
         * there is one and from any live widget otherwise, because this
         * executor presents exactly one panel and only needs to name it. */
        intent.panel = s->tab_panel;
        intent.widget = -1;
        if( intent.panel < 0 )
        {
            for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
            {
                struct ToriRSChromeMirrorWidget const* w =
                    ToriRSChromeMirror_Widget(&s->mirror, i);
                if( w )
                {
                    intent.panel = w->panel;
                    break;
                }
            }
        }
        if( intent.panel < 0 )
            return 0;
        ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
        return 1;
    }

    if( component_id == CS2_ID_SCROLL_UP || component_id == CS2_ID_SCROLL_DOWN )
    {
        int const before = s->scroll_row;
        s->scroll_row += component_id == CS2_ID_SCROLL_DOWN ? 1 : -1;
        if( s->scroll_row < 0 )
            s->scroll_row = 0;
        /* The upper bound is the rebuild's job -- it is the only place that
         * knows how many rows are visible on this tab. */
        if( s->scroll_row != before )
            s->dirty = 1;
        return 1;
    }

    if( component_id >= TORIRS_CHROME_CS2_ID_TAB_BASE &&
        component_id < TORIRS_CHROME_CS2_ID_TAB_BASE + TORIRS_CHROME_CS2_TABS_MAX )
    {
        struct ToriRSChromeIntent intent;
        memset(&intent, 0, sizeof(intent));
        intent.kind = TORIRS_CHROME_INTENT_TAB;
        intent.panel = s->tab_panel;
        intent.widget = s->tab_strip_widget;
        intent.value = component_id - TORIRS_CHROME_CS2_ID_TAB_BASE;
        ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
        /* A new tab is a different list, so an offset measured against the old
         * one means nothing -- and a tab that opens already scrolled reads as
         * a broken panel. */
        s->scroll_row = 0;
        return 1;
    }

    /*
     * A cell of the open picker's axis bars.
     *
     * The block is indexed by CELL, not by widget: only one picker can be open
     * at a time, so which one this belongs to comes from the executor's own
     * `colorpick_open` rather than from arithmetic on the id. That is what
     * keeps the block 96 ids instead of 96 per row.
     */
    if( component_id >= CS2_ID_CELL_BASE &&
        component_id < CS2_ID_CELL_BASE + CS2_COLOR_BARS * CS2_COLOR_CELLS )
    {
        int const offset = component_id - CS2_ID_CELL_BASE;
        int const bar = offset / CS2_COLOR_CELLS;
        int const cell = offset % CS2_COLOR_CELLS;
        int const handle = s->colorpick_open;
        struct ToriRSChromeMirrorWidget* w =
            handle >= 0 ? ToriRSChromeMirror_Widget(&s->mirror, handle) : NULL;
        struct ToriRSChromeIntent intent;

        if( !w )
            return 0;
        memset(&intent, 0, sizeof(intent));
        intent.kind = TORIRS_CHROME_INTENT_PICK;
        intent.panel = w->panel;
        intent.widget = handle;
        /* The cell's own value, mapped back onto the axis's full range: the
         * bar is cut into CS2_COLOR_CELLS pieces for the tree's sake, and the
         * VALUE it names is still one of the axis's own. */
        intent.value = cs2_colorbar_with(
            s->selected[handle], bar, cs2_colorbar_steps(bar) * cell / CS2_COLOR_CELLS);
        ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
        return 1;
    }

    /* A COLORPICK's swatch: the picker's popup toggle, in its own block. */
    if( component_id >= CS2_ID_SWATCH_BASE &&
        component_id < CS2_ID_SWATCH_BASE + TORIRS_CHROME_MAX_WIDGETS )
    {
        int const handle = component_id - CS2_ID_SWATCH_BASE;
        struct ToriRSChromeMirrorWidget* w =
            ToriRSChromeMirror_Widget(&s->mirror, handle);

        if( !w )
            return 0;
        /* ACTIVATE means "the swatch", and the model turns that into a toggle
         * of the axis popup -- see the two-zone note in ToriRSChromeIntent_Apply.
         * Remembered here as well because the CELLS have no widget in their id
         * and have to ask who is open. */
        s->colorpick_open = s->checked[handle] ? -1 : handle;
        ToriRSChromeMirror_PushActivate(&s->mirror, w->panel, handle);
        return 1;
    }

    /*
     * A row of the open list.
     *
     * The block is indexed by SCREEN ROW, so which option it names comes from
     * the executor's own scroll offset -- the same trade the picker's cells
     * make, and for the same reason: only one list is open at a time.
     */
    if( component_id >= CS2_ID_DROP_ROW_BASE &&
        component_id < CS2_ID_DROP_ROW_BASE + CS2_DROP_ROWS )
    {
        int const handle = s->drop_open;
        int const index = s->drop_scroll + (component_id - CS2_ID_DROP_ROW_BASE);
        struct ToriRSChromeMirrorWidget* w =
            handle >= 0 ? ToriRSChromeMirror_Widget(&s->mirror, handle) : NULL;
        struct ToriRSChromeIntent intent;

        if( !w || index < 0 || index >= s->option_count[handle] )
            return 0;
        memset(&intent, 0, sizeof(intent));
        intent.kind = TORIRS_CHROME_INTENT_PICK;
        intent.panel = w->panel;
        intent.widget = handle;
        intent.value = index;
        ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
        /* Shut on the way out, as every dropdown in the game does: the model
         * answers with WIDGET_SELECTED and the button shows the new value. */
        cs2_dropdown_close(s);
        s->dirty = 1;
        return 1;
    }

    /* The open list's own scroll arrows -- its offset, not the panel's. */
    if( component_id == CS2_ID_DROP_UP || component_id == CS2_ID_DROP_DOWN )
    {
        int const before = s->drop_scroll;
        s->drop_scroll += component_id == CS2_ID_DROP_DOWN ? 1 : -1;
        if( s->drop_scroll < 0 )
            s->drop_scroll = 0;
        /* The upper bound is the build's job -- it is the only place that
         * knows how many rows the list ended up showing. */
        if( s->drop_scroll != before )
            s->dirty = 1;
        return 1;
    }

    /* A LISTROW's settings affordance: the same handle, in the action block. */
    if( component_id >= CS2_ID_ACTION_BASE &&
        component_id < CS2_ID_ACTION_BASE + TORIRS_CHROME_MAX_WIDGETS )
    {
        int const handle = component_id - CS2_ID_ACTION_BASE;
        struct ToriRSChromeMirrorWidget* w =
            ToriRSChromeMirror_Widget(&s->mirror, handle);
        struct ToriRSChromeIntent intent;

        if( !w )
            return 0;
        memset(&intent, 0, sizeof(intent));
        intent.kind = TORIRS_CHROME_INTENT_ACTION;
        intent.panel = w->panel;
        intent.widget = handle;
        ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
        /* The page about to open is a different list; an offset measured
         * against this one would open it already scrolled. */
        s->scroll_row = 0;
        return 1;
    }

    {
        int const handle = component_id - CS2_ID_WIDGET_BASE;
        struct ToriRSChromeMirrorWidget* w =
            ToriRSChromeMirror_Widget(&s->mirror, handle);
        if( !w )
            return 0;

        switch( w->kind )
        {
        case TORIRS_CHROME_W_LISTROW:
        case TORIRS_CHROME_W_CHECKBOX:
            /* Toggled from what this executor last drew, not from the model:
             * the model is what the intent is about to change, and reading it
             * here would need a second path back into it. */
            ToriRSChromeMirror_PushToggle(
                &s->mirror, w->panel, handle, !s->checked[handle]);
            return 1;

        case TORIRS_CHROME_W_DROPDOWN:
            /*
             * The button OPENS the list, and closes it again -- it does not
             * step the selection.
             *
             * Stepping was what this did while the executor had no popup, and
             * it is wrong on its own terms once the list exists: a click that
             * changes the setting on the way to reading the options commits a
             * value nobody chose, and on a palette of two thousand loc names
             * it is not an affordance at all.
             *
             * The list is a rebuild, so it costs the same frame either way.
             */
            if( s->option_count[handle] > 0 )
            {
                if( s->drop_open == handle )
                    cs2_dropdown_close(s);
                else
                {
                    s->drop_open = handle;
                    /* Opened at the top, not where it was last time: an offset
                     * measured against a list that has since been restated is
                     * a list that opens already scrolled. */
                    s->drop_scroll = 0;
                }
                s->dirty = 1;
                return 1;
            }
            ToriRSChromeMirror_PushActivate(&s->mirror, w->panel, handle);
            return 1;

        case TORIRS_CHROME_W_TEXTINPUT:
            /*
             * An activation, which the intent layer turns into FOCUS on the
             * model's own field -- from there the host's keyboard routing
             * types into it and the text mirrors back here per keystroke, and
             * the WIDGET_FOCUS that comes back draws the ring that says so.
             */
            ToriRSChromeMirror_PushActivate(&s->mirror, w->panel, handle);
            return 1;

        case TORIRS_CHROME_W_COLORPICK:
        {
            /* The widget's own id is the FIELD half of a colour row -- the
             * swatch has a block of its own above -- and a click on a field
             * takes the focus. ACTION is what says "the second zone", the same
             * way it does for a LISTROW. */
            struct ToriRSChromeIntent intent;
            memset(&intent, 0, sizeof(intent));
            intent.kind = TORIRS_CHROME_INTENT_ACTION;
            intent.panel = w->panel;
            intent.widget = handle;
            ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
            return 1;
        }

        default:
            ToriRSChromeMirror_PushActivate(&s->mirror, w->panel, handle);
            return 1;
        }
    }
}

struct ToriRSChromeExec
ToriRSChromeExec_Cs2(
    struct UITree* tree,
    int32_t mount_node,
    int font_id,
    int cache_font_id,
    int skin_scene_id,
    int (*resolve_font)(void*, int),
    void* resolve_ud)
{
    struct ToriRSChromeExec exec;

    memset(&exec, 0, sizeof(exec));
    /* Before the zeroing, not after: the option lists are owned, and a memset
     * over their pointers is the leak rather than the release. */
    cs2_options_free_all(&g_chrome_cs2);
    memset(&g_chrome_cs2, 0, sizeof(g_chrome_cs2));
    g_chrome_cs2.tree = tree;
    g_chrome_cs2.mount = mount_node;
    /* The id, because the index dies with the next gameframe rebuild -- see
     * the re-find in cs2_rebuild. */
    if( tree && mount_node >= 0 && (uint32_t)mount_node < tree->component_count )
        g_chrome_cs2.mount_com_id = tree->components[mount_node].component_id;
    g_chrome_cs2.font_id = font_id;
    g_chrome_cs2.cache_font_id = cache_font_id;
    g_chrome_cs2.skin_scene_id = skin_scene_id;
    g_chrome_cs2.resolve_font_cb = resolve_font;
    g_chrome_cs2.resolve_cb_ud = resolve_ud;
    g_chrome_cs2.panel_node = -1;
    g_chrome_cs2.colorpick_open = -1;
    g_chrome_cs2.drop_open = -1;

    exec.user = &g_chrome_cs2;
    exec.begin = chrome_cs2_begin;
    exec.apply = chrome_cs2_apply;
    exec.end = chrome_cs2_end;
    exec.poll = chrome_cs2_poll;
    return exec;
}

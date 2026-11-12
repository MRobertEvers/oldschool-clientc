/*
 * The palette: underlay and overlay SWATCHES (each a cell in the record's
 * own colour), the overlay SHAPE thumbnails sampled from the mesh builder
 * (edpanel_shapes.c), the rotation (design #terrain) -- and, under them, the
 * blending aids (the owner's request, 2026-11-06): the ground is never a
 * record's colour but the client's five-tile average of the underlays
 * around it (editor/editor_floorcolor.h), so what matters when painting is
 * how two underlays average, and which record is nearest a colour wanted.
 *
 * The swatches list the ids the open squares use, plus any id typed into the
 * id fields -- the ids in front of you are the ones you are about to paint
 * with, and a palette of every id is 255 rows of mostly nothing -- unless
 * "Every id" is on, which lists every record the window has, and the Sort
 * row orders a well by id, by hue (so a well reads as a colour wheel and
 * the neighbours of a swatch are the records that blend with it) or by
 * lightness. A hover names the swatch under the pointer in the tooltip: its
 * id, its colour, and the hue / sat / light the client packs it to.
 *
 * The BLEND strip is the ground across a seam between the previous underlay
 * pick and the current one, as the client will draw it (Editor_FloorBlend
 * Strip), with the three HSL deltas: pick the ground's underlay with the
 * eyedropper (I), then a candidate, and the strip shows the join.
 *
 * The COLOUR PICKER: hue / sat / light sliders or a hex, a swatch of that
 * colour, and the nearest underlays and overlays to it (Editor_Floor
 * Distance), each a swatch that picks it on a click. A pick from a well or
 * the eyedropper sets the picker to the pick's colour, so the matches are
 * then the records nearest what is on the ground.
 *
 * The EYEDROPPERS: a button under each well arms a one-shot pick, and the
 * next click on the ground (whatever tool is armed; the App asks
 * Editor_Palette_DropperArmed before the tool sees the click) takes that
 * tile's underlay, or its overlay with its shape and rotation, as I does for
 * every layer at once. Pressing the button again disarms it.
 *
 * The TEXTURES well (the owner's request, 2026-11-10: "How do you specify a
 * textured overlay? The UI doesn't seem to have a way to select"): a textured
 * overlay's own colour is a placeholder, so in the Overlay well water, a road
 * and lava were the same magenta cell, or a flat average once the texture had
 * loaded, and nothing said which id lays which. The well shows every ground
 * texture the cache's overlays lay, as the picture itself, one cell a
 * texture; a click picks the lowest overlay id that lays it and the tooltip
 * names it and how many others do. The panel asks the window for the overlay
 * records and the textures a batch a tick while it is open
 * (app_services.overlay_texture), so the well fills in over the first second.
 *
 * NEW RECORDS (the owner's request, 2026-11-10: "you also need to be able
 * to create an underlay and overlay and then add it to the palette"). The
 * picker's colour is a colour nothing in the cache may have, and the nearest
 * record is then the wrong ground. Under the picker: a name, "New underlay
 * of this colour" and "New overlay of this colour". Either copies the
 * type's first record to the name with a fresh id (Editor_CopyRecord, the
 * Record panel's Copy to new: the id is the allocator's, the line goes into
 * pack/<type>.alloc and pack/<type>.client, the block into
 * configs/all.<type>), sets its `colour` to the picker's, and picks it, so
 * the brush that paints it is armed. "Recolour" sets the picked record's
 * colour again, when the tree made it.
 *
 * The records the tree made are what pack/underlay.alloc and
 * pack/overlay.alloc name, and each well lists them all, used or not: that
 * is "in the palette". Their ids are past the cache's (the cache's own
 * underlays fill 1..256), which is why a tile's underlay is sixteen bits
 * (editor_types.h EDITOR_UNDERLAY_MAX).
 *
 * A record made here is in no cache until the next build, so the window is
 * told what it is (hooks.floor_define): its swatch is its colour and ground
 * painted with it shows at once. That runs in every window that has the
 * module, shown or not -- the World may be one window and this panel
 * another -- from the record's own document, so an undo, another window's
 * edit and a restart all land the same way.
 *
 * A new square states no floor at all, and a well that lists what the open
 * squares use was then one crossed-out cell: "How do I pick an underlay
 * color?" So while the open squares use three underlays or fewer the wells
 * list every record, as "Every id" does.
 *
 * Colours come from the window (hooks.floor_rgb, the provider's records); a
 * record the window has not loaded draws as a hatched grey cell with its id,
 * so the palette still works on a cache the editor cannot fully decode.
 *
 * NAMES, THE COMMON SET AND FIND (2026-11-11, a village site built in the
 * running editor). The cache's floors have no names, so the wells were
 * unlabelled cells and "every record" on a new square was 900 of them: which
 * is a dirt road, a stone floor, wood planks, water? editor_floornames.h
 * answers. A well now lists THE COMMON SET first -- the floors the content's
 * own maps are made of, in the order grass, earth, sand, snow, rock, then
 * tracks, stone floors, wood, water -- then what the open squares use, and
 * every record only with "Every id" or a search. The hover, the pick's row
 * and the status sentence say the name ("overlay 14 dirt track"). "Find"
 * narrows both wells to the records a word, an id or a "#colour" finds, over
 * every record; and the two pick fields take a name as well as an id, so
 * "water" typed into "Pick overlay" picks overlay 6 -- which is also how a
 * script picks one, a swatch being a cell of a drawn well.
 *
 * A cache whose records are not the common set's (another revision: an
 * entry answers only for the colour or texture it states) falls back to
 * what the wells did before: every record while the open squares use next
 * to none.
 *
 * An overlay that lays a TEXTURE is drawn as the texture in its cell. Its
 * own colour is a placeholder (overlay 7's cell was tan and painted the
 * swamp's teal water), so the cell showed what would not be painted.
 *
 * THE ORDER OF THE PAGE is what a stroke needs first: Find and Every id,
 * the underlay well and its pick field, the overlay's, the shape strip and
 * rotation, the sort. The one-layer eyedroppers, the Textures well, the
 * blend strip, the colour finder and New floor record follow, under the fold
 * of a docked panel. The wells
 * are bare (no 48 px frame around one row of cells) and as wide as the
 * panel, so the whole first part is one screen of the Terrain workspace's
 * column.
 *
 * THE PATH PAINTER'S DEFAULT (Editor_Palette_PathOverlay): a path dragged
 * with no overlay picked was refused. It now lays the common floor of its
 * style -- dirt the dirt track, pavement the stone floor, river the water --
 * and says which; an overlay the user picked is never replaced.
 */

#include "edpanel_common.h"
#include "edpanel_shapes.h"
#include "edpanels.h"
#include "editor/editor.h"
#include "editor/editor_cfg.h"
#include "editor/editor_floorcolor.h"
#include "editor/editor_floornames.h"
#include "editor/editor_path.h"
#include "mem/torirs_mem.h"
#include "ui/torirs_chrome_icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Enough for every OSRS overlay (659) with room; the chrome's graph items
 * are a pool of 4096 across every well, so the wells are not unbounded. */
#define PALETTE_IDS_MAX 768
/** "Every id" probes the window for records up to these ids (.jm2 ids,
 *  config id + 1): underlays are a byte, overlays run past 600 in OSRS. */
#define PALETTE_UNDERLAY_PROBE 256
#define PALETTE_OVERLAY_PROBE 1024
/** The records the tree made, a well lists at most; a record name's bytes
 *  (a `[block]` header's, well inside EDITOR_CMD_NAME_MAX). */
#define PALETTE_CUSTOM_MAX 128
#define PALETTE_NAME_MAX 48
/** Frames between looks at the tree's records while nothing asked. */
#define PALETTE_CUSTOM_EVERY 15
/** Open squares that use this many distinct underlays or fewer get every
 *  record in the wells without "Every id". */
#define PALETTE_AUTO_ALL_UNDERLAYS 3
/** The least a well is wide (a column at EDP_DOCK_W_MIN), and what it is
 *  taken to be before the first layout has given it a box. */
#define PALETTE_WELL_W 184
/** A swatch's cell: 16 px of colour, enough to tell a texture by. */
#define PALETTE_CELL 18
/** The wells' fewest columns, and the most a column that wide may show. */
#define PALETTE_COLS_MIN (PALETTE_WELL_W / PALETTE_CELL)
#define PALETTE_COLS_MAX 32
/** The Find box's bytes. */
#define PALETTE_FIND_MAX 48
_Static_assert(EDITOR_PALETTE_DESCRIBE_MAX >= EDITOR_FLOOR_NAME_MAX + 8, "an id and a name fit a description");
/** Fewer common underlays than this resident and the wells fall back to
 *  every record on squares that state next to no floor (another cache). */
#define PALETTE_COMMON_MIN 4
/** Looks (PALETTE_CUSTOM_EVERY frames apart) the panel keeps asking the
 *  window for the common set's records over. */
#define PALETTE_COMMON_ROUNDS 40
/** The Textures well: the service's sprite, a cell around it, and how long
 *  the panel keeps asking after the last answer (ids past the cache's
 *  records never answer). */
#define PALETTE_TEX_PX 24
#define PALETTE_TEX_CELL (PALETTE_TEX_PX + 2)
#define PALETTE_TEX_COLS (PALETTE_WELL_W / PALETTE_TEX_CELL)
#define PALETTE_TEX_MAX 256
#define PALETTE_TEX_BATCH 64
#define PALETTE_TEX_SETTLE_TICKS 300
#define SHAPE_PX 3
#define SHAPE_BOX (EDITOR_SHAPE_GRID * SHAPE_PX + 2)
#define SHAPE_COLS 6
/** The blend strip: an odd count of cells across the seam, 8 px each. */
#define PALETTE_BLEND_CELLS 23
#define PALETTE_BLEND_CELL_W (PALETTE_WELL_W / PALETTE_BLEND_CELLS)
/** The picker's matches per kind, and their keys in the match well. */
#define PALETTE_MATCH_MAX 9
#define PALETTE_MATCH_CELL 18
#define PALETTE_MATCH_SWATCH_W 20
#define PALETTE_MATCH_KEY_OVERLAY 100

enum
{
    PALETTE_GROUP_ROTATION = 11,
};

enum PaletteSort
{
    PALETTE_SORT_ID = 0,
    PALETTE_SORT_HUE,
    PALETTE_SORT_LIGHT,
    PALETTE_SORT_COUNT,
};

static const char* const k_rotation_names[4] = { "0 degrees", "90 degrees", "180 degrees", "270 degrees" };
static const char* const k_sort_names[PALETTE_SORT_COUNT] = { "common first", "by hue", "by lightness" };

struct PaletteList
{
    /** File ids (the `.jm2` value, config id + 1); 0 is "none". */
    int ids[PALETTE_IDS_MAX];
    int count;
};

/** One floor record the tree made (a line of pack/<type>.alloc). */
struct PaletteCustom
{
    /** The file id (config id + 1) and the record's name. */
    int id;
    char name[PALETTE_NAME_MAX];
    /** Its CFG document was asked for. */
    int opened;
    /** The document text the window was last told the record from. */
    const char* defined_text;
    uint32_t defined_revision;
};

static const char* const k_floor_types[2] = { "underlay", "overlay" };

struct PalettePanel
{
    int panel;
    int row_underlay;
    int graph_underlay;
    int input_underlay;
    int row_overlay;
    int graph_overlay;
    int input_overlay;
    int button_drop_underlay;
    int button_drop_overlay;
    int row_textures;
    int graph_textures;
    int row_shape;
    int graph_shape;
    int dropdown_rotation;
    /** Find: the words, id or #colour both wells are narrowed to. */
    int input_find;
    char find[PALETTE_FIND_MAX];
    int check_all;
    int dropdown_sort;
    int row_blend;
    int graph_blend;
    int row_pick;
    int slider_hue;
    int slider_sat;
    int slider_light;
    int input_hex;
    int graph_match;
    int row_match_under;
    int row_match_over;

    struct PaletteList underlays;
    struct PaletteList overlays;
    struct Editor_ShapeThumb thumbs[4][EDITOR_SHAPE_COUNT];

    /** The open-square set the lists were built for. */
    int built_signature;
    int dirty_draw;
    /** The picks the well was drawn for: the eyedropper (I) writes them
     *  behind the panel's back, and the well and the readout follow. */
    int shown_picks[5];
    /** "Every id" and the sort the lists were built with. */
    int list_all;
    enum PaletteSort sort;
    /** Every id asks the window for the records it has not loaded, a batch
     *  a tick: the next id to ask per kind, and the ticks left to rebuild
     *  the wells on while answers land. */
    int all_asked[2];
    int all_settle;
    /** The underlay the blend strip runs from: the pick before the current
     *  one. 0 until two have been picked. */
    int blend_from;
    /** The picker's colour and its HSL24 (the sliders' values). */
    uint32_t pick_rgb;
    int pick_hue;
    int pick_sat;
    int pick_light;
    int match_under[PALETTE_MATCH_MAX];
    int match_under_count;
    int match_over[PALETTE_MATCH_MAX];
    int match_over_count;
    /** The well and key the tooltip names. */
    int hover_graph;
    int hover_key;
    /** The eyedropper the next ground click answers: 0 none, else 1 +
     *  is_overlay. */
    int dropper;

    /** New records: the name, New underlay / overlay, Recolour. */
    int row_new;
    int input_new_name;
    int button_new[2];
    int button_recolour[2];
    /** The records the tree made, per kind, in the ledger's order. */
    struct PaletteCustom custom[2][PALETTE_CUSTOM_MAX];
    int custom_count[2];
    /** The ledgers were asked for; each one's text as last read. */
    int ledger_opened;
    const char* ledger_text[2];
    uint32_t ledger_revision[2];
    int custom_tick;
    /** The open squares state no floor: the wells list every record. */
    int auto_all;

    /** The wells' columns as last drawn (the panel's width in cells), and
     *  the well's width they were worked out from. */
    int well_cols;
    int well_w_seen;
    /** Looks left to ask the window for the common set's records over, and
     *  how many of them were resident at the last look. */
    int common_rounds;
    int common_seen;
    /** The Find box's text as last read (untrimmed), to see it change. */
    char find_typed[PALETTE_FIND_MAX + 16];
    /** The overlay the path painter picked by itself, and the style it was
     *  for: such a pick follows the style; one the user made does not. */
    int auto_overlay;
    int auto_style;

    /** The Textures well. Per overlay id what the window said: 0 not yet,
     *  1 it lays a texture, -1 it lays none. */
    signed char texture_state[PALETTE_OVERLAY_PROBE + 1];
    /** The texture each overlay that lays one lays (texture_state 1). */
    short texture_of[PALETTE_OVERLAY_PROBE + 1];
    /** The next id to ask, and the ticks left to keep asking. */
    int texture_cursor;
    int texture_settle;
    int texture_started;
    /** One cell a texture, in texture-id order: the texture, the lowest
     *  overlay id that lays it, and how many overlays do. */
    int texture_ids[PALETTE_TEX_MAX];
    int texture_overlay[PALETTE_TEX_MAX];
    int texture_users[PALETTE_TEX_MAX];
    int texture_count;
};

static const char* const k_dropper_captions[2] = { "Eyedropper: pick a tile's underlay",
                                                   "Eyedropper: pick a tile's overlay" };

static int
palette_rgb(
    struct EditorUi* ui,
    int is_overlay,
    int id,
    uint32_t* out)
{
    if( id <= 0 || !ui->hooks.floor_rgb )
        return 0;
    return ui->hooks.floor_rgb(ui->hooks_user, is_overlay, id, out);
}

/* ---- names ------------------------------------------------------------------ */

static struct PaletteCustom*
palette_custom_find(
    struct PalettePanel* self,
    int kind,
    int id);

/** The texture overlay `id` lays as the panel knows it: the texture, -1 for
 *  none, -2 while the window has not said (Editor_FloorCommonFind's terms). */
static int
palette_texture_known(
    const struct PalettePanel* self,
    int is_overlay,
    int id)
{
    assert(self);
    if( !is_overlay )
        return -1;
    if( id <= 0 || id > PALETTE_OVERLAY_PROBE || self->texture_state[id] == 0 )
        return -2;
    return self->texture_state[id] > 0 ? self->texture_of[id] : -1;
}

/** The same for a caller that may be in a window with no Palette (`self`
 *  NULL: Editor_Palette_Describe, the path painter's default): there no
 *  overlay's texture is known. */
static int
palette_texture_maybe(
    const struct PalettePanel* self,
    int is_overlay,
    int id)
{
    if( !self )
        return is_overlay ? -2 : -1;
    return palette_texture_known(self, is_overlay, id);
}

/**
 * What floor `id` of a kind is called (EDITOR_FLOOR_NAME_MAX bytes): "none",
 * the name the tree gave a record it made, else editor_floornames.h's --
 * the common name, the texture's, the colour's words. "" for a record the
 * window has not loaded. `self` is NULL in a window with no Palette: no
 * record of the tree's and no texture is then known.
 */
static void
palette_name(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int is_overlay,
    int id,
    char* out)
{
    const struct PaletteCustom* custom;
    uint32_t rgb = 0;

    out[0] = '\0';
    if( id == 0 )
    {
        snprintf(out, EDITOR_FLOOR_NAME_MAX, "none");
        return;
    }
    custom = self ? palette_custom_find(self, is_overlay, id) : NULL;
    if( custom )
    {
        snprintf(out, EDITOR_FLOOR_NAME_MAX, "%.*s", EDITOR_FLOOR_NAME_MAX - 1, custom->name);
        return;
    }
    if( !palette_rgb(ui, is_overlay, id, &rgb) )
        return;
    Editor_FloorName(is_overlay, id, rgb, palette_texture_maybe(self, is_overlay, id), out, EDITOR_FLOOR_NAME_MAX);
}

/** Is common entry `common`'s record in this window, and what the entry
 *  says it is? */
static int
palette_common_ok(
    struct EditorUi* ui,
    const struct PalettePanel* self,
    const struct Editor_FloorCommon* common)
{
    uint32_t rgb = 0;

    if( !palette_rgb(ui, common->is_overlay, common->id, &rgb) )
        return 0;
    return Editor_FloorCommonFind(common->is_overlay, common->id, rgb,
               palette_texture_maybe(self, common->is_overlay, common->id))
           == common;
}

/** Ask the window for the common set's records it has not loaded. */
static void
palette_common_request(struct EditorUi* ui)
{
    int count = 0;
    const struct Editor_FloorCommon* common = Editor_FloorCommonList(&count);
    uint32_t rgb;

    if( !ui->hooks.floor_request )
        return;
    for( int i = 0; i < count; i++ )
        if( !palette_rgb(ui, common[i].is_overlay, common[i].id, &rgb) )
            ui->hooks.floor_request(ui->hooks_user, common[i].is_overlay, common[i].id);
}

/** Does the Find box's query find floor `id`? */
static int
palette_find_keeps(
    struct EditorUi* ui,
    struct PalettePanel* self,
    const char* query,
    int is_overlay,
    int id)
{
    char name[EDITOR_FLOOR_NAME_MAX];
    uint32_t rgb = 0;

    if( id == 0 )
        return 0;
    (void)palette_rgb(ui, is_overlay, id, &rgb);
    palette_name(ui, self, is_overlay, id, name);
    return Editor_FloorMatches(query, id, name, rgb);
}

/**
 * The floor of a kind a typed name means: the first common floor the words
 * find (the set is most used first, so "dirt" is the dirt track and "water"
 * the water), else a record the tree made, else the lowest id among every
 * record the window has. 0 when nothing is called that.
 */
static int
palette_find_best(
    struct EditorUi* ui,
    struct PalettePanel* self,
    const char* query,
    int is_overlay)
{
    int count = 0;
    const struct Editor_FloorCommon* common = Editor_FloorCommonList(&count);
    int const probe = is_overlay ? PALETTE_OVERLAY_PROBE : PALETTE_UNDERLAY_PROBE;
    uint32_t rgb;

    for( int i = 0; i < count; i++ )
        if( common[i].is_overlay == is_overlay && palette_common_ok(ui, self, &common[i])
            && Editor_FloorMatches(query, common[i].id, common[i].name, common[i].rgb) )
            return common[i].id;
    for( int i = 0; i < self->custom_count[is_overlay]; i++ )
        if( palette_find_keeps(ui, self, query, is_overlay, self->custom[is_overlay][i].id) )
            return self->custom[is_overlay][i].id;
    for( int id = 1; id <= probe; id++ )
        if( palette_rgb(ui, is_overlay, id, &rgb) && palette_find_keeps(ui, self, query, is_overlay, id) )
            return id;
    return 0;
}

/* ---- the picker ------------------------------------------------------------ */

/** The picker's colour from its HSL24; the sliders and the hex follow, the
 *  matches are found again. */
static void
palette_pick_set_hsl(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int hue,
    int sat,
    int light)
{
    self->pick_hue = hue < 0 ? 0 : hue > 255 ? 255 : hue;
    self->pick_sat = sat < 0 ? 0 : sat > 255 ? 255 : sat;
    self->pick_light = light < 0 ? 0 : light > 255 ? 255 : light;
    self->pick_rgb = Editor_FloorRgbOfHsl(self->pick_hue, self->pick_sat, self->pick_light);
    ToriRSChrome_SliderSet(ui->chrome, self->slider_hue, (self->pick_hue * 360 + 128) / 256);
    ToriRSChrome_SliderSet(ui->chrome, self->slider_sat, (self->pick_sat * 100 + 128) / 255);
    ToriRSChrome_SliderSet(ui->chrome, self->slider_light, (self->pick_light * 100 + 128) / 255);
    self->dirty_draw = 1;
}

/** The picker's colour from a record colour (a pick, a swatch, a hex). */
static void
palette_pick_set_rgb(
    struct EditorUi* ui,
    struct PalettePanel* self,
    uint32_t rgb)
{
    struct Editor_FloorHsl hsl;
    char text[16];

    Editor_FloorHslOfRgb(rgb, &hsl);
    palette_pick_set_hsl(ui, self, hsl.hue, hsl.sat, hsl.light);
    /* The record's bytes, not the sliders' rounding of them. */
    self->pick_rgb = rgb & 0xffffff;
    snprintf(text, sizeof(text), "#%06x", (unsigned)self->pick_rgb);
    ToriRSChrome_SetText(ui->chrome, self->input_hex, text);
}

/** The nearest `max` ids of one kind to the picker's colour, nearest first,
 *  among the records the window has. */
static int
palette_match_kind(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int is_overlay,
    int* out,
    int max)
{
    int distance[PALETTE_MATCH_MAX];
    int count = 0;
    int const probe = is_overlay ? PALETTE_OVERLAY_PROBE : PALETTE_UNDERLAY_PROBE;

    assert(max <= PALETTE_MATCH_MAX);
    /* The cache's ids, then the tree's own past them. */
    for( int n = 1; n <= probe + self->custom_count[is_overlay]; n++ )
    {
        int const id = n <= probe ? n : self->custom[is_overlay][n - probe - 1].id;
        uint32_t rgb;
        int d;
        int at;
        if( n > probe && id <= probe )
            continue;
        if( !palette_rgb(ui, is_overlay, id, &rgb) )
            continue;
        /* The record of that very colour first, ahead of any the client
         * packs to the same HSL. */
        d = (rgb & 0xffffff) == self->pick_rgb ? -1 : Editor_FloorDistance(self->pick_rgb, rgb);
        if( count == max && d >= distance[count - 1] )
            continue;
        at = count < max ? count++ : max - 1;
        while( at > 0 && distance[at - 1] > d )
        {
            distance[at] = distance[at - 1];
            out[at] = out[at - 1];
            at--;
        }
        distance[at] = d;
        out[at] = id;
    }
    return count;
}

static void
palette_match(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    self->match_under_count = palette_match_kind(ui, self, 0, self->match_under, PALETTE_MATCH_MAX);
    self->match_over_count = palette_match_kind(ui, self, 1, self->match_over, PALETTE_MATCH_MAX);
    self->dirty_draw = 1;
}

/* ---- the tree's own records ---------------------------------------------------- */

/** The open document (kind, key) in this window's mirror, NULL when it is
 *  not open (or the window has no session). */
static const struct Editor_TextDoc*
palette_doc(
    struct EditorUi* ui,
    enum Editor_DocKind kind,
    const char* key)
{
    struct Editor_DocHandle handle;

    if( !ui->editor || !Editor_DocFind(&ui->editor->doc, kind, key, &handle) || !handle.text )
        return NULL;
    return handle.text;
}

/** The value of field `key` of a record's block, copied out. 0 when the
 *  block does not state it (or it does not fit). */
static int
palette_block_value(
    const struct Editor_CfgBlock* block,
    const char* key,
    char* out,
    size_t out_size)
{
    int const line = Editor_CfgBlockFieldFind(block, key, 0);
    const char* value;
    size_t length = 0;

    if( line < 0 )
        return 0;
    value = Editor_CfgBlockValue(block, line, &length);
    if( length >= out_size )
        return 0;
    memcpy(out, value, length);
    out[length] = '\0';
    return 1;
}

/** A field's colour: "0xRRGGBB". 0 for anything else ("default"). */
static int
palette_value_rgb(
    const char* value,
    uint32_t* out)
{
    char* end = NULL;
    unsigned long rgb;

    if( value[0] != '0' || (value[1] != 'x' && value[1] != 'X') )
        return 0;
    rgb = strtoul(value + 2, &end, 16);
    if( end == value + 2 || *end != '\0' || rgb > 0xffffff )
        return 0;
    *out = (uint32_t)rgb;
    return 1;
}

/** 1 when ground of an open square wears floor `id` of `kind`. */
static int
palette_floor_in_use(
    struct EditorUi* ui,
    int kind,
    int id)
{
    const struct Editor_Doc* doc;

    if( !ui->editor )
        return 0;
    doc = &ui->editor->doc;
    for( int s = 0; s < doc->square_count; s++ )
    {
        const struct Editor_Square* square = &doc->squares[s];
        if( !square->loaded )
            continue;
        for( int i = 0; i < EDITOR_SQUARE_TILES; i++ )
            if( kind ? (square->tiles[i].has_overlay && square->tiles[i].overlay_id == id)
                     : square->tiles[i].underlay_id == id )
                return 1;
    }
    return 0;
}

/** The record of `kind` the tree made under file id `id`, or NULL. */
static struct PaletteCustom*
palette_custom_find(
    struct PalettePanel* self,
    int kind,
    int id)
{
    for( int i = 0; i < self->custom_count[kind]; i++ )
        if( self->custom[kind][i].id == id )
            return &self->custom[kind][i];
    return NULL;
}

/** Append a record to the list (one the ledger's text did not give: the
 *  ledger is not open in this mirror). Nothing when it is there or the list
 *  is full. */
static void
palette_custom_add(
    struct PalettePanel* self,
    int kind,
    int id,
    const char* name)
{
    struct PaletteCustom* custom;

    if( palette_custom_find(self, kind, id) || self->custom_count[kind] >= PALETTE_CUSTOM_MAX
        || strlen(name) >= PALETTE_NAME_MAX )
        return;
    custom = &self->custom[kind][self->custom_count[kind]++];
    memset(custom, 0, sizeof(*custom));
    custom->id = id;
    snprintf(custom->name, sizeof(custom->name), "%s", name);
}

/** The list of `kind` again from its ledger's text: one `id=name` a line
 *  (comments and the allocator's marker are not such lines). A record that
 *  was listed keeps what the window was told of it. */
static void
palette_custom_read(
    struct PalettePanel* self,
    int kind,
    const char* text,
    size_t length)
{
    static struct PaletteCustom next[PALETTE_CUSTOM_MAX];
    int const limit = kind ? 0xffff : EDITOR_UNDERLAY_MAX;
    int count = 0;
    size_t at = 0;

    while( at < length && count < PALETTE_CUSTOM_MAX )
    {
        size_t end = at;
        size_t scan = at;
        size_t name_end;
        int id = 0;
        int digits = 0;

        while( end < length && text[end] != '\n' )
            end++;
        while( scan < end && text[scan] >= '0' && text[scan] <= '9' && id <= 0xffff )
        {
            id = id * 10 + (text[scan] - '0');
            scan++;
            digits++;
        }
        name_end = end;
        while( name_end > scan && (text[name_end - 1] == '\r' || text[name_end - 1] == ' ' || text[name_end - 1] == '\t') )
            name_end--;
        if( digits > 0 && scan < end && text[scan] == '=' && name_end > scan + 1
            && name_end - scan - 1 < PALETTE_NAME_MAX && id + 1 <= limit )
        {
            struct PaletteCustom* custom = &next[count];
            const struct PaletteCustom* was;

            memset(custom, 0, sizeof(*custom));
            custom->id = id + 1;
            memcpy(custom->name, text + scan + 1, name_end - scan - 1);
            was = palette_custom_find(self, kind, custom->id);
            if( was && strcmp(was->name, custom->name) == 0 )
                *custom = *was;
            count++;
        }
        at = end + 1;
    }
    memcpy(self->custom[kind], next, (size_t)count * sizeof(next[0]));
    self->custom_count[kind] = count;
}

/** Tell the window what a record the tree made is, when its document moved
 *  since the window was last told (or never was): its colour, and for an
 *  overlay its texture, blend colour and hideunderlay. */
static void
palette_custom_define(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int kind,
    struct PaletteCustom* custom)
{
    char key[EDITOR_DOC_KEY_MAX];
    char value[32];
    const struct Editor_TextDoc* doc;
    struct Editor_CfgBlock block;
    uint32_t rgb = 0;
    uint32_t blend = 0;
    int have_rgb;
    int texture = -1;
    int blend_rgb = -1;
    int hide = kind;

    snprintf(key, sizeof(key), "%s:%s", k_floor_types[kind], custom->name);
    if( !custom->opened )
    {
        custom->opened = 1;
        (void)edp_ensure_doc(ui, EDITOR_DOC_CFG, key);
    }
    doc = palette_doc(ui, EDITOR_DOC_CFG, key);
    if( !doc || !doc->loaded )
        return;
    if( doc->text == custom->defined_text && doc->revision == custom->defined_revision )
        return;
    custom->defined_text = doc->text;
    custom->defined_revision = doc->revision;
    /* No block: the record was taken back (an undo). The window keeps what
     * it was told; nothing lists it once the ledger's line is gone too. */
    if( doc->length == 0 )
        return;
    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, doc->text, doc->length);
    have_rgb = palette_block_value(&block, "colour", value, sizeof(value)) && palette_value_rgb(value, &rgb);
    if( kind )
    {
        /* cp_flo.c's reading: `texture` a number or "default", hideunderlay
         * true unless "no", blendcolour a colour or "default". */
        if( palette_block_value(&block, "texture", value, sizeof(value)) && value[0] >= '0' && value[0] <= '9' )
            texture = atoi(value);
        if( palette_block_value(&block, "hideunderlay", value, sizeof(value)) && strcmp(value, "no") == 0 )
            hide = 0;
        if( palette_block_value(&block, "blendcolour", value, sizeof(value)) && palette_value_rgb(value, &blend) )
            blend_rgb = (int)blend;
    }
    Editor_CfgBlockFree(&block);
    /* An underlay with no colour is no record the ground can wear. */
    if( !kind && !have_rgb )
        return;
    if( ui->hooks.floor_define )
        ui->hooks.floor_define(ui->hooks_user, kind, custom->id, rgb, texture, blend_rgb, hide,
            palette_floor_in_use(ui, kind, custom->id));
    self->dirty_draw = 1;
    /* The wells and the matches are of the colours the window has. */
    self->built_signature = -1;
}

/**
 * Follow the records the tree made: the two ledgers (opened once, read
 * again when their text moved) and each record's own document. Run in every
 * window that has this module, shown or not, so the window that draws the
 * world knows a record the Palette of another window made.
 */
static void
palette_custom_follow(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    char key[EDITOR_DOC_KEY_MAX];

    if( !ui->editor )
        return;
    if( !self->ledger_opened )
    {
        self->ledger_opened = 1;
        for( int kind = 0; kind < 2; kind++ )
        {
            snprintf(key, sizeof(key), "pack:%s.alloc", k_floor_types[kind]);
            (void)edp_ensure_doc(ui, EDITOR_DOC_TEXT, key);
        }
    }
    for( int kind = 0; kind < 2; kind++ )
    {
        const struct Editor_TextDoc* ledger;

        snprintf(key, sizeof(key), "pack:%s.alloc", k_floor_types[kind]);
        ledger = palette_doc(ui, EDITOR_DOC_TEXT, key);
        if( ledger && ledger->loaded
            && (ledger->text != self->ledger_text[kind] || ledger->revision != self->ledger_revision[kind]) )
        {
            self->ledger_text[kind] = ledger->text;
            self->ledger_revision[kind] = ledger->revision;
            palette_custom_read(self, kind, ledger->text, ledger->length);
            self->built_signature = -1;
        }
        for( int i = 0; i < self->custom_count[kind]; i++ )
            palette_custom_define(ui, self, kind, &self->custom[kind][i]);
    }
}

/* ---- the lists -------------------------------------------------------------- */

/** The pick moved (a well click, the eyedropper, another window): the blend
 *  strip's "from" becomes the underlay before, and the picker takes the
 *  pick's colour. */
static int
palette_picks_changed(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    int const now[5] = { ui->tools.underlay_id, ui->tools.overlay_id, ui->tools.overlay_none,
                         ui->tools.overlay_shape, ui->tools.overlay_rotation };
    int const before = self->shown_picks[0];
    uint32_t rgb;

    if( memcmp(now, self->shown_picks, sizeof(now)) == 0 )
        return 0;
    memcpy(self->shown_picks, now, sizeof(now));
    if( now[0] != before )
    {
        if( before > 0 && now[0] > 0 )
            self->blend_from = before;
        if( palette_rgb(ui, 0, now[0], &rgb) )
        {
            palette_pick_set_rgb(ui, self, rgb);
            palette_match(ui, self);
        }
    }
    return 1;
}

static void
palette_list_add(
    struct PaletteList* list,
    int id)
{
    for( int i = 0; i < list->count; i++ )
        if( list->ids[i] == id )
            return;
    if( list->count >= PALETTE_IDS_MAX )
        return;
    list->ids[list->count++] = id;
}

struct PaletteSortKey
{
    int id;
    int key;
};

static int
palette_cmp_key(
    const void* a,
    const void* b)
{
    const struct PaletteSortKey* ka = a;
    const struct PaletteSortKey* kb = b;
    if( ka->key != kb->key )
        return ka->key - kb->key;
    return ka->id - kb->id;
}

/** A floor's place in the common set (0 first), or -1 when it is not one of
 *  them in this window. */
static int
palette_common_rank(
    struct EditorUi* ui,
    const struct PalettePanel* self,
    int is_overlay,
    int id)
{
    int count = 0;
    const struct Editor_FloorCommon* common = Editor_FloorCommonList(&count);

    for( int i = 0; i < count; i++ )
        if( common[i].id == id && common[i].is_overlay == is_overlay )
            return palette_common_ok(ui, self, &common[i]) ? i : -1;
    return -1;
}

/** Order a list (past its leading "none") by the sort: the common set in its
 *  own order and then ids, hue (greys at the end by lightness, since their
 *  hue says nothing) or lightness; a record the window has not loaded goes
 *  last, by id. */
static void
palette_list_sort(
    struct EditorUi* ui,
    const struct PalettePanel* self,
    struct PaletteList* list,
    int is_overlay,
    enum PaletteSort sort)
{
    struct PaletteSortKey* keys;
    int const n = list->count - 1;

    if( n <= 1 )
        return;
    keys = ToriMem_Calloc(ToriMem_Editor(), (size_t)n, sizeof(*keys));
    assert(keys);
    for( int i = 0; i < n; i++ )
    {
        struct Editor_FloorHsl hsl;
        uint32_t rgb;
        keys[i].id = list->ids[i + 1];
        if( sort == PALETTE_SORT_ID )
        {
            /* The set reads grass, earth, sand, snow, rock: by id it was
             * 26, 33, 48 ... with the families shuffled together. */
            int const rank = palette_common_rank(ui, self, is_overlay, keys[i].id);
            keys[i].key = rank >= 0 ? rank : (1 << 12) + keys[i].id;
            continue;
        }
        if( !palette_rgb(ui, is_overlay, keys[i].id, &rgb) )
        {
            keys[i].key = 1 << 20;
            continue;
        }
        Editor_FloorHslOfRgb(rgb, &hsl);
        if( sort == PALETTE_SORT_LIGHT )
            keys[i].key = hsl.light * 256 + hsl.hue;
        else if( hsl.sat < 24 )
            keys[i].key = (1 << 16) + hsl.light;
        else
            keys[i].key = hsl.hue * 256 + hsl.light;
    }
    qsort(keys, (size_t)n, sizeof(*keys), palette_cmp_key);
    for( int i = 0; i < n; i++ )
        list->ids[i + 1] = keys[i].id;
    ToriMem_Free(ToriMem_Editor(), keys);
}

static int
palette_signature(struct EditorUi* ui)
{
    int signature = 0;
    struct Editor_Doc* doc;

    if( !ui->editor )
        return 0;
    doc = &ui->editor->doc;
    for( int i = 0; i < doc->square_count; i++ )
        if( doc->squares[i].loaded )
            signature = signature * 31 + doc->squares[i].map_x * 1024 + doc->squares[i].map_z + 1;
    return signature;
}

/** Keep of a list (past its leading "none") what the Find box finds. */
static void
palette_list_filter(
    struct EditorUi* ui,
    struct PalettePanel* self,
    struct PaletteList* list,
    int is_overlay)
{
    int kept = 1;

    for( int i = 1; i < list->count; i++ )
        if( palette_find_keeps(ui, self, self->find, is_overlay, list->ids[i]) )
            list->ids[kept++] = list->ids[i];
    list->count = kept;
}

static void
palette_rebuild(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    struct Editor_Doc* doc;
    int common_count = 0;
    const struct Editor_FloorCommon* common = Editor_FloorCommonList(&common_count);
    int const searching = self->find[0] != '\0';
    int common_underlays = 0;
    int used_underlays;

    self->underlays.count = 0;
    self->overlays.count = 0;
    palette_list_add(&self->underlays, 0);
    palette_list_add(&self->overlays, 0);
    if( ui->editor )
    {
        doc = &ui->editor->doc;
        for( int s = 0; s < doc->square_count; s++ )
        {
            const struct Editor_Square* square = &doc->squares[s];
            if( !square->loaded )
                continue;
            for( int i = 0; i < EDITOR_SQUARE_TILES; i++ )
            {
                if( square->tiles[i].underlay_id )
                    palette_list_add(&self->underlays, square->tiles[i].underlay_id);
                if( square->tiles[i].has_overlay )
                    palette_list_add(&self->overlays, square->tiles[i].overlay_id);
            }
        }
    }
    used_underlays = self->underlays.count - 1;
    /* The common set, whatever the squares use: on a new square it is the
     * palette, and on a built one the floors the next stroke most likely
     * wants are still one click away. */
    for( int i = 0; i < common_count; i++ )
        if( palette_common_ok(ui, self, &common[i]) )
        {
            palette_list_add(common[i].is_overlay ? &self->overlays : &self->underlays, common[i].id);
            if( !common[i].is_overlay )
                common_underlays++;
        }
    /* A cache the common set does not describe, on squares that state no
     * floor or next to none (a new square, and the same square three colours
     * in): every record, or each well is one crossed-out cell and nothing to
     * pick -- and then, after the first stroke, that one colour and nothing
     * to pick next. A map that uses more underlays than that is its own
     * palette. */
    self->auto_all = !self->list_all && !searching && common_underlays < PALETTE_COMMON_MIN
                     && used_underlays <= PALETTE_AUTO_ALL_UNDERLAYS;
    /* Every id, and a search: a search is over every record the window has,
     * not over what happens to be listed. */
    if( self->list_all || searching || self->auto_all )
    {
        uint32_t rgb;
        for( int id = 1; id <= PALETTE_UNDERLAY_PROBE; id++ )
            if( palette_rgb(ui, 0, id, &rgb) )
                palette_list_add(&self->underlays, id);
        for( int id = 1; id <= PALETTE_OVERLAY_PROBE; id++ )
            if( palette_rgb(ui, 1, id, &rgb) )
                palette_list_add(&self->overlays, id);
    }
    /* The records the tree made are in the palette, used or not. */
    for( int i = 0; i < self->custom_count[0]; i++ )
        palette_list_add(&self->underlays, self->custom[0][i].id);
    for( int i = 0; i < self->custom_count[1]; i++ )
        palette_list_add(&self->overlays, self->custom[1][i].id);
    if( ui->tools.underlay_id )
        palette_list_add(&self->underlays, ui->tools.underlay_id);
    /* The strip's other end stays in reach: a pick is often "this one or
     * the one before". */
    if( self->blend_from )
        palette_list_add(&self->underlays, self->blend_from);
    if( !ui->tools.overlay_none )
        palette_list_add(&self->overlays, ui->tools.overlay_id);
    if( searching )
    {
        palette_list_filter(ui, self, &self->underlays, 0);
        palette_list_filter(ui, self, &self->overlays, 1);
    }
    palette_list_sort(ui, self, &self->underlays, 0, self->sort);
    palette_list_sort(ui, self, &self->overlays, 1, self->sort);
    self->built_signature = palette_signature(ui);
    self->dirty_draw = 1;
}

/* ---- drawing ---------------------------------------------------------------- */

/** A well's width in logical px: the panel's content column, or the least
 *  one before a layout has given the well a box.
 *
 *  A well scrolled out of the panel's view has no box (w 0) either, and it
 *  keeps the width it was last seen at: falling back to the least width
 *  there re-flowed every well into more, shorter rows the moment the page
 *  scrolled past the underlay well, so the page grew under the wheel (the
 *  scroll range read 803 at the top and 951 at the bottom). */
static int
palette_well_w(
    struct EditorUi* ui,
    const struct PalettePanel* self)
{
    int const w = ui->chrome->widgets[self->graph_underlay].w;

    if( w > 0 )
        return ToriRSChrome_UnscalePx(ToriRSChrome_ScaleQ(ui->chrome), w);
    return self->well_w_seen > 0 ? self->well_w_seen : PALETTE_WELL_W;
}

/** The swatch wells' columns at the well's width now. */
static int
palette_cols(
    struct EditorUi* ui,
    const struct PalettePanel* self)
{
    int const cols = (palette_well_w(ui, self) - 2) / PALETTE_CELL;

    return cols < 4 ? 4 : cols > PALETTE_COLS_MAX ? PALETTE_COLS_MAX : cols;
}

static int
palette_well_h(
    int count,
    int cols)
{
    int const rows = (count + cols - 1) / cols;
    return (rows > 0 ? rows : 1) * PALETTE_CELL + 2;
}

/** One swatch: what the floor paints -- the texture of an overlay that lays
 *  one, else the record's colour -- "none" crossed out, unloaded hatched. */
static void
palette_draw_swatch(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int graph,
    int x,
    int y,
    int w,
    int h,
    int is_overlay,
    int id,
    int key)
{
    struct ToriRSChrome* chrome = ui->chrome;
    const struct EditorUiAppServices* services = &ui->app_services;
    uint32_t rgb = 0;
    int texture = -1;
    int scene_id = 0;

    /* Only an overlay the window said lays a texture is asked for its
     * picture: a well of plain colours asks the window nothing. */
    if( is_overlay && id > 0 && id <= PALETTE_OVERLAY_PROBE && self->texture_state[id] > 0
        && services->overlay_texture )
        scene_id = services->overlay_texture(services->user_data, id, &texture);
    if( id == 0 )
    {
        ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, 0x201c16, 1, key);
        ToriRSChrome_GraphLine(chrome, graph, x, y + h - 1, x + w - 1, y, 0xb04030);
    }
    else if( scene_id > 0 )
    {
        /* The keyed cell under the picture: the press and the hover land on it. */
        ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, 0x201c16, 1, key);
        ToriRSChrome_GraphImage(chrome, graph, x, y, w, h, scene_id, 0, 1);
    }
    else if( palette_rgb(ui, is_overlay, id, &rgb) )
        ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, rgb, 1, key);
    else
    {
        ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, 0x5a564c, 1, key);
        ToriRSChrome_GraphLine(chrome, graph, x, y, x + w - 1, y + h - 1, 0x3a362c);
    }
}

static void
palette_draw_list(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int graph,
    const struct PaletteList* list,
    int is_overlay,
    int picked)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const cols = self->well_cols;

    /* Bare: the cells are the well. Framed, one row of them sat in the
     * least height a framed well has (48 px), most of it empty. */
    ToriRSChrome_GraphSetBare(chrome, graph, 1, palette_well_h(list->count, cols));
    ToriRSChrome_GraphBegin(chrome, graph);
    for( int i = 0; i < list->count; i++ )
    {
        int const x = 1 + (i % cols) * PALETTE_CELL;
        int const y = 1 + (i / cols) * PALETTE_CELL;

        palette_draw_swatch(
            ui, self, graph, x, y, PALETTE_CELL - 2, PALETTE_CELL - 2, is_overlay, list->ids[i], i + 1);
        if( list->ids[i] == picked )
            ToriRSChrome_GraphRect(
                chrome, graph, x - 1, y - 1, PALETTE_CELL, PALETTE_CELL, 0xffffff, 0, 0);
    }
    ToriRSChrome_GraphEnd(chrome, graph);
}

static void
palette_draw_shapes(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const rotation = ui->tools.overlay_rotation & 3;
    uint32_t under = 0x6a7a3c;
    uint32_t over = 0x9c8a6a;

    (void)palette_rgb(ui, 0, ui->tools.underlay_id, &under);
    if( !ui->tools.overlay_none )
        (void)palette_rgb(ui, 1, ui->tools.overlay_id, &over);

    ToriRSChrome_GraphBegin(chrome, self->graph_shape);
    for( int shape = 0; shape < EDITOR_SHAPE_COUNT; shape++ )
    {
        const struct Editor_ShapeThumb* thumb = &self->thumbs[rotation][shape];
        int const bx = 1 + (shape % SHAPE_COLS) * (SHAPE_BOX + 4);
        int const by = 1 + (shape / SHAPE_COLS) * (SHAPE_BOX + 4);

        ToriRSChrome_GraphRect(chrome, self->graph_shape, bx, by, SHAPE_BOX, SHAPE_BOX, under, 1, shape + 1);
        /* One rect per run of overlay cells in a row. */
        for( int row = 0; row < EDITOR_SHAPE_GRID; row++ )
        {
            int col = 0;
            while( col < EDITOR_SHAPE_GRID )
            {
                int start;
                if( thumb->cells[row * EDITOR_SHAPE_GRID + col] != EDITOR_SHAPE_CELL_OVERLAY )
                {
                    col++;
                    continue;
                }
                start = col;
                while( col < EDITOR_SHAPE_GRID
                       && thumb->cells[row * EDITOR_SHAPE_GRID + col] == EDITOR_SHAPE_CELL_OVERLAY )
                    col++;
                ToriRSChrome_GraphRect(
                    chrome, self->graph_shape, bx + 1 + start * SHAPE_PX, by + 1 + row * SHAPE_PX,
                    (col - start) * SHAPE_PX, SHAPE_PX, over, 1, 0);
            }
        }
        if( shape == ui->tools.overlay_shape )
            ToriRSChrome_GraphRect(
                chrome, self->graph_shape, bx - 1, by - 1, SHAPE_BOX + 2, SHAPE_BOX + 2, 0xffffff, 0, 0);
    }
    ToriRSChrome_GraphEnd(chrome, self->graph_shape);
}

/** The ground across the seam from blend_from to the pick, the seam marked;
 *  the plain shown colour when there is only one. */
static void
palette_draw_blend(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    uint32_t strip[PALETTE_BLEND_CELLS];
    uint32_t from = 0;
    uint32_t to = 0;
    int const have_from = palette_rgb(ui, 0, self->blend_from, &from);
    int const have_to = palette_rgb(ui, 0, ui->tools.underlay_id, &to);

    ToriRSChrome_GraphBegin(chrome, self->graph_blend);
    if( have_from || have_to )
    {
        if( !have_from )
            from = to;
        if( !have_to )
            to = from;
        Editor_FloorBlendStrip(from, to, strip, PALETTE_BLEND_CELLS);
        for( int i = 0; i < PALETTE_BLEND_CELLS; i++ )
            ToriRSChrome_GraphRect(
                chrome, self->graph_blend, 1 + i * PALETTE_BLEND_CELL_W, 1, PALETTE_BLEND_CELL_W, PALETTE_CELL,
                strip[i], 1, 0);
        ToriRSChrome_GraphLine(
            chrome, self->graph_blend, 1 + (PALETTE_BLEND_CELLS / 2) * PALETTE_BLEND_CELL_W, 1,
            1 + (PALETTE_BLEND_CELLS / 2) * PALETTE_BLEND_CELL_W, PALETTE_CELL, 0xffffff);
    }
    else
        ToriRSChrome_GraphRect(chrome, self->graph_blend, 1, 1, PALETTE_WELL_W, PALETTE_CELL, 0x201c16, 1, 0);
    ToriRSChrome_GraphEnd(chrome, self->graph_blend);
}

/** The picker's colour as a tall swatch, then its nearest underlays (row
 *  one) and overlays (row two), keyed for a click. */
static void
palette_draw_match(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const row_h = PALETTE_CELL + 2;
    int const x0 = PALETTE_MATCH_SWATCH_W + 3;

    ToriRSChrome_GraphBegin(chrome, self->graph_match);
    ToriRSChrome_GraphRect(chrome, self->graph_match, 1, 1, PALETTE_MATCH_SWATCH_W, 2 * row_h - 2, self->pick_rgb, 1, 0);
    for( int i = 0; i < self->match_under_count; i++ )
        palette_draw_swatch(ui, self, self->graph_match, x0 + i * PALETTE_MATCH_CELL, 1, PALETTE_MATCH_CELL - 2,
            PALETTE_CELL, 0, self->match_under[i], 1 + i);
    for( int i = 0; i < self->match_over_count; i++ )
        palette_draw_swatch(ui, self, self->graph_match, x0 + i * PALETTE_MATCH_CELL, 1 + row_h,
            PALETTE_MATCH_CELL - 2, PALETTE_CELL, 1, self->match_over[i], PALETTE_MATCH_KEY_OVERLAY + 1 + i);
    ToriRSChrome_GraphEnd(chrome, self->graph_match);
}

/** "12 33 45 ..." of a match row into `line` after `prefix`. */
static void
palette_match_line(
    char* line,
    int size,
    const char* prefix,
    const int* ids,
    int count)
{
    int at = snprintf(line, (size_t)size, "%s", prefix);
    for( int i = 0; i < count && at < size; i++ )
        at += snprintf(line + at, (size_t)(size - at), " %d", ids[i]);
}

/**
 * A floor in one line of a label or a tooltip (TORIRS_CHROME_LABEL_MAX
 * bytes): "<lead> 48 grass  #35720a", an overlay that lays a texture
 * "<lead> 6 water  texture 1". The colour is left off rather than cut when
 * the name leaves it no room: the chrome reports a cut label, and half a
 * colour is no colour.
 */
static void
palette_floor_line(
    struct EditorUi* ui,
    struct PalettePanel* self,
    const char* lead,
    int is_overlay,
    int id,
    char* line)
{
    char name[EDITOR_FLOOR_NAME_MAX];
    char tail[24];
    uint32_t rgb = 0;
    int const texture = palette_texture_known(self, is_overlay, id);
    int used;

    if( !palette_rgb(ui, is_overlay, id, &rgb) )
    {
        snprintf(line, TORIRS_CHROME_LABEL_MAX, "%s %d (not loaded)", lead, id);
        return;
    }
    palette_name(ui, self, is_overlay, id, name);
    if( texture >= 0 )
        snprintf(tail, sizeof(tail), "  texture %d", texture);
    else
        snprintf(tail, sizeof(tail), "  #%06x", (unsigned)rgb);
    used = snprintf(line, TORIRS_CHROME_LABEL_MAX, "%s %d %s", lead, id, name);
    if( used + (int)strlen(tail) < TORIRS_CHROME_LABEL_MAX )
        snprintf(line + used, (size_t)(TORIRS_CHROME_LABEL_MAX - used), "%s", tail);
}

static void
palette_labels(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    uint32_t rgb = 0;
    uint32_t from = 0;

    if( ui->tools.underlay_id == 0 )
        snprintf(line, sizeof(line), "Underlay: none");
    else
        palette_floor_line(ui, self, "Underlay:", 0, ui->tools.underlay_id, line);
    ToriRSChrome_SetText(ui->chrome, self->row_underlay, line);

    if( ui->tools.overlay_none )
        snprintf(line, sizeof(line), "Overlay: none");
    else
        palette_floor_line(ui, self, "Overlay:", 1, ui->tools.overlay_id, line);
    ToriRSChrome_SetText(ui->chrome, self->row_overlay, line);

    snprintf(line, sizeof(line), "Shape %d, rotation %d", ui->tools.overlay_shape,
        (ui->tools.overlay_rotation & 3) * 90);
    ToriRSChrome_SetText(ui->chrome, self->row_shape, line);

    /* The blend: the two ids and how far apart the client packs them. */
    if( self->blend_from > 0 && ui->tools.underlay_id > 0 && palette_rgb(ui, 0, self->blend_from, &from)
        && palette_rgb(ui, 0, ui->tools.underlay_id, &rgb) )
    {
        struct Editor_FloorHsl a;
        struct Editor_FloorHsl b;
        int dh;
        Editor_FloorHslOfRgb(from, &a);
        Editor_FloorHslOfRgb(rgb, &b);
        dh = a.hue / 4 - b.hue / 4;
        if( dh < 0 )
            dh = -dh;
        if( dh > 32 )
            dh = 64 - dh;
        snprintf(line, sizeof(line), "Blend %d -> %d  hue %d sat %d light %d apart", self->blend_from,
            ui->tools.underlay_id, dh, abs(a.sat / 32 - b.sat / 32), abs(a.light / 2 - b.light / 2));
    }
    else if( ui->tools.underlay_id > 0 )
        snprintf(line, sizeof(line), "Blend: %d alone (pick another to see the seam)", ui->tools.underlay_id);
    else
        snprintf(line, sizeof(line), "Blend: pick an underlay (I picks the ground's)");
    ToriRSChrome_SetText(ui->chrome, self->row_blend, line);

    snprintf(line, sizeof(line), "Find by colour  #%06x", (unsigned)self->pick_rgb);
    ToriRSChrome_SetText(ui->chrome, self->row_pick, line);
    snprintf(line, sizeof(line), "New floor record in #%06x", (unsigned)self->pick_rgb);
    ToriRSChrome_SetText(ui->chrome, self->row_new, line);
    palette_match_line(line, (int)sizeof(line), "Underlays:", self->match_under, self->match_under_count);
    ToriRSChrome_SetText(ui->chrome, self->row_match_under, line);
    palette_match_line(line, (int)sizeof(line), "Overlays:", self->match_over, self->match_over_count);
    ToriRSChrome_SetText(ui->chrome, self->row_match_over, line);
}

/* ---- the Textures well -------------------------------------------------------- */

/** The cells again from what the window has said so far. */
static void
palette_textures_rebuild(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    const struct EditorUiAppServices* services = &ui->app_services;

    self->texture_count = 0;
    if( !services->overlay_texture )
        return;
    for( int id = 1; id <= PALETTE_OVERLAY_PROBE; id++ )
    {
        int texture = -1;
        int at;

        if( self->texture_state[id] != 1 )
            continue;
        if( services->overlay_texture(services->user_data, id, &texture) <= 0 || texture < 0 )
            continue;
        /* Kept in texture-id order: an insertion into a short sorted list. */
        for( at = 0; at < self->texture_count && self->texture_ids[at] < texture; at++ )
            ;
        if( at < self->texture_count && self->texture_ids[at] == texture )
        {
            self->texture_users[at]++;
            continue;
        }
        if( self->texture_count == PALETTE_TEX_MAX )
            continue;
        for( int move = self->texture_count; move > at; move-- )
        {
            self->texture_ids[move] = self->texture_ids[move - 1];
            self->texture_overlay[move] = self->texture_overlay[move - 1];
            self->texture_users[move] = self->texture_users[move - 1];
        }
        self->texture_ids[at] = texture;
        self->texture_overlay[at] = id;
        self->texture_users[at] = 1;
        self->texture_count++;
    }
}

/** Ask the window about the overlays it has not answered for, a batch a
 *  tick, until every id has an answer or none has landed for a while. */
static void
palette_textures_probe(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    const struct EditorUiAppServices* services = &ui->app_services;
    int budget = PALETTE_TEX_BATCH;
    int answered = 0;
    int found = 0;

    if( !services->overlay_texture )
        return;
    if( !self->texture_started )
    {
        self->texture_started = 1;
        self->texture_settle = PALETTE_TEX_SETTLE_TICKS;
    }
    if( self->texture_settle <= 0 )
        return;
    for( int n = 0; n < PALETTE_OVERLAY_PROBE && budget > 0; n++ )
    {
        int const id = self->texture_cursor % PALETTE_OVERLAY_PROBE + 1;
        int texture = -1;
        int scene_id;

        self->texture_cursor++;
        if( self->texture_state[id] != 0 )
            continue;
        budget--;
        scene_id = services->overlay_texture(services->user_data, id, &texture);
        if( scene_id == 0 )
            continue;
        self->texture_state[id] = scene_id > 0 ? 1 : -1;
        if( scene_id > 0 )
            self->texture_of[id] = (short)texture;
        answered = 1;
        if( scene_id > 0 )
            found = 1;
    }
    if( answered )
        self->texture_settle = PALETTE_TEX_SETTLE_TICKS;
    else
        self->texture_settle--;
    if( found )
    {
        palette_textures_rebuild(ui, self);
        /* The Overlay well's cells of these overlays are the texture now
         * that it is resident, and a common floor that is one of them is
         * known for what it is: the lists again. */
        self->built_signature = -1;
        self->dirty_draw = 1;
    }
    else if( self->texture_settle == 0 )
        self->dirty_draw = 1; /* the label stops saying "loading" */
}

static void
palette_draw_textures(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    const struct EditorUiAppServices* services = &ui->app_services;
    int const cols = (palette_well_w(ui, self) - 2) / PALETTE_TEX_CELL > PALETTE_TEX_COLS
                         ? (palette_well_w(ui, self) - 2) / PALETTE_TEX_CELL
                         : PALETTE_TEX_COLS;
    int const rows = (self->texture_count + cols - 1) / cols;
    int const picked = ui->tools.overlay_none ? 0 : ui->tools.overlay_id;
    int picked_texture = -1;
    char line[TORIRS_CHROME_LABEL_MAX];

    if( picked > 0 && services->overlay_texture )
        (void)services->overlay_texture(services->user_data, picked, &picked_texture);
    ToriRSChrome_GraphSetBare(chrome, self->graph_textures, 1, (rows > 0 ? rows : 1) * PALETTE_TEX_CELL + 2);
    ToriRSChrome_GraphBegin(chrome, self->graph_textures);
    for( int i = 0; i < self->texture_count; i++ )
    {
        int const x = 1 + (i % cols) * PALETTE_TEX_CELL;
        int const y = 1 + (i / cols) * PALETTE_TEX_CELL;
        int texture = -1;
        int const scene_id = services->overlay_texture(services->user_data, self->texture_overlay[i], &texture);

        /* The keyed cell under the picture: the press and the hover land on it. */
        ToriRSChrome_GraphRect(chrome, self->graph_textures, x, y, PALETTE_TEX_PX, PALETTE_TEX_PX, 0x201c16, 1, i + 1);
        if( scene_id > 0 )
            ToriRSChrome_GraphImage(
                chrome, self->graph_textures, x, y, PALETTE_TEX_PX, PALETTE_TEX_PX, scene_id, 0, 1);
        /* The picked overlay's texture is outlined, whichever of the
         * overlays that lay it is the pick. */
        if( self->texture_ids[i] == picked_texture )
            ToriRSChrome_GraphRect(
                chrome, self->graph_textures, x - 1, y - 1, PALETTE_TEX_CELL, PALETTE_TEX_CELL, 0xffffff, 0, 0);
    }
    ToriRSChrome_GraphEnd(chrome, self->graph_textures);
    if( !services->overlay_texture )
        snprintf(line, sizeof(line), "Textures: this window cannot draw them");
    else if( self->texture_count > 0 )
        snprintf(line, sizeof(line), "Textures: %d%s", self->texture_count, self->texture_settle > 0 ? " (loading)" : "");
    else
        snprintf(line, sizeof(line), "Textures: %s", self->texture_settle > 0 ? "loading" : "none in this cache");
    ToriRSChrome_SetText(chrome, self->row_textures, line);
}

/** The Textures well's tooltip: the cell under the pointer. */
static void
palette_textures_tooltip(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int key)
{
    char text[TORIRS_CHROME_LABEL_MAX];
    const char* const name =
        key > 0 && key <= self->texture_count ? Editor_FloorTextureName(self->texture_ids[key - 1]) : NULL;

    if( key <= 0 || key > self->texture_count )
        snprintf(text, sizeof(text), "click a texture to pick an overlay that lays it");
    /* What the picture is, when this build can say, ahead of the numbers. */
    else if( name && self->texture_users[key - 1] > 1 )
        snprintf(text, sizeof(text), "%s: texture %d, overlay %d (and %d more)", name, self->texture_ids[key - 1],
            self->texture_overlay[key - 1], self->texture_users[key - 1] - 1);
    else if( name )
        snprintf(text, sizeof(text), "%s: texture %d, overlay %d", name, self->texture_ids[key - 1],
            self->texture_overlay[key - 1]);
    else if( self->texture_users[key - 1] > 1 )
        snprintf(text, sizeof(text), "texture %d: overlay %d lays it (and %d more)", self->texture_ids[key - 1],
            self->texture_overlay[key - 1], self->texture_users[key - 1] - 1);
    else
        snprintf(text, sizeof(text), "texture %d: overlay %d lays it", self->texture_ids[key - 1],
            self->texture_overlay[key - 1]);
    ToriRSChrome_SetTooltip(ui->chrome, self->graph_textures, text);
}

/** The tooltip of a well names the swatch under the pointer: its id, what
 *  it is called, its colour (or the texture it lays), and, while there is
 *  room, the hue / sat / light the client packs it to (64, 8 and 128
 *  steps). */
static void
palette_tooltip(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int graph,
    int is_overlay,
    int id)
{
    char text[TORIRS_CHROME_LABEL_MAX];
    char name[EDITOR_FLOOR_NAME_MAX];
    char packed[32];
    uint32_t rgb;
    int used;

    if( id < 0 )
        snprintf(text, sizeof(text), "click a swatch to pick it; hover for its name and id");
    else if( id == 0 )
        snprintf(text, sizeof(text), "none%s", is_overlay ? ": clears the overlay" : "");
    else if( !palette_rgb(ui, is_overlay, id, &rgb) )
        snprintf(text, sizeof(text), "%s %d (not loaded)", is_overlay ? "overlay" : "underlay", id);
    else
    {
        struct Editor_FloorHsl hsl;
        int const texture = palette_texture_known(self, is_overlay, id);

        palette_name(ui, self, is_overlay, id, name);
        if( texture >= 0 )
            used = snprintf(text, sizeof(text), "overlay %d  texture %d  %s", id, texture, name);
        else
            used = snprintf(text, sizeof(text), "%s %d  #%06x  %s", is_overlay ? "overlay" : "underlay", id,
                (unsigned)rgb, name);
        Editor_FloorHslOfRgb(rgb, &hsl);
        snprintf(packed, sizeof(packed), "  hue %d sat %d light %d", hsl.hue / 4, hsl.sat / 32, hsl.light / 2);
        if( texture < 0 && used + (int)strlen(packed) < (int)sizeof(text) )
            snprintf(text + used, sizeof(text) - (size_t)used, "%s", packed);
    }
    ToriRSChrome_SetTooltip(ui->chrome, graph, text);
}

/** Every id: ask the window for the records it has not loaded yet, up to
 *  PALETTE_REQUEST_BATCH of each kind a tick (the loads are a hash lookup
 *  each once the group is decoded), then rebuild the wells now and then
 *  while the answers land. A window without the hook lists what it has. */
#define PALETTE_REQUEST_BATCH 64
#define PALETTE_SETTLE_TICKS 150

static void
palette_request_all(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    int asked = 0;

    for( int kind = 0; kind < 2; kind++ )
    {
        int const probe = kind ? PALETTE_OVERLAY_PROBE : PALETTE_UNDERLAY_PROBE;
        int budget = PALETTE_REQUEST_BATCH;
        if( !ui->hooks.floor_request )
        {
            self->all_asked[kind] = probe;
            continue;
        }
        while( self->all_asked[kind] < probe && budget-- > 0 )
        {
            uint32_t rgb;
            int const id = ++self->all_asked[kind];
            if( palette_rgb(ui, kind, id, &rgb) )
                continue;
            ui->hooks.floor_request(ui->hooks_user, kind, id);
            asked++;
        }
    }
    if( asked )
        self->all_settle = PALETTE_SETTLE_TICKS;
}

/** How many of the common set's records are in this window and are what
 *  the set says. */
static int
palette_common_resident(
    struct EditorUi* ui,
    const struct PalettePanel* self)
{
    int count = 0;
    const struct Editor_FloorCommon* common = Editor_FloorCommonList(&count);
    int resident = 0;

    for( int i = 0; i < count; i++ )
        if( palette_common_ok(ui, self, &common[i]) )
            resident++;
    return resident;
}

/** Narrow the wells to what `typed` finds (trimmed; "" lists as before). */
static void
palette_find_set(
    struct EditorUi* ui,
    struct PalettePanel* self,
    const char* typed)
{
    size_t length;

    snprintf(self->find_typed, sizeof(self->find_typed), "%s", typed);
    while( *typed == ' ' )
        typed++;
    length = strlen(typed);
    while( length > 0 && typed[length - 1] == ' ' )
        length--;
    if( length >= sizeof(self->find) )
        length = sizeof(self->find) - 1;
    memcpy(self->find, typed, length);
    self->find[length] = '\0';
    /* A search is over every record: the wells fill in as the window
     * answers for the ones it had not loaded. */
    if( self->find[0] != '\0' )
        self->all_settle = PALETTE_SETTLE_TICKS;
    palette_rebuild(ui, self);
}

static void
palette_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct PalettePanel* self = opaque;

    /* Before the visibility test: the window that draws the world learns a
     * record the tree made whether or not its own Palette is up. */
    if( self->custom_tick-- <= 0 )
    {
        self->custom_tick = PALETTE_CUSTOM_EVERY;
        palette_custom_follow(ui, self);
        /* The common set's records, shown or not: the path painter's default
         * overlay is one of them, and a well that opens should not start as
         * one crossed-out cell while forty records load. */
        if( self->common_rounds > 0 )
        {
            int const resident = palette_common_resident(ui, self);

            self->common_rounds--;
            palette_common_request(ui);
            if( resident != self->common_seen )
            {
                self->common_seen = resident;
                self->built_signature = -1;
            }
        }
    }
    if( !ui->chrome->panels[self->panel].visible )
        return;
    /* The wells are as wide as the panel: a column dragged wider is more
     * cells a row. */
    if( self->well_w_seen != palette_well_w(ui, self) || self->well_cols == 0 )
    {
        self->well_w_seen = palette_well_w(ui, self);
        self->well_cols = palette_cols(ui, self);
        self->dirty_draw = 1;
    }
    /* Find narrows as it is typed; Enter says how many it found. */
    if( strcmp(ToriRSChrome_Text(ui->chrome, self->input_find), self->find_typed) != 0 )
        palette_find_set(ui, self, ToriRSChrome_Text(ui->chrome, self->input_find));
    if( palette_picks_changed(ui, self) )
    {
        palette_rebuild(ui, self);
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_rotation, ui->tools.overlay_rotation & 3);
    }
    if( self->built_signature != palette_signature(ui) || self->underlays.count == 0 )
        palette_rebuild(ui, self);
    if( self->list_all || self->auto_all || self->find[0] != '\0' )
    {
        if( self->all_asked[0] < PALETTE_UNDERLAY_PROBE || self->all_asked[1] < PALETTE_OVERLAY_PROBE )
            palette_request_all(ui, self);
        if( self->all_settle > 0 && --self->all_settle % 15 == 0 )
            palette_rebuild(ui, self);
    }
    palette_textures_probe(ui, self);
    if( !self->dirty_draw )
        return;
    palette_draw_list(ui, self, self->graph_underlay, &self->underlays, 0, ui->tools.underlay_id);
    palette_draw_list(
        ui, self, self->graph_overlay, &self->overlays, 1, ui->tools.overlay_none ? 0 : ui->tools.overlay_id);
    palette_draw_textures(ui, self);
    palette_draw_shapes(ui, self);
    palette_draw_blend(ui, self);
    palette_draw_match(ui, self);
    palette_labels(ui, self);
    self->dirty_draw = 0;
}

/* ---- input ------------------------------------------------------------------ */

/** Which of the palette's picks a press made. */
enum PalettePick
{
    PALETTE_PICK_UNDERLAY = 0,
    PALETTE_PICK_OVERLAY,
    PALETTE_PICK_SHAPE
};

/** Room for what a pick did to the armed tool: "Rectangle select uses it". */
#define PALETTE_ARM_TEXT 48

/** Whether the armed tool itself lays this pick. Such a tool stays armed: a
 *  pick made FOR it must not swap it for a paint brush (2026-11-09: an
 *  overlay picked for Paint path armed Paint overlay, and the path drag
 *  painted a brush blob over a road). The list is who reads the pick:
 *  Editor_TileRectFill (all three), Editor_BrushCommands (the two paint
 *  brushes), Editor_PathCommands (the overlay; the path shapes its own
 *  edges) and editor_panel_region.c (the overlay, or the underlay when the
 *  Terrain panel's target is Underlay; shapes are fitted). Tidy and Plateau
 *  read no pick. */
static int
palette_tool_uses(
    const struct EditorUi* ui,
    enum PalettePick pick)
{
    enum Editor_Tool const tool = ui->tools.tool;

    if( tool == EDITOR_TOOL_RECT_SELECT )
        return 1;
    if( tool == EDITOR_TOOL_PAINT_UNDERLAY )
        return pick == PALETTE_PICK_UNDERLAY;
    if( tool == EDITOR_TOOL_PAINT_OVERLAY )
        return pick != PALETTE_PICK_UNDERLAY;
    if( tool == EDITOR_TOOL_PAINT_PATH )
        return pick == PALETTE_PICK_OVERLAY;
    if( tool == EDITOR_TOOL_REGION || tool == EDITOR_TOOL_LASSO )
        return pick == (ui->tools.region_underlay ? PALETTE_PICK_UNDERLAY : PALETTE_PICK_OVERLAY);
    return 0;
}

/** A pick arms its paint brush only when the armed tool does not lay that
 *  pick itself. `out` (PALETTE_ARM_TEXT bytes) is which happened, for the
 *  status bar: "Paint path uses it" / "Paint overlay armed". */
static void
palette_arm(
    struct EditorUi* ui,
    enum PalettePick pick,
    char* out)
{
    enum Editor_Tool const brush =
        pick == PALETTE_PICK_UNDERLAY ? EDITOR_TOOL_PAINT_UNDERLAY : EDITOR_TOOL_PAINT_OVERLAY;

    if( palette_tool_uses(ui, pick) )
    {
        snprintf(out, PALETTE_ARM_TEXT, "%s uses it", Editor_ToolName(ui->tools.tool));
        return;
    }
    Editor_UiSetTool(ui, brush);
    snprintf(out, PALETTE_ARM_TEXT, "%s armed", Editor_ToolName(brush));
}

static void
palette_pick_underlay(
    struct EditorUi* ui,
    int id,
    char* out_armed)
{
    ui->tools.underlay_id = id;
    palette_arm(ui, PALETTE_PICK_UNDERLAY, out_armed);
}

static struct PalettePanel*
palette_self(struct EditorUi* ui);

static void
palette_pick_overlay(
    struct EditorUi* ui,
    int id,
    char* out_armed)
{
    ui->tools.overlay_none = id == 0;
    ui->tools.overlay_id = id;
    /* The user's pick now, even of the floor the path painter chose: a
     * path of another style no longer swaps it for its own. */
    palette_self(ui)->auto_overlay = 0;
    palette_arm(ui, PALETTE_PICK_OVERLAY, out_armed);
}

/** Arm (1 + is_overlay) or disarm (0) the eyedropper; the armed button
 *  says what the next click does. */
static void
palette_dropper_set(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int dropper)
{
    self->dropper = dropper;
    ToriRSChrome_SetLabel(ui->chrome, self->button_drop_underlay,
        dropper == 1 ? "Click a tile... (press to cancel)" : k_dropper_captions[0]);
    ToriRSChrome_SetLabel(ui->chrome, self->button_drop_overlay,
        dropper == 2 ? "Click a tile... (press to cancel)" : k_dropper_captions[1]);
}

/** The id a key of a well names, or -1. */
static int
palette_key_id(
    struct PalettePanel* self,
    int graph,
    int key,
    int* out_is_overlay)
{
    if( key <= 0 )
        return -1;
    if( graph == self->graph_underlay && key <= self->underlays.count )
    {
        *out_is_overlay = 0;
        return self->underlays.ids[key - 1];
    }
    if( graph == self->graph_overlay && key <= self->overlays.count )
    {
        *out_is_overlay = 1;
        return self->overlays.ids[key - 1];
    }
    if( graph == self->graph_textures && key <= self->texture_count )
    {
        *out_is_overlay = 1;
        return self->texture_overlay[key - 1];
    }
    if( graph == self->graph_match )
    {
        if( key > PALETTE_MATCH_KEY_OVERLAY && key - PALETTE_MATCH_KEY_OVERLAY <= self->match_over_count )
        {
            *out_is_overlay = 1;
            return self->match_over[key - PALETTE_MATCH_KEY_OVERLAY - 1];
        }
        if( key < PALETTE_MATCH_KEY_OVERLAY && key <= self->match_under_count )
        {
            *out_is_overlay = 0;
            return self->match_under[key - 1];
        }
    }
    return -1;
}

static int
palette_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct PalettePanel* self = opaque;
    int const key = event->key;
    char armed[PALETTE_ARM_TEXT];
    char name[EDITOR_FLOOR_NAME_MAX];
    int is_overlay = 0;
    int id;

    if( event->widget != self->graph_underlay && event->widget != self->graph_overlay
        && event->widget != self->graph_shape && event->widget != self->graph_match
        && event->widget != self->graph_textures )
        return 0;
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_HOVER )
    {
        if( event->widget == self->graph_shape )
            return 1;
        if( self->hover_graph == event->widget && self->hover_key == key )
            return 1;
        self->hover_graph = event->widget;
        self->hover_key = key;
        if( event->widget == self->graph_textures )
        {
            palette_textures_tooltip(ui, self, key);
            return 1;
        }
        id = palette_key_id(self, event->widget, key, &is_overlay);
        palette_tooltip(ui, self, event->widget, is_overlay, id);
        return 1;
    }
    if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS || key <= 0 )
        return 1;

    if( event->widget == self->graph_shape )
    {
        if( key <= EDITOR_SHAPE_COUNT )
        {
            ui->tools.overlay_shape = key - 1;
            palette_arm(ui, PALETTE_PICK_SHAPE, armed);
            Editor_UiStatus(ui, "Shape %d picked: %s", key - 1, armed);
        }
        self->dirty_draw = 1;
        return 1;
    }
    id = palette_key_id(self, event->widget, key, &is_overlay);
    if( id < 0 )
        return 1;
    /* The bar says what the pick did to the armed tool, either way: a tool
     * that changed under the pointer with nothing said is how T1 happened. */
    palette_name(ui, self, is_overlay, id, name);
    if( !is_overlay )
    {
        palette_pick_underlay(ui, id, armed);
        if( id == 0 )
            Editor_UiStatus(ui, "No underlay picked (painting it leaves the ground bare): %s", armed);
        else
            Editor_UiStatus(ui, "Underlay %d %s picked: %s", id, name, armed);
    }
    else
    {
        palette_pick_overlay(ui, id, armed);
        if( id == 0 )
            Editor_UiStatus(ui, "No overlay picked (painting it takes the overlay off): %s", armed);
        else if( event->widget == self->graph_textures )
            Editor_UiStatus(ui, "Overlay %d %s picked (texture %d): %s", id, name, self->texture_ids[key - 1], armed);
        else
            Editor_UiStatus(ui, "Overlay %d %s picked: %s", id, name, armed);
    }
    self->dirty_draw = 1;
    return 1;
}

static void
palette_pick_floor(
    struct EditorUi* ui,
    int kind,
    int id,
    char* out_armed);

/**
 * A pick field's Enter: an id, or a name ("grass", "dirt track", "water")
 * that picks the floor palette_find_best says it means. The pick is a well
 * click's -- the brush armed unless the armed tool lays it -- and the bar
 * says what was picked by name, or what to type when nothing is called that.
 */
static void
palette_pick_typed(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int is_overlay,
    int widget)
{
    const char* typed = ToriRSChrome_Text(ui->chrome, widget);
    const char* const kind = is_overlay ? "overlay" : "underlay";
    int const limit = is_overlay ? 0xffff : EDITOR_UNDERLAY_MAX;
    char query[PALETTE_FIND_MAX];
    char name[EDITOR_FLOOR_NAME_MAX];
    char armed[PALETTE_ARM_TEXT];
    size_t length;
    int digits = 1;
    int id;

    while( *typed == ' ' )
        typed++;
    length = strlen(typed);
    while( length > 0 && typed[length - 1] == ' ' )
        length--;
    if( length == 0 )
    {
        Editor_UiStatus(ui, "Pick %s: type an id or a name (%s) and press Enter", kind,
            is_overlay ? "dirt, stone, wood, water" : "grass, dirt, sand, snow");
        return;
    }
    if( length >= sizeof(query) )
        length = sizeof(query) - 1;
    memcpy(query, typed, length);
    query[length] = '\0';
    for( size_t i = 0; i < length; i++ )
        if( query[i] < '0' || query[i] > '9' )
            digits = 0;
    if( digits )
    {
        id = length > 6 ? limit + 1 : atoi(query);
        if( id > limit )
        {
            Editor_UiStatus(ui, "Pick %s refused: %s ids are 0..%d", kind, kind, limit);
            return;
        }
    }
    else
    {
        id = palette_find_best(ui, self, query, is_overlay);
        if( id == 0 )
        {
            Editor_UiStatus(ui, "Pick %s: nothing is called \"%s\" (try %s, an id, or Find with Every id)", kind, query,
                is_overlay ? "dirt, stone, wood, water" : "grass, dirt, sand, snow");
            return;
        }
    }
    palette_pick_floor(ui, is_overlay, id, armed);
    palette_list_add(is_overlay ? &self->overlays : &self->underlays, id);
    palette_name(ui, self, is_overlay, id, name);
    if( id == 0 )
        Editor_UiStatus(ui, "No %s picked: %s", kind, armed);
    else
        Editor_UiStatus(ui, "%s %d %s picked: %s", is_overlay ? "Overlay" : "Underlay", id, name, armed);
    /* The pick's row above says what is picked; the field is for the next. */
    ToriRSChrome_SetText(ui->chrome, widget, "");
    self->dirty_draw = 1;
}

/* ---- new records ----------------------------------------------------------------- */

/**
 * The name a new record of `kind` takes: what was typed, or with nothing
 * typed one that says its colour ("ed_underlay_5a7f3c"), so a second record
 * of the same colour is the first one again. 0 when the typed text is not a
 * name a `[block]` header can carry here: a-z, 0-9 and _, at most
 * PALETTE_NAME_MAX - 1 bytes, and not `<type>_<number>`, which is how the
 * cache's own records are spelled (the allocator reads such a block as a
 * claim on that number).
 */
static int
palette_new_name(
    const char* typed,
    int kind,
    uint32_t rgb,
    char* out,
    size_t out_size)
{
    size_t length = 0;
    size_t const type_length = strlen(k_floor_types[kind]);

    while( *typed == ' ' )
        typed++;
    while( typed[length] && typed[length] != ' ' )
        length++;
    if( typed[length] )
    {
        /* Only trailing spaces may follow. */
        const char* rest = typed + length;
        while( *rest == ' ' )
            rest++;
        if( *rest )
            return 0;
    }
    if( length == 0 )
    {
        snprintf(out, out_size, "ed_%s_%06x", k_floor_types[kind], (unsigned)(rgb & 0xffffff));
        return 1;
    }
    if( length >= out_size || length >= PALETTE_NAME_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
        if( !((typed[i] >= 'a' && typed[i] <= 'z') || (typed[i] >= '0' && typed[i] <= '9') || typed[i] == '_') )
            return 0;
    if( length > type_length + 1 && strncmp(typed, k_floor_types[kind], type_length) == 0 && typed[type_length] == '_' )
    {
        size_t digit = type_length + 1;
        while( digit < length && typed[digit] >= '0' && typed[digit] <= '9' )
            digit++;
        if( digit == length )
            return 0;
    }
    memcpy(out, typed, length);
    out[length] = '\0';
    return 1;
}

/** The record a new one of `kind` is copied from: the first the type's
 *  member index names (configs/all.<type>.compack). Its fields are the
 *  shape of a record; the ones that matter are then set. 0 when the tree
 *  has no record of the type. */
static int
palette_template(
    struct EditorUi* ui,
    int kind,
    char* out,
    size_t out_size)
{
    char key[EDITOR_DOC_KEY_MAX];
    const struct Editor_TextDoc* index;
    size_t at = 0;

    snprintf(key, sizeof(key), "compack:%s", k_floor_types[kind]);
    if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, key) )
        return 0;
    index = palette_doc(ui, EDITOR_DOC_TEXT, key);
    if( !index )
        return 0;
    while( at < index->length )
    {
        size_t end = at;
        size_t scan = at;
        size_t name_end;

        while( end < index->length && index->text[end] != '\n' )
            end++;
        while( scan < end && index->text[scan] >= '0' && index->text[scan] <= '9' )
            scan++;
        name_end = end;
        while( name_end > scan && (index->text[name_end - 1] == '\r' || index->text[name_end - 1] == ' ') )
            name_end--;
        if( scan > at && scan < end && index->text[scan] == '=' && name_end > scan + 1 && name_end - scan - 1 < out_size )
        {
            memcpy(out, index->text + scan + 1, name_end - scan - 1);
            out[name_end - scan - 1] = '\0';
            return 1;
        }
        at = end + 1;
    }
    return 0;
}

/** The CFG_FIELD command that makes field `key` of record document
 *  `doc_key` read `value`, from the document as this mirror holds it.
 *  Returns 1 with the command, 0 when it reads so already (or the document
 *  is not open). */
static int
palette_field_command(
    struct EditorUi* ui,
    const char* doc_key,
    const char* key,
    const char* value,
    struct Editor_Cmd* out)
{
    const struct Editor_TextDoc* doc = palette_doc(ui, EDITOR_DOC_CFG, doc_key);
    struct Editor_CfgBlock block;
    char before[EDITOR_CMD_VALUE_MAX];
    int have;

    if( !doc )
        return 0;
    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, doc->text, doc->length);
    have = palette_block_value(&block, key, before, sizeof(before));
    Editor_CfgBlockFree(&block);
    if( have && strcmp(before, value) == 0 )
        return 0;
    Editor_CmdInit(out, EDITOR_DOC_CFG, doc_key, EDITOR_CMD_CFG_FIELD);
    out->has_before = have ? 1 : 0;
    out->has_after = 1;
    snprintf(out->u.cfg_field.key, sizeof(out->u.cfg_field.key), "%s", key);
    snprintf(out->u.cfg_field.before, sizeof(out->u.cfg_field.before), "%s", have ? before : "");
    snprintf(out->u.cfg_field.after, sizeof(out->u.cfg_field.after), "%s", value);
    return 1;
}

/** Pick floor `id` of `kind` as a well click does; `out_armed` says what
 *  that did to the armed tool. */
static void
palette_pick_floor(
    struct EditorUi* ui,
    int kind,
    int id,
    char* out_armed)
{
    if( kind )
        palette_pick_overlay(ui, id, out_armed);
    else
        palette_pick_underlay(ui, id, out_armed);
}

/** New underlay / New overlay of this colour. */
static void
palette_new_record(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int kind)
{
    const char* const type = k_floor_types[kind];
    char name[PALETTE_NAME_MAX];
    char template_name[PALETTE_NAME_MAX];
    char doc_key[EDITOR_DOC_KEY_MAX];
    char value[16];
    char armed[PALETTE_ARM_TEXT];
    struct Editor_Cmd commands[3];
    enum EditorHost_Status status;
    int count = 0;
    int id = -1;

    if( !ui->editor )
    {
        Editor_UiStatus(ui, "New %s refused: this window has no editor session", type);
        return;
    }
    if( !palette_new_name(ToriRSChrome_Text(ui->chrome, self->input_new_name), kind, self->pick_rgb, name, sizeof(name)) )
    {
        Editor_UiStatus(ui, "New %s refused: a name is a-z, 0-9 and _ (not %s_<number>), under %d letters", type, type,
            PALETTE_NAME_MAX);
        return;
    }
    /* The client reads an underlay of #000000 as no underlay at all
     * (blendmap.c tests the colour, not the id). */
    if( !kind && self->pick_rgb == 0 )
    {
        Editor_UiStatus(ui, "New underlay refused: the client draws #000000 as no underlay; #010101 is black");
        return;
    }
    /* The ledgers are in the mirror before the copy, so its lines land in
     * them; and a name the tree already made is that record. */
    palette_custom_follow(ui, self);
    for( int i = 0; i < self->custom_count[kind]; i++ )
        if( strcmp(self->custom[kind][i].name, name) == 0 )
        {
            palette_pick_floor(ui, kind, self->custom[kind][i].id, armed);
            Editor_UiStatus(ui, "%s is %s %d already; picked: %s", name, type, self->custom[kind][i].id, armed);
            self->dirty_draw = 1;
            return;
        }
    if( !palette_template(ui, kind, template_name, sizeof(template_name)) )
    {
        Editor_UiStatus(ui, "New %s refused: configs/all.%s.compack names no record to copy the fields of", type, type);
        return;
    }
    status = Editor_CopyRecord(ui->editor, type, template_name, name, &id);
    if( status != EDITOR_HOST_OK || id < 0 )
    {
        if( status == EDITOR_HOST_REFUSED )
            Editor_UiStatus(ui, "New %s refused: the name %s is taken (a record, or an id's name in the tree)", type, name);
        else if( status == EDITOR_HOST_UNSUPPORTED )
            Editor_UiStatus(ui, "New %s refused: the editor server this window is on cannot make one (it is older)", type);
        else
            Editor_UiStatus(ui, "New %s failed (status %d); the Output console has the server's line", type, (int)status);
        return;
    }
    /* The fields that make it this floor: the colour, and for an overlay no
     * texture and no blend colour of the record it was copied from. */
    snprintf(doc_key, sizeof(doc_key), "%s:%s", type, name);
    snprintf(value, sizeof(value), "0x%06X", (unsigned)(self->pick_rgb & 0xffffff));
    count += palette_field_command(ui, doc_key, "colour", value, &commands[count]);
    if( kind )
    {
        count += palette_field_command(ui, doc_key, "texture", "default", &commands[count]);
        count += palette_field_command(ui, doc_key, "blendcolour", "default", &commands[count]);
    }
    if( count > 0 && edp_apply(ui, commands, count, kind ? "new overlay" : "new underlay") <= 0 )
        Editor_UiLog(ui, "editor: %s %s was made but its colour was not set; Recolour sets it", type, name);
    /* The window learns it now rather than at the next look. */
    palette_custom_add(self, kind, id + 1, name);
    palette_custom_follow(ui, self);
    palette_pick_floor(ui, kind, id + 1, armed);
    ToriRSChrome_SetText(ui->chrome, self->input_new_name, "");
    Editor_UiStatus(ui, "New %s %d %s #%06x: %s (unsaved; Save writes it)", type, id + 1, name,
        (unsigned)(self->pick_rgb & 0xffffff), armed);
    palette_rebuild(ui, self);
}

/** Recolour: the picked record's colour becomes the picker's, when the
 *  tree made the record. */
static void
palette_recolour(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int kind)
{
    const char* const type = k_floor_types[kind];
    int const id = kind ? (ui->tools.overlay_none ? 0 : ui->tools.overlay_id) : ui->tools.underlay_id;
    const struct PaletteCustom* custom;
    char doc_key[EDITOR_DOC_KEY_MAX];
    char value[16];
    struct Editor_Cmd command;

    if( id <= 0 )
    {
        Editor_UiStatus(ui, "Recolour refused: no %s is picked", type);
        return;
    }
    palette_custom_follow(ui, self);
    custom = palette_custom_find(self, kind, id);
    if( !custom )
    {
        Editor_UiStatus(ui, "Recolour refused: %s %d is the cache's own; New %s makes one that is yours", type, id, type);
        return;
    }
    if( !kind && self->pick_rgb == 0 )
    {
        Editor_UiStatus(ui, "Recolour refused: the client draws #000000 as no underlay; #010101 is black");
        return;
    }
    snprintf(doc_key, sizeof(doc_key), "%s:%s", type, custom->name);
    if( !edp_ensure_doc(ui, EDITOR_DOC_CFG, doc_key) )
    {
        Editor_UiStatus(ui, "Recolour refused: %s could not be opened", doc_key);
        return;
    }
    snprintf(value, sizeof(value), "0x%06X", (unsigned)(self->pick_rgb & 0xffffff));
    if( !palette_field_command(ui, doc_key, "colour", value, &command) )
    {
        Editor_UiStatus(ui, "%s %d %s is #%06x already", type, id, custom->name, (unsigned)(self->pick_rgb & 0xffffff));
        return;
    }
    if( edp_apply(ui, &command, 1, kind ? "recolour overlay" : "recolour underlay") <= 0 )
    {
        Editor_UiStatus(ui, "Recolour refused by the editor server; the Output console has its line");
        return;
    }
    palette_custom_follow(ui, self);
    Editor_UiStatus(ui, "%s %d %s is #%06x now (unsaved; Save writes it)", type, id, custom->name,
        (unsigned)(self->pick_rgb & 0xffffff));
    palette_rebuild(ui, self);
}

/** "#rrggbb" or "rrggbb" into `out`. */
static int
palette_parse_hex(
    const char* text,
    uint32_t* out)
{
    char* end = NULL;
    unsigned long value;

    while( *text == ' ' )
        text++;
    if( *text == '#' )
        text++;
    if( strlen(text) != 6 )
        return 0;
    value = strtoul(text, &end, 16);
    if( !end || *end != '\0' )
        return 0;
    *out = (uint32_t)value;
    return 1;
}

static int
palette_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct PalettePanel* self = opaque;

    if( widget == self->button_drop_underlay || widget == self->button_drop_overlay )
    {
        int const dropper = widget == self->button_drop_overlay ? 2 : 1;
        if( self->dropper == dropper )
        {
            palette_dropper_set(ui, self, 0);
            Editor_UiStatus(ui, "Eyedropper off");
            return 1;
        }
        palette_dropper_set(ui, self, dropper);
        Editor_UiStatus(ui, "Eyedropper: click a tile to pick its %s", dropper == 2 ? "overlay" : "underlay");
        return 1;
    }
    if( widget == self->dropdown_rotation )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( row >= 0 && row < 4 )
            ui->tools.overlay_rotation = row;
        self->dirty_draw = 1;
        return 1;
    }
    if( widget == self->input_find )
    {
        /* Enter: the wells already follow the typing (palette_tick); this
         * says what was found. */
        palette_find_set(ui, self, ToriRSChrome_Text(ui->chrome, widget));
        if( self->find[0] == '\0' )
            Editor_UiStatus(ui, "Palette: Find cleared");
        else if( self->underlays.count + self->overlays.count <= 2 )
            Editor_UiStatus(ui, "Palette: \"%s\" finds nothing (try grass, dirt, sand, stone, wood, water, an id, a #colour)",
                self->find);
        else
            Editor_UiStatus(ui, "Palette: \"%s\" finds %d underlay%s and %d overlay%s; click one, or Pick by name",
                self->find, self->underlays.count - 1, self->underlays.count == 2 ? "" : "s",
                self->overlays.count - 1, self->overlays.count == 2 ? "" : "s");
        return 1;
    }
    for( int kind = 0; kind < 2; kind++ )
    {
        if( widget == self->button_new[kind] )
        {
            palette_new_record(ui, self, kind);
            return 1;
        }
        if( widget == self->button_recolour[kind] )
        {
            palette_recolour(ui, self, kind);
            return 1;
        }
    }
    /* Enter in the name: the name is read when a button is pressed. */
    if( widget == self->input_new_name )
        return 1;
    if( widget == self->input_underlay || widget == self->input_overlay )
    {
        palette_pick_typed(ui, self, widget == self->input_overlay, widget);
        return 1;
    }
    if( widget == self->check_all )
    {
        self->list_all = ToriRSChrome_Checked(ui->chrome, widget) ? 1 : 0;
        palette_rebuild(ui, self);
        return 1;
    }
    if( widget == self->dropdown_sort )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( row >= 0 && row < PALETTE_SORT_COUNT )
            self->sort = (enum PaletteSort)row;
        palette_rebuild(ui, self);
        return 1;
    }
    if( widget == self->slider_hue || widget == self->slider_sat || widget == self->slider_light )
    {
        char text[16];
        palette_pick_set_hsl(ui, self, (ToriRSChrome_SliderValue(ui->chrome, self->slider_hue) * 256 + 180) / 360,
            (ToriRSChrome_SliderValue(ui->chrome, self->slider_sat) * 255 + 50) / 100,
            (ToriRSChrome_SliderValue(ui->chrome, self->slider_light) * 255 + 50) / 100);
        snprintf(text, sizeof(text), "#%06x", (unsigned)self->pick_rgb);
        ToriRSChrome_SetText(ui->chrome, self->input_hex, text);
        palette_match(ui, self);
        return 1;
    }
    if( widget == self->input_hex )
    {
        uint32_t rgb;
        if( !palette_parse_hex(ToriRSChrome_Text(ui->chrome, widget), &rgb) )
        {
            Editor_UiLog(ui, "editor: a colour is #rrggbb");
            return 1;
        }
        palette_pick_set_rgb(ui, self, rgb);
        palette_match(ui, self);
        return 1;
    }
    return 0;
}

static void
palette_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct PalettePanel* self = opaque;
    if( visible )
    {
        self->dirty_draw = 1;
        /* Ask again for a common record that was not there at boot. */
        self->common_rounds = PALETTE_COMMON_ROUNDS;
    }
    /* A dropper the user can no longer see is not left to eat a click. */
    else if( self->dropper )
        palette_dropper_set(ui, self, 0);
}

static void
palette_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

/* ---- for the tests ------------------------------------------------------------ */

static struct PalettePanel*
palette_self(struct EditorUi* ui)
{
    struct EditorUiPanel* module = Editor_UiFindPanel(ui, "palette");
    assert(module);
    return module->self;
}

int
Editor_Palette_SwatchAt(
    struct EditorUi* ui,
    int is_overlay,
    int id,
    int* out_x,
    int* out_y)
{
    struct PalettePanel* self;
    const struct PaletteList* list;
    int cols;

    assert(ui);
    assert(out_x);
    assert(out_y);
    self = palette_self(ui);
    list = is_overlay ? &self->overlays : &self->underlays;
    cols = self->well_cols > 0 ? self->well_cols : palette_cols(ui, self);
    for( int i = 0; i < list->count; i++ )
    {
        if( list->ids[i] != id )
            continue;
        *out_x = 1 + (i % cols) * PALETTE_CELL + (PALETTE_CELL - 2) / 2;
        *out_y = 1 + (i / cols) * PALETTE_CELL + (PALETTE_CELL - 2) / 2;
        return 1;
    }
    return 0;
}

void
Editor_Palette_SwatchGrid(
    struct EditorUi* ui,
    int* out_cell,
    int* out_cols)
{
    struct PalettePanel* self;

    assert(ui);
    assert(out_cell);
    assert(out_cols);
    self = palette_self(ui);
    *out_cell = PALETTE_CELL;
    *out_cols = self->well_cols > 0 ? self->well_cols : palette_cols(ui, self);
}

/* ---- names and defaults, for the other panels ------------------------------------- */

void
Editor_Palette_Describe(
    struct EditorUi* ui,
    int is_overlay,
    int id,
    char* out,
    size_t out_size)
{
    struct EditorUiPanel* module;
    char name[EDITOR_FLOOR_NAME_MAX];

    assert(ui);
    assert(out);
    assert(out_size >= EDITOR_PALETTE_DESCRIBE_MAX);
    /* A window without the palette still names a floor: by the common set
     * and its colour, with no texture known. */
    module = Editor_UiFindPanel(ui, "palette");
    palette_name(ui, module ? module->self : NULL, is_overlay ? 1 : 0, id, name);
    if( id == 0 )
        snprintf(out, out_size, "none");
    else if( name[0] )
        snprintf(out, out_size, "%d %s", id, name);
    else
        snprintf(out, out_size, "%d", id);
}

int
Editor_Palette_PathFloor(
    struct EditorUi* ui,
    int style,
    char* out_name,
    size_t out_size)
{
    static const enum Editor_FloorRole k_roles[EDITOR_PATH_STYLE_COUNT] = {
        [EDITOR_PATH_DIRT] = EDITOR_FLOOR_ROLE_DIRT_TRACK,
        [EDITOR_PATH_PAVED] = EDITOR_FLOOR_ROLE_PAVING,
        [EDITOR_PATH_RIVER] = EDITOR_FLOOR_ROLE_WATER,
    };
    struct EditorUiPanel* module;
    struct PalettePanel* self;
    int count = 0;
    const struct Editor_FloorCommon* common = Editor_FloorCommonList(&count);

    assert(ui);
    assert(out_name);
    assert(out_size >= EDITOR_FLOOR_NAME_MAX);
    assert(style >= 0);
    assert(style < EDITOR_PATH_STYLE_COUNT);
    module = Editor_UiFindPanel(ui, "palette");
    self = module ? module->self : NULL;
    out_name[0] = '\0';
    for( int i = 0; i < count; i++ )
    {
        if( common[i].role != k_roles[style] )
            continue;
        if( !palette_common_ok(ui, self, &common[i]) )
        {
            /* Not loaded yet (asked for now), or not this cache's. */
            palette_common_request(ui);
            if( self )
                self->common_rounds = PALETTE_COMMON_ROUNDS;
            return -1;
        }
        snprintf(out_name, out_size, "%s", common[i].name);
        return common[i].id;
    }
    return -1;
}

int
Editor_Palette_PathOverlay(
    struct EditorUi* ui,
    int style,
    char* out_name,
    size_t out_size)
{
    struct EditorUiPanel* module;
    struct PalettePanel* self;
    int id;

    assert(ui);
    assert(out_name);
    assert(out_size >= EDITOR_FLOOR_NAME_MAX);
    module = Editor_UiFindPanel(ui, "palette");
    self = module ? module->self : NULL;
    out_name[0] = '\0';
    /* An overlay the user picked is the path's. One picked here for another
     * style follows the style: dirt's track is not what a river lays. */
    if( !ui->tools.overlay_none
        && !(self && self->auto_overlay == ui->tools.overlay_id && self->auto_style != style) )
        return 0;
    id = Editor_Palette_PathFloor(ui, style, out_name, out_size);
    if( id <= 0 )
        return -1;
    ui->tools.overlay_none = 0;
    ui->tools.overlay_id = id;
    if( self )
    {
        self->auto_overlay = id;
        self->auto_style = style;
        self->dirty_draw = 1;
    }
    return id;
}

int
Editor_Palette_PathUseFloor(
    struct EditorUi* ui,
    int style,
    char* out_name,
    size_t out_size)
{
    int const was_none = ui->tools.overlay_none;
    int id;

    assert(ui);
    /* As if nothing were picked: the style's floor, picked the way the
     * path painter picks it (so it follows a later change of style). */
    ui->tools.overlay_none = 1;
    id = Editor_Palette_PathOverlay(ui, style, out_name, out_size);
    if( id < 0 )
        ui->tools.overlay_none = was_none;
    return id;
}

int
Editor_Palette_Matches(
    struct EditorUi* ui,
    int is_overlay,
    int* out_ids,
    int max)
{
    struct PalettePanel* self;
    const int* ids;
    int count;

    assert(ui);
    assert(out_ids);
    self = palette_self(ui);
    ids = is_overlay ? self->match_over : self->match_under;
    count = is_overlay ? self->match_over_count : self->match_under_count;
    if( count > max )
        count = max;
    memcpy(out_ids, ids, (size_t)count * sizeof(int));
    return count;
}

int
Editor_Palette_BlendFrom(struct EditorUi* ui)
{
    assert(ui);
    return palette_self(ui)->blend_from;
}

/* ---- the eyedropper, for the App -------------------------------------------------- */

int
Editor_Palette_DropperArmed(struct EditorUi* ui)
{
    struct EditorUiPanel* module;

    assert(ui);
    /* A window without the palette has no dropper to arm. */
    module = Editor_UiFindPanel(ui, "palette");
    if( !module )
        return 0;
    return ((struct PalettePanel*)module->self)->dropper;
}

int
Editor_Palette_DropperAt(
    struct EditorUi* ui,
    int x,
    int z,
    int level)
{
    struct PalettePanel* self;
    struct Editor_Square* square;
    struct Editor_Tile const* tile;
    char armed[PALETTE_ARM_TEXT];
    int is_overlay;

    assert(ui);
    self = palette_self(ui);
    assert(self->dropper);
    is_overlay = self->dropper == 2;
    /* One pick per arming, landed or refused: a refusal is said, not
     *  retried on every later click. */
    palette_dropper_set(ui, self, 0);
    square = ui->editor ? Editor_DocFindSquare(&ui->editor->doc, x / 64, z / 64) : NULL;
    if( !square || !square->loaded )
    {
        Editor_UiStatus(ui, "Eyedropper refused: m%d_%d is not open", x / 64, z / 64);
        return 0;
    }
    tile = &square->tiles[Editor_TileIndex(x % 64, z % 64, level)];
    if( !is_overlay )
    {
        palette_pick_underlay(ui, tile->underlay_id, armed);
        Editor_UiStatus(ui, "Eyedropper: underlay %d from %d,%d L%d: %s", tile->underlay_id, x, z, level, armed);
    }
    else if( !tile->has_overlay )
    {
        palette_pick_overlay(ui, 0, armed);
        Editor_UiStatus(ui, "Eyedropper: no overlay at %d,%d L%d (none picked): %s", x, z, level, armed);
    }
    else
    {
        /* As I picks it: overlay 0 with a shape is a cut-out of the
         * underlay, not "none". */
        ui->tools.overlay_none = 0;
        ui->tools.overlay_id = tile->overlay_id;
        ui->tools.overlay_shape = tile->shape;
        ui->tools.overlay_rotation = tile->rotation & 3;
        palette_arm(ui, PALETTE_PICK_OVERLAY, armed);
        Editor_UiStatus(ui, "Eyedropper: overlay %d shape %d rot %d from %d,%d L%d: %s", tile->overlay_id, tile->shape,
            (tile->rotation & 3) * 90, x, z, level, armed);
    }
    self->dirty_draw = 1;
    return 1;
}

/* ---- registration -------------------------------------------------------------- */

void
Editor_Palette_Register(struct EditorUi* ui)
{
    struct PalettePanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    for( int r = 0; r < 4; r++ )
        for( int s = 0; s < EDITOR_SHAPE_COUNT; s++ )
            Editor_ShapeThumbBuild(s, r, &self->thumbs[r][s]);
    self->hover_graph = -1;
    self->hover_key = -1;
    self->common_rounds = PALETTE_COMMON_ROUNDS;
    self->common_seen = -1;

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X), edp_px(ui, 300),
        edp_px(ui, EDP_COL_W), "Palette");
    assert(self->panel >= 0);
    /* What the wells list. Tooltips are cut at 63 bytes by the chrome. */
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "A name, an id or a #colour: dirt, water, wood, 48, #3d2b0b");
    self->check_all = ToriRSChrome_Checkbox(chrome, self->panel, "Every id", 0);
    ToriRSChrome_SetTooltip(chrome, self->check_all, "Every record the window has, not only the common and used");

    self->row_underlay = ToriRSChrome_Label(chrome, self->panel, "Underlay");
    self->graph_underlay = ToriRSChrome_Graph(chrome, self->panel, "", PALETTE_CELL * 2 + 2);
    ToriRSChrome_GraphSetBare(chrome, self->graph_underlay, 1, PALETTE_CELL * 2 + 2);
    self->input_underlay = ToriRSChrome_TextInput(chrome, self->panel, "Pick underlay", "");
    ToriRSChrome_SetTooltip(chrome, self->input_underlay, "An id or a name (grass, dirt, sand, snow); Enter picks it");
    self->row_overlay = ToriRSChrome_Label(chrome, self->panel, "Overlay");
    self->graph_overlay = ToriRSChrome_Graph(chrome, self->panel, "", PALETTE_CELL * 2 + 2);
    ToriRSChrome_GraphSetBare(chrome, self->graph_overlay, 1, PALETTE_CELL * 2 + 2);
    self->input_overlay = ToriRSChrome_TextInput(chrome, self->panel, "Pick overlay", "");
    ToriRSChrome_SetTooltip(chrome, self->input_overlay, "An id or a name (dirt, stone, wood, water); Enter picks it");
    /* The overlay's shape and turn: one row each (four radios were four). */
    self->row_shape = ToriRSChrome_Label(chrome, self->panel, "Shape");
    self->graph_shape = ToriRSChrome_Graph(
        chrome, self->panel, "", ((EDITOR_SHAPE_COUNT + SHAPE_COLS - 1) / SHAPE_COLS) * (SHAPE_BOX + 4) + 2);
    ToriRSChrome_GraphSetBare(chrome, self->graph_shape, 1,
        ((EDITOR_SHAPE_COUNT + SHAPE_COLS - 1) / SHAPE_COLS) * (SHAPE_BOX + 4) + 2);
    ToriRSChrome_SetTooltip(chrome, self->graph_shape, "The overlay's shape: how much of the tile it covers");
    self->dropdown_rotation = ToriRSChrome_Dropdown(chrome, self->panel, "Rotation", k_rotation_names, 4, 0);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_rotation, "Turns the overlay's shape a quarter turn at a time");
    self->dropdown_sort = ToriRSChrome_Dropdown(chrome, self->panel, "Sort", k_sort_names, PALETTE_SORT_COUNT, 0);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_sort, "By hue a well is a colour wheel: neighbours blend together");
    /* The one-layer eyedroppers, under what a stroke needs: I picks every
     * layer of the tile under the pointer at once, from the keyboard. */
    self->button_drop_underlay =
        ToriRSChrome_IconButton(chrome, self->panel, TORIRS_CHROME_SKIN_TOOL_EYEDROPPER, k_dropper_captions[0]);
    ToriRSChrome_IconButtonSetGlyph(chrome, self->button_drop_underlay, TORIRS_CHROME_ICON_EYEDROPPER);
    ToriRSChrome_SetTooltip(chrome, self->button_drop_underlay, "The next click on the ground picks its underlay");
    self->button_drop_overlay =
        ToriRSChrome_IconButton(chrome, self->panel, TORIRS_CHROME_SKIN_TOOL_EYEDROPPER, k_dropper_captions[1]);
    ToriRSChrome_IconButtonSetGlyph(chrome, self->button_drop_overlay, TORIRS_CHROME_ICON_EYEDROPPER);
    ToriRSChrome_SetTooltip(chrome, self->button_drop_overlay, "The next click on the ground picks its overlay and shape");

    /* Under the fold of a docked panel from here: the Textures well. */
    ToriRSChrome_Separator(chrome, self->panel);
    self->row_textures = ToriRSChrome_Label(chrome, self->panel, "Textures");
    self->graph_textures = ToriRSChrome_Graph(chrome, self->panel, "", PALETTE_TEX_CELL + 2);
    ToriRSChrome_GraphSetBare(chrome, self->graph_textures, 1, PALETTE_TEX_CELL + 2);
    ToriRSChrome_SetTooltip(chrome, self->graph_textures, "click a texture to pick an overlay that lays it");

    /* The blend strip. */
    ToriRSChrome_Separator(chrome, self->panel);
    self->row_blend = ToriRSChrome_Label(chrome, self->panel, "Blend");
    self->graph_blend = ToriRSChrome_Graph(chrome, self->panel, "", PALETTE_CELL + 2);
    ToriRSChrome_GraphSetBare(chrome, self->graph_blend, 1, PALETTE_CELL + 2);
    ToriRSChrome_SetTooltip(chrome, self->graph_blend, "The ground across a seam between the last two underlay picks");

    /* The colour picker. */
    ToriRSChrome_Separator(chrome, self->panel);
    self->row_pick = ToriRSChrome_Label(chrome, self->panel, "Find by colour");
    self->slider_hue = ToriRSChrome_Slider(chrome, self->panel, "Hue", 0, 359, 0, 1);
    self->slider_sat = ToriRSChrome_Slider(chrome, self->panel, "Sat (%)", 0, 100, 0, 1);
    self->slider_light = ToriRSChrome_Slider(chrome, self->panel, "Light (%)", 0, 100, 0, 1);
    self->input_hex = ToriRSChrome_TextInput(chrome, self->panel, "#", "");
    ToriRSChrome_SetTooltip(chrome, self->input_hex, "Type a colour as #rrggbb and press Enter");
    self->graph_match = ToriRSChrome_Graph(chrome, self->panel, "", 2 * (PALETTE_CELL + 2) + 2);
    ToriRSChrome_GraphSetBare(chrome, self->graph_match, 1, 2 * (PALETTE_CELL + 2) + 2);
    ToriRSChrome_SetTooltip(chrome, self->graph_match, "The colour, its nearest underlays and overlays; click picks");
    self->row_match_under = ToriRSChrome_Label(chrome, self->panel, "Underlays:");
    self->row_match_over = ToriRSChrome_Label(chrome, self->panel, "Overlays:");

    /* New records, in the picker's colour. Tooltips are cut at 63 bytes. */
    ToriRSChrome_Separator(chrome, self->panel);
    self->row_new = ToriRSChrome_Label(chrome, self->panel, "New floor record");
    self->input_new_name = ToriRSChrome_TextInput(chrome, self->panel, "name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_new_name, "a-z 0-9 _ for the new record; empty names it by its colour");
    self->button_new[0] = ToriRSChrome_Button(chrome, self->panel, "New underlay of this colour");
    ToriRSChrome_SetTooltip(chrome, self->button_new[0], "A new underlay record in the colour above, picked to paint");
    self->button_new[1] = ToriRSChrome_Button(chrome, self->panel, "New overlay of this colour");
    ToriRSChrome_SetTooltip(chrome, self->button_new[1], "A new overlay record in the colour above, picked to paint");
    self->button_recolour[0] = ToriRSChrome_Button(chrome, self->panel, "Recolour the picked underlay");
    ToriRSChrome_SetTooltip(chrome, self->button_recolour[0], "Its colour becomes the one above; only a record made here");
    self->button_recolour[1] = ToriRSChrome_Button(chrome, self->panel, "Recolour the picked overlay");
    ToriRSChrome_SetTooltip(chrome, self->button_recolour[1], "Its colour becomes the one above; only a record made here");
    for( int kind = 0; kind < 2; kind++ )
    {
        ToriRSChrome_ButtonSetFill(chrome, self->button_new[kind], 1);
        ToriRSChrome_ButtonSetFill(chrome, self->button_recolour[kind], 1);
    }
    palette_pick_set_hsl(ui, self, 64, 96, 96);

    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "palette";
    desc.title = "Palette";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = palette_tick;
    desc.event = palette_event;
    desc.activate = palette_activate;
    desc.set_visible = palette_set_visible;
    desc.destroy = palette_destroy;
    Editor_UiAddPanel(ui, &desc);
}

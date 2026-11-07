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
 * Colours come from the window (hooks.floor_rgb, the provider's records); a
 * record the window has not loaded draws as a hatched grey cell with its id,
 * so the palette still works on a cache the editor cannot fully decode.
 */

#include "edpanel_common.h"
#include "edpanel_shapes.h"
#include "edpanels.h"
#include "editor/editor_floorcolor.h"
#include "mem/torirs_mem.h"

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
#define PALETTE_WELL_W 184
#define PALETTE_CELL 14
#define PALETTE_COLS (PALETTE_WELL_W / PALETTE_CELL)
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

static const char* const k_rotation_names[4] = { "0", "90", "180", "270" };
static const char* const k_sort_names[PALETTE_SORT_COUNT] = { "by id", "by hue", "by lightness" };

struct PaletteList
{
    /** File ids (the `.jm2` value, config id + 1); 0 is "none". */
    int ids[PALETTE_IDS_MAX];
    int count;
};

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
    int row_shape;
    int graph_shape;
    int radio_rotation[4];
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
    for( int id = 1; id <= probe; id++ )
    {
        uint32_t rgb;
        int d;
        int at;
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

static int
palette_cmp_int(
    const void* a,
    const void* b)
{
    return *(const int*)a - *(const int*)b;
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

/** Order a list (past its leading "none") by the sort: ids, hue (greys at
 *  the end by lightness, since their hue says nothing) or lightness; a record
 *  the window has not loaded goes last, by id. */
static void
palette_list_sort(
    struct EditorUi* ui,
    struct PaletteList* list,
    int is_overlay,
    enum PaletteSort sort)
{
    struct PaletteSortKey* keys;
    int const n = list->count - 1;

    if( n <= 1 )
        return;
    if( sort == PALETTE_SORT_ID )
    {
        qsort(list->ids + 1, (size_t)n, sizeof(int), palette_cmp_int);
        return;
    }
    keys = ToriMem_Calloc(ToriMem_Editor(), (size_t)n, sizeof(*keys));
    assert(keys);
    for( int i = 0; i < n; i++ )
    {
        struct Editor_FloorHsl hsl;
        uint32_t rgb;
        keys[i].id = list->ids[i + 1];
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

static void
palette_rebuild(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    struct Editor_Doc* doc;

    self->underlays.count = 0;
    self->overlays.count = 0;
    palette_list_add(&self->underlays, 0);
    palette_list_add(&self->overlays, 0);
    if( self->list_all )
    {
        uint32_t rgb;
        for( int id = 1; id <= PALETTE_UNDERLAY_PROBE; id++ )
            if( palette_rgb(ui, 0, id, &rgb) )
                palette_list_add(&self->underlays, id);
        for( int id = 1; id <= PALETTE_OVERLAY_PROBE; id++ )
            if( palette_rgb(ui, 1, id, &rgb) )
                palette_list_add(&self->overlays, id);
    }
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
    if( ui->tools.underlay_id )
        palette_list_add(&self->underlays, ui->tools.underlay_id);
    /* The strip's other end stays in reach: a pick is often "this one or
     * the one before". */
    if( self->blend_from )
        palette_list_add(&self->underlays, self->blend_from);
    if( !ui->tools.overlay_none )
        palette_list_add(&self->overlays, ui->tools.overlay_id);
    palette_list_sort(ui, &self->underlays, 0, self->sort);
    palette_list_sort(ui, &self->overlays, 1, self->sort);
    self->built_signature = palette_signature(ui);
    self->dirty_draw = 1;
}

/* ---- drawing ---------------------------------------------------------------- */

static int
palette_well_h(int count)
{
    int const rows = (count + PALETTE_COLS - 1) / PALETTE_COLS;
    return (rows > 0 ? rows : 1) * PALETTE_CELL + 2;
}

/** One swatch: the record's colour, "none" crossed out, unloaded hatched. */
static void
palette_draw_swatch(
    struct EditorUi* ui,
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
    uint32_t rgb = 0;

    if( id == 0 )
    {
        ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, 0x201c16, 1, key);
        ToriRSChrome_GraphLine(chrome, graph, x, y + h - 1, x + w - 1, y, 0xb04030);
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
    int graph,
    const struct PaletteList* list,
    int is_overlay,
    int picked)
{
    struct ToriRSChrome* chrome = ui->chrome;

    ToriRSChrome_SetCustomHeight(chrome, graph, palette_well_h(list->count));
    ToriRSChrome_GraphBegin(chrome, graph);
    for( int i = 0; i < list->count; i++ )
    {
        int const x = 1 + (i % PALETTE_COLS) * PALETTE_CELL;
        int const y = 1 + (i / PALETTE_COLS) * PALETTE_CELL;

        palette_draw_swatch(ui, graph, x, y, PALETTE_CELL - 2, PALETTE_CELL - 2, is_overlay, list->ids[i], i + 1);
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
        palette_draw_swatch(ui, self->graph_match, x0 + i * PALETTE_MATCH_CELL, 1, PALETTE_MATCH_CELL - 2,
            PALETTE_CELL, 0, self->match_under[i], 1 + i);
    for( int i = 0; i < self->match_over_count; i++ )
        palette_draw_swatch(ui, self->graph_match, x0 + i * PALETTE_MATCH_CELL, 1 + row_h, PALETTE_MATCH_CELL - 2,
            PALETTE_CELL, 1, self->match_over[i], PALETTE_MATCH_KEY_OVERLAY + 1 + i);
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
    else if( palette_rgb(ui, 0, ui->tools.underlay_id, &rgb) )
        snprintf(line, sizeof(line), "Underlay: %d  #%06x", ui->tools.underlay_id, (unsigned)rgb);
    else
        snprintf(line, sizeof(line), "Underlay: %d (not loaded)", ui->tools.underlay_id);
    ToriRSChrome_SetText(ui->chrome, self->row_underlay, line);

    if( ui->tools.overlay_none )
        snprintf(line, sizeof(line), "Overlay: none");
    else if( palette_rgb(ui, 1, ui->tools.overlay_id, &rgb) )
        snprintf(line, sizeof(line), "Overlay: %d  #%06x", ui->tools.overlay_id, (unsigned)rgb);
    else
        snprintf(line, sizeof(line), "Overlay: %d", ui->tools.overlay_id);
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
    palette_match_line(line, (int)sizeof(line), "Underlays:", self->match_under, self->match_under_count);
    ToriRSChrome_SetText(ui->chrome, self->row_match_under, line);
    palette_match_line(line, (int)sizeof(line), "Overlays:", self->match_over, self->match_over_count);
    ToriRSChrome_SetText(ui->chrome, self->row_match_over, line);
}

/** The tooltip of a well names the swatch under the pointer: id, colour and
 *  the hue / sat / light the client packs it to (64, 8 and 128 steps). */
static void
palette_tooltip(
    struct EditorUi* ui,
    struct PalettePanel* self,
    int graph,
    int is_overlay,
    int id)
{
    char text[TORIRS_CHROME_LABEL_MAX];
    uint32_t rgb;

    (void)self;
    if( id < 0 )
        snprintf(text, sizeof(text), "click a swatch to pick it; hover for its id and colour");
    else if( id == 0 )
        snprintf(text, sizeof(text), "none%s", is_overlay ? ": clears the overlay" : "");
    else if( palette_rgb(ui, is_overlay, id, &rgb) )
    {
        struct Editor_FloorHsl hsl;
        Editor_FloorHslOfRgb(rgb, &hsl);
        snprintf(text, sizeof(text), "%s %d  #%06x  hue %d sat %d light %d", is_overlay ? "overlay" : "underlay", id,
            (unsigned)rgb, hsl.hue / 4, hsl.sat / 32, hsl.light / 2);
    }
    else
        snprintf(text, sizeof(text), "%s %d (not loaded)", is_overlay ? "overlay" : "underlay", id);
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

static void
palette_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct PalettePanel* self = opaque;

    if( !ui->chrome->panels[self->panel].visible )
        return;
    if( palette_picks_changed(ui, self) )
    {
        palette_rebuild(ui, self);
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_rotation[ui->tools.overlay_rotation & 3]);
    }
    if( self->built_signature != palette_signature(ui) || self->underlays.count == 0 )
        palette_rebuild(ui, self);
    if( self->list_all )
    {
        if( self->all_asked[0] < PALETTE_UNDERLAY_PROBE || self->all_asked[1] < PALETTE_OVERLAY_PROBE )
            palette_request_all(ui, self);
        if( self->all_settle > 0 && --self->all_settle % 15 == 0 )
            palette_rebuild(ui, self);
    }
    if( !self->dirty_draw )
        return;
    palette_draw_list(ui, self->graph_underlay, &self->underlays, 0, ui->tools.underlay_id);
    palette_draw_list(
        ui, self->graph_overlay, &self->overlays, 1, ui->tools.overlay_none ? 0 : ui->tools.overlay_id);
    palette_draw_shapes(ui, self);
    palette_draw_blend(ui, self);
    palette_draw_match(ui, self);
    palette_labels(ui, self);
    self->dirty_draw = 0;
}

/* ---- input ------------------------------------------------------------------ */

/** Picking paint arms the matching paint brush -- except under the rectangle
 *  tool, whose Fill reads the same picks. */
static void
palette_arm(
    struct EditorUi* ui,
    enum Editor_Tool tool)
{
    if( ui->tools.tool != EDITOR_TOOL_RECT_SELECT )
        Editor_UiSetTool(ui, tool);
}

static void
palette_pick_underlay(
    struct EditorUi* ui,
    int id)
{
    ui->tools.underlay_id = id;
    palette_arm(ui, EDITOR_TOOL_PAINT_UNDERLAY);
}

static void
palette_pick_overlay(
    struct EditorUi* ui,
    int id)
{
    ui->tools.overlay_none = id == 0;
    ui->tools.overlay_id = id;
    palette_arm(ui, EDITOR_TOOL_PAINT_OVERLAY);
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
    int is_overlay = 0;
    int id;

    if( event->widget != self->graph_underlay && event->widget != self->graph_overlay
        && event->widget != self->graph_shape && event->widget != self->graph_match )
        return 0;
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_HOVER )
    {
        if( event->widget == self->graph_shape )
            return 1;
        if( self->hover_graph == event->widget && self->hover_key == key )
            return 1;
        self->hover_graph = event->widget;
        self->hover_key = key;
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
            palette_arm(ui, EDITOR_TOOL_PAINT_OVERLAY);
        }
        self->dirty_draw = 1;
        return 1;
    }
    id = palette_key_id(self, event->widget, key, &is_overlay);
    if( id < 0 )
        return 1;
    if( is_overlay )
        palette_pick_overlay(ui, id);
    else
        palette_pick_underlay(ui, id);
    self->dirty_draw = 1;
    return 1;
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
    for( int r = 0; r < 4; r++ )
        if( widget == self->radio_rotation[r] )
        {
            ui->tools.overlay_rotation = r;
            self->dirty_draw = 1;
            return 1;
        }
    if( widget == self->input_underlay || widget == self->input_overlay )
    {
        int const is_overlay = widget == self->input_overlay;
        int const id = atoi(ToriRSChrome_Text(ui->chrome, widget));
        if( id < 0 || id > (is_overlay ? 0xffff : 0xff) )
        {
            Editor_UiLog(ui, "editor: %s ids are 0..%d", is_overlay ? "overlay" : "underlay",
                is_overlay ? 0xffff : 0xff);
            return 1;
        }
        if( is_overlay )
        {
            ui->tools.overlay_id = id;
            ui->tools.overlay_none = id == 0;
            palette_list_add(&self->overlays, id);
        }
        else
        {
            ui->tools.underlay_id = id;
            palette_list_add(&self->underlays, id);
        }
        self->dirty_draw = 1;
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
        self->dirty_draw = 1;
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

    assert(ui);
    assert(out_x);
    assert(out_y);
    self = palette_self(ui);
    list = is_overlay ? &self->overlays : &self->underlays;
    for( int i = 0; i < list->count; i++ )
    {
        if( list->ids[i] != id )
            continue;
        *out_x = 1 + (i % PALETTE_COLS) * PALETTE_CELL + (PALETTE_CELL - 2) / 2;
        *out_y = 1 + (i / PALETTE_COLS) * PALETTE_CELL + (PALETTE_CELL - 2) / 2;
        return 1;
    }
    return 0;
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
        palette_pick_underlay(ui, tile->underlay_id);
        Editor_UiStatus(ui, "Eyedropper: underlay %d from %d,%d L%d", tile->underlay_id, x, z, level);
    }
    else if( !tile->has_overlay )
    {
        palette_pick_overlay(ui, 0);
        Editor_UiStatus(ui, "Eyedropper: no overlay at %d,%d L%d (none picked)", x, z, level);
    }
    else
    {
        /* As I picks it: overlay 0 with a shape is a cut-out of the
         * underlay, not "none". */
        ui->tools.overlay_none = 0;
        ui->tools.overlay_id = tile->overlay_id;
        ui->tools.overlay_shape = tile->shape;
        ui->tools.overlay_rotation = tile->rotation & 3;
        palette_arm(ui, EDITOR_TOOL_PAINT_OVERLAY);
        Editor_UiStatus(ui, "Eyedropper: overlay %d shape %d rot %d from %d,%d L%d", tile->overlay_id, tile->shape,
            (tile->rotation & 3) * 90, x, z, level);
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

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X), edp_px(ui, 300),
        edp_px(ui, EDP_COL_W), "Palette");
    assert(self->panel >= 0);
    self->row_underlay = ToriRSChrome_Label(chrome, self->panel, "Underlay");
    self->graph_underlay = ToriRSChrome_Graph(chrome, self->panel, "", PALETTE_CELL * 2 + 2);
    self->input_underlay = ToriRSChrome_TextInput(chrome, self->panel, "id", "");
    ToriRSChrome_SetTooltip(chrome, self->input_underlay, "Type an underlay id and press Enter");
    self->button_drop_underlay =
        ToriRSChrome_IconButton(chrome, self->panel, TORIRS_CHROME_SKIN_TOOL_EYEDROPPER, k_dropper_captions[0]);
    ToriRSChrome_SetTooltip(chrome, self->button_drop_underlay, "The next click on the ground picks its underlay");
    self->row_overlay = ToriRSChrome_Label(chrome, self->panel, "Overlay");
    self->graph_overlay = ToriRSChrome_Graph(chrome, self->panel, "", PALETTE_CELL * 2 + 2);
    self->input_overlay = ToriRSChrome_TextInput(chrome, self->panel, "id", "");
    ToriRSChrome_SetTooltip(chrome, self->input_overlay, "Type an overlay id and press Enter");
    self->button_drop_overlay =
        ToriRSChrome_IconButton(chrome, self->panel, TORIRS_CHROME_SKIN_TOOL_EYEDROPPER, k_dropper_captions[1]);
    ToriRSChrome_SetTooltip(chrome, self->button_drop_overlay, "The next click on the ground picks its overlay and shape");
    self->row_shape = ToriRSChrome_Label(chrome, self->panel, "Shape");
    self->graph_shape = ToriRSChrome_Graph(
        chrome, self->panel, "", ((EDITOR_SHAPE_COUNT + SHAPE_COLS - 1) / SHAPE_COLS) * (SHAPE_BOX + 4) + 2);
    for( int r = 0; r < 4; r++ )
        self->radio_rotation[r] =
            ToriRSChrome_Radio(chrome, self->panel, k_rotation_names[r], PALETTE_GROUP_ROTATION, r == 0);

    /* The wells' scope and order. */
    ToriRSChrome_Separator(chrome, self->panel);
    self->check_all = ToriRSChrome_Checkbox(chrome, self->panel, "Every id", 0);
    /* Tooltips are cut at 63 bytes by the chrome. */
    ToriRSChrome_SetTooltip(chrome, self->check_all, "List every record the window has, not only the open squares'");
    self->dropdown_sort = ToriRSChrome_Dropdown(chrome, self->panel, "Sort", k_sort_names, PALETTE_SORT_COUNT, 0);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_sort, "By hue a well is a colour wheel: neighbours blend together");

    /* The blend strip. */
    ToriRSChrome_Separator(chrome, self->panel);
    self->row_blend = ToriRSChrome_Label(chrome, self->panel, "Blend");
    self->graph_blend = ToriRSChrome_Graph(chrome, self->panel, "", PALETTE_CELL + 2);
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
    ToriRSChrome_SetTooltip(chrome, self->graph_match, "The colour, its nearest underlays and overlays; click picks");
    self->row_match_under = ToriRSChrome_Label(chrome, self->panel, "Underlays:");
    self->row_match_over = ToriRSChrome_Label(chrome, self->panel, "Overlays:");
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

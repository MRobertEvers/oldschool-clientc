/*
 * The palette: underlay and overlay SWATCHES (each a cell in the record's
 * own colour), the overlay SHAPE thumbnails sampled from the mesh builder
 * (edpanel_shapes.c), and the rotation (design #terrain).
 *
 * The swatches list the ids the open squares use, plus any id typed into the
 * id fields -- the ids in front of you are the ones you are about to paint
 * with, and a palette of every id is 255 rows of mostly nothing. Colours come
 * from the window (hooks.floor_rgb, the provider's records); a record the
 * window has not loaded draws as a hatched grey cell with its id, so the
 * palette still works on a cache the editor cannot fully decode.
 */

#include "edpanel_common.h"
#include "edpanel_shapes.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PALETTE_IDS_MAX 512
#define PALETTE_WELL_W 184
#define PALETTE_CELL 14
#define PALETTE_COLS (PALETTE_WELL_W / PALETTE_CELL)
#define SHAPE_PX 3
#define SHAPE_BOX (EDITOR_SHAPE_GRID * SHAPE_PX + 2)
#define SHAPE_COLS 6

enum
{
    PALETTE_GROUP_ROTATION = 11,
};

static const char* const k_rotation_names[4] = { "0", "90", "180", "270" };

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
    int row_shape;
    int graph_shape;
    int radio_rotation[4];

    struct PaletteList underlays;
    struct PaletteList overlays;
    struct Editor_ShapeThumb thumbs[4][EDITOR_SHAPE_COUNT];

    /** The open-square set the lists were built for. */
    int built_signature;
    int dirty_draw;
};

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
    if( !ui->tools.overlay_none )
        palette_list_add(&self->overlays, ui->tools.overlay_id);
    qsort(self->underlays.ids + 1, (size_t)(self->underlays.count - 1), sizeof(int), palette_cmp_int);
    qsort(self->overlays.ids + 1, (size_t)(self->overlays.count - 1), sizeof(int), palette_cmp_int);
    self->built_signature = palette_signature(ui);
    self->dirty_draw = 1;
}

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

static int
palette_well_h(int count)
{
    int const rows = (count + PALETTE_COLS - 1) / PALETTE_COLS;
    return (rows > 0 ? rows : 1) * PALETTE_CELL + 2;
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
        uint32_t rgb = 0;
        int const known = palette_rgb(ui, is_overlay, list->ids[i], &rgb);

        if( list->ids[i] == 0 )
        {
            /* "none": a dark cell crossed out. */
            ToriRSChrome_GraphRect(chrome, graph, x, y, PALETTE_CELL - 2, PALETTE_CELL - 2, 0x201c16, 1, i + 1);
            ToriRSChrome_GraphLine(chrome, graph, x, y + PALETTE_CELL - 3, x + PALETTE_CELL - 3, y, 0xb04030);
        }
        else if( known )
            ToriRSChrome_GraphRect(chrome, graph, x, y, PALETTE_CELL - 2, PALETTE_CELL - 2, rgb, 1, i + 1);
        else
        {
            ToriRSChrome_GraphRect(chrome, graph, x, y, PALETTE_CELL - 2, PALETTE_CELL - 2, 0x5a564c, 1, i + 1);
            ToriRSChrome_GraphLine(chrome, graph, x, y, x + PALETTE_CELL - 3, y + PALETTE_CELL - 3, 0x3a362c);
        }
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

static void
palette_labels(
    struct EditorUi* ui,
    struct PalettePanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    uint32_t rgb = 0;

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
}

static void
palette_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct PalettePanel* self = opaque;

    if( !ui->chrome->panels[self->panel].visible )
        return;
    if( self->built_signature != palette_signature(ui) || self->underlays.count == 0 )
        palette_rebuild(ui, self);
    if( !self->dirty_draw )
        return;
    palette_draw_list(ui, self->graph_underlay, &self->underlays, 0, ui->tools.underlay_id);
    palette_draw_list(
        ui, self->graph_overlay, &self->overlays, 1, ui->tools.overlay_none ? 0 : ui->tools.overlay_id);
    palette_draw_shapes(ui, self);
    palette_labels(ui, self);
    self->dirty_draw = 0;
}

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

static int
palette_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct PalettePanel* self = opaque;
    int const key = event->key;

    if( event->widget != self->graph_underlay && event->widget != self->graph_overlay
        && event->widget != self->graph_shape )
        return 0;
    if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS || key <= 0 )
        return 1;

    if( event->widget == self->graph_underlay && key <= self->underlays.count )
    {
        ui->tools.underlay_id = self->underlays.ids[key - 1];
        palette_arm(ui, EDITOR_TOOL_PAINT_UNDERLAY);
    }
    else if( event->widget == self->graph_overlay && key <= self->overlays.count )
    {
        int const id = self->overlays.ids[key - 1];
        ui->tools.overlay_none = id == 0;
        ui->tools.overlay_id = id;
        palette_arm(ui, EDITOR_TOOL_PAINT_OVERLAY);
    }
    else if( event->widget == self->graph_shape && key <= EDITOR_SHAPE_COUNT )
    {
        ui->tools.overlay_shape = key - 1;
        palette_arm(ui, EDITOR_TOOL_PAINT_OVERLAY);
    }
    self->dirty_draw = 1;
    return 1;
}

static int
palette_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct PalettePanel* self = opaque;

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
    return 0;
}

static void
palette_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct PalettePanel* self = opaque;
    (void)ui;
    if( visible )
        self->dirty_draw = 1;
}

static void
palette_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

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

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X), edp_px(ui, 300),
        edp_px(ui, EDP_COL_W), "Palette");
    assert(self->panel >= 0);
    self->row_underlay = ToriRSChrome_Label(chrome, self->panel, "Underlay");
    self->graph_underlay = ToriRSChrome_Graph(chrome, self->panel, "", PALETTE_CELL * 2 + 2);
    self->input_underlay = ToriRSChrome_TextInput(chrome, self->panel, "id", "");
    ToriRSChrome_SetTooltip(chrome, self->input_underlay, "Type an underlay id and press Enter");
    self->row_overlay = ToriRSChrome_Label(chrome, self->panel, "Overlay");
    self->graph_overlay = ToriRSChrome_Graph(chrome, self->panel, "", PALETTE_CELL * 2 + 2);
    self->input_overlay = ToriRSChrome_TextInput(chrome, self->panel, "id", "");
    ToriRSChrome_SetTooltip(chrome, self->input_overlay, "Type an overlay id and press Enter");
    self->row_shape = ToriRSChrome_Label(chrome, self->panel, "Shape");
    self->graph_shape = ToriRSChrome_Graph(
        chrome, self->panel, "", ((EDITOR_SHAPE_COUNT + SHAPE_COLS - 1) / SHAPE_COLS) * (SHAPE_BOX + 4) + 2);
    for( int r = 0; r < 4; r++ )
        self->radio_rotation[r] =
            ToriRSChrome_Radio(chrome, self->panel, k_rotation_names[r], PALETTE_GROUP_ROTATION, r == 0);
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

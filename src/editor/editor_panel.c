#include "editor_panel.h"

#include "app.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_layout.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * The selection relay speaks ABSOLUTE WORLD TILES, never scene coordinates.
 *
 * A scene coordinate is meaningless outside the world that produced it: it is
 * an offset from that load region's `_base_tile_x/z`, so the same number names
 * a different tile in a viewer showing a different region, and names nothing
 * at all in a controller connection that has no world. Absolute tiles are the
 * one address every connection can agree on — the same reason the document's
 * commands are keyed by (map square, tile) rather than by anything the
 * renderer holds.
 *
 * The two converters below are the whole of it, and they are the only place
 * a scene coordinate may cross the wire boundary.
 */

/** Scene -> absolute. 0 when this connection has no world to measure from,
 *  which is a legitimate state (a controller connection), not a bug. */
static int
panel_scene_to_world(
    struct App const* app,
    int scene_x,
    int scene_z,
    int* out_world_x,
    int* out_world_z)
{
    assert(app);
    assert(out_world_x);
    assert(out_world_z);

    if( !app->world )
        return 0;
    *out_world_x = app->world->_base_tile_x + scene_x;
    *out_world_z = app->world->_base_tile_z + scene_z;
    return 1;
}

/** Absolute -> scene. 0 when this connection has no world, or when the tile
 *  lies outside the region it is showing — a viewer legitimately cannot latch
 *  onto a selection made in a region it never loaded. */
static int
panel_world_to_scene(
    struct App const* app,
    int world_x,
    int world_z,
    int* out_scene_x,
    int* out_scene_z)
{
    int scene_x;
    int scene_z;
    int span;

    assert(app);
    assert(out_scene_x);
    assert(out_scene_z);

    if( !app->world )
        return 0;
    scene_x = world_x - app->world->_base_tile_x;
    scene_z = world_z - app->world->_base_tile_z;
    span = app->world->_scene_size;
    if( scene_x < 0 || scene_z < 0 || (span > 0 && (scene_x >= span || scene_z >= span)) )
        return 0;

    *out_scene_x = scene_x;
    *out_scene_z = scene_z;
    return 1;
}

/**
 * Announce the SELECT latch to this connection's Client, so a controller
 * connection with no world of its own can follow this one's clicks. Values
 * per TORIRSMAPED_STATE_SELECTION's registry entry, in absolute world tiles.
 *
 * Publishing is skipped while APPLYING a received fact — the server said it,
 * repeating it back would ping-pong — and before the session exists. A
 * connection with no world publishes nothing but a cleared selection: it has
 * no way to say WHERE anything is, and a scene coordinate sent raw would be
 * read by the receiver as a different tile entirely.
 */
static void
panel_publish_selection(
    struct Editor_Panel* panel,
    struct App const* app)
{
    int32_t values[7];
    int world_x = 0;
    int world_z = 0;

    assert(panel);
    assert(app);

    if( !panel->editor || panel->applying_shared_state )
        return;
    if( panel->sel_kind != EDITOR_SELECTION_NONE
        && !panel_scene_to_world(app, panel->sel_scene_x, panel->sel_scene_z, &world_x, &world_z) )
        return;

    values[0] = (int32_t)panel->sel_kind;
    values[1] = world_x;
    values[2] = world_z;
    values[3] = panel->sel_level;
    values[4] = panel->sel_loc_id;
    values[5] = panel->sel_shape;
    values[6] = panel->sel_angle;
    Editor_StateSet(panel->editor, TORIRSMAPED_STATE_SELECTION, values, 7);
}

/* Chrome strings. Baked into the binary like every other row label here — the
 * editor's own UI never reads the cache, because the cache is what it repairs. */
/*
 * The Tool dropdown's rows. Deliberately NOT the whole enum: Height, Underlay,
 * Overlay and Flags stopped being tools when the panel went selection-driven
 * -- they are FIELDS of a selected tile now, applying the moment they change
 * -- but their enum values live on as the field selectors
 * editor_apply_tool_field dispatches on. A tool is a click behaviour; these
 * three are the click behaviours that exist.
 */
static char const* const editor_tool_names[] = {
    "Select", "Place loc", "Move loc",
};
static enum Editor_Tool const editor_tool_ids[] = {
    EDITOR_TOOL_SELECT, EDITOR_TOOL_LOC_PLACE, EDITOR_TOOL_LOC_MOVE,
};
#define EDITOR_TOOL_ROW_COUNT ((int)(sizeof(editor_tool_ids) / sizeof(editor_tool_ids[0])))

/** Edit-level choices. Index 0 is "follow the pick"; the rest are planes. */
static char const* const editor_level_names[] = { "auto", "0", "1", "2", "3" };
#define EDITOR_LEVEL_COUNT ((int)(sizeof(editor_level_names) / sizeof(editor_level_names[0])))

/** Vis-level choices, sharing the edit row's shape: index 0 is "every level",
 *  the rest cap the view at that plane. Named separately so the two rows can
 *  drift -- "auto" and "all" are different statements. */
static char const* const editor_vis_names[] = { "all", "0", "1", "2", "3" };
#define EDITOR_VIS_COUNT ((int)(sizeof(editor_vis_names) / sizeof(editor_vis_names[0])))

/* Loc shapes worth offering when placing. The full 0..22 space is mostly roof
 * pieces; these are the ones a hand-placed loc is nearly always one of. */
static char const* const editor_loc_shape_names[] = {
    "10 centrepiece", "11 centrepiece diag", "22 ground decor",
    "0 wall", "1 wall corner", "2 wall L", "3 wall square corner", "9 wall diag",
    "4 decor inside", "5 decor outside",
};
static int const editor_loc_shape_ids[] = { 10, 11, 22, 0, 1, 2, 3, 9, 4, 5 };
#define EDITOR_LOC_SHAPE_COUNT                                                                     \
    ((int)(sizeof(editor_loc_shape_ids) / sizeof(editor_loc_shape_ids[0])))

static char const* const editor_shape_names[] = {
    "0 plain",  "1 diag",   "2 corner", "3 corner",  "4 half",  "5 half",   "6 tri",
    "7 tri",    "8 tri",    "9 tri",    "10 tri",    "11 tri",
};
#define EDITOR_SHAPE_COUNT ((int)(sizeof(editor_shape_names) / sizeof(editor_shape_names[0])))

static char const* const editor_rotation_names[] = { "0", "90", "180", "270" };

/* The File/Edit menus. Row order IS the dispatch contract: the activation
 * handler switches on the row index, so a row added here needs its case there
 * (Editor_PanelTick), and the compiler cannot check the pairing. Keep them
 * adjacent to their counts. */
static char const* const editor_menu_file_rows[] = {
    "Save changed squares",
    "Bake cache (slow)",
    "Close editor  [8]",
};
#define EDITOR_MENU_FILE_COUNT ((int)(sizeof(editor_menu_file_rows) / sizeof(editor_menu_file_rows[0])))

/* View-menu rows toggle panel visibility; order pairs with the dispatch. */
static char const* const editor_menu_view_rows[] = {
    "Catalog",
    "Squares",
    "Map Editor",
    "Loc",
};
#define EDITOR_MENU_VIEW_COUNT ((int)(sizeof(editor_menu_view_rows) / sizeof(editor_menu_view_rows[0])))

static char const* const editor_menu_edit_rows[] = {
    "Undo",
    "Redo",
    "Apply to selection  [E]",
    "Deselect",
    "Clear tile locs",
    "Swap loc to catalog pick",
};
#define EDITOR_MENU_EDIT_COUNT ((int)(sizeof(editor_menu_edit_rows) / sizeof(editor_menu_edit_rows[0])))

/* Catalog kinds, in enum CacheProvider_CatalogKind order. */
static char const* const editor_catalog_kind_names[] = { "Locs", "NPCs", "Objs" };
#define EDITOR_CATALOG_KIND_COUNT                                                                  \
    ((int)(sizeof(editor_catalog_kind_names) / sizeof(editor_catalog_kind_names[0])))

/* ---- palettes ------------------------------------------------------------ *
 *
 * Built from the ids that actually occur in the loaded squares, not from the
 * whole id space. A palette of every possible underlay is 255 rows of mostly
 * nothing; the ids in front of you are the ones you are about to paint with,
 * and the list stays short enough to scan.
 */

static void
palette_reset(struct Editor_Panel* panel)
{
    panel->underlay_count = 0;
    panel->overlay_count = 0;
}

static void
palette_add(
    char labels[][EDITOR_PALETTE_LABEL_MAX],
    char const** options,
    int* ids,
    int* count,
    int id,
    char const* prefix)
{
    for( int i = 0; i < *count; i++ )
        if( ids[i] == id )
            return;
    if( *count >= EDITOR_PALETTE_MAX )
        return;

    snprintf(labels[*count], EDITOR_PALETTE_LABEL_MAX, "%s %d", prefix, id);
    options[*count] = labels[*count];
    ids[*count] = id;
    (*count)++;
}

/** Refresh both palettes from every open square's authored tiles. */
static void
palette_rebuild(
    struct Editor_Panel* panel,
    struct Editor* editor)
{
    palette_reset(panel);
    /* "None" first in both, so clearing a tile is a palette choice rather than
     * a separate control. */
    palette_add(
        panel->underlay_labels, panel->underlay_options, panel->underlay_ids,
        &panel->underlay_count, 0, "none");
    palette_add(
        panel->overlay_labels, panel->overlay_options, panel->overlay_ids,
        &panel->overlay_count, 0, "none");

    for( int s = 0; s < editor->doc.square_count; s++ )
    {
        struct Editor_Square const* square = &editor->doc.squares[s];
        if( !square->loaded )
            continue;
        for( int i = 0; i < EDITOR_SQUARE_TILES; i++ )
        {
            struct Editor_Tile const* tile = &square->tiles[i];
            if( tile->underlay_id )
                palette_add(
                    panel->underlay_labels, panel->underlay_options, panel->underlay_ids,
                    &panel->underlay_count, tile->underlay_id, "underlay");
            if( tile->has_overlay )
                palette_add(
                    panel->overlay_labels, panel->overlay_options, panel->overlay_ids,
                    &panel->overlay_count, tile->overlay_id, "overlay");
        }
    }
}

/* ---- construction -------------------------------------------------------- */

/*
 * Where the editor's panels open, in CHROME pixels.
 *
 * Authored once at 1x and multiplied by the chrome's scale, the same way every
 * metric inside the chrome is. A panel origin is the one coordinate ToriRSChrome
 * does not scale for you -- it cannot know whether a caller meant a device
 * pixel or a chrome one -- so the editor states which it meant here.
 */
#define EDITOR_PANEL_TOOL_X 192
#define EDITOR_PANEL_TOOL_Y 28
#define EDITOR_PANEL_COL_X 8
#define EDITOR_PANEL_CATALOG_Y 28
/*
 * Below the catalog, not on top of it.
 *
 * 176 put the Squares panel over the bottom two thirds of the catalog -- and
 * the model-view well sits at 148..246 in that column, so most of the preview
 * was behind another panel. That is not merely a cosmetic overlap: the hit
 * test resolves the TOPMOST panel under the cursor and then searches only
 * THAT panel's widgets, so a click aimed at the well found the Squares panel
 * instead, which clears the focus rather than taking it. The well could
 * therefore never be focused by clicking anywhere but its top sliver, and the
 * camera keys it owns looked dead.
 *
 * The catalog is ~265 rows tall with its variant block hidden; the stacker
 * below re-places this panel from the measured height, and this constant is
 * only where it starts before the first layout has run.
 */
#define EDITOR_PANEL_SQUARE_Y 300
#define EDITOR_PANEL_LOC_Y 260
#define EDITOR_PANEL_COL_W 176
/** Breathing room between the left column's two stacked panels. */
#define EDITOR_PANEL_STACK_GAP 6

/** A 1x-authored editor layout constant at the chrome's current scale. */
static int
editor_px(struct ToriRSChrome const* ui, int px)
{
    assert(ui);
    return px * ToriRSChrome_Scale(ui);
}

/* ---- one loc record, as rows ---------------------------------------------
 *
 * Shared by the Loc panel (the selected placement's record) and the catalog's
 * multiloc variant readout (a rung of the picked shell's transform table).
 * Both answer "what is this loc", so both go through the same fill -- the two
 * used to be one readout, and letting the second grow its own formatting is
 * how they would end up disagreeing about the same record.
 */

/** Fill the rows from a record that IS resident. */
static void
editor_loc_rows_fill(
    struct ToriRSChrome* ui,
    struct Editor_LocRows const* rows,
    struct ToriRS_Location const* cfg,
    int loc_id)
{
    char line[TORIRS_CHROME_LABEL_MAX];

    assert(ui);
    assert(rows);
    assert(cfg);

    snprintf(line, sizeof(line), "%s  (loc %d)", cfg->name[0] ? cfg->name : "<unnamed>", loc_id);
    ToriRSChrome_SetText(ui, rows->name, line);

    ToriRSChrome_SetText(ui, rows->desc, cfg->desc[0] ? cfg->desc : "(no examine)");

    snprintf(
        line, sizeof(line), "size %dx%d  walk %s  proj %s  approach %d", cfg->size_x,
        cfg->size_z, cfg->blocks_walk ? "block" : "-", cfg->blocks_projectiles ? "block" : "-",
        cfg->force_approach);
    ToriRSChrome_SetText(ui, rows->cfg, line);

    /* The rest of the record: what it is built from, and how it renders.
     * "Pull up the loc config" means the CONFIG, not a summary of it. */
    snprintf(
        line, sizeof(line), "models %d  seq %d  wallw %d", cfg->shapes_and_model_count,
        cfg->seq_id, cfg->wall_width);
    ToriRSChrome_SetText(ui, rows->model, line);

    snprintf(
        line, sizeof(line), "amb %d con %d%s%s%s%s", cfg->ambient, cfg->contrast,
        cfg->sharelight ? "  sharelight" : "", cfg->occlude ? "  occlude" : "",
        cfg->shadowed ? "  shadowed" : "", cfg->contoured_ground ? "  contoured" : "");
    ToriRSChrome_SetText(ui, rows->render, line);

    line[0] = '\0';
    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
        if( cfg->actions[i][0] )
        {
            size_t const at = strlen(line);
            snprintf(line + at, sizeof(line) - at, "%s%s", at ? " / " : "", cfg->actions[i]);
        }
    ToriRSChrome_SetText(ui, rows->ops, line[0] ? line : "(no ops)");
}

/** Fill the rows for a record that is NOT resident, or for no record at all.
 *  `why` names which -- a trimmed cache and an empty rung look identical in a
 *  blank readout, and they are not the same thing. */
static void
editor_loc_rows_blank(
    struct ToriRSChrome* ui,
    struct Editor_LocRows const* rows,
    char const* why)
{
    assert(ui);
    assert(rows);
    assert(why);

    ToriRSChrome_SetText(ui, rows->name, why);
    ToriRSChrome_SetText(ui, rows->desc, "");
    ToriRSChrome_SetText(ui, rows->cfg, "");
    ToriRSChrome_SetText(ui, rows->model, "");
    ToriRSChrome_SetText(ui, rows->render, "");
    ToriRSChrome_SetText(ui, rows->ops, "");
}

/** Show or hide the catalog's whole multiloc block in one go. */
static void
editor_variant_rows_hidden(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui,
    int hidden)
{
    assert(panel);
    assert(ui);

    ToriRSChrome_SetHidden(ui, panel->cat_row_multi, hidden);
    ToriRSChrome_SetHidden(ui, panel->cat_dd_variant, hidden);
    ToriRSChrome_SetHidden(ui, panel->cat_var_rows.name, hidden);
    ToriRSChrome_SetHidden(ui, panel->cat_var_rows.desc, hidden);
    ToriRSChrome_SetHidden(ui, panel->cat_var_rows.cfg, hidden);
    ToriRSChrome_SetHidden(ui, panel->cat_var_rows.model, hidden);
    ToriRSChrome_SetHidden(ui, panel->cat_var_rows.render, hidden);
    ToriRSChrome_SetHidden(ui, panel->cat_var_rows.ops, hidden);
}

/*
 * Put every panel back at its default spot for the current scale.
 *
 * Runs at init and again whenever the chrome's scale changes, because a
 * position is a pixel count and the pixels changed size underneath it: panels
 * authored 192 apart at 1x sit on top of each other once each is twice as
 * wide. A display change therefore re-places them -- including panels the user
 * had dragged, which is the honest trade: their old position is in units that
 * no longer exist.
 */
void
Editor_PanelPlaceForScale(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui)
{
    assert(panel);
    assert(ui);
    if( panel->panel >= 0 )
        ToriRSChrome_PanelMove(
            ui, panel->panel, editor_px(ui, EDITOR_PANEL_TOOL_X),
            editor_px(ui, EDITOR_PANEL_TOOL_Y));
    /* The left column's two panels carry a hand-picked width as well as a
     * position, and a width in 1x pixels around 2x rows cuts "nothing picked"
     * off at "nothing picke". Both follow the scale. */
    if( panel->catalog_panel >= 0 )
    {
        ToriRSChrome_PanelMove(
            ui, panel->catalog_panel, editor_px(ui, EDITOR_PANEL_COL_X),
            editor_px(ui, EDITOR_PANEL_CATALOG_Y));
        ToriRSChrome_PanelSetFixedWidth(
            ui, panel->catalog_panel, editor_px(ui, EDITOR_PANEL_COL_W));
    }
    if( panel->square_panel >= 0 )
    {
        ToriRSChrome_PanelMove(
            ui, panel->square_panel, editor_px(ui, EDITOR_PANEL_COL_X),
            editor_px(ui, EDITOR_PANEL_SQUARE_Y));
        ToriRSChrome_PanelSetFixedWidth(
            ui, panel->square_panel, editor_px(ui, EDITOR_PANEL_COL_W));
        /* This is a placement, so the stacker still owns the panel. */
        panel->square_stack_y = editor_px(ui, EDITOR_PANEL_SQUARE_Y);
    }
    if( panel->loc_panel >= 0 )
        ToriRSChrome_PanelMove(
            ui, panel->loc_panel, editor_px(ui, EDITOR_PANEL_TOOL_X),
            editor_px(ui, EDITOR_PANEL_LOC_Y));
    panel->placed_scale = ToriRSChrome_Scale(ui);
}

void
Editor_PanelInit(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui)
{
    assert(panel);
    assert(ui);

    memset(panel, 0, sizeof(*panel));
    panel->shown_x = -1;
    panel->shown_z = -1;
    panel->placed_scale = ToriRSChrome_Scale(ui);
    /* The authored spot IS a stacker placement -- without this the memset's 0
     * reads as "the user dragged it" and the stacker never follows the catalog
     * at all, because Editor_PanelPlaceForScale does not run at boot (the
     * scale it would re-place for is the one just recorded above). */
    panel->square_stack_y = editor_px(ui, EDITOR_PANEL_SQUARE_Y);

    /* The tool stack sits to the RIGHT of the catalog column below, so the two
     * do not open on top of each other. Both are draggable by their headers, so
     * this is only where they start. */
    panel->panel = ToriRSChrome_PanelAdd(
        ui, TORIRS_CHROME_PANEL_WINDOW, editor_px(ui, EDITOR_PANEL_TOOL_X),
        editor_px(ui, EDITOR_PANEL_TOOL_Y), 0, "Map Editor");
    if( panel->panel < 0 )
        return;

    panel->row_status = ToriRSChrome_Label(ui, panel->panel, "no square");
    panel->row_square = ToriRSChrome_Label(ui, panel->panel, "");
    panel->row_tile = ToriRSChrome_Label(ui, panel->panel, "");
    /* The authored row is the one that makes the editor honest about terrain:
     * it says whether the FILE gave this tile a height or the noise routine
     * did, which the drawn ground cannot tell you. */
    panel->row_authored = ToriRSChrome_Label(ui, panel->panel, "");
    ToriRSChrome_Separator(ui, panel->panel);

    panel->dd_tool = ToriRSChrome_Dropdown(
        ui, panel->panel, "Tool", editor_tool_names, EDITOR_TOOL_ROW_COUNT, 0);
    panel->in_height = ToriRSChrome_TextInput(ui, panel->panel, "Height", "0");
    panel->dd_underlay = ToriRSChrome_Dropdown(ui, panel->panel, "Under", NULL, 0, -1);
    panel->dd_overlay = ToriRSChrome_Dropdown(ui, panel->panel, "Over", NULL, 0, -1);
    panel->dd_shape = ToriRSChrome_Dropdown(
        ui, panel->panel, "Shape", editor_shape_names, EDITOR_SHAPE_COUNT, 0);
    panel->dd_rotation =
        ToriRSChrome_Dropdown(ui, panel->panel, "Rot", editor_rotation_names, 4, 0);
    panel->dd_level =
        ToriRSChrome_Dropdown(ui, panel->panel, "Level", editor_level_names, EDITOR_LEVEL_COUNT, 0);
    /* A VIEW row, not an edit one: it changes which planes are painted and
     * touches no document. It sits next to Level because the two are read
     * together -- pin the plane you are shaping, then cap what hides it. */
    panel->dd_vis =
        ToriRSChrome_Dropdown(ui, panel->panel, "Vis", editor_vis_names, EDITOR_VIS_COUNT, 0);
    panel->cb_vis_solo = ToriRSChrome_Checkbox(ui, panel->panel, "vis solo", 0);
    panel->dd_loc_shape = ToriRSChrome_Dropdown(
        ui, panel->panel, "LocSh", editor_loc_shape_names, EDITOR_LOC_SHAPE_COUNT, 0);
    panel->dd_loc_rot =
        ToriRSChrome_Dropdown(ui, panel->panel, "LocRot", editor_rotation_names, 4, 0);
    panel->item_delete = ToriRSChrome_MenuItem(ui, panel->panel, "Delete selection");
    panel->cb_flag_block = ToriRSChrome_Checkbox(ui, panel->panel, "block", 0);
    panel->cb_flag_bridge = ToriRSChrome_Checkbox(ui, panel->panel, "link below", 0);
    panel->cb_flag_roof = ToriRSChrome_Checkbox(ui, panel->panel, "remove roof", 0);
    panel->cb_flag_below = ToriRSChrome_Checkbox(ui, panel->panel, "vis below", 0);

    /* Resizable: the rows here are dropdowns over cache names -- "Coffin
     * (mahogany)" is not a width the panel can guess at build time and not one
     * worth hard-coding, so the grip is how the user makes room for whatever
     * they are actually looking at. */
    ToriRSChrome_PanelSetResizable(ui, panel->panel, 1);

    ToriRSChrome_PanelSetVisible(ui, panel->panel, 0);

    /*
     * The catalog, as its own panel down the left.
     *
     * Separate from the tool panel because it is a different shape of thing: a
     * column you scan, next to a stack of controls you set. Its rows are the
     * same three widgets everything else here uses -- a kind dropdown, a search
     * box, and the list -- so it inherits the skin and the drag handle without
     * needing a widget kind of its own.
     */
    panel->catalog_panel = ToriRSChrome_PanelAdd(
        ui, TORIRS_CHROME_PANEL_WINDOW, editor_px(ui, EDITOR_PANEL_COL_X),
        editor_px(ui, EDITOR_PANEL_CATALOG_Y), editor_px(ui, EDITOR_PANEL_COL_W), "Catalog");
    if( panel->catalog_panel >= 0 )
    {
        panel->cat_dd_kind = ToriRSChrome_Dropdown(
            ui, panel->catalog_panel, "Kind", editor_catalog_kind_names,
            EDITOR_CATALOG_KIND_COUNT, 0);
        panel->cat_in_search = ToriRSChrome_TextInput(ui, panel->catalog_panel, "Find", "");
        panel->cat_dd_list = ToriRSChrome_Dropdown(ui, panel->catalog_panel, "", NULL, 0, -1);
        ToriRSChrome_Separator(ui, panel->catalog_panel);
        panel->cat_row_picked = ToriRSChrome_Label(ui, panel->catalog_panel, "nothing picked");
        /* The multiloc header and its variant list sit ABOVE the well, because
         * choosing a rung is what the well is then showing. The rung's config
         * rows go BELOW everything: they are six rows that come and go, and
         * putting them above would shove the well down the column -- onto the
         * Squares panel -- every time the pick happened to be a multiloc.
         *
         * All of it is created up front and hidden, not built on demand: row
         * order in a chrome panel is creation order, so a dropdown made later
         * would appear at the bottom rather than under the header it belongs
         * to. */
        panel->cat_row_multi = ToriRSChrome_Label(ui, panel->catalog_panel, "");
        panel->cat_dd_variant = ToriRSChrome_Dropdown(ui, panel->catalog_panel, "", NULL, 0, -1);
        panel->cat_view = ToriRSChrome_ModelView(ui, panel->catalog_panel, 120, 96);
        panel->cat_reset_view = ToriRSChrome_MenuItem(ui, panel->catalog_panel, "Reset view");
        panel->cat_row_count = ToriRSChrome_Label(ui, panel->catalog_panel, "");
        panel->cat_var_rows.name = ToriRSChrome_Label(ui, panel->catalog_panel, "");
        panel->cat_var_rows.desc = ToriRSChrome_Label(ui, panel->catalog_panel, "");
        panel->cat_var_rows.cfg = ToriRSChrome_Label(ui, panel->catalog_panel, "");
        panel->cat_var_rows.model = ToriRSChrome_Label(ui, panel->catalog_panel, "");
        panel->cat_var_rows.render = ToriRSChrome_Label(ui, panel->catalog_panel, "");
        panel->cat_var_rows.ops = ToriRSChrome_Label(ui, panel->catalog_panel, "");
        editor_variant_rows_hidden(panel, ui, 1);
        /* The list is the widest thing in the editor and the one most worth
         * widening, so this panel is resizable for the same reason. */
        ToriRSChrome_PanelSetResizable(ui, panel->catalog_panel, 1);
        ToriRSChrome_PanelSetVisible(ui, panel->catalog_panel, 0);
    }
    /* The square browser, under the catalog in the left column. */
    panel->square_panel = ToriRSChrome_PanelAdd(
        ui, TORIRS_CHROME_PANEL_WINDOW, editor_px(ui, EDITOR_PANEL_COL_X),
        editor_px(ui, EDITOR_PANEL_SQUARE_Y), editor_px(ui, EDITOR_PANEL_COL_W), "Squares");
    if( panel->square_panel >= 0 )
    {
        panel->sq_row_current = ToriRSChrome_Label(ui, panel->square_panel, "-");
        panel->sq_in_search = ToriRSChrome_TextInput(ui, panel->square_panel, "Find", "");
        panel->sq_dd_list = ToriRSChrome_Dropdown(ui, panel->square_panel, "", NULL, 0, -1);
        panel->sq_item_open = ToriRSChrome_MenuItem(ui, panel->square_panel, "Open square");
        /* Resizable like the other two: its list is square names, and the
         * column it shares with the catalog is the one worth widening. */
        ToriRSChrome_PanelSetResizable(ui, panel->square_panel, 1);
        ToriRSChrome_PanelSetVisible(ui, panel->square_panel, 0);
    }

    /* The Loc panel: what the selected loc IS. Shown only while a loc is
     * selected; sits under the tool panel's default spot. */
    panel->loc_panel = ToriRSChrome_PanelAdd(
        ui, TORIRS_CHROME_PANEL_WINDOW, editor_px(ui, EDITOR_PANEL_TOOL_X),
        editor_px(ui, EDITOR_PANEL_LOC_Y), 0, "Loc");
    if( panel->loc_panel >= 0 )
    {
        panel->loc_rows.name = ToriRSChrome_Label(ui, panel->loc_panel, "");
        panel->loc_rows.desc = ToriRSChrome_Label(ui, panel->loc_panel, "");
        panel->loc_row_place = ToriRSChrome_Label(ui, panel->loc_panel, "");
        panel->loc_rows.cfg = ToriRSChrome_Label(ui, panel->loc_panel, "");
        panel->loc_rows.model = ToriRSChrome_Label(ui, panel->loc_panel, "");
        panel->loc_rows.render = ToriRSChrome_Label(ui, panel->loc_panel, "");
        panel->loc_rows.ops = ToriRSChrome_Label(ui, panel->loc_panel, "");
        panel->loc_row_view_tile = ToriRSChrome_MenuItem(ui, panel->loc_panel, "View tile");
        ToriRSChrome_PanelSetResizable(ui, panel->loc_panel, 1);
        ToriRSChrome_PanelSetVisible(ui, panel->loc_panel, 0);
    }

    /*
     * The File/Edit bar. Commands live here, not as rows on the tool panel:
     * a command is something you do once and move on from, and rows of them
     * below the inputs made the panel read as one undifferentiated pile --
     * which is exactly the complaint that moved them.
     */
    panel->menubar_panel =
        ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_MENUBAR, 0, 0, UITREE_LAYOUT_ROOT_W, "");
    if( panel->menubar_panel >= 0 )
    {
        panel->menu_file = ToriRSChrome_MenuDrop(
            ui, panel->menubar_panel, "File", editor_menu_file_rows, EDITOR_MENU_FILE_COUNT);
        panel->menu_edit = ToriRSChrome_MenuDrop(
            ui, panel->menubar_panel, "Edit", editor_menu_edit_rows, EDITOR_MENU_EDIT_COUNT);
        panel->menu_view = ToriRSChrome_MenuDrop(
            ui, panel->menubar_panel, "View", editor_menu_view_rows, EDITOR_MENU_VIEW_COUNT);
        ToriRSChrome_PanelSetVisible(ui, panel->menubar_panel, 0);
    }

    panel->cat_picked_id = -1;
    panel->edit_level = -1;
    panel->vis_level = -1;
    panel->cat_kind = CACHEPROVIDER_CATALOG_LOC;
    panel->cat_shown_kind = -1;
    panel->cat_var_shown_id = -1;
    panel->cat_var_shown_kind = -1;
    panel->cat_var_shown_epoch = -1;

    panel->visible = 0;
    panel->built = 1;
}

void
Editor_PanelSetVisible(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui,
    int visible)
{
    assert(panel);
    assert(ui);

    if( !panel->built )
        return;
    panel->visible = visible;
    ToriRSChrome_PanelSetVisible(ui, panel->panel, visible);
    /* The catalog opens and closes with the editor: it is part of the editor's
     * chrome, not a second tool to go and find. */
    if( panel->catalog_panel >= 0 )
        ToriRSChrome_PanelSetVisible(ui, panel->catalog_panel, visible);
    if( panel->square_panel >= 0 )
        ToriRSChrome_PanelSetVisible(ui, panel->square_panel, visible);
    if( panel->menubar_panel >= 0 )
        ToriRSChrome_PanelSetVisible(ui, panel->menubar_panel, visible);
    /* The Loc panel tracks the selection while the editor is open; closing the
     * editor closes it regardless, and reopening does NOT bring it back until
     * a loc is selected again -- visible == (editor open && loc selected). */
    if( panel->loc_panel >= 0 )
        ToriRSChrome_PanelSetVisible(
            ui, panel->loc_panel, visible && panel->sel_kind == EDITOR_SELECTION_LOC);
}

/* ---- tile addressing ----------------------------------------------------- */

/**
 * Scene tile -> the square that owns it and the tile inside that square.
 *
 * The scene is a window onto the world grid, so a scene tile has to be lifted
 * back to world coordinates through the world's base before it names anything
 * on disk. Returns 0 when the world has no base yet.
 */
static int
scene_to_square(
    struct App* app,
    int scene_x,
    int scene_z,
    int* out_map_x,
    int* out_map_z,
    int* out_tile_x,
    int* out_tile_z)
{
    int world_x;
    int world_z;

    if( !app->world )
        return 0;

    world_x = app->world->_base_tile_x + scene_x;
    world_z = app->world->_base_tile_z + scene_z;
    if( world_x < 0 || world_z < 0 )
        return 0;

    *out_map_x = world_x / EDITOR_SQUARE_X;
    *out_map_z = world_z / EDITOR_SQUARE_Z;
    *out_tile_x = world_x % EDITOR_SQUARE_X;
    *out_tile_z = world_z % EDITOR_SQUARE_Z;
    return 1;
}

/* ---- catalog -------------------------------------------------------------
 *
 * The loc/npc/obj picker's list. Built from what the provider has DECODED --
 * after a world load that is every loc in the loaded squares, which is the set
 * a map editor places from. See CacheProvider_VisitLoaded for why it is not
 * the whole cache.
 */

struct Editor_CatalogFill
{
    struct Editor_Panel* panel;
    /** Lowercased filter, or "" for everything. */
    char const* filter;
};

/** ASCII lowercase; the cache's names are ASCII and a locale-aware fold here
 *  would answer differently per machine for the same cache. */
static char
editor_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int
editor_name_matches(char const* name, char const* filter)
{
    if( !filter || !filter[0] )
        return 1;
    if( !name || !name[0] )
        return 0;
    for( int i = 0; name[i]; i++ )
    {
        int j = 0;
        while( filter[j] && editor_lower(name[i + j]) == editor_lower(filter[j]) )
            j++;
        if( !filter[j] )
            return 1;
        if( !name[i + j] )
            break;
    }
    return 0;
}

/** Counts loaded records without touching the list -- the cheap "did the
 *  provider gain anything?" probe the tick runs every frame. */
static void
editor_catalog_count_only(void* user, int id, char const* name)
{
    (void)user;
    (void)id;
    (void)name;
}

static void
editor_catalog_visit(void* user, int id, char const* name)
{
    struct Editor_CatalogFill* fill = user;
    struct Editor_Panel* panel = fill->panel;

    if( panel->cat_count >= EDITOR_CATALOG_MAX )
        return;
    /* An unnamed record is a real cache record with no name field, not a
     * broken one -- plenty of locs are scenery with nothing to call them. It
     * still gets a row, addressed by id, because it is still placeable. */
    if( !editor_name_matches(name && name[0] ? name : "", fill->filter) &&
        !(!name || !name[0]) )
        return;
    if( (!name || !name[0]) && fill->filter && fill->filter[0] )
        return; /* filtering by text excludes the nameless */

    snprintf(
        panel->cat_labels[panel->cat_count], EDITOR_CATALOG_LABEL_MAX, "%s (%d)",
        (name && name[0]) ? name : "<unnamed>", id);
    panel->cat_options[panel->cat_count] = panel->cat_labels[panel->cat_count];
    panel->cat_ids[panel->cat_count] = id;
    panel->cat_count++;
}

/** Refill the list from the provider. Cheap enough to run on any change: it
 *  walks a few hundred loaded records and copies matching names. */
static void
editor_catalog_rebuild(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct Editor_CatalogFill fill;
    char const* search;

    assert(panel);
    assert(app);

    search = ToriRSChrome_Text(&app->dbg_ui, panel->cat_in_search);
    panel->cat_count = 0;
    fill.panel = panel;
    fill.filter = search;
    if( app->provider )
        CacheProvider_VisitLoaded(
            app->provider, (enum CacheProvider_CatalogKind)panel->cat_kind,
            editor_catalog_visit, &fill);

    snprintf(panel->cat_shown_search, sizeof(panel->cat_shown_search), "%s", search ? search : "");
    panel->cat_shown_kind = panel->cat_kind;

    ToriRSChrome_DropdownSetOptions(
        &app->dbg_ui, panel->cat_dd_list, panel->cat_options, panel->cat_count, -1);
    snprintf(
        panel->cat_picked_name, sizeof(panel->cat_picked_name), "%d loaded", panel->cat_count);
}

/* ---- multiloc variants ---------------------------------------------------
 *
 * A multiloc's transform table, as a list you can preview from. See the field
 * block in editor_panel.h for why the shell alone answers nothing.
 */

/** The loc a variant row previews, or -1 for the shell row / a blank rung. */
static int
editor_variant_loc_id(struct Editor_Panel const* panel)
{
    assert(panel);

    if( panel->cat_var_choice <= 0 || panel->cat_var_choice >= panel->cat_var_count )
        return -1;
    return panel->cat_var_ids[panel->cat_var_choice];
}

int
Editor_PanelCatalogPreviewId(struct Editor_Panel const* panel)
{
    assert(panel);

    if( panel->cat_kind != CACHEPROVIDER_CATALOG_LOC )
        return panel->cat_picked_id;
    if( panel->cat_var_count > 0 && panel->cat_var_choice > 0 )
        return editor_variant_loc_id(panel);
    return panel->cat_picked_id;
}

/** Write the chosen row's record into the variant rows. */
static void
editor_variant_readout(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct ToriRSChrome* ui = &app->dbg_ui;
    int const loc_id = panel->cat_var_choice > 0 ? editor_variant_loc_id(panel)
                                                 : panel->cat_picked_id;
    struct ToriRS_Location* cfg;

    assert(panel);
    assert(app);

    if( loc_id < 0 )
    {
        /* Row 0 with no pick cannot happen -- the block is hidden then -- so
         * this is the -1 rung: the value that draws nothing. */
        editor_loc_rows_blank(ui, &panel->cat_var_rows, "(nothing at this value)");
        return;
    }

    cfg = app->provider ? CacheProvider_LocationGet(app->provider, loc_id) : NULL;
    if( cfg )
        editor_loc_rows_fill(ui, &panel->cat_var_rows, cfg, loc_id);
    else
        editor_loc_rows_blank(ui, &panel->cat_var_rows, "(config not resident)");
}

/**
 * Rebuild the variant list for the current pick.
 *
 * Rungs are ordinarily NOT resident: the catalog lists what the loaded squares
 * decoded, and a transform table names locs no square placed -- the closed
 * door, the built bridge, the stage-3 quest prop. So each missing rung is
 * queued here, once per build, and the tick rebuilds when the resident count
 * moves; that converges, because a rung that has landed is never queued again.
 */
static void
editor_variant_rebuild(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct ToriRSChrome* ui = &app->dbg_ui;
    struct ToriRS_Location* cfg = NULL;
    char line[TORIRS_CHROME_LABEL_MAX];

    assert(panel);
    assert(app);

    /* A new pick starts on its shell row: carrying rung 3 across from the
     * last multiloc would preview a variant the user never chose. */
    if( panel->cat_var_shown_id != panel->cat_picked_id ||
        panel->cat_var_shown_kind != panel->cat_kind )
        panel->cat_var_choice = 0;
    panel->cat_var_shown_id = panel->cat_picked_id;
    panel->cat_var_shown_kind = panel->cat_kind;
    panel->cat_var_shown_epoch = panel->cat_built_epoch;
    panel->cat_var_count = 0;

    if( app->provider && panel->cat_kind == CACHEPROVIDER_CATALOG_LOC &&
        panel->cat_picked_id >= 0 )
        cfg = CacheProvider_LocationGet(app->provider, panel->cat_picked_id);

    if( !cfg || cfg->transform_count <= 0 || !cfg->transforms )
    {
        /* Not a multiloc. The rows go away rather than sitting empty: an
         * always-present variant list would read as "this loc has one". */
        panel->cat_var_choice = 0;
        ToriRSChrome_DropdownSetOptions(ui, panel->cat_dd_variant, NULL, 0, -1);
        editor_variant_rows_hidden(panel, ui, 1);
        return;
    }

    snprintf(
        panel->cat_var_labels[0], EDITOR_CATALOG_LABEL_MAX, "shell: %s (%d)",
        cfg->name[0] ? cfg->name : "<unnamed>", panel->cat_picked_id);
    panel->cat_var_options[0] = panel->cat_var_labels[0];
    panel->cat_var_ids[0] = -1;
    panel->cat_var_count = 1;

    for( int i = 0; i < cfg->transform_count && panel->cat_var_count < EDITOR_LOC_VARIANT_MAX;
         i++ )
    {
        int const rung = cfg->transforms[i];
        int const row = panel->cat_var_count;
        /* The last entry answers every value past the end of the table, so it
         * is labelled for what it does rather than for its index. */
        int const is_fallback = i == cfg->transform_count - 1;
        struct ToriRS_Location* rung_cfg =
            rung > 0 ? CacheProvider_LocationGet(app->provider, rung) : NULL;

        /* The row is addressed by the value that SELECTS it -- what content
         * writes to the varbit/varp to make this rung live -- except the
         * fallback, which answers every value the table does not name. */
        char key[8];
        if( is_fallback )
            snprintf(key, sizeof(key), "else");
        else
            snprintf(key, sizeof(key), "%d", i);

        if( rung <= 0 )
            snprintf(panel->cat_var_labels[row], EDITOR_CATALOG_LABEL_MAX, "%s: none", key);
        else if( rung_cfg )
            snprintf(
                panel->cat_var_labels[row], EDITOR_CATALOG_LABEL_MAX, "%s: %s (%d)", key,
                rung_cfg->name[0] ? rung_cfg->name : "<unnamed>", rung);
        else
            snprintf(
                panel->cat_var_labels[row], EDITOR_CATALOG_LABEL_MAX, "%s: loc %d (loading)",
                key, rung);
        panel->cat_var_options[row] = panel->cat_var_labels[row];
        panel->cat_var_ids[row] = rung > 0 ? rung : -1;
        panel->cat_var_count++;

        if( rung > 0 && !rung_cfg )
        {
            struct ToriRS_Task* task = CreateTask_LocLoad(app->provider, rung);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
    }
    if( cfg->transform_count > EDITOR_LOC_VARIANT_MAX - 1 )
        TORIRS_LOG("editor: loc %d has %d variants, listing the first %d\n",
            panel->cat_picked_id, cfg->transform_count, EDITOR_LOC_VARIANT_MAX - 1);

    if( panel->cat_var_choice >= panel->cat_var_count )
        panel->cat_var_choice = 0;

    if( cfg->transform_varbit != -1 )
        snprintf(
            line, sizeof(line), "multiloc: varbit %d, %d variants", cfg->transform_varbit,
            cfg->transform_count);
    else if( cfg->transform_varp != -1 )
        snprintf(
            line, sizeof(line), "multiloc: varp %d, %d variants", cfg->transform_varp,
            cfg->transform_count);
    else
        /* No selector at all: the resolver falls to the last entry forever,
         * which is worth saying out loud rather than showing a live-looking
         * list nothing can move. */
        snprintf(line, sizeof(line), "multiloc: no selector, %d variants", cfg->transform_count);
    ToriRSChrome_SetText(ui, panel->cat_row_multi, line);

    ToriRSChrome_DropdownSetOptions(
        ui, panel->cat_dd_variant, panel->cat_var_options, panel->cat_var_count,
        panel->cat_var_choice);
    editor_variant_rows_hidden(panel, ui, 0);
    editor_variant_readout(panel, app);
}

/* Defined with the loc-placement helpers below; SelectLoc runs earlier. */
static void
editor_loc_panel_refresh(
    struct Editor_Panel* panel,
    struct App* app);

static void
editor_seed_terrain_fields(
    struct Editor_Panel* panel,
    struct App* app);

static int
editor_apply_tool_field(
    struct Editor_Panel* panel,
    struct App* app,
    enum Editor_Tool field,
    int scene_x,
    int scene_z,
    int level);

static void
editor_read_loc_dropdowns(
    struct Editor_Panel* panel,
    struct App* app,
    int* out_shape,
    int* out_angle);

/* ---- readout ------------------------------------------------------------- */

static void
panel_refresh(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct ToriRSChrome* ui = &app->dbg_ui;
    struct Editor* editor = app->editor;
    char line[TORIRS_CHROME_LABEL_MAX];
    int map_x;
    int map_z;
    int tile_x;
    int tile_z;
    /*
     * A latch pins the readout to what was selected, whatever tool is active.
     *
     * Deliberately NOT gated on the SELECT tool. Gating it there made the
     * selection evaporate the moment you switched to the tool you selected
     * something in order to use, which is the whole select-then-operate flow
     * broken in one line: you would select a tile, reach for Height, and the
     * panel would silently go back to describing whatever the mouse was over.
     * The selection is a subject the tools act on, so it outlives the choice
     * of which tool acts.
     */
    int const latched = panel->sel_kind != EDITOR_SELECTION_NONE;
    int const probe_x = latched ? panel->sel_scene_x : app->world_hover_tile_x;
    int const probe_z = latched ? panel->sel_scene_z : app->world_hover_tile_z;
    int level = latched ? panel->sel_level : Editor_PanelEditLevel(panel, app);

    snprintf(
        line,
        sizeof(line),
        "%s%s",
        editor->writable ? "writable" : "READ-ONLY",
        Editor_DocHasUnsaved(&editor->doc) ? "  *unsaved*" : "");
    ToriRSChrome_SetText(ui, panel->row_status, line);

    if( probe_x < 0 || !scene_to_square(app, probe_x, probe_z, &map_x, &map_z, &tile_x, &tile_z) )
    {
        ToriRSChrome_SetText(
            ui, panel->row_square, latched ? "latched tile is off-map" : "no tile under cursor");
        ToriRSChrome_SetText(ui, panel->row_tile, "");
        ToriRSChrome_SetText(ui, panel->row_authored, "");
        return;
    }

    if( latched && panel->sel_kind == EDITOR_SELECTION_LOC )
        snprintf(
            line, sizeof(line), "[loc %d] m%d_%d  tile %d,%d  level %d", panel->sel_loc_id, map_x,
            map_z, tile_x, tile_z, level);
    else
        snprintf(
            line, sizeof(line), "%sm%d_%d  tile %d,%d  level %d", latched ? "[latched] " : "", map_x,
            map_z, tile_x, tile_z, level);
    ToriRSChrome_SetText(ui, panel->row_square, line);

    {
        struct Editor_Square* square = Editor_DocFindSquare(&editor->doc, map_x, map_z);
        struct Editor_Tile const* tile;

        if( !square || !square->loaded )
        {
            ToriRSChrome_SetText(ui, panel->row_tile, "square not open in the editor");
            ToriRSChrome_SetText(ui, panel->row_authored, "");
            return;
        }

        tile = &square->tiles[Editor_TileIndex(tile_x, tile_z, level)];
        snprintf(
            line,
            sizeof(line),
            "u%d o%d;%d;%d f%d",
            tile->underlay_id,
            tile->has_overlay ? tile->overlay_id : 0,
            tile->shape,
            tile->rotation,
            tile->settings);
        ToriRSChrome_SetText(ui, panel->row_tile, line);

        if( tile->has_height )
            snprintf(line, sizeof(line), "height h%d (authored)", tile->height);
        else
            snprintf(line, sizeof(line), "height: generated (no h token)");
        ToriRSChrome_SetText(ui, panel->row_authored, line);
    }
}

/* ---- editing ------------------------------------------------------------- */

static int
panel_flags_from_checkboxes(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui)
{
    int flags = 0;
    if( ToriRSChrome_Checked(ui, panel->cb_flag_block) )
        flags |= 0x1;
    if( ToriRSChrome_Checked(ui, panel->cb_flag_bridge) )
        flags |= 0x2;
    if( ToriRSChrome_Checked(ui, panel->cb_flag_roof) )
        flags |= 0x4;
    if( ToriRSChrome_Checked(ui, panel->cb_flag_below) )
        flags |= 0x8;
    return flags;
}

int
Editor_PanelApplyToolAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    return editor_apply_tool_field(panel, app, panel->tool, scene_x, scene_z, level);
}

/**
 * Apply ONE field -- named by the tool enum that edits it -- to a tile.
 *
 * Factored out of ApplyToolAt so the selection-driven panel can apply a single
 * field the moment its widget changes (choose an underlay -> that tile's
 * underlay changes) without pretending the active tool changed. The tool
 * click path and the widget path land on the same command construction, which
 * is what keeps their undo steps identical.
 */
static void
editor_seed_terrain_fields(
    struct Editor_Panel* panel,
    struct App* app);

static int
editor_apply_tool_field(
    struct Editor_Panel* panel,
    struct App* app,
    enum Editor_Tool field,
    int scene_x,
    int scene_z,
    int level)
{
    struct ToriRSChrome* ui = &app->dbg_ui;
    struct Editor* editor = app->editor;
    struct Editor_Square* square;
    struct Editor_Cmd command;
    int map_x;
    int map_z;
    int tile_x;
    int tile_z;

    assert(panel);
    assert(app);
    assert(editor);

    if( field == EDITOR_TOOL_SELECT )
        return 0;
    if( field == EDITOR_TOOL_LOC_PLACE )
    {
        /* Place stamps whatever KIND the catalog is showing: locs go through
         * the loc command path; npcs and objs are session spawns -- a scene
         * entity now, a line in the edited .spawn file at save. */
        if( panel->cat_kind == CACHEPROVIDER_CATALOG_NPC ||
            panel->cat_kind == CACHEPROVIDER_CATALOG_OBJ )
        {
            int const is_obj = panel->cat_kind == CACHEPROVIDER_CATALOG_OBJ;
            int idx;

            if( panel->cat_picked_id < 0 )
            {
                TORIRS_LOG("editor: pick an entry in the catalog first\n");
                return 0;
            }
            idx = Editor_PanelSpawnAdd(
                panel, app, is_obj, panel->cat_picked_id, panel->cat_picked_name, scene_x,
                scene_z, level, 1);
            if( idx < 0 )
                return 0;
            App_EditorPlaceSpawn(app, is_obj, panel->cat_picked_id, scene_x, scene_z, level);
            panel->sel_kind = is_obj ? EDITOR_SELECTION_OBJ : EDITOR_SELECTION_NPC;
            panel->sel_scene_x = scene_x;
            panel->sel_scene_z = scene_z;
            panel->sel_level = level;
            panel->sel_spawn = idx;
            panel->sel_element_id = -1;
            panel->loc_panel_stale = 1;
            panel_publish_selection(panel, app);
            return 1;
        }
        return Editor_PanelPlaceLocAt(panel, app, scene_x, scene_z, level);
    }
    if( field == EDITOR_TOOL_LOC_MOVE )
        return Editor_PanelMoveSelectedLocTo(panel, app, scene_x, scene_z);
    if( !scene_to_square(app, scene_x, scene_z, &map_x, &map_z, &tile_x, &tile_z) )
        return 0;

    square = Editor_DocFindSquare(&editor->doc, map_x, map_z);
    if( !square || !square->loaded )
        return 0;

    memset(&command, 0, sizeof(command));
    command.kind = EDITOR_CMD_TILE;
    command.map_x = map_x;
    command.map_z = map_z;
    command.level = level;
    command.x = tile_x;
    command.z = tile_z;
    command.tile_before = square->tiles[Editor_TileIndex(tile_x, tile_z, level)];
    command.tile_after = command.tile_before;

    switch( field )
    {
    case EDITOR_TOOL_HEIGHT:
    {
        int value = atoi(ToriRSChrome_Text(ui, panel->in_height));
        if( value < 0 )
            value = 0;
        if( value > 0xFF )
            value = 0xFF;
        /* Editing a generated tile makes it authored — that is what the user
         * meant by setting a height on it. Handing it back to the noise
         * routine is a separate act: clear the field to empty. */
        if( ToriRSChrome_Text(ui, panel->in_height)[0] == '\0' )
        {
            command.tile_after.has_height = 0;
            command.tile_after.height = 0;
        }
        else
        {
            command.tile_after.has_height = 1;
            command.tile_after.height = (uint8_t)value;
        }
        break;
    }

    case EDITOR_TOOL_UNDERLAY:
    {
        int const choice = ToriRSChrome_DropdownSelected(ui, panel->dd_underlay);
        if( choice < 0 || choice >= panel->underlay_count )
            return 0;
        command.tile_after.underlay_id = (uint8_t)panel->underlay_ids[choice];
        break;
    }

    case EDITOR_TOOL_OVERLAY:
    {
        int const choice = ToriRSChrome_DropdownSelected(ui, panel->dd_overlay);
        int const shape = ToriRSChrome_DropdownSelected(ui, panel->dd_shape);
        int const rotation = ToriRSChrome_DropdownSelected(ui, panel->dd_rotation);
        if( choice < 0 || choice >= panel->overlay_count )
            return 0;
        if( panel->overlay_ids[choice] == 0 && shape <= 0 )
        {
            /* "none" with no shape clears the overlay outright. */
            command.tile_after.has_overlay = 0;
            command.tile_after.overlay_id = 0;
            command.tile_after.shape = 0;
            command.tile_after.rotation = 0;
        }
        else
        {
            command.tile_after.has_overlay = 1;
            command.tile_after.overlay_id = (uint16_t)panel->overlay_ids[choice];
            command.tile_after.shape = (uint8_t)(shape < 0 ? 0 : shape);
            command.tile_after.rotation = (uint8_t)(rotation < 0 ? 0 : rotation);
        }
        break;
    }

    case EDITOR_TOOL_FLAGS:
        command.tile_after.settings = (uint8_t)panel_flags_from_checkboxes(panel, ui);
        break;

    case EDITOR_TOOL_SELECT:
    case EDITOR_TOOL_LOC_PLACE:
    case EDITOR_TOOL_LOC_MOVE:
    case EDITOR_TOOL_COUNT:
    default:
        /* The loc tools returned above; the rest are tile edits. */
        return 0;
    }

    /* A command that changes nothing is not worth an undo step. */
    if( memcmp(&command.tile_before, &command.tile_after, sizeof(command.tile_before)) == 0 )
        return 0;

    return Editor_Apply(editor, &command);
}

int
Editor_PanelRecordLocEdit(
    struct Editor_Panel* panel,
    struct App* app,
    int from_scene_x,
    int from_scene_z,
    int level,
    int loc_id,
    int shape,
    int from_angle,
    int to_scene_x,
    int to_scene_z,
    int to_angle)
{
    struct Editor_Cmd command;
    int from_map_x;
    int from_map_z;
    int from_tile_x;
    int from_tile_z;
    int to_map_x;
    int to_map_z;
    int to_tile_x;
    int to_tile_z;

    assert(panel);
    assert(app);
    (void)panel;

    if( !app->editor )
        return 0;
    if( !scene_to_square(
            app, from_scene_x, from_scene_z, &from_map_x, &from_map_z, &from_tile_x, &from_tile_z) )
        return 0;
    if( !scene_to_square(
            app, to_scene_x, to_scene_z, &to_map_x, &to_map_z, &to_tile_x, &to_tile_z) )
        return 0;

    /* A move across a square border is two edits to two files, which this one
     * command cannot express -- it carries a single map_x/map_z. Refusing is
     * better than writing the placement into the wrong square's `.jl2`. */
    if( from_map_x != to_map_x || from_map_z != to_map_z )
    {
        TORIRS_LOG("editor: loc moves across a square border are not saved yet\n");
        return 0;
    }
    if( !Editor_DocFindSquare(&app->editor->doc, from_map_x, from_map_z) )
        return 0;

    memset(&command, 0, sizeof(command));
    command.kind = EDITOR_CMD_LOC;
    command.map_x = from_map_x;
    command.map_z = from_map_z;

    command.has_before = 1;
    command.loc_before.loc_id = loc_id;
    command.loc_before.shape = shape;
    command.loc_before.rotation = from_angle;
    command.loc_before.level = level;
    command.loc_before.x = from_tile_x;
    command.loc_before.z = from_tile_z;

    command.has_after = 1;
    command.loc_after = command.loc_before;
    command.loc_after.rotation = to_angle;
    command.loc_after.x = to_tile_x;
    command.loc_after.z = to_tile_z;

    return Editor_Apply(app->editor, &command);
}

/** Fill the terrain widgets from the latched tile's authored record, so the
 *  panel shows THIS tile's data. Palette rows are found by id; an id the
 *  palette has not met yet leaves the row unchanged rather than lying. */
static void
editor_seed_terrain_fields(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct ToriRSChrome* ui = &app->dbg_ui;
    struct Editor_Square* square;
    struct Editor_Tile const* tile;
    char text[16];
    int map_x;
    int map_z;
    int tile_x;
    int tile_z;

    if( !app->editor )
        return;
    if( !scene_to_square(
            app, panel->sel_scene_x, panel->sel_scene_z, &map_x, &map_z, &tile_x, &tile_z) )
        return;
    square = Editor_DocFindSquare(&app->editor->doc, map_x, map_z);
    if( !square || !square->loaded )
        return;
    tile = &square->tiles[Editor_TileIndex(tile_x, tile_z, panel->sel_level)];

    /* Height: authored value, or empty for procedural -- the empty box IS the
     * tri-state's "absent", matching what applying an empty box writes. */
    if( tile->has_height )
    {
        snprintf(text, sizeof(text), "%d", tile->height);
        ToriRSChrome_SetText(ui, panel->in_height, text);
    }
    else
        ToriRSChrome_SetText(ui, panel->in_height, "");

    for( int i = 0; i < panel->underlay_count; i++ )
        if( panel->underlay_ids[i] == tile->underlay_id )
        {
            ToriRSChrome_DropdownSetSelected(ui, panel->dd_underlay, i);
            break;
        }
    for( int i = 0; i < panel->overlay_count; i++ )
        if( panel->overlay_ids[i] == (tile->has_overlay ? tile->overlay_id : 0) )
        {
            ToriRSChrome_DropdownSetSelected(ui, panel->dd_overlay, i);
            break;
        }
    if( tile->shape < EDITOR_SHAPE_COUNT )
        ToriRSChrome_DropdownSetSelected(ui, panel->dd_shape, tile->has_overlay ? tile->shape : 0);
    ToriRSChrome_DropdownSetSelected(ui, panel->dd_rotation, tile->rotation & 3);

    ToriRSChrome_SetChecked(ui, panel->cb_flag_block, (tile->settings & 0x1) != 0);
    ToriRSChrome_SetChecked(ui, panel->cb_flag_bridge, (tile->settings & 0x2) != 0);
    ToriRSChrome_SetChecked(ui, panel->cb_flag_roof, (tile->settings & 0x4) != 0);
    ToriRSChrome_SetChecked(ui, panel->cb_flag_below, (tile->settings & 0x8) != 0);
}

/* ---- select tool ----------------------------------------------------------
 *
 * EDITOR_TOOL_SELECT's latch. `panel_refresh` reads it (instead of the live
 * hover) once one exists, so the readout and the highlight the overlay draws
 * (app_overlay_build_editor_selection, src/app.c) stay pinned to what the
 * user actually latched even after the mouse moves off it. */

void
Editor_PanelSelectTerrain(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    assert(panel);
    assert(app);

    panel->sel_kind = EDITOR_SELECTION_TERRAIN;
    panel->sel_scene_x = scene_x;
    panel->sel_scene_z = scene_z;
    panel->sel_level = level;
    panel->sel_element_id = -1;
    panel->sel_loc_id = -1;
    panel->loc_panel_stale = 1;
    panel_publish_selection(panel, app);
}

void
Editor_PanelSelectLoc(
    struct Editor_Panel* panel,
    struct App* app,
    int element_id)
{
    struct WorldEntity_Scenery* scenery;

    assert(panel);
    assert(app);

    if( !app->world )
        return;
    scenery = World_SceneryGetByElementId(app->world, element_id);
    if( !scenery )
        return;

    panel->sel_kind = EDITOR_SELECTION_LOC;
    panel->sel_scene_x = scenery->grid_position.x;
    panel->sel_scene_z = scenery->grid_position.z;
    panel->sel_level = scenery->grid_position.level;
    panel->sel_element_id = element_id;
    panel->sel_loc_id = scenery->loc_id;
    panel->sel_shape = scenery->shape;
    panel->sel_angle = scenery->angle;

    /* Seed the placement editors from what is actually there, so "Apply"
     * with nothing touched is a no-op rather than a snap to the dropdowns'
     * stale values. A shape outside the curated list (a roof piece) seeds
     * -1; Apply then keeps the shape it cannot express. */
    {
        int row = -1;
        for( int i = 0; i < EDITOR_LOC_SHAPE_COUNT; i++ )
            if( editor_loc_shape_ids[i] == scenery->shape )
            {
                row = i;
                break;
            }
        ToriRSChrome_DropdownSetSelected(&app->dbg_ui, panel->dd_loc_shape, row);
        ToriRSChrome_DropdownSetSelected(
            &app->dbg_ui, panel->dd_loc_rot, scenery->angle & 3);
    }

    editor_loc_panel_refresh(panel, app);
    if( panel->loc_panel >= 0 && panel->visible )
        ToriRSChrome_PanelSetVisible(&app->dbg_ui, panel->loc_panel, 1);

    /* The catalog follows the selection: selecting a maploc IS picking that
     * loc, so the catalog flips to Locs, adopts the pick, and highlights the
     * row when the current list carries it -- the swap/place flows then act
     * on what was just selected without a second hunt through the list. */
    panel->cat_kind = CACHEPROVIDER_CATALOG_LOC;
    ToriRSChrome_DropdownSetSelected(&app->dbg_ui, panel->cat_dd_kind, CACHEPROVIDER_CATALOG_LOC);
    panel->cat_picked_id = scenery->loc_id;
    snprintf(
        panel->cat_picked_name, sizeof(panel->cat_picked_name), "%s (%d)",
        scenery->info->name[0] ? scenery->info->name : "<unnamed>",
        scenery->loc_id);
    {
        char line[TORIRS_CHROME_LABEL_MAX];
        snprintf(line, sizeof(line), "Locs %s", panel->cat_picked_name);
        ToriRSChrome_SetText(&app->dbg_ui, panel->cat_row_picked, line);
        for( int i = 0; i < panel->cat_count; i++ )
            if( panel->cat_ids[i] == scenery->loc_id )
            {
                ToriRSChrome_DropdownSetSelected(&app->dbg_ui, panel->cat_dd_list, i);
                break;
            }
    }
    panel_publish_selection(panel, app);
}

void
Editor_PanelClearSelection(
    struct Editor_Panel* panel,
    struct App* app)
{
    assert(panel);
    assert(app);

    panel->sel_kind = EDITOR_SELECTION_NONE;
    panel->sel_element_id = -1;
    panel->sel_loc_id = -1;
    panel->loc_panel_stale = 1;
    panel_publish_selection(panel, app);
}

/** Fill the Loc panel from the selection: placement from the scene entity,
 *  config from the provider. Config absent (trimmed cache) degrades to the
 *  placement half rather than hiding the panel. */
static void
editor_loc_panel_refresh(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct ToriRSChrome* ui = &app->dbg_ui;
    struct ToriRS_Location* cfg;
    char line[TORIRS_CHROME_LABEL_MAX];

    if( panel->loc_panel < 0 || panel->sel_kind != EDITOR_SELECTION_LOC )
        return;

    cfg = app->provider ? CacheProvider_LocationGet(app->provider, panel->sel_loc_id) : NULL;
    if( cfg )
        editor_loc_rows_fill(ui, &panel->loc_rows, cfg, panel->sel_loc_id);
    else
    {
        snprintf(line, sizeof(line), "loc %d  (config not resident)", panel->sel_loc_id);
        editor_loc_rows_blank(ui, &panel->loc_rows, line);
    }

    snprintf(
        line, sizeof(line), "tile %d,%d  lvl %d  shape %d  rot %d", panel->sel_scene_x,
        panel->sel_scene_z, panel->sel_level, panel->sel_shape, panel->sel_angle);
    ToriRSChrome_SetText(ui, panel->loc_row_place, line);
}

/* ---- loc placement -------------------------------------------------------
 *
 * Place, delete and clear-tile are ONE command shape with different halves --
 * EDITOR_CMD_LOC's has_before/has_after -- so none of them needs a new command
 * kind. What each needs is the matching App_WorldLocChange so the scene shows
 * it immediately, and a document write so it survives a reload.
 */

uint8_t
Editor_PanelVisLevelMask(
    struct Editor_Panel const* panel)
{
    assert(panel);

    if( panel->vis_level < 0 )
        return 0;
    if( panel->vis_solo )
        return (uint8_t)(1u << panel->vis_level);
    /* Cumulative: levels 0..vis_level, which is how the game's own view floor
     * builds its mask and the only reading VIS_BELOW tiles come out right
     * under (a tile physically on 2 is revealed while viewing 1). */
    return (uint8_t)((1u << (panel->vis_level + 1)) - 1);
}

int
Editor_PanelEditLevel(
    struct Editor_Panel const* panel,
    struct App const* app)
{
    if( panel->edit_level >= 0 )
        return panel->edit_level;
    return app->world ? app->world_hover_tile_level : 0;
}

/** Record one loc add/remove against the document. Returns 1 if it stuck. */
static int
editor_loc_command(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int adding)
{
    struct Editor_Cmd command;
    int map_x;
    int map_z;
    int tile_x;
    int tile_z;

    assert(panel);
    assert(app);
    (void)panel;

    if( !app->editor )
        return 0;
    if( !scene_to_square(app, scene_x, scene_z, &map_x, &map_z, &tile_x, &tile_z) )
        return 0;
    /* A loc whose square is not open cannot be saved, and showing one that
     * vanishes on the next reload is worse than refusing it. Same rule the
     * cross-square move refuses under. */
    if( !Editor_DocFindSquare(&app->editor->doc, map_x, map_z) )
    {
        TORIRS_ERR("editor: square m%d_%d is not open, refusing the loc edit\n", map_x, map_z);
        return 0;
    }

    memset(&command, 0, sizeof(command));
    command.kind = EDITOR_CMD_LOC;
    command.map_x = map_x;
    command.map_z = map_z;
    if( adding )
    {
        command.has_after = 1;
        command.loc_after.loc_id = loc_id;
        command.loc_after.shape = shape;
        command.loc_after.rotation = angle;
        command.loc_after.level = level;
        command.loc_after.x = tile_x;
        command.loc_after.z = tile_z;
    }
    else
    {
        command.has_before = 1;
        command.loc_before.loc_id = loc_id;
        command.loc_before.shape = shape;
        command.loc_before.rotation = angle;
        command.loc_before.level = level;
        command.loc_before.x = tile_x;
        command.loc_before.z = tile_z;
    }
    return Editor_Apply(app->editor, &command);
}

int
Editor_PanelApplyToSelection(
    struct Editor_Panel* panel,
    struct App* app)
{
    assert(panel);
    assert(app);

    if( panel->sel_kind == EDITOR_SELECTION_NONE )
    {
        TORIRS_LOG("editor: select a tile or loc first\n");
        return 0;
    }

    /*
     * A selected LOC applies the placement editors -- shape and rotation --
     * to itself, whatever tile tool happens to be active. That is the
     * select-then-operate reading of "apply" for a loc: the subject is the
     * loc, and the fields on screen for it are LocSh/LocRot.
     */
    if( panel->sel_kind == EDITOR_SELECTION_LOC )
    {
        int new_shape;
        int new_angle;
        editor_read_loc_dropdowns(panel, app, &new_shape, &new_angle);
        return Editor_PanelReplaceSelectedLoc(
            panel, app, panel->sel_loc_id, new_shape, new_angle, panel->sel_level);
    }

    /* Terrain fields apply themselves the moment they change; there is
     * nothing batched up for an Apply to flush. Saying so beats silently
     * doing nothing. */
    TORIRS_LOG("editor: terrain fields apply as you change them\n");
    return 0;
}

/** The placement editors' current values, with the selection's own as the
 *  fallback for anything the dropdowns cannot express (a roof shape seeds
 *  row -1 and survives edits untouched). */
static void
editor_seed_terrain_fields(
    struct Editor_Panel* panel,
    struct App* app);

static int
editor_apply_tool_field(
    struct Editor_Panel* panel,
    struct App* app,
    enum Editor_Tool field,
    int scene_x,
    int scene_z,
    int level);

static void
editor_read_loc_dropdowns(
    struct Editor_Panel* panel,
    struct App* app,
    int* out_shape,
    int* out_angle)
{
    struct ToriRSChrome* ui = &app->dbg_ui;
    int const shape_row = ToriRSChrome_DropdownSelected(ui, panel->dd_loc_shape);
    int const rot_row = ToriRSChrome_DropdownSelected(ui, panel->dd_loc_rot);

    *out_shape = (shape_row >= 0 && shape_row < EDITOR_LOC_SHAPE_COUNT)
                     ? editor_loc_shape_ids[shape_row]
                     : panel->sel_shape;
    *out_angle = rot_row >= 0 ? rot_row : panel->sel_angle;
}

int
Editor_PanelReplaceSelectedLoc(
    struct Editor_Panel* panel,
    struct App* app,
    int new_loc_id,
    int new_shape,
    int new_angle,
    int new_level)
{
    struct Editor_Cmd command;
    int map_x;
    int map_z;
    int tile_x;
    int tile_z;

    assert(panel);
    assert(app);

    if( panel->sel_kind != EDITOR_SELECTION_LOC )
        return 0;
    if( new_loc_id == panel->sel_loc_id && new_shape == panel->sel_shape &&
        new_angle == panel->sel_angle && new_level == panel->sel_level )
        return 0; /* nothing changed; not worth an undo step */
    if( !app->editor )
        return 0;
    if( !scene_to_square(
            app, panel->sel_scene_x, panel->sel_scene_z, &map_x, &map_z, &tile_x, &tile_z) )
        return 0;
    if( !Editor_DocFindSquare(&app->editor->doc, map_x, map_z) )
    {
        TORIRS_ERR("editor: square m%d_%d is not open, refusing the loc edit\n", map_x, map_z);
        return 0;
    }

    /* One command, both halves: undo restores the old placement in one step
     * rather than as a delete you undo separately from an add. */
    memset(&command, 0, sizeof(command));
    command.kind = EDITOR_CMD_LOC;
    command.map_x = map_x;
    command.map_z = map_z;
    command.has_before = 1;
    command.loc_before.loc_id = panel->sel_loc_id;
    command.loc_before.shape = panel->sel_shape;
    command.loc_before.rotation = panel->sel_angle;
    command.loc_before.level = panel->sel_level;
    command.loc_before.x = tile_x;
    command.loc_before.z = tile_z;
    command.has_after = 1;
    command.loc_after = command.loc_before;
    command.loc_after.loc_id = new_loc_id;
    command.loc_after.shape = new_shape;
    command.loc_after.rotation = new_angle;
    command.loc_after.level = new_level;
    if( !Editor_Apply(app->editor, &command) )
        return 0;

    /* Scene: the old placement out, the new one in -- a shape change moves the
     * loc between layers, which a single in-place change cannot express. */
    App_WorldLocChange(
        app, panel->sel_scene_x, panel->sel_scene_z, panel->sel_level, -1, panel->sel_shape,
        panel->sel_angle);
    App_WorldLocChange(
        app, panel->sel_scene_x, panel->sel_scene_z, new_level, new_loc_id, new_shape,
        new_angle);

    /* The old scene element is gone; the overlay re-finds the new one by tile
     * and shape once the async change lands (see the editor-selection overlay
     * builder). */
    panel->sel_loc_id = new_loc_id;
    panel->sel_shape = new_shape;
    panel->sel_angle = new_angle;
    panel->sel_level = new_level;
    panel->sel_element_id = -1;
    editor_loc_panel_refresh(panel, app);
    return 1;
}

void
Editor_PanelGhostSpec(
    struct Editor_Panel* panel,
    struct App* app,
    int* out_shape,
    int* out_angle)
{
    struct ToriRSChrome* ui = &app->dbg_ui;
    int const shape_row = ToriRSChrome_DropdownSelected(ui, panel->dd_loc_shape);
    int const rot_row = ToriRSChrome_DropdownSelected(ui, panel->dd_loc_rot);

    assert(panel);
    assert(app);

    *out_shape = (shape_row >= 0 && shape_row < EDITOR_LOC_SHAPE_COUNT)
                     ? editor_loc_shape_ids[shape_row]
                     : editor_loc_shape_ids[0];
    *out_angle = rot_row >= 0 ? rot_row : 0;
}

int
Editor_PanelPlaceLocAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    struct ToriRSChrome* ui = &app->dbg_ui;
    int shape_row;
    int shape;
    int angle;

    assert(panel);
    assert(app);

    if( panel->cat_picked_id < 0 )
    {
        TORIRS_LOG("editor: pick a loc in the catalog first\n");
        return 0;
    }
    shape_row = ToriRSChrome_DropdownSelected(ui, panel->dd_loc_shape);
    if( shape_row < 0 || shape_row >= EDITOR_LOC_SHAPE_COUNT )
        shape_row = 0;
    shape = editor_loc_shape_ids[shape_row];
    angle = ToriRSChrome_DropdownSelected(ui, panel->dd_loc_rot);
    if( angle < 0 )
        angle = 0;

    if( !editor_loc_command(
            panel, app, scene_x, scene_z, level, panel->cat_picked_id, shape, angle, 1) )
        return 0;

    /* The live preview, through the same seam a zone LOC_ADD_CHANGE takes. */
    App_WorldLocChange(app, scene_x, scene_z, level, panel->cat_picked_id, shape, angle);

    /* What you place is what you have selected: the Loc panel fills with it,
     * rotate/reshape act on it, and Move carries it -- without a second trip
     * through the minimenu to select the thing that was just made. */
    panel->sel_kind = EDITOR_SELECTION_LOC;
    panel->sel_scene_x = scene_x;
    panel->sel_scene_z = scene_z;
    panel->sel_level = level;
    panel->sel_loc_id = panel->cat_picked_id;
    panel->sel_shape = shape;
    panel->sel_angle = angle;
    panel->sel_element_id = -1; /* async add; the overlay re-finds it */
    editor_loc_panel_refresh(panel, app);
    if( panel->loc_panel >= 0 && panel->visible )
        ToriRSChrome_PanelSetVisible(&app->dbg_ui, panel->loc_panel, 1);
    panel_publish_selection(panel, app);
    return 1;
}

int
Editor_PanelDeleteLocAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    struct WorldEntity_Scenery* scenery;
    int idx;

    assert(panel);
    assert(app);

    if( !app->world )
        return 0;
    idx = World_SceneryFindAt(app->world, scene_x, scene_z, level, -1);
    if( idx < 0 )
        return 0;
    scenery = World_EntityPoolGet(&app->world->entities.scenery, idx);
    if( !scenery )
        return 0;

    if( !editor_loc_command(
            panel, app, scene_x, scene_z, level, scenery->loc_id, scenery->shape, scenery->angle,
            0) )
        return 0;

    /* loc_id < 0 is the pure delete the zone packet uses. */
    App_WorldLocChange(app, scene_x, scene_z, level, -1, scenery->shape, scenery->angle);
    return 1;
}

int
Editor_PanelMoveSelectedLocTo(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z)
{
    assert(panel);
    assert(app);

    if( panel->sel_kind != EDITOR_SELECTION_LOC )
    {
        TORIRS_LOG("editor: select a loc to move first\n");
        return 0;
    }
    if( scene_x == panel->sel_scene_x && scene_z == panel->sel_scene_z )
        return 0;
    if( !Editor_PanelRecordLocEdit(
            panel, app, panel->sel_scene_x, panel->sel_scene_z, panel->sel_level,
            panel->sel_loc_id, panel->sel_shape, panel->sel_angle, scene_x, scene_z,
            panel->sel_angle) )
        return 0;

    App_WorldLocChange(
        app, panel->sel_scene_x, panel->sel_scene_z, panel->sel_level, -1, panel->sel_shape,
        panel->sel_angle);
    App_WorldLocChange(
        app, scene_x, scene_z, panel->sel_level, panel->sel_loc_id, panel->sel_shape,
        panel->sel_angle);

    /* The selection RIDES the move -- that is the point of a move tool: click
     * again and it moves again, without re-selecting at each stop. */
    panel->sel_scene_x = scene_x;
    panel->sel_scene_z = scene_z;
    panel->sel_element_id = -1; /* the overlay re-finds the new element */
    editor_loc_panel_refresh(panel, app);
    return 1;
}

int
Editor_PanelDeleteSpawn(
    struct Editor_Panel* panel,
    struct App* app,
    int index)
{
    struct Editor_SpawnEntry const* e;
    int scene_x;
    int scene_z;

    assert(panel);
    assert(app);

    if( index < 0 || index >= panel->spawn_count || !app->world )
        return 0;
    e = &panel->spawns[index];
    scene_x = e->abs_x - app->world->_base_tile_x;
    scene_z = e->abs_z - app->world->_base_tile_z;

    if( e->is_obj )
        App_WorldObjStackDel(app, scene_x, scene_z, e->level, e->id);
    else
    {
        /* No World_NpcFindAt exists; walk the pool for the npc on this tile
         * with this id. Session-placed, so at most a handful ever live. */
        struct World_EntityPool* pool = &app->world->entities.npc;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
            if( npc && npc->npc_id == e->id && npc->grid_position.x == scene_x &&
                npc->grid_position.z == scene_z && npc->grid_position.level == e->level )
            {
                World_NpcDespawn(app->world, i);
                break;
            }
        }
    }

    /* Order is irrelevant to the emitted file; swap-with-last keeps this O(1). */
    panel->spawns[index] = panel->spawns[panel->spawn_count - 1];
    panel->spawn_count--;
    panel->spawns_dirty = 1;
    if( panel->sel_kind == EDITOR_SELECTION_NPC || panel->sel_kind == EDITOR_SELECTION_OBJ )
        Editor_PanelClearSelection(panel, app);
    app->need_redraw = 1;
    return 1;
}

int
Editor_PanelSpawnAdd(
    struct Editor_Panel* panel,
    struct App* app,
    int is_obj,
    int id,
    char const* name,
    int scene_x,
    int scene_z,
    int level,
    int count)
{
    struct Editor_SpawnEntry* e;

    assert(panel);
    assert(app);

    if( !app->world )
        return -1;
    if( panel->spawn_count >= EDITOR_SPAWN_MAX )
    {
        TORIRS_LOG("editor: spawn list full (%d); save first\n", EDITOR_SPAWN_MAX);
        return -1;
    }
    e = &panel->spawns[panel->spawn_count];
    e->is_obj = is_obj;
    e->id = id;
    e->abs_x = app->world->_base_tile_x + scene_x;
    e->abs_z = app->world->_base_tile_z + scene_z;
    e->level = level;
    e->count = count > 0 ? count : 1;
    snprintf(e->name, sizeof(e->name), "%s", name ? name : "");
    panel->spawns_dirty = 1;
    return panel->spawn_count++;
}

/**
 * Emit the session spawns as edited .spawn files, one per square touched.
 *
 * Entries write as `#<id>` rather than a pack symbol: the editor holds cache
 * display names ("Hans"), and the spawn grammar resolves PACK symbols
 * ("hans") -- a display name would fail resolution or, worse, resolve to the
 * wrong record (the loc-symbol-is-not-cache-name trap). The loader grows a
 * numeric escape instead; the display name rides behind `//` for the human.
 */
int
Editor_PanelSpawnsSave(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct Editor* editor = app->editor;
    int saved = 0;

    assert(panel);
    assert(app);

    if( !editor || !editor->host_open || !editor->host.vtable->spawn_save )
        return 0;
    if( !panel->spawns_dirty )
        return 0;

    /* Group by square. Quadratic over a hand-placed list is nothing. */
    for( int i = 0; i < panel->spawn_count; i++ )
    {
        int const sq_x = panel->spawns[i].abs_x / 64;
        int const sq_z = panel->spawns[i].abs_z / 64;
        char text[EDITOR_SPAWN_MAX * 96];
        size_t at = 0;
        int emitted = 0;
        int already = 0;

        for( int j = 0; j < i; j++ )
            if( panel->spawns[j].abs_x / 64 == sq_x && panel->spawns[j].abs_z / 64 == sq_z )
            {
                already = 1;
                break;
            }
        if( already )
            continue;

        at += (size_t)snprintf(
            text + at, sizeof(text) - at,
            "// Session spawns for m%d_%d -- written by the map editor.\n"
            "// `#<id>` is the numeric-id escape; the trailing comment is the\n"
            "// display name, for humans only.\n",
            sq_x, sq_z);
        for( int pass = 0; pass < 2; pass++ )
        {
            int wrote_header = 0;
            for( int j = i; j < panel->spawn_count; j++ )
            {
                struct Editor_SpawnEntry const* e = &panel->spawns[j];
                if( e->abs_x / 64 != sq_x || e->abs_z / 64 != sq_z )
                    continue;
                if( e->is_obj != pass )
                    continue;
                if( !wrote_header )
                {
                    at += (size_t)snprintf(
                        text + at, sizeof(text) - at, "\n==== %s ====\n",
                        pass ? "OBJ" : "NPC");
                    wrote_header = 1;
                }
                if( pass )
                    at += (size_t)snprintf(
                        text + at, sizeof(text) - at, "#%d %d %d %d %d // %s\n", e->id,
                        e->abs_x, e->abs_z, e->level, e->count, e->name);
                else
                    at += (size_t)snprintf(
                        text + at, sizeof(text) - at, "#%d %d %d %d // %s\n", e->id,
                        e->abs_x, e->abs_z, e->level, e->name);
                emitted++;
            }
        }

        if( emitted &&
            editor->host.vtable->spawn_save(editor->host.user_data, sq_x, sq_z, text, at) ==
                EDITOR_HOST_OK )
            saved++;
    }

    panel->spawns_dirty = 0;
    return saved;
}

int
Editor_PanelDeleteSelection(
    struct Editor_Panel* panel,
    struct App* app)
{
    assert(panel);
    assert(app);

    if( panel->sel_kind == EDITOR_SELECTION_NPC || panel->sel_kind == EDITOR_SELECTION_OBJ )
    {
        /* Session spawns: drop the entry, despawn the scene half. The world
         * has no server, so nothing else can own this npc/obj. */
        if( panel->sel_spawn < 0 || panel->sel_spawn >= panel->spawn_count )
            return 0;
        return Editor_PanelDeleteSpawn(panel, app, panel->sel_spawn);
    }
    if( panel->sel_kind != EDITOR_SELECTION_LOC )
    {
        TORIRS_LOG("editor: nothing deletable is selected\n");
        return 0;
    }
    /* By element when the overlay has healed one -- exact layer -- else by
     * the selection's own record, which is just as precise: it names the
     * tile, level and shape the selection was taken with. */
    if( panel->sel_element_id >= 0 &&
        Editor_PanelDeleteLocByElement(panel, app, panel->sel_element_id) )
        return 1;
    if( !editor_loc_command(
            panel, app, panel->sel_scene_x, panel->sel_scene_z, panel->sel_level,
            panel->sel_loc_id, panel->sel_shape, panel->sel_angle, 0) )
        return 0;
    App_WorldLocChange(
        app, panel->sel_scene_x, panel->sel_scene_z, panel->sel_level, -1, panel->sel_shape,
        panel->sel_angle);
    Editor_PanelSelectTerrain(panel, app, panel->sel_scene_x, panel->sel_scene_z, panel->sel_level);
    return 1;
}

int
Editor_PanelDeleteLocByElement(
    struct Editor_Panel* panel,
    struct App* app,
    int element_id)
{
    struct WorldEntity_Scenery* scenery;

    assert(panel);
    assert(app);

    if( !app->world )
        return 0;
    scenery = World_SceneryGetByElementId(app->world, element_id);
    if( !scenery )
        return 0;

    if( !editor_loc_command(
            panel, app, scenery->grid_position.x, scenery->grid_position.z,
            scenery->grid_position.level, scenery->loc_id, scenery->shape, scenery->angle, 0) )
        return 0;

    /* A deleted selection cannot stay a selection; its tile can. */
    if( panel->sel_kind == EDITOR_SELECTION_LOC && panel->sel_element_id == element_id )
        Editor_PanelSelectTerrain(
            panel, app, scenery->grid_position.x, scenery->grid_position.z,
            scenery->grid_position.level);

    App_WorldLocChange(
        app, scenery->grid_position.x, scenery->grid_position.z, scenery->grid_position.level,
        -1, scenery->shape, scenery->angle);
    return 1;
}

int
Editor_PanelClearLocsAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    int removed = 0;

    assert(panel);
    assert(app);

    /* One undo step for the whole tile, which is what the user did: a stroke
     * groups the per-layer commands on the authority, so Ctrl+Z restores the
     * wall AND its decoration AND the ground loc together rather than
     * peeling them back one press at a time. */
    if( panel->editor )
        Editor_StrokeBegin(panel->editor);

    /* Bounded rather than `while(found)`: each delete should retire one loc, but
     * a layer this fails to remove would spin here forever, and a tile holds a
     * handful of layers at most. */
    for( int guard = 0; guard < 16; guard++ )
    {
        if( !Editor_PanelDeleteLocAt(panel, app, scene_x, scene_z, level) )
            break;
        removed++;
    }

    if( panel->editor )
        Editor_StrokeEnd(panel->editor);
    return removed;
}

/* Clear the locs on the SELECT latch's tile when there is one, else on the
 * remembered hover -- choosing a menu row necessarily moved the cursor onto
 * the bar, so the live hover would name whatever tile the bar happens to sit
 * over. Same reasoning as the loc editor's Reselect. */
static void
editor_clear_tile_locs(
    struct Editor_Panel* panel,
    struct App* app)
{
    int const sx =
        panel->sel_kind != EDITOR_SELECTION_NONE ? panel->sel_scene_x : app->locedit_hover_x;
    int const sz =
        panel->sel_kind != EDITOR_SELECTION_NONE ? panel->sel_scene_z : app->locedit_hover_z;

    if( sx < 0 || sz < 0 )
        return;
    TORIRS_LOG("editor: cleared %d loc(s) at %d,%d\n",
        Editor_PanelClearLocsAt(panel, app, sx, sz, Editor_PanelEditLevel(panel, app)), sx, sz);
}

/* ---- tick ---------------------------------------------------------------- */

static void
panel_bake_progress(
    void* user_data,
    char const* line)
{
    (void)user_data;
    TORIRS_LOG("bake: %s\n", line);
}

/*
 * Keep the Squares panel under the catalog rather than over it.
 *
 * The catalog's height is not a constant: the multiloc variant block is six
 * rows that appear when the pick has a transform table, and the panel is
 * resizable besides. A fixed Y therefore cannot stay clear of it -- and an
 * overlap here is not cosmetic, because the hit test only searches the topmost
 * panel under the cursor, so whatever the Squares panel covers stops taking
 * clicks (EDITOR_PANEL_SQUARE_Y says what that cost).
 *
 * Follows only while the panel is still where this function put it: the moment
 * the user drags it, `square_stack_y` stops matching and the stacker lets go
 * for the rest of the session. A panel that walked back out from under the
 * cursor after being placed by hand would be the worse bug.
 */
static void
editor_square_panel_stack(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui)
{
    struct ToriRSChromeRect cat;
    int want;

    assert(panel);
    assert(ui);

    if( panel->catalog_panel < 0 || panel->square_panel < 0 )
        return;
    cat = ToriRSChrome_PanelRect(ui, panel->catalog_panel);
    if( cat.h <= 0 )
        return; /* not laid out yet; the authored Y stands until it is */
    if( ToriRSChrome_PanelRect(ui, panel->square_panel).y != panel->square_stack_y )
        return; /* dragged by hand -- the user's placement wins */

    want = cat.y + cat.h + editor_px(ui, EDITOR_PANEL_STACK_GAP);
    if( want == panel->square_stack_y )
        return;
    ToriRSChrome_PanelMove(ui, panel->square_panel, cat.x, want);
    panel->square_stack_y = want;
}

void
Editor_PanelTick(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct ToriRSChrome* ui;
    struct Editor* editor;
    int activated;

    assert(panel);
    assert(app);

    if( !panel->built || !panel->visible || !app->editor )
        return;

    ui = &app->dbg_ui;
    editor = app->editor;

    /* The display's pixel density can change mid-session -- the window is
     * dragged to another monitor -- and the chrome follows it. Panel origins
     * do not follow on their own: they are pixel counts written when the
     * pixels were a different size. */
    if( panel->placed_scale != ToriRSChrome_Scale(ui) )
        Editor_PanelPlaceForScale(panel, ui);
    editor_square_panel_stack(panel, ui);

    panel_refresh(panel, app);

    /* The bar spans whatever the screen is NOW. fixed_w was read once at
     * build; a window resize since would leave a stub bar (or one running off
     * screen), so it follows the live root width here. Cheap: the write only
     * happens on an actual change, and dirtying is what triggers a rebuild. */
    if( panel->menubar_panel >= 0 &&
        ui->panels[panel->menubar_panel].fixed_w != UITREE_LAYOUT_ROOT_W )
        ToriRSChrome_PanelSetFixedWidth(
            ui, panel->menubar_panel, UITREE_LAYOUT_ROOT_W);

    /*
     * Only the active tool's inputs are visible.
     *
     * Every input used to show at once, which made the panel read as one
     * undifferentiated pile -- with no way to tell that Height fed only the
     * Height tool and LocSh only Place loc. The rows are built once and
     * hidden/shown here, rather than rebuilt per switch, so the widget
     * handles this file holds stay valid for the panel's whole life.
     *
     * Level and Tool stay put: Level scopes every tool, and Tool is the thing
     * doing the choosing.
     */
    {
        enum Editor_Tool const t = panel->tool;
        struct ToriRSChrome* chrome = &app->dbg_ui;
        /*
         * The SELECTION decides what is editable; the tool only fills in when
         * nothing is selected.
         *
         * A selected tile shows EVERY terrain field -- height, underlay,
         * overlay+shape+rot, flags -- each applying to that tile the moment
         * it changes. A selected loc shows the maploc fields. Only with no
         * selection at all does the panel fall back to showing the active
         * tool's input, which is the painting flow.
         */
        int const sel_terrain = panel->sel_kind == EDITOR_SELECTION_TERRAIN;
        int const sel_loc = panel->sel_kind == EDITOR_SELECTION_LOC;
        int const show_loc = sel_loc || t == EDITOR_TOOL_LOC_PLACE;

        /* Terrain fields exist only as properties of a selected tile now --
         * there is no paint mode left to show them for. */
        ToriRSChrome_SetHidden(chrome, panel->in_height, !sel_terrain);
        ToriRSChrome_SetHidden(chrome, panel->dd_underlay, !sel_terrain);
        ToriRSChrome_SetHidden(chrome, panel->dd_overlay, !sel_terrain);
        ToriRSChrome_SetHidden(chrome, panel->dd_shape, !sel_terrain);
        ToriRSChrome_SetHidden(chrome, panel->dd_rotation, !sel_terrain);
        ToriRSChrome_SetHidden(chrome, panel->cb_flag_block, !sel_terrain);
        ToriRSChrome_SetHidden(chrome, panel->cb_flag_bridge, !sel_terrain);
        ToriRSChrome_SetHidden(chrome, panel->cb_flag_roof, !sel_terrain);
        ToriRSChrome_SetHidden(chrome, panel->cb_flag_below, !sel_terrain);
        ToriRSChrome_SetHidden(chrome, panel->dd_loc_shape, !show_loc);
        ToriRSChrome_SetHidden(chrome, panel->dd_loc_rot, !show_loc);
        /* Deletable selections only: bare terrain has nothing to delete. */
        ToriRSChrome_SetHidden(
            chrome, panel->item_delete,
            !(sel_loc || panel->sel_kind == EDITOR_SELECTION_NPC ||
              panel->sel_kind == EDITOR_SELECTION_OBJ));
    }

    /* A newly latched tile seeds the fields with ITS values, so the panel
     * reads as "this tile's data", not as leftover tool state. Keyed on the
     * latch coordinates, so the seed re-runs only when the subject moves --
     * an immediate apply below changes the tile under an unchanged latch,
     * and re-seeding then would fight the user's own edit in the widgets. */
    if( panel->sel_kind == EDITOR_SELECTION_TERRAIN &&
        (panel->seeded_x != panel->sel_scene_x || panel->seeded_z != panel->sel_scene_z ||
         panel->seeded_level != panel->sel_level) )
    {
        editor_seed_terrain_fields(panel, app);
        panel->seeded_x = panel->sel_scene_x;
        panel->seeded_z = panel->sel_scene_z;
        panel->seeded_level = panel->sel_level;
    }

    /* Selection moved off a loc: the Loc panel follows it down. */
    if( panel->loc_panel_stale )
    {
        panel->loc_panel_stale = 0;
        if( panel->loc_panel >= 0 && panel->sel_kind != EDITOR_SELECTION_LOC )
            ToriRSChrome_PanelSetVisible(ui, panel->loc_panel, 0);
    }

    /* Palettes follow the loaded squares, so opening a new region brings its
     * underlays into the list without a reload. */
    if( panel->underlay_count == 0 || panel->overlay_count == 0 )
    {
        palette_rebuild(panel, editor);
        ToriRSChrome_DropdownSetOptions(
            ui, panel->dd_underlay, panel->underlay_options, panel->underlay_count, 0);
        ToriRSChrome_DropdownSetOptions(
            ui, panel->dd_overlay, panel->overlay_options, panel->overlay_count, 0);
    }

    /*
     * Keep the catalog current.
     *
     * Rebuilt when the kind or the search text changed, and also when the
     * loaded-record count moved -- a world load pulls a few hundred locs into
     * the provider, and a catalog that only refreshed on a keystroke would sit
     * empty until the user typed something after opening a new region.
     */
    if( panel->catalog_panel >= 0 )
    {
        char const* search = ToriRSChrome_Text(ui, panel->cat_in_search);
        int const loaded = app->provider
            ? CacheProvider_VisitLoaded(
                  app->provider, (enum CacheProvider_CatalogKind)panel->cat_kind,
                  editor_catalog_count_only, panel)
            : 0;

        if( panel->cat_shown_kind != panel->cat_kind ||
            strcmp(panel->cat_shown_search, search ? search : "") != 0 ||
            loaded != panel->cat_built_epoch )
        {
            panel->cat_built_epoch = loaded;
            editor_catalog_rebuild(panel, app);
        }

        {
            char line[TORIRS_CHROME_LABEL_MAX];
            snprintf(
                line, sizeof(line), "%d of %d %s", panel->cat_count, loaded,
                editor_catalog_kind_names[panel->cat_kind]);
            ToriRSChrome_SetText(ui, panel->cat_row_count, line);
        }

        /* The variant list follows the pick, and again once a rung it queued
         * has landed -- that is what the epoch half catches, and it is the
         * only reason a name in the list ever changes. */
        if( panel->cat_var_shown_id != panel->cat_picked_id ||
            panel->cat_var_shown_kind != panel->cat_kind ||
            panel->cat_var_shown_epoch != panel->cat_built_epoch )
            editor_variant_rebuild(panel, app);
    }

    /* The square coordinates, listed once. The content tree does not gain
     * squares while a session runs, and re-listing every frame would stat a
     * directory every frame for an answer that never changes. */
    if( panel->square_panel >= 0 && !panel->sq_listed && editor->host_open )
    {
        int total = 0;
        if( editor->host.vtable->square_list(
                editor->host.user_data, panel->sq_coords, EDITOR_SQUARE_MAX, &total) ==
            EDITOR_HOST_OK )
        {
            panel->sq_total = total < EDITOR_SQUARE_MAX ? total : EDITOR_SQUARE_MAX;
            if( total > panel->sq_total )
                TORIRS_LOG("editor: %d squares in the tree, holding the first %d\n", total,
                    panel->sq_total);
        }
        panel->sq_listed = 1;
        panel->sq_shown_search[0] = '\1'; /* force the first view build */
    }

    /* The visible rows: every square whose name contains the filter, up to the
     * row cap. Rebuilt only when the filter text changes. */
    if( panel->square_panel >= 0 && panel->sq_listed )
    {
        char const* search = ToriRSChrome_Text(ui, panel->sq_in_search);
        if( strcmp(panel->sq_shown_search, search ? search : "") != 0 )
        {
            char line[TORIRS_CHROME_LABEL_MAX];
            int shown = 0;

            for( int i = 0; i < panel->sq_total && shown < EDITOR_SQUARE_ROWS; i++ )
            {
                char name[16];
                snprintf(
                    name, sizeof(name), "m%d_%d", panel->sq_coords[i * 2],
                    panel->sq_coords[i * 2 + 1]);
                if( !editor_name_matches(name, search) )
                    continue;
                snprintf(panel->sq_labels[shown], sizeof(panel->sq_labels[shown]), "%s", name);
                panel->sq_options[shown] = panel->sq_labels[shown];
                panel->sq_row_index[shown] = i;
                shown++;
            }
            panel->sq_count = shown;
            snprintf(
                panel->sq_shown_search, sizeof(panel->sq_shown_search), "%s",
                search ? search : "");
            ToriRSChrome_DropdownSetOptions(
                ui, panel->sq_dd_list, panel->sq_options, panel->sq_count, -1);

            /* Say when the view is a truncation, so a square the filter would
             * have matched but the cap dropped is not read as absent. */
            if( shown >= EDITOR_SQUARE_ROWS )
                snprintf(line, sizeof(line), "%d+ of %d - narrow it", shown, panel->sq_total);
            else
                snprintf(line, sizeof(line), "%d of %d squares", shown, panel->sq_total);
            ToriRSChrome_SetText(ui, panel->sq_row_current, line);
        }
    }

    activated = ToriRSChrome_TakeActivated(ui);
    if( activated < 0 )
        return;

    if( activated == panel->sq_item_open )
    {
        int const row = ToriRSChrome_DropdownSelected(ui, panel->sq_dd_list);
        if( row < 0 || row >= panel->sq_count )
            TORIRS_LOG("editor: pick a square in the list first\n");
        else if( Editor_DocHasUnsaved(&editor->doc) )
            /* Loading discards the scene the edits were made against, so an
             * unsaved edit would be silently lost. Refuse and say why rather
             * than opening a dialog this chrome has no widget for. */
            TORIRS_LOG("editor: save (or undo) your changes before opening another square\n");
        else
        {
            /* The row is an index into the FILTERED view; sq_row_index maps it
             * back. Using `row` against sq_coords directly would open whatever
             * square happened to sit at that position in the unfiltered list. */
            int const at = panel->sq_row_index[row];
            int const chunks[2] = { panel->sq_coords[at * 2], panel->sq_coords[at * 2 + 1] };
            char line[TORIRS_CHROME_LABEL_MAX];
            snprintf(line, sizeof(line), "opening %s", panel->sq_labels[row]);
            ToriRSChrome_SetText(ui, panel->sq_row_current, line);
            /* Requested, not called: app_world_load_begin is app.c's own, and
             * a panel reaching into it would be the chrome driving the world
             * directly. The app drains this next frame (app_map_editor_drain's
             * neighbour), which also puts the load on the frame boundary
             * instead of mid-tick. */
            panel->sq_open_x = chunks[0];
            panel->sq_open_z = chunks[1];
            panel->sq_open_pending = 1;
        }
    }
    else if( activated == panel->dd_level )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui, panel->dd_level);
        /* Row 0 is "auto"; rows 1..4 are planes 0..3. */
        panel->edit_level = (choice > 0 && choice < EDITOR_LEVEL_COUNT) ? choice - 1 : -1;

        /* With a selection, the row acts on it NOW: a latched loc moves to the
         * chosen plane as one undoable edit, a latched tile re-latches there.
         * Without this the row read as dead while anything was selected --
         * the readout pins to the selection's level, so changing the row
         * changed no pixels at all. "auto" (edit_level -1) retargets nothing:
         * it is a statement about future picks, not about the selection. */
        if( panel->edit_level >= 0 && panel->sel_kind == EDITOR_SELECTION_LOC )
            Editor_PanelReplaceSelectedLoc(
                panel, app, panel->sel_loc_id, panel->sel_shape, panel->sel_angle,
                panel->edit_level);
        else if( panel->edit_level >= 0 && panel->sel_kind == EDITOR_SELECTION_TERRAIN )
        {
            panel->sel_level = panel->edit_level;
            app->need_redraw = 1;
        }
    }
    else if( activated == panel->dd_vis || activated == panel->cb_vis_solo )
    {
        /* Row 0 is "all"; rows 1..4 are planes 0..3. The checkbox narrows the
         * same choice rather than being a second selector, so the two can
         * never disagree about WHICH plane is meant. */
        int const choice = ToriRSChrome_DropdownSelected(ui, panel->dd_vis);
        panel->vis_level = (choice > 0 && choice < EDITOR_VIS_COUNT) ? choice - 1 : -1;
        panel->vis_solo = ToriRSChrome_Checked(ui, panel->cb_vis_solo) ? 1 : 0;
        /* The mask is read by the paint path, so nothing else has to change
         * for the world to come back with fewer floors on it -- but a still
         * frame would not repaint on its own. */
        app->need_redraw = 1;
    }
    else if( activated == panel->menu_view )
    {
        /* Toggle the chosen panel. The Loc row is the one oddity: it is
         * selection-driven, so showing it with nothing selected shows an
         * empty shell -- still honoured, because a menu that second-guesses
         * its user teaches them not to trust it. */
        int const row = ToriRSChrome_DropdownSelected(ui, panel->menu_view);
        int const targets[] = {
            panel->catalog_panel, panel->square_panel, panel->panel, panel->loc_panel
        };
        if( row >= 0 && row < EDITOR_MENU_VIEW_COUNT && targets[row] >= 0 )
            ToriRSChrome_PanelSetVisible(
                ui, targets[row], !ui->panels[targets[row]].visible);
    }
    else if( activated == panel->loc_row_view_tile )
    {
        /* The selection moves from the loc to the GROUND it stands on, at the
         * loc's own level -- a wall on a bridge deck views the deck, not the
         * ground floor. The terrain fields then seed from that tile and the
         * Loc panel follows the selection down (loc_panel_stale). */
        if( panel->sel_kind == EDITOR_SELECTION_LOC )
        {
            Editor_PanelSelectTerrain(
                panel, app, panel->sel_scene_x, panel->sel_scene_z, panel->sel_level);
            app->need_redraw = 1;
        }
    }
    else if( activated == panel->cat_reset_view )
    {
        /* Default framing + refit, exactly what a fresh pick gets. */
        app->preview_dirty = 1;
        app->preview_keep_camera = 0;
    }
    else if( activated == panel->item_delete )
    {
        Editor_PanelDeleteSelection(panel, app);
    }
    else if(
        panel->sel_kind == EDITOR_SELECTION_TERRAIN &&
        (activated == panel->in_height || activated == panel->dd_underlay ||
         activated == panel->dd_overlay || activated == panel->dd_shape ||
         activated == panel->dd_rotation || activated == panel->cb_flag_block ||
         activated == panel->cb_flag_bridge || activated == panel->cb_flag_roof ||
         activated == panel->cb_flag_below) )
    {
        /* Direct manipulation for terrain, symmetric with the loc rows: the
         * changed widget names the field, the latch names the tile. Height
         * applies on Enter (that is when a text input activates). */
        enum Editor_Tool field;
        if( activated == panel->in_height )
            field = EDITOR_TOOL_HEIGHT;
        else if( activated == panel->dd_underlay )
            field = EDITOR_TOOL_UNDERLAY;
        else if(
            activated == panel->dd_overlay || activated == panel->dd_shape ||
            activated == panel->dd_rotation )
            field = EDITOR_TOOL_OVERLAY;
        else
            field = EDITOR_TOOL_FLAGS;
        editor_apply_tool_field(
            panel, app, field, panel->sel_scene_x, panel->sel_scene_z, panel->sel_level);
    }
    else if(
        (activated == panel->dd_loc_shape || activated == panel->dd_loc_rot) &&
        panel->sel_kind == EDITOR_SELECTION_LOC )
    {
        /* Direct manipulation: choosing a shape or rotation with a loc
         * selected IS the edit -- each change is one undoable command. The
         * old flow parked the choice until Apply-to-selection, which read as
         * "the dropdowns do nothing" because for a picker that never needed
         * an Apply anywhere else in the panel, it does. */
        int new_shape;
        int new_angle;
        editor_read_loc_dropdowns(panel, app, &new_shape, &new_angle);
        Editor_PanelReplaceSelectedLoc(
            panel, app, panel->sel_loc_id, new_shape, new_angle, panel->sel_level);
    }
    else if( activated == panel->menu_file )
    {
        switch( ToriRSChrome_DropdownSelected(ui, panel->menu_file) )
        {
        case 0: /* Save changed squares */
        {
            int const saved = Editor_SaveAll(editor);
            int const spawned = Editor_PanelSpawnsSave(panel, app);
            if( saved < 0 )
                TORIRS_LOG("editor: read-only session, nothing saved\n");
            else
                TORIRS_LOG("editor: saved %d square(s) as text, %d spawn file(s)\n", saved,
                    spawned);
            break;
        }
        case 1: /* Bake cache. The one bake call site in the tree. */
            TORIRS_LOG("editor: baking, this takes a while\n");
            TORIRS_ERR("editor: bake %s\n",
                Editor_Bake(editor, panel_bake_progress, NULL) ? "finished" : "FAILED");
            break;
        case 2: /* Close editor */
            Editor_PanelSetVisible(panel, ui, 0);
            break;
        default:
            break;
        }
    }
    else if( activated == panel->menu_edit )
    {
        switch( ToriRSChrome_DropdownSelected(ui, panel->menu_edit) )
        {
        case 0:
            TORIRS_LOG("editor: undo reverted %d edit(s)\n", Editor_Undo(editor));
            break;
        case 1:
            TORIRS_LOG("editor: redo reapplied %d edit(s)\n", Editor_Redo(editor));
            break;
        case 2:
            Editor_PanelApplyToSelection(panel, app);
            break;
        case 3:
            Editor_PanelClearSelection(panel, app);
            break;
        case 4:
            editor_clear_tile_locs(panel, app);
            break;
        case 5:
            /* Same placement, different loc: the selected loc becomes the
             * catalog's pick, keeping its tile, shape and rotation. */
            if( panel->sel_kind != EDITOR_SELECTION_LOC )
                TORIRS_LOG("editor: select a loc to swap first\n");
            else if( panel->cat_picked_id < 0 )
                TORIRS_LOG("editor: pick a replacement in the catalog first\n");
            else
                Editor_PanelReplaceSelectedLoc(
                    panel, app, panel->cat_picked_id, panel->sel_shape, panel->sel_angle,
                    panel->sel_level);
            break;
        default:
            break;
        }
    }
    else if( activated == panel->cat_dd_kind )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui, panel->cat_dd_kind);
        panel->cat_kind = (choice >= 0 && choice < EDITOR_CATALOG_KIND_COUNT)
                              ? choice
                              : CACHEPROVIDER_CATALOG_LOC;
    }
    else if( activated == panel->cat_dd_list )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui, panel->cat_dd_list);
        char line[TORIRS_CHROME_LABEL_MAX];

        if( choice >= 0 && choice < panel->cat_count )
        {
            panel->cat_picked_id = panel->cat_ids[choice];
            snprintf(
                panel->cat_picked_name, sizeof(panel->cat_picked_name), "%s",
                panel->cat_labels[choice]);
            snprintf(
                line, sizeof(line), "%s %s", editor_catalog_kind_names[panel->cat_kind],
                panel->cat_picked_name);
        }
        else
        {
            panel->cat_picked_id = -1;
            snprintf(line, sizeof(line), "nothing picked");
        }
        ToriRSChrome_SetText(ui, panel->cat_row_picked, line);
    }
    else if( activated == panel->cat_dd_variant )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui, panel->cat_dd_variant);

        panel->cat_var_choice =
            (choice >= 0 && choice < panel->cat_var_count) ? choice : 0;
        /* The well re-renders on its own: the preview updater keys on
         * Editor_PanelCatalogPreviewId, which just changed. */
        editor_variant_readout(panel, app);
    }
    else if( activated == panel->dd_tool )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui, panel->dd_tool);
        panel->tool = (choice >= 0 && choice < EDITOR_TOOL_ROW_COUNT) ? editor_tool_ids[choice]
                                                                     : EDITOR_TOOL_SELECT;
    }
}

void
Editor_PanelApplySharedState(
    struct Editor_Panel* panel,
    struct App* app,
    uint32_t key,
    const int32_t* values,
    int count)
{
    assert(panel);
    assert(app);
    assert(values || count == 0);

    /* Open key space: a key this panel build does not know is another
     * subscriber's business, not an error. */
    if( key != TORIRSMAPED_STATE_SELECTION )
        return;
    if( count < 7 )
        return;

    panel->applying_shared_state = 1;

    if( values[0] == EDITOR_SELECTION_NONE )
    {
        Editor_PanelClearSelection(panel, app);
        panel->applying_shared_state = 0;
        return;
    }

    /*
     * The fact addresses an absolute world tile. A connection showing a region
     * that does not contain it keeps its own selection: it cannot draw a
     * highlight on a tile it never loaded, and inventing scene coordinates
     * outside the region would put the highlight on the wrong ground.
     *
     * A connection with NO world (the controller case) has no scene frame at
     * all, so it stores the absolute tile: its readouts show the square and
     * tile the selection names, which is the whole of what it can display.
     */
    {
        int scene_x = values[1];
        int scene_z = values[2];

        if( app->world
            && !panel_world_to_scene(app, values[1], values[2], &scene_x, &scene_z) )
        {
            panel->applying_shared_state = 0;
            return;
        }

        switch( values[0] )
        {
        case EDITOR_SELECTION_TERRAIN:
            Editor_PanelSelectTerrain(panel, app, scene_x, scene_z, values[3]);
            break;
        case EDITOR_SELECTION_LOC:
        {
            /* Latch directly rather than through Editor_PanelSelectLoc: the
             * fact carries tile + loc identity, and the ELEMENT id is each
             * viewer's own to re-find — a controller connection has no world
             * and keeps -1, which every consumer of sel_element_id already
             * tolerates (a loc change kills elements mid-session anyway). */
            panel->sel_kind = EDITOR_SELECTION_LOC;
            panel->sel_scene_x = scene_x;
            panel->sel_scene_z = scene_z;
            panel->sel_level = values[3];
            panel->sel_loc_id = values[4];
            panel->sel_shape = values[5];
            panel->sel_angle = values[6];
            panel->sel_element_id = -1;
            if( app->world )
            {
                int idx =
                    World_SceneryFindAt(app->world, scene_x, scene_z, values[3], values[5]);
                if( idx >= 0 )
                {
                    struct WorldEntity_Scenery* scenery =
                        World_EntityPoolGet(&app->world->entities.scenery, idx);
                    if( scenery )
                        panel->sel_element_id = scenery->element_id;
                }
            }
            editor_loc_panel_refresh(panel, app);
            if( panel->loc_panel >= 0 && panel->visible )
                ToriRSChrome_PanelSetVisible(&app->dbg_ui, panel->loc_panel, 1);
            break;
        }
        case EDITOR_SELECTION_NPC:
        case EDITOR_SELECTION_OBJ:
            panel->sel_kind = (enum Editor_SelectionKind)values[0];
            panel->sel_scene_x = scene_x;
            panel->sel_scene_z = scene_z;
            panel->sel_level = values[3];
            panel->sel_element_id = -1;
            panel->loc_panel_stale = 1;
            break;
        default:
            break;
        }
    }

    panel->applying_shared_state = 0;
}

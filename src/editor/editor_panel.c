#include "editor_panel.h"

#include "app.h"
#include "ui/uitree_debug_overlay.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Chrome strings. Baked into the binary like every other row label here — the
 * editor's own UI never reads the cache, because the cache is what it repairs. */
static char const* const editor_tool_names[EDITOR_TOOL_COUNT] = {
    "Select", "Height", "Underlay", "Overlay", "Flags",
};

static char const* const editor_shape_names[] = {
    "0 plain",  "1 diag",   "2 corner", "3 corner",  "4 half",  "5 half",   "6 tri",
    "7 tri",    "8 tri",    "9 tri",    "10 tri",    "11 tri",
};
#define EDITOR_SHAPE_COUNT ((int)(sizeof(editor_shape_names) / sizeof(editor_shape_names[0])))

static char const* const editor_rotation_names[] = { "0", "90", "180", "270" };

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

    panel->panel = ToriRSChrome_PanelAdd(ui, TORIDBG_PANEL_WINDOW, 8, 8, 0, "Map Editor");
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

    panel->dd_tool =
        ToriRSChrome_Dropdown(ui, panel->panel, "Tool", editor_tool_names, EDITOR_TOOL_COUNT, 0);
    panel->in_height = ToriRSChrome_TextInput(ui, panel->panel, "Height", "0");
    panel->dd_underlay = ToriRSChrome_Dropdown(ui, panel->panel, "Under", NULL, 0, -1);
    panel->dd_overlay = ToriRSChrome_Dropdown(ui, panel->panel, "Over", NULL, 0, -1);
    panel->dd_shape = ToriRSChrome_Dropdown(
        ui, panel->panel, "Shape", editor_shape_names, EDITOR_SHAPE_COUNT, 0);
    panel->dd_rotation = ToriRSChrome_Dropdown(ui, panel->panel, "Rot", editor_rotation_names, 4, 0);
    ToriRSChrome_Separator(ui, panel->panel);

    panel->cb_flag_block = ToriRSChrome_Checkbox(ui, panel->panel, "block", 0);
    panel->cb_flag_bridge = ToriRSChrome_Checkbox(ui, panel->panel, "link below", 0);
    panel->cb_flag_roof = ToriRSChrome_Checkbox(ui, panel->panel, "remove roof", 0);
    panel->cb_flag_below = ToriRSChrome_Checkbox(ui, panel->panel, "vis below", 0);
    ToriRSChrome_Separator(ui, panel->panel);

    panel->item_undo = ToriRSChrome_MenuItem(ui, panel->panel, "Undo  [Z]");
    panel->item_redo = ToriRSChrome_MenuItem(ui, panel->panel, "Redo  [Y]");
    panel->item_save = ToriRSChrome_MenuItem(ui, panel->panel, "Save changed squares");
    /* Deliberately the only thing in the tree that bakes. Saving writes text
     * and stops; the cache the game reads is rebuilt when the user says so. */
    panel->item_bake = ToriRSChrome_MenuItem(ui, panel->panel, "Bake cache (slow)");
    panel->item_close = ToriRSChrome_MenuItem(ui, panel->panel, "Close  [8]");

    ToriRSChrome_PanelSetVisible(ui, panel->panel, 0);
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

/* ---- readout ------------------------------------------------------------- */

static void
panel_refresh(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct ToriRSChrome* ui = &app->dbg_ui;
    struct Editor* editor = app->editor;
    char line[TORIDBG_LABEL_MAX];
    int map_x;
    int map_z;
    int tile_x;
    int tile_z;
    /* SELECT with a latch pins the readout to what was latched, not wherever
     * the mouse has since wandered -- the same reason the loc editor reads
     * locedit_scene_x/z rather than the live hover once a target is set. */
    int const latched = panel->tool == EDITOR_TOOL_SELECT && panel->sel_kind != EDITOR_SELECTION_NONE;
    int const probe_x = latched ? panel->sel_scene_x : app->world_hover_tile_x;
    int const probe_z = latched ? panel->sel_scene_z : app->world_hover_tile_z;
    int level = latched ? panel->sel_level : (app->world ? app->world_hover_tile_level : 0);

    snprintf(
        line,
        sizeof(line),
        "%s%s",
        editor->writable ? "writable" : "READ-ONLY",
        Editor_DocHasUnsaved(&editor->doc) ? "  *unsaved*" : "");
    ToriRSChrome_SetText(ui, panel->row_status, line);

    if( probe_x < 0 || !scene_to_square(app, probe_x, probe_z, &map_x, &map_z, &tile_x, &tile_z) )
    {
        ToriRSChrome_SetText(ui, panel->row_square, latched ? "latched tile is off-map" : "no tile under cursor");
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

    if( panel->tool == EDITOR_TOOL_SELECT )
        return 0;
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

    switch( panel->tool )
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
    case EDITOR_TOOL_COUNT:
    default:
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
        fprintf(stderr, "editor: loc moves across a square border are not saved yet\n");
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

/* ---- select tool ----------------------------------------------------------
 *
 * EDITOR_TOOL_SELECT's latch. `panel_refresh` reads it (instead of the live
 * hover) once one exists, so the readout and the highlight the overlay draws
 * (app_overlay_build_editor_selection, src/app.c) stay pinned to what the
 * user actually latched even after the mouse moves off it. */

void
Editor_PanelSelectTerrain(
    struct Editor_Panel* panel,
    int scene_x,
    int scene_z,
    int level)
{
    assert(panel);

    panel->sel_kind = EDITOR_SELECTION_TERRAIN;
    panel->sel_scene_x = scene_x;
    panel->sel_scene_z = scene_z;
    panel->sel_level = level;
    panel->sel_element_id = -1;
    panel->sel_loc_id = -1;
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
}

void
Editor_PanelClearSelection(
    struct Editor_Panel* panel)
{
    assert(panel);

    panel->sel_kind = EDITOR_SELECTION_NONE;
    panel->sel_element_id = -1;
    panel->sel_loc_id = -1;
}

/* ---- tick ---------------------------------------------------------------- */

static void
panel_bake_progress(
    void* user_data,
    char const* line)
{
    (void)user_data;
    fprintf(stderr, "bake: %s\n", line);
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

    panel_refresh(panel, app);

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

    activated = ToriRSChrome_TakeActivated(ui);
    if( activated < 0 )
        return;

    if( activated == panel->dd_tool )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui, panel->dd_tool);
        panel->tool = (choice >= 0 && choice < EDITOR_TOOL_COUNT) ? (enum Editor_Tool)choice
                                                                  : EDITOR_TOOL_SELECT;
    }
    else if( activated == panel->item_undo )
    {
        fprintf(stderr, "editor: undo reverted %d edit(s)\n", Editor_Undo(editor));
    }
    else if( activated == panel->item_redo )
    {
        fprintf(stderr, "editor: redo reapplied %d edit(s)\n", Editor_Redo(editor));
    }
    else if( activated == panel->item_save )
    {
        int const saved = Editor_SaveAll(editor);
        if( saved < 0 )
            fprintf(stderr, "editor: read-only session, nothing saved\n");
        else
            fprintf(stderr, "editor: saved %d square(s) as text\n", saved);
    }
    else if( activated == panel->item_bake )
    {
        /* The one bake call site. Everything else the editor does stops at the
         * text sources. */
        fprintf(stderr, "editor: baking, this takes a while\n");
        fprintf(
            stderr,
            "editor: bake %s\n",
            Editor_Bake(editor, panel_bake_progress, NULL) ? "finished" : "FAILED");
    }
    else if( activated == panel->item_close )
    {
        Editor_PanelSetVisible(panel, ui, 0);
    }
}

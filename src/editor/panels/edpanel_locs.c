/*
 * Locs (design #placement): the multi-selection -- shift-click adds, the
 * list shows each by its document address -- and what acts on it: nudge one
 * tile (also the arrow keys), rotate (R), reshape within the shape family
 * (S), duplicate (D), delete (Del), swap to the catalog's pick. Plus the
 * Place tool's pose, which the hover ghost shows before the click commits it.
 *
 * Every action is a set of LOC commands submitted as one undo step
 * (edpanel_actions.c); the EDIT world shows them through its per-frame flush,
 * the STATIC one through a square rebuild. A loc moved across a square border
 * is a delete from one square and an add to the other in the same stroke.
 */

#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanel_ops_stamps.h"
#include "edpanels.h"
#include "engine/torirs_types.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "world/world.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LOCS_LIST_ROWS 6

/* The shape strip: one button per shape the subject loc has (its config's
 * models), at most this many -- no loc in the cache has more. */
#define LOCS_STRIP_MAX 16
/* The strip's well (logical px): the App renders this wide, this tall. */
#define LOCS_STRIP_W 224
#define LOCS_STRIP_H 44
#define LOCS_STRIP_SLOT_MAX 52
#define LOCS_STRIP_CURRENT_RGB 0xffff40u
#define LOCS_STRIP_SLOT_RGB 0x6a6050u
static const char* const k_rotation_names[4] = { "0", "90", "180", "270" };
static const char* const k_layer_names[4] = { "wall layer", "wall decor layer", "ground layer", "ground decor layer" };

struct LocsPanel
{
    int panel;
    int row_count;
    /* The list's search box (design R3: a list that can pass 20 rows has
     * one): what the rows are filtered by, and the rows it lets through. */
    int input_find;
    char shown_filter[TORIRS_CHROME_INPUT_MAX];
    int rows[EDITOR_LOC_SELECTION_MAX];
    int listed;
    /* Follow: a click on a row puts the camera on that loc. */
    int check_follow;
    /* The selection inspector's rows (design #placing, "The Locs panel as
     * a selection inspector"): the primary's footprint and multiloc. */
    int row_footprint;
    int row_multiloc;
    int info_loc_id;
    int info_rotation;
    int info_shape;
    int info_known;
    int button_level_up;
    int button_level_down;
    int button_find_same;
    int input_stamp_name;
    int check_stamp_tiles;
    int check_stamp_spawns;
    int check_stamp_overwrite;
    int button_save_stamp;
    /* The strip's picture as the App last handed it (0: none yet), and the
     * shape the drawn picture marked current. */
    int strip_sprite;
    int strip_sprite_w;
    int strip_sprite_h;
    int strip_slot_w;
    uint32_t strip_version;
    int strip_hover;
    int list;
    int button_north;
    int button_south;
    int button_west;
    int button_east;
    int button_rotate;
    int button_reshape;
    int button_duplicate;
    int button_delete;
    int button_swap;
    int button_fit;
    int dropdown_shape;
    int dropdown_rotation;
    int check_fit;
    int strip_label;
    int strip_view;
    int strip_buttons[LOCS_STRIP_MAX];

    /* The shape strip's subject: the selection's first loc, else the Place
     * tool's pick. Rebuilt when the subject, its shape set or its current
     * shape moves. */
    int strip_loc_id;
    uint32_t strip_mask;
    int strip_current;
    int strip_selected;
    int strip_count;
    int strip_shapes[EDITOR_LOC_SHAPE_COUNT]; /* Editor_LocShapeList fills up to 23 */
    /* The Place pose dropdown lists the pick's shapes. */
    int pose_loc_id;
    uint32_t pose_mask;
    int pose_count;
    int pose_shapes[EDITOR_LOC_SHAPE_COUNT];
    /* The dropdown keeps pointers to its option texts: they live here. */
    char pose_names[LOCS_STRIP_MAX][32];
    const char* pose_name_ptrs[LOCS_STRIP_MAX];
    int pose_row;

    struct Editor_LocRef shown[EDITOR_LOC_SELECTION_MAX];
    int shown_count;
    int shown_valid;
    /** The selection was empty last frame: the panel follows it up and down. */
    int was_empty;
    /** ui->hotkeys.serial the buttons' tips were labelled from. */
    uint32_t hint_serial;
};

/** The one Locs panel of this window, for the App's thumbnail render
 *  (Editor_Locs_ShapeStrip). */
static struct LocsPanel* g_locs;

/** The strip's subject: the selection's first loc, else a loc pick. */
static int
locs_subject(
    const struct EditorUi* ui,
    int* out_shape)
{
    if( ui->tools.loc_count > 0 )
    {
        *out_shape = ui->tools.locs[0].shape;
        return ui->tools.locs[0].loc_id;
    }
    if( ui->tools.pick_kind == 0 && ui->tools.pick_id >= 0 )
    {
        *out_shape = ui->tools.place_shape;
        return ui->tools.pick_id;
    }
    *out_shape = -1;
    return -1;
}

/** The strip's well: the App's picture (when there is one) with a keyed
 *  slot per shape over it, the current shape outlined, the hovered one
 *  brighter; before the picture lands, the shape numbers. Restated only
 *  when something it shows moved (GraphEnd records nothing for the same
 *  picture anyway). */
static void
locs_strip_draw(
    struct EditorUi* ui,
    struct LocsPanel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int slot_w = self->strip_slot_w;

    if( self->strip_count <= 0 )
    {
        ToriRSChrome_GraphBegin(chrome, self->strip_view);
        ToriRSChrome_GraphEnd(chrome, self->strip_view);
        return;
    }
    if( slot_w <= 0 || !self->strip_sprite )
    {
        slot_w = LOCS_STRIP_W / self->strip_count;
        if( slot_w > LOCS_STRIP_SLOT_MAX )
            slot_w = LOCS_STRIP_SLOT_MAX;
    }
    ToriRSChrome_GraphBegin(chrome, self->strip_view);
    if( self->strip_sprite )
        ToriRSChrome_GraphImage(chrome, self->strip_view, 0, 0, self->strip_sprite_w, self->strip_sprite_h,
            self->strip_sprite, 0, self->strip_version);
    for( int i = 0; i < self->strip_count; i++ )
    {
        int const shape = self->strip_shapes[i];
        int const current = shape == self->strip_current;
        uint32_t const rgb = current                     ? LOCS_STRIP_CURRENT_RGB
                             : i + 1 == self->strip_hover ? 0xd8d0c0u
                                                          : LOCS_STRIP_SLOT_RGB;
        /* The key is the slot + 1: GraphKeyAt answers 0 for none. */
        ToriRSChrome_GraphRect(chrome, self->strip_view, i * slot_w, 0, slot_w - 1, LOCS_STRIP_H - 1, rgb, 0, i + 1);
        if( !self->strip_sprite )
        {
            char number[4];
            snprintf(number, sizeof(number), "%d", shape);
            ToriRSChrome_GraphText(chrome, self->strip_view, i * slot_w + 4, LOCS_STRIP_H / 2 + 4, rgb, number);
        }
    }
    ToriRSChrome_GraphEnd(chrome, self->strip_view);
}

/** A strip slot chosen (its thumbnail or its button): the selection takes
 *  that shape, or with nothing selected the Place pose does. */
static void
locs_strip_choose(
    struct EditorUi* ui,
    struct LocsPanel* self,
    int slot)
{
    int const shape = self->strip_shapes[slot];

    assert(slot >= 0);
    assert(slot < self->strip_count);
    if( ui->tools.loc_count > 0 )
        (void)edp_locs_set_shape(ui, shape);
    else
    {
        ui->tools.place_shape = shape;
        Editor_UiLog(ui, "editor: place shape %d %s", shape, Editor_LocShapeName(shape));
    }
}

/** The strip follows its subject: a button per shape the loc has, the
 *  current one marked. */
static void
locs_strip_refresh(
    struct EditorUi* ui,
    struct LocsPanel* self)
{
    int shape = -1;
    int const loc_id = locs_subject(ui, &shape);
    uint32_t mask = 0;
    int known = loc_id >= 0 && edp_loc_shapes(ui, loc_id, &mask);
    char line[TORIRS_CHROME_LABEL_MAX];

    if( !known )
        mask = 0;
    if( loc_id == self->strip_loc_id && mask == self->strip_mask && shape == self->strip_current
        && (ui->tools.loc_count > 0) == self->strip_selected )
        return;
    self->strip_selected = ui->tools.loc_count > 0;
    /* A new loc or shape set is a new picture: the old one shows other
     * shapes in those slots. */
    if( loc_id != self->strip_loc_id || mask != self->strip_mask )
        self->strip_sprite = 0;
    self->strip_loc_id = loc_id;
    self->strip_mask = mask;
    self->strip_current = shape;
    self->strip_count = Editor_LocShapeList(mask, self->strip_shapes);
    if( self->strip_count > LOCS_STRIP_MAX )
        self->strip_count = LOCS_STRIP_MAX;
    if( loc_id < 0 )
        snprintf(line, sizeof(line), "Shapes: select a loc or pick one in the catalog");
    else if( !known )
        snprintf(line, sizeof(line), "Shapes of loc %d: loading...", loc_id);
    else
        snprintf(line, sizeof(line), "Shapes of loc %d (%d)%s", loc_id, self->strip_count,
            ui->tools.loc_count > 0 ? " -- click to reshape" : " -- click to set the pose");
    ToriRSChrome_SetText(ui->chrome, self->strip_label, line);
    for( int i = 0; i < LOCS_STRIP_MAX; i++ )
    {
        char label[TORIRS_CHROME_LABEL_MAX];
        if( i >= self->strip_count )
        {
            ToriRSChrome_SetHidden(ui->chrome, self->strip_buttons[i], 1);
            continue;
        }
        snprintf(label, sizeof(label), "%s%d %s", self->strip_shapes[i] == shape ? "> " : "", self->strip_shapes[i],
            Editor_LocShapeName(self->strip_shapes[i]));
        ToriRSChrome_SetLabel(ui->chrome, self->strip_buttons[i], label);
        ToriRSChrome_SetHidden(ui->chrome, self->strip_buttons[i], 0);
    }
    ToriRSChrome_SetHidden(ui->chrome, self->strip_view, self->strip_count == 0);
    locs_strip_draw(ui, self);
}

void
Editor_Locs_ShapeStripSprite(
    struct EditorUi* ui,
    int scene_id,
    int strip_w,
    int strip_h,
    int slot_w,
    uint32_t version)
{
    assert(ui);
    assert(scene_id > 0);
    assert(strip_w > 0);
    assert(strip_h > 0);
    assert(slot_w > 0);
    if( !g_locs )
        return;
    g_locs->strip_sprite = scene_id;
    g_locs->strip_sprite_w = strip_w;
    g_locs->strip_sprite_h = strip_h;
    g_locs->strip_slot_w = slot_w;
    g_locs->strip_version = version;
    locs_strip_draw(ui, g_locs);
}

/** The strip's GRAPH: a press on a thumbnail chooses its shape; the hover
 *  says what the click would do (R5) and brightens the slot. */
static int
locs_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct LocsPanel* self = opaque;
    int const slot = event->key >= 1 && event->key <= self->strip_count ? event->key - 1 : -1;

    if( event->widget != self->strip_view )
        return 0;
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_PRESS && slot >= 0 )
        locs_strip_choose(ui, self, slot);
    else if( event->kind == TORIRS_CHROME_EVENT_GRAPH_HOVER && slot + 1 != self->strip_hover )
    {
        char tip[TORIRS_CHROME_LABEL_MAX];
        self->strip_hover = slot + 1;
        if( slot >= 0 )
            snprintf(tip, sizeof(tip), "%s %d %s", ui->tools.loc_count > 0 ? "Reshape to" : "Place as",
                self->strip_shapes[slot], Editor_LocShapeName(self->strip_shapes[slot]));
        else
            snprintf(tip, sizeof(tip), "Click a shape to %s", ui->tools.loc_count > 0 ? "reshape" : "set the pose");
        ToriRSChrome_SetTooltip(ui->chrome, self->strip_view, tip);
        edp_tooltip_chord(ui, self->strip_view, EDITOR_HK_EDIT_RESHAPE);
        locs_strip_draw(ui, self);
    }
    return 1;
}

/** The Place pose's Shape dropdown lists the pick's shapes only. */
static void
locs_pose_refresh(
    struct EditorUi* ui,
    struct LocsPanel* self)
{
    int const loc_id = ui->tools.pick_kind == 0 ? ui->tools.pick_id : -1;
    uint32_t mask = 0;
    int row = 0;

    if( loc_id < 0 || !edp_loc_shapes(ui, loc_id, &mask) )
        mask = 0;
    if( loc_id != self->pose_loc_id || mask != self->pose_mask )
    {
        self->pose_loc_id = loc_id;
        self->pose_mask = mask;
        self->pose_count = Editor_LocShapeList(mask, self->pose_shapes);
        if( self->pose_count > LOCS_STRIP_MAX )
            self->pose_count = LOCS_STRIP_MAX;
        if( self->pose_count == 0 )
        {
            /* No loc picked (or still loading): the pose as it stands. */
            self->pose_count = 1;
            self->pose_shapes[0] = ui->tools.place_shape;
        }
        self->pose_row = -1;
    }
    for( int i = 0; i < self->pose_count; i++ )
    {
        int const shape = self->pose_shapes[i];
        snprintf(self->pose_names[i], sizeof(self->pose_names[i]), "%d %s", shape,
            shape >= 0 && shape < EDITOR_LOC_SHAPE_COUNT ? Editor_LocShapeName(shape) : "?");
        self->pose_name_ptrs[i] = self->pose_names[i];
        if( shape == ui->tools.place_shape )
            row = i;
    }
    if( row != self->pose_row )
    {
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_shape, self->pose_name_ptrs, self->pose_count, row);
        self->pose_row = row;
    }
}

int
Editor_Locs_ShapeStrip(
    struct EditorUi* ui,
    int* out_loc_id,
    int out_shapes[EDITOR_LOC_SHAPE_COUNT],
    int* out_view)
{
    assert(ui);
    assert(out_loc_id);
    assert(out_shapes);
    assert(out_view);
    (void)ui;
    if( !g_locs || g_locs->strip_count == 0 )
        return 0;
    *out_loc_id = g_locs->strip_loc_id;
    *out_view = g_locs->strip_view;
    memcpy(out_shapes, g_locs->strip_shapes, sizeof(int) * (size_t)g_locs->strip_count);
    return g_locs->strip_count;
}

/** Each button wears its chord as a keycap (and its tooltip ends with every
 *  chord), from the hotkey table; again whenever the table's serial moves. */
static void
locs_hints(
    struct EditorUi* ui,
    struct LocsPanel* self)
{
    static const struct
    {
        size_t offset;
        enum Editor_HotkeyAction action;
        const char* tip;
    } k_hints[] = {
        { offsetof(struct LocsPanel, button_north), EDITOR_HK_EDIT_NUDGE_N, "Nudge the selection one tile north" },
        { offsetof(struct LocsPanel, button_south), EDITOR_HK_EDIT_NUDGE_S, "Nudge the selection one tile south" },
        { offsetof(struct LocsPanel, button_west), EDITOR_HK_EDIT_NUDGE_W, "Nudge the selection one tile west" },
        { offsetof(struct LocsPanel, button_east), EDITOR_HK_EDIT_NUDGE_E, "Nudge the selection one tile east" },
        { offsetof(struct LocsPanel, button_rotate), EDITOR_HK_EDIT_ROTATE, "Rotate the selection a quarter turn" },
        { offsetof(struct LocsPanel, button_reshape), EDITOR_HK_EDIT_RESHAPE, "Cycle the selection's shape" },
        { offsetof(struct LocsPanel, button_duplicate), EDITOR_HK_EDIT_DUPLICATE, "Duplicate one tile east" },
        { offsetof(struct LocsPanel, button_delete), EDITOR_HK_EDIT_DELETE, "Delete the selected locs" },
        { offsetof(struct LocsPanel, button_swap), EDITOR_HK_PLACE_SWAP,
          "Swap the selected locs to the catalog's pick" },
        { offsetof(struct LocsPanel, button_level_up), EDITOR_HK_EDIT_LEVEL_UP,
          "Move the selection a level up (each loc keeps its tile)" },
        { offsetof(struct LocsPanel, button_level_down), EDITOR_HK_EDIT_LEVEL_DOWN,
          "Move the selection a level down (each loc keeps its tile)" },
    };
    for( size_t i = 0; i < sizeof(k_hints) / sizeof(k_hints[0]); i++ )
    {
        int const widget = *(const int*)((const char*)self + k_hints[i].offset);
        ToriRSChrome_SetTooltip(ui->chrome, widget, k_hints[i].tip);
        edp_button_chord(ui, widget, k_hints[i].action);
    }
    self->hint_serial = ui->hotkeys.serial;
}

/* ---- the Place tool with an npc / obj pick ---------------------------------- */

/* ---- the selection inspector ------------------------------------------------ */

/** A row passes the Find box: its loc id, its shape's name, or its tile
 *  ("3222,3218") holds the text. */
static int
locs_row_matches(
    const struct Editor_LocRef* ref,
    const char* filter)
{
    char row[96];
    int wx;
    int wz;

    if( !filter[0] )
        return 1;
    Editor_LocRefWorld(ref, &wx, &wz);
    snprintf(row, sizeof(row), "%d %s %d,%d L%d", ref->loc_id,
        ref->shape >= 0 && ref->shape < EDITOR_LOC_SHAPE_COUNT ? Editor_LocShapeName(ref->shape) : "?", wx, wz,
        ref->level);
    return strstr(row, filter) != NULL;
}

/**
 * The primary's footprint and multiloc rows, from the App's loc record:
 * "Footprint 2x1 | ground layer | blocks: yes" (the placed size, turned
 * with the loc), "Multiloc: shell of 3 (varbit 4153)" or "Multiloc: no
 * shell". Rebuilt when the primary, its pose or the record's residency
 * moves; a record not resident is asked for once and says "loading".
 */
static void
locs_info_refresh(
    struct EditorUi* ui,
    struct LocsPanel* self)
{
    char footprint[TORIRS_CHROME_LABEL_MAX];
    char multiloc[TORIRS_CHROME_LABEL_MAX];
    struct ToriRS_Location* cfg = NULL;
    const struct Editor_LocRef* ref;
    int layer;

    if( ui->tools.loc_count == 0 )
    {
        if( self->info_loc_id != -1 )
        {
            self->info_loc_id = -1;
            ToriRSChrome_SetText(ui->chrome, self->row_footprint, "Footprint: select a loc");
            ToriRSChrome_SetText(ui->chrome, self->row_multiloc, "Multiloc: select a loc");
        }
        return;
    }
    ref = &ui->tools.locs[0];
    if( ui->app_services.loc_record )
        cfg = ui->app_services.loc_record(ui->app_services.user_data, ref->loc_id);
    if( ref->loc_id == self->info_loc_id && ref->rotation == self->info_rotation && ref->shape == self->info_shape
        && (cfg != NULL) == self->info_known )
        return;
    if( !cfg && ui->app_services.loc_request && ref->loc_id != self->info_loc_id )
        ui->app_services.loc_request(ui->app_services.user_data, ref->loc_id);
    self->info_loc_id = ref->loc_id;
    self->info_rotation = ref->rotation;
    self->info_shape = ref->shape;
    self->info_known = cfg != NULL;
    layer = World_LocShapeToLayer(ref->shape);
    if( !cfg )
    {
        snprintf(footprint, sizeof(footprint), "Footprint: %s, loc %d loading...",
            layer >= 0 ? k_layer_names[layer] : "no layer", ref->loc_id);
        snprintf(multiloc, sizeof(multiloc), "Multiloc: loc %d loading...", ref->loc_id);
    }
    else
    {
        int const sx = (ref->rotation & 1) ? cfg->size_z : cfg->size_x;
        int const sz = (ref->rotation & 1) ? cfg->size_x : cfg->size_z;
        snprintf(footprint, sizeof(footprint), "Footprint %dx%d | %s | blocks: %s", sx > 0 ? sx : 1, sz > 0 ? sz : 1,
            layer >= 0 ? k_layer_names[layer] : "no layer", cfg->blocks_walk ? "yes" : "no");
        if( cfg->transform_count > 0 && cfg->transform_varbit >= 0 )
            snprintf(multiloc, sizeof(multiloc), "Multiloc: shell of %d (varbit %d)", cfg->transform_count,
                cfg->transform_varbit);
        else if( cfg->transform_count > 0 )
            snprintf(multiloc, sizeof(multiloc), "Multiloc: shell of %d (varp %d)", cfg->transform_count,
                cfg->transform_varp);
        else
            snprintf(multiloc, sizeof(multiloc), "Multiloc: no shell");
    }
    ToriRSChrome_SetText(ui->chrome, self->row_footprint, footprint);
    ToriRSChrome_SetText(ui->chrome, self->row_multiloc, multiloc);
}

/** Follow: the camera onto the row's loc, keeping its yaw and pitch. */
static void
locs_follow_row(
    struct EditorUi* ui,
    struct LocsPanel* self,
    int row)
{
    const struct Editor_LocRef* ref;
    int wx;
    int wz;

    if( row < 0 || row >= self->listed || !ToriRSChrome_Checked(ui->chrome, self->check_follow) )
        return;
    ref = &self->shown[self->rows[row]];
    Editor_LocRefWorld(ref, &wx, &wz);
    if( !ui->app_services.camera_to_tile )
    {
        Editor_UiStatus(ui, "Follow: this window has no camera to move");
        return;
    }
    if( !ui->app_services.camera_to_tile(ui->app_services.user_data, wx, wz, ref->level) )
        Editor_UiStatus(ui, "Follow: %d,%d is not in the loaded scene", wx, wz);
}

int
Editor_Locs_PlaceSpawn(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level)
{
    struct EditorHost_CatalogRecord record;
    struct Editor_SpawnRow row;
    struct Editor_Cmd command;
    char key[EDITOR_DOC_KEY_MAX];
    uint32_t kind;
    int placed;

    assert(ui);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);
    /* The caller armed this for an npc / obj pick; a loc pick is the loc
     * path's. */
    assert(ui->tools.pick_kind == 1 || ui->tools.pick_kind == 2);

    if( !ui->editor || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        Editor_UiLog(ui, "editor: the server has no catalog; place the spawn by name in Spawns");
        return 0;
    }
    /* The provider knows display names; the spawn grammar takes the pack
     * symbol, which the server's catalog answers. */
    kind = ui->tools.pick_kind == 1 ? TORIRSMAPED_CATALOG_NPC : TORIRSMAPED_CATALOG_OBJ;
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(&ui->editor->host, kind, ui->tools.pick_id, "", &record) != EDITOR_HOST_OK
        || !record.name || !record.name[0] || strlen(record.name) >= EDITOR_CMD_NAME_MAX )
    {
        Editor_UiLog(ui, "editor: no catalog symbol for %s %d", ui->tools.pick_kind == 1 ? "npc" : "obj",
            ui->tools.pick_id);
        Editor_HostCatalogRecordFree(&record);
        return 0;
    }
    memset(&row, 0, sizeof(row));
    row.is_obj = ui->tools.pick_kind == 2;
    snprintf(row.name, sizeof(row.name), "%s", record.name);
    Editor_HostCatalogRecordFree(&record);
    row.abs_x = world_x;
    row.abs_z = world_z;
    row.level = level;
    row.count = 1;
    Editor_SpawnDocKeyAt(key, world_x, world_z);
    if( !edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key) )
    {
        Editor_UiLog(ui, "editor: could not open the spawn document %s", key);
        return 0;
    }
    Editor_SpawnCommand(&command, &row, 1);
    placed = edp_apply(ui, &command, 1, "place spawn");
    if( placed )
        Editor_UiLog(ui, "editor: placed %s %s at %d,%d level %d", row.is_obj ? "obj" : "npc", row.name,
            world_x, world_z, level);
    return placed;
}

static void
locs_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct LocsPanel* self = opaque;
    int first;
    int shown;
    /* Open while locs are selected, and while the Place tool holds a loc
     * pick: the pose and the shape strip are the Place tool's controls. */
    int const empty = ui->tools.loc_count == 0
                      && !(ui->tools.tool == EDITOR_TOOL_LOC_PLACE && ui->tools.pick_kind == 0
                           && ui->tools.pick_id >= 0);

    if( self->hint_serial != ui->hotkeys.serial )
        locs_hints(ui, self);
    /* The selection follows an undo or redo of a rotate or swap: a ref
     * resolves by tile, shape and id, so its rotation (and an id the swap
     * changed under it) is taken from the document again. */
    if( ui->editor )
        for( int i = 0; i < ui->tools.loc_count; i++ )
        {
            struct Editor_LocRef* ref = &ui->tools.locs[i];
            struct Editor_Square* square = Editor_DocFindSquare(&ui->editor->doc, ref->map_x, ref->map_z);
            int const index = Editor_LocRefResolve(&ui->editor->doc, ref);
            if( index >= 0 && square->locs[index].rotation != ref->rotation )
                ref->rotation = square->locs[index].rotation;
        }
    (void)edp_place_pose_follow(ui);
    locs_pose_refresh(ui, self);
    locs_strip_refresh(ui, self);
    /* Q turns the pose from the keyboard: the dropdown follows. */
    if( ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_rotation) != (ui->tools.place_rotation & 3) )
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_rotation, ui->tools.place_rotation & 3);
    if( ToriRSChrome_Checked(ui->chrome, self->check_fit) != edp_place_fit_enabled(ui) )
        ToriRSChrome_SetChecked(ui->chrome, self->check_fit, edp_place_fit_enabled(ui));

    /* Opens when something is selected, closes when nothing is, like the
     * old Loc panel; the View menu still opens it by hand in between. */
    if( empty != self->was_empty )
    {
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, !empty);
        self->was_empty = empty;
    }

    locs_info_refresh(ui, self);
    {
        const char* filter = ToriRSChrome_Text(ui->chrome, self->input_find);
        if( !filter )
            filter = "";
        if( !self->shown_valid || self->shown_count != ui->tools.loc_count
            || memcmp(self->shown, ui->tools.locs, sizeof(self->shown[0]) * (size_t)ui->tools.loc_count) != 0
            || strcmp(filter, self->shown_filter) != 0 )
        {
            char line[TORIRS_CHROME_LABEL_MAX];
            memcpy(self->shown, ui->tools.locs, sizeof(self->shown[0]) * (size_t)ui->tools.loc_count);
            self->shown_count = ui->tools.loc_count;
            self->shown_valid = 1;
            snprintf(self->shown_filter, sizeof(self->shown_filter), "%s", filter);
            self->listed = 0;
            for( int i = 0; i < self->shown_count; i++ )
                if( locs_row_matches(&self->shown[i], filter) )
                    self->rows[self->listed++] = i;
            ToriRSChrome_VListSetCount(ui->chrome, self->list, self->listed);
            if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
                for( int r = first; r < first + shown; r++ )
                    ToriRSChrome_VListSetRow(ui->chrome, self->list, r, "");
            if( filter[0] )
                snprintf(line, sizeof(line), "%d of %d selected match \"%.16s\"", self->listed, self->shown_count,
                    filter);
            else
                snprintf(line, sizeof(line), "%d loc%s selected (shift-click adds)", self->shown_count,
                    self->shown_count == 1 ? "" : "s");
            ToriRSChrome_SetText(ui->chrome, self->row_count, line);
        }
    }
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->listed; r++ )
    {
        const struct Editor_LocRef* ref = &self->shown[self->rows[r]];
        char text[TORIRS_CHROME_INPUT_MAX];
        int wx;
        int wz;
        int const live = ui->editor ? Editor_LocRefResolve(&ui->editor->doc, ref) >= 0 : 0;
        Editor_LocRefWorld(ref, &wx, &wz);
        /* The primary first and marked: the gizmo stands on it. */
        snprintf(text, sizeof(text), "%sloc %d  shape %d %s  rot %d  %d,%d L%d%s", self->rows[r] == 0 ? "> " : "",
            ref->loc_id, ref->shape,
            ref->shape >= 0 && ref->shape < EDITOR_LOC_SHAPE_COUNT ? Editor_LocShapeName(ref->shape) : "?",
            ref->rotation, wx, wz, ref->level, live ? "" : "  (gone)");
        ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
    }
}

static int
locs_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct LocsPanel* self = opaque;

    if( widget == self->list )
    {
        /* A row's primary action (a click, Enter): with Follow on, the
         * camera goes to it. */
        locs_follow_row(ui, self, ToriRSChrome_VListSelected(ui->chrome, self->list));
        return 1;
    }
    if( widget == self->input_find || widget == self->input_stamp_name || widget == self->check_follow
        || widget == self->check_stamp_tiles || widget == self->check_stamp_spawns
        || widget == self->check_stamp_overwrite )
        return 1;
    for( int i = 0; i < self->strip_count; i++ )
        if( widget == self->strip_buttons[i] )
        {
            locs_strip_choose(ui, self, i);
            return 1;
        }
    if( widget == self->button_level_up )
        (void)edp_locs_level_move(ui, 1);
    else if( widget == self->button_level_down )
        (void)edp_locs_level_move(ui, -1);
    else if( widget == self->button_find_same )
        (void)edp_locs_find_same(ui);
    else if( widget == self->button_save_stamp )
    {
        const char* name = ToriRSChrome_Text(ui->chrome, self->input_stamp_name);
        if( !name || !name[0] )
            Editor_UiStatus(ui, "Save as stamp refused: type the stamp's name first");
        else
            (void)edp_stamp_save_selection(ui, name, ToriRSChrome_Checked(ui->chrome, self->check_stamp_tiles),
                ToriRSChrome_Checked(ui->chrome, self->check_stamp_spawns),
                ToriRSChrome_Checked(ui->chrome, self->check_stamp_overwrite));
    }
    else if( widget == self->button_north )
        (void)edp_locs_nudge(ui, 0, 1);
    else if( widget == self->button_south )
        (void)edp_locs_nudge(ui, 0, -1);
    else if( widget == self->button_west )
        (void)edp_locs_nudge(ui, -1, 0);
    else if( widget == self->button_east )
        (void)edp_locs_nudge(ui, 1, 0);
    else if( widget == self->button_rotate )
        (void)edp_locs_rotate(ui);
    else if( widget == self->button_reshape )
        (void)edp_locs_reshape(ui);
    else if( widget == self->button_duplicate )
        (void)edp_locs_duplicate(ui);
    else if( widget == self->button_delete )
        (void)edp_locs_delete(ui);
    else if( widget == self->button_swap )
        (void)edp_locs_swap(ui);
    else if( widget == self->dropdown_shape )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( row >= 0 && row < self->pose_count )
            ui->tools.place_shape = self->pose_shapes[row];
    }
    else if( widget == self->check_fit )
    {
        edp_place_fit_set(ui, ToriRSChrome_Checked(ui->chrome, widget));
        Editor_UiLog(ui, "editor: fit walls to neighbours %s", edp_place_fit_enabled(ui) ? "on" : "off");
    }
    else if( widget == self->button_fit )
        (void)edp_locs_fit(ui);
    else if( widget == self->dropdown_rotation )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( row >= 0 && row < 4 )
            ui->tools.place_rotation = row;
    }
    else
        return 0;
    return 1;
}

static void
locs_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct LocsPanel* self = opaque;
    if( visible && ui->tools.loc_count == 0 )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    self->was_empty = ui->tools.loc_count == 0;
}

static void
locs_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    if( g_locs == opaque )
        g_locs = NULL;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Locs_Register(struct EditorUi* ui)
{
    struct LocsPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;
    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->was_empty = 1;
    self->strip_loc_id = -2;
    self->strip_current = -2;
    self->pose_loc_id = -2;
    self->pose_row = -1;
    snprintf(self->pose_names[0], sizeof(self->pose_names[0]), "%d", ui->tools.place_shape);
    self->pose_name_ptrs[0] = self->pose_names[0];

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8), edp_px(ui, 260),
        edp_px(ui, 240), "Locs");
    assert(self->panel >= 0);
    self->info_loc_id = -2;
    self->row_count = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "loc id, shape name or tile (3222,3218)");
    self->check_follow = ToriRSChrome_Checkbox(chrome, self->panel, "Follow", 0);
    ToriRSChrome_SetTooltip(chrome, self->check_follow, "Clicking a row puts the camera on that loc");
    self->list = ToriRSChrome_VList(chrome, self->panel, "", LOCS_LIST_ROWS, 0);
    self->row_footprint = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_multiloc = ToriRSChrome_Label(chrome, self->panel, "");
    ToriRSChrome_SetTooltip(chrome, self->row_multiloc,
        "Is the primary a shell? Shells using it are not indexed yet");
    self->button_north = ToriRSChrome_Button(chrome, self->panel, "Nudge north");
    self->button_south = ToriRSChrome_Button(chrome, self->panel, "Nudge south");
    self->button_west = ToriRSChrome_Button(chrome, self->panel, "Nudge west");
    self->button_east = ToriRSChrome_Button(chrome, self->panel, "Nudge east");
    self->button_rotate = ToriRSChrome_Button(chrome, self->panel, "Rotate");
    self->button_reshape = ToriRSChrome_Button(chrome, self->panel, "Reshape");
    self->button_duplicate = ToriRSChrome_Button(chrome, self->panel, "Duplicate");
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete");
    self->button_swap = ToriRSChrome_Button(chrome, self->panel, "Swap to catalog pick");
    self->button_fit = ToriRSChrome_Button(chrome, self->panel, "Fit selection to neighbours");
    self->button_level_up = ToriRSChrome_Button(chrome, self->panel, "Up a level");
    self->button_level_down = ToriRSChrome_Button(chrome, self->panel, "Down a level");
    self->button_find_same = ToriRSChrome_Button(chrome, self->panel, "Find same");
    ToriRSChrome_SetTooltip(chrome, self->button_find_same,
        "Select every loc of this id on its level, open squares");
    locs_hints(ui, self);
    ToriRSChrome_Separator(chrome, self->panel);
    /* Save as stamp (editor/stamps/<name>.stamp): the Catalog's Stamps
     * places it again. */
    self->input_stamp_name = ToriRSChrome_TextInput(chrome, self->panel, "Stamp name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_stamp_name, "letters, digits, _ and -");
    self->check_stamp_tiles = ToriRSChrome_Checkbox(chrome, self->panel, "with the terrain under it", 0);
    self->check_stamp_spawns = ToriRSChrome_Checkbox(chrome, self->panel, "with the spawns on it", 0);
    self->check_stamp_overwrite = ToriRSChrome_Checkbox(chrome, self->panel, "Overwrite a stamp of that name", 0);
    self->button_save_stamp = ToriRSChrome_Button(chrome, self->panel, "Save as stamp...");
    ToriRSChrome_SetTooltip(chrome, self->button_save_stamp,
        "Save the selection to editor/stamps/<name>.stamp");
    ToriRSChrome_Separator(chrome, self->panel);
    /* The shape strip: the subject's shapes as thumbnails (the App renders
     * them, Editor_Locs_ShapeStrip, and hands the picture back,
     * Editor_Locs_ShapeStripSprite), each one clickable, and one button
     * each. */
    self->strip_label = ToriRSChrome_Label(chrome, self->panel, "");
    self->strip_view = ToriRSChrome_Graph(chrome, self->panel, "", LOCS_STRIP_H);
    ToriRSChrome_SetTooltip(chrome, self->strip_view, "Click a shape to reshape");
    for( int i = 0; i < LOCS_STRIP_MAX; i++ )
    {
        self->strip_buttons[i] = ToriRSChrome_Button(chrome, self->panel, "");
        ToriRSChrome_SetHidden(chrome, self->strip_buttons[i], 1);
    }
    ToriRSChrome_Separator(chrome, self->panel);
    ToriRSChrome_Label(chrome, self->panel, "Place pose");
    self->dropdown_shape = ToriRSChrome_Dropdown(chrome, self->panel, "Shape", self->pose_name_ptrs, 1, 0);
    self->check_fit = ToriRSChrome_Checkbox(chrome, self->panel, "Fit walls to neighbours", edp_place_fit_enabled(ui));
    self->dropdown_rotation = ToriRSChrome_Dropdown(
        chrome, self->panel, "Rotation", k_rotation_names, 4, ui->tools.place_rotation & 3);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "locs";
    desc.title = "Locs";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = locs_tick;
    desc.activate = locs_activate;
    desc.event = locs_event;
    desc.set_visible = locs_set_visible;
    desc.destroy = locs_destroy;
    Editor_UiAddPanel(ui, &desc);
    g_locs = self;
}

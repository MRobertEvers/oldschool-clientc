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
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LOCS_LIST_ROWS 6

/* Shapes worth offering when placing; the full 0..22 space is mostly roof
 * pieces. */
static const char* const k_shape_names[] = {
    "10 centrepiece", "11 centrepiece diag", "22 ground decor", "0 wall", "1 wall corner",
    "2 wall L",       "3 wall square corner", "9 wall diag",     "4 decor inside", "5 decor outside",
};
static const int k_shape_ids[] = { 10, 11, 22, 0, 1, 2, 3, 9, 4, 5 };
#define LOCS_SHAPE_COUNT ((int)(sizeof(k_shape_ids) / sizeof(k_shape_ids[0])))
static const char* const k_rotation_names[4] = { "0", "90", "180", "270" };

struct LocsPanel
{
    int panel;
    int row_count;
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
    int dropdown_shape;
    int dropdown_rotation;

    struct Editor_LocRef shown[EDITOR_LOC_SELECTION_MAX];
    int shown_count;
    int shown_valid;
    /** The selection was empty last frame: the panel follows it up and down. */
    int was_empty;
    /** ui->hotkeys.serial the buttons' tips were labelled from. */
    uint32_t hint_serial;
};

/** Each button's tip names its chord from the hotkey table. */
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
    };
    for( size_t i = 0; i < sizeof(k_hints) / sizeof(k_hints[0]); i++ )
    {
        char tip[TORIRS_CHROME_LABEL_MAX];
        int const widget = *(const int*)((const char*)self + k_hints[i].offset);
        edp_hotkey_tip(ui, k_hints[i].action, k_hints[i].tip, tip, (int)sizeof(tip));
        ToriRSChrome_SetTooltip(ui->chrome, widget, tip);
    }
    self->hint_serial = ui->hotkeys.serial;
}

/* ---- the Place tool with an npc / obj pick ---------------------------------- */

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
    int const empty = ui->tools.loc_count == 0;

    if( self->hint_serial != ui->hotkeys.serial )
        locs_hints(ui, self);

    /* Opens when something is selected, closes when nothing is, like the
     * old Loc panel; the View menu still opens it by hand in between. */
    if( empty != self->was_empty )
    {
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, !empty);
        self->was_empty = empty;
    }

    if( !self->shown_valid || self->shown_count != ui->tools.loc_count
        || memcmp(self->shown, ui->tools.locs, sizeof(self->shown[0]) * (size_t)ui->tools.loc_count)
               != 0 )
    {
        char line[TORIRS_CHROME_LABEL_MAX];
        memcpy(self->shown, ui->tools.locs, sizeof(self->shown[0]) * (size_t)ui->tools.loc_count);
        self->shown_count = ui->tools.loc_count;
        self->shown_valid = 1;
        ToriRSChrome_VListSetCount(ui->chrome, self->list, self->shown_count);
        if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list, r, "");
        snprintf(line, sizeof(line), "%d loc%s selected (shift-click adds)", self->shown_count,
            self->shown_count == 1 ? "" : "s");
        ToriRSChrome_SetText(ui->chrome, self->row_count, line);
    }
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->shown_count; r++ )
    {
        const struct Editor_LocRef* ref = &self->shown[r];
        char text[TORIRS_CHROME_INPUT_MAX];
        int wx;
        int wz;
        int const live = ui->editor ? Editor_LocRefResolve(&ui->editor->doc, ref) >= 0 : 0;
        Editor_LocRefWorld(ref, &wx, &wz);
        snprintf(text, sizeof(text), "loc %d  shape %d rot %d  %d,%d L%d%s", ref->loc_id, ref->shape,
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
        return 1;
    if( widget == self->button_north )
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
    {
        /* Same placements, the catalog's loc: a delete and an add per loc,
         * one stroke, keeping tile, shape and rotation. */
        int capacity;
        struct Editor_Cmd* commands = edp_scratch(&capacity);
        int count = 0;

        if( ui->tools.pick_kind != 0 || ui->tools.pick_id < 0 )
        {
            Editor_UiLog(ui, "editor: pick a replacement loc in the catalog first");
            return 1;
        }
        if( !ui->editor || ui->tools.loc_count == 0 )
        {
            Editor_UiLog(ui, "editor: select the locs to swap first");
            return 1;
        }
        for( int i = 0; i < ui->tools.loc_count && count < capacity; i++ )
        {
            struct Editor_LocRef* ref = &ui->tools.locs[i];
            struct Editor_Square* square = Editor_DocFindSquare(&ui->editor->doc, ref->map_x, ref->map_z);
            int const index = Editor_LocRefResolve(&ui->editor->doc, ref);
            if( index < 0 )
                continue;
            Editor_CmdInitSquare(&commands[count], EDITOR_CMD_LOC, ref->map_x, ref->map_z);
            commands[count].has_before = 1;
            commands[count].has_after = 1;
            commands[count].u.loc.before = square->locs[index];
            commands[count].u.loc.after = square->locs[index];
            commands[count].u.loc.after.loc_id = ui->tools.pick_id;
            count++;
        }
        if( edp_apply(ui, commands, count, "swap") > 0 )
        {
            for( int i = 0; i < ui->tools.loc_count; i++ )
                ui->tools.locs[i].loc_id = ui->tools.pick_id;
            if( ui->hooks.selection_changed )
                ui->hooks.selection_changed(ui->hooks_user);
        }
    }
    else if( widget == self->dropdown_shape )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( row >= 0 && row < LOCS_SHAPE_COUNT )
            ui->tools.place_shape = k_shape_ids[row];
    }
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
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Locs_Register(struct EditorUi* ui)
{
    struct LocsPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;
    int shape_row = 0;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->was_empty = 1;
    for( int i = 0; i < LOCS_SHAPE_COUNT; i++ )
        if( k_shape_ids[i] == ui->tools.place_shape )
            shape_row = i;

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8), edp_px(ui, 260),
        edp_px(ui, 240), "Locs");
    assert(self->panel >= 0);
    self->row_count = ToriRSChrome_Label(chrome, self->panel, "");
    self->list = ToriRSChrome_VList(chrome, self->panel, "", LOCS_LIST_ROWS, 0);
    self->button_north = ToriRSChrome_Button(chrome, self->panel, "Nudge north");
    self->button_south = ToriRSChrome_Button(chrome, self->panel, "Nudge south");
    self->button_west = ToriRSChrome_Button(chrome, self->panel, "Nudge west");
    self->button_east = ToriRSChrome_Button(chrome, self->panel, "Nudge east");
    self->button_rotate = ToriRSChrome_Button(chrome, self->panel, "Rotate");
    self->button_reshape = ToriRSChrome_Button(chrome, self->panel, "Reshape");
    self->button_duplicate = ToriRSChrome_Button(chrome, self->panel, "Duplicate");
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete");
    self->button_swap = ToriRSChrome_Button(chrome, self->panel, "Swap to catalog pick");
    locs_hints(ui, self);
    ToriRSChrome_Separator(chrome, self->panel);
    ToriRSChrome_Label(chrome, self->panel, "Place pose");
    self->dropdown_shape =
        ToriRSChrome_Dropdown(chrome, self->panel, "Shape", k_shape_names, LOCS_SHAPE_COUNT, shape_row);
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
    desc.set_visible = locs_set_visible;
    desc.destroy = locs_destroy;
    Editor_UiAddPanel(ui, &desc);
}

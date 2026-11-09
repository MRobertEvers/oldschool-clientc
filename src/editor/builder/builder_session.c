/*
 * The builder panels bound to the client's editor session: Builder_Services
 * over struct Editor (Editor_Apply, the mirror, the catalog through the
 * session's ToriRSMapEd connection). The panels themselves are
 * builder_panels.c; this file is what links them to editor.c, which a panel
 * test does not want.
 */
#include "builder_panels.h"

#include "editor/editor.h"
#include "editor/editor_ui.h"
#include "editor/editor_doc.h"
#include "editor/editor_spawn.h"
#include "editor/map_editor_ghost.h"
#include "editor/panels/edpanel_npcdoc.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct Editor*
session_editor(void* user_data)
{
    struct EditorUi* ui = user_data;
    assert(ui);
    return ui->editor;
}

static int
session_apply(
    void* user_data,
    const struct Editor_Cmd* command)
{
    struct Editor* editor = session_editor(user_data);
    return editor && Editor_Apply(editor, command);
}

static void
session_stroke(
    void* user_data,
    int begin)
{
    struct Editor* editor = session_editor(user_data);
    if( !editor )
        return;
    if( begin )
        Editor_StrokeBegin(editor);
    else
        Editor_StrokeEnd(editor);
}

static const char*
session_current(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key,
    size_t* out_length)
{
    struct Editor* editor = session_editor(user_data);
    struct Editor_DocHandle handle;

    assert(key);
    assert(out_length);
    *out_length = 0;
    if( !editor )
        return NULL;
    /* The echoes of what this session just sent land on the next pump; a
     * regeneration reads the text it diffs against right after. */
    Editor_PumpFacts(editor);
    if( !Editor_DocFind(&editor->doc, kind, key, &handle) || !Editor_DocHandleLoaded(&handle) )
    {
        if( !Editor_OpenDoc(editor, kind, key) )
            return NULL;
        if( !Editor_DocFind(&editor->doc, kind, key, &handle) )
            return NULL;
    }
    if( !handle.text )
        return NULL;
    *out_length = handle.text->length;
    return handle.text->text ? handle.text->text : "";
}

static int
session_save(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key)
{
    struct Editor* editor = session_editor(user_data);
    return editor ? Editor_SaveDoc(editor, kind, key) : -1;
}

static const char*
record_field(
    const struct EditorHost_CatalogRecord* record,
    const char* key)
{
    const char* value = NULL;
    /* The last source wins: a server overlay restates over the cache. */
    for( int i = 0; i < record->field_count; i++ )
    {
        if( record->fields[i].key && strcmp(record->fields[i].key, key) == 0 )
            value = record->fields[i].value;
    }
    return value;
}

static int
session_record(
    void* user_data,
    enum Builder_Subject subject,
    const char* name,
    int id,
    struct Builder_RecordInfo* out)
{
    struct Editor* editor = session_editor(user_data);
    struct EditorHost_CatalogRecord record;
    uint32_t kind = subject == BUILDER_SUBJECT_NPC ? TORIRSMAPED_CATALOG_NPC : TORIRSMAPED_CATALOG_LOC;

    assert(name);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( !editor || !(editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        /* No catalog: trust the name; the compile check is the backstop. */
        if( !name[0] )
            return 0;
        out->found = 1;
        out->id = -1;
        snprintf(out->name, sizeof(out->name), "%s", name);
        return 1;
    }
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(&editor->host, kind, id, name, &record) != EDITOR_HOST_OK )
        return 0;
    out->found = 1;
    out->id = record.id;
    snprintf(out->name, sizeof(out->name), "%s", record.name ? record.name : name);
    snprintf(out->display, sizeof(out->display), "%s", record.display ? record.display : "");
    for( int op = 0; op < 5; op++ )
    {
        char key[8];
        const char* value;
        snprintf(key, sizeof(key), "op%d", op + 1);
        value = record_field(&record, key);
        if( value && strcmp(value, "default") != 0 && strcmp(value, "hidden") != 0 )
            snprintf(out->ops[op], sizeof(out->ops[op]), "%s", value);
    }
    /* The movement statements, the later source over the earlier (the
     * merged record lists them in merge order, so the winner lands last). */
    if( subject == BUILDER_SUBJECT_NPC )
    {
        Editor_NpcMovementInit(&out->movement);
        for( int i = 0; i < record.field_count; i++ )
            if( record.fields[i].key && record.fields[i].value )
                (void)Editor_NpcMovementRead(&out->movement, record.fields[i].key, record.fields[i].value);
        out->has_movement = 1;
    }
    Editor_HostCatalogRecordFree(&record);
    return 1;
}

/** From selection: the Spawns panel's anchor row for an npc, else the
 *  Catalog's npc pick; the selected loc for a loc. */
static int
session_selection(
    void* user_data,
    enum Builder_Subject subject,
    char* out,
    size_t out_size)
{
    struct EditorUi* ui = user_data;

    assert(ui);
    assert(out);
    if( subject == BUILDER_SUBJECT_NPC )
    {
        struct Editor_SpawnRow row;
        if( Editor_UiFindPanel(ui, "spawns") && Editor_Spawns_Selection(ui, &row, 1) >= 1 && !row.is_obj )
        {
            snprintf(out, out_size, "%s", row.name);
            return 1;
        }
        if( ui->tools.pick_kind == 1 )
        {
            snprintf(out, out_size, "#%d", ui->tools.pick_id);
            return 1;
        }
        return 0;
    }
    if( ui->tools.loc_count > 0 )
    {
        snprintf(out, out_size, "#%d", ui->tools.locs[0].loc_id);
        return 1;
    }
    return 0;
}

static int
session_open_file(
    void* user_data,
    const char* rel_path,
    int line)
{
    struct EditorUi* ui = user_data;

    assert(ui);
    assert(rel_path);
    if( !Editor_UiFindPanel(ui, "triggers") )
    {
        Editor_UiStatus(ui, "Open in editor refused: this window has no Triggers panel to open scripts through");
        return 0;
    }
    return Editor_Triggers_OpenInEditor(ui, rel_path, line);
}

static int
session_triggers(
    void* user_data,
    const char* subject,
    struct Builder_ExistingList* out)
{
    struct Editor* editor = session_editor(user_data);
    struct EditorHost_CatalogPage page;
    char filter[160];
    uint32_t page_index = 0;

    assert(subject);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( !editor || !(editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return 0;
    snprintf(filter, sizeof(filter), "subject=%s", subject);
    for( ;; )
    {
        memset(&page, 0, sizeof(page));
        if( Editor_HostMapEdCatalogQuery(&editor->host, TORIRSMAPED_CATALOG_TRIGGER, filter, 0,
                                         page_index, 0, &page)
            != EDITOR_HOST_OK )
            return 0;
        for( int i = 0; i < page.row_count && out->count < BUILDER_EXISTING_MAX; i++ )
        {
            const struct EditorHost_CatalogRow* row = &page.rows[i];
            const char* comma = row->name ? strchr(row->name, ',') : NULL;
            /* The filter also matches a longer subject sharing the prefix. */
            if( !comma || strcmp(comma + 1, subject) != 0 )
                continue;
            snprintf(out->names[out->count], sizeof(out->names[0]), "%s", row->name);
            snprintf(out->files[out->count], sizeof(out->files[0]), "%s",
                     row->display ? row->display : "");
            out->rows[out->count].name = out->names[out->count];
            out->rows[out->count].file = out->files[out->count];
            out->count++;
        }
        page_index++;
        if( page.row_count == 0 || page_index * page.page_size >= page.total )
        {
            Editor_HostCatalogPageFree(&page);
            break;
        }
        Editor_HostCatalogPageFree(&page);
    }
    return 1;
}

/** Is `name` one exact record of catalog kind `kind`? 1, 0, or -1 when
 *  the catalog cannot be asked. */
static int
session_catalog_has(
    struct Editor* editor,
    uint32_t kind,
    const char* name)
{
    struct EditorHost_CatalogPage page;
    int found = 0;

    memset(&page, 0, sizeof(page));
    if( Editor_HostMapEdCatalogQuery(&editor->host, kind, name, TORIRSMAPED_CATALOG_EXACT, 0, 0, &page)
        != EDITOR_HOST_OK )
        return -1;
    for( int i = 0; i < page.row_count && !found; i++ )
        found = page.rows[i].name && strcmp(page.rows[i].name, name) == 0;
    Editor_HostCatalogPageFree(&page);
    return found;
}

/** The lint's names through the catalog: a var is a varp or a varbit
 *  record, a proc a [proc,<name>] trigger. */
static int
session_name_known(
    void* user_data,
    int kind,
    const char* name)
{
    struct Editor* editor = session_editor(user_data);
    char trigger[BUILDER_NAME_MAX + 8];
    int found;

    assert(name);
    if( !editor || !(editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return -1;
    if( kind == BUILDER_NAME_VAR )
    {
        found = session_catalog_has(editor, TORIRSMAPED_CATALOG_VARP, name);
        if( found == 0 )
            found = session_catalog_has(editor, TORIRSMAPED_CATALOG_VARBIT, name);
        return found;
    }
    snprintf(trigger, sizeof(trigger), "proc,%s", name);
    return session_catalog_has(editor, TORIRSMAPED_CATALOG_TRIGGER, trigger);
}

static int
session_state_set(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct EditorUi* ui = user_data;
    return Editor_UiPublish(ui, key, values, count);
}

static int
session_copy_record(
    void* user_data,
    enum Builder_Subject subject,
    const char* name,
    const char* new_name)
{
    struct EditorUi* ui = user_data;
    return Editor_Catalog_CopyRecord(
        ui, subject == BUILDER_SUBJECT_NPC ? TORIRSMAPED_CATALOG_NPC : TORIRSMAPED_CATALOG_LOC, name, new_name);
}

/* ---- the NPC editor's Movement and Spawns tabs ---------------------------- */

/** One npc record per window, opened on first use and kept while the editor
 *  asks about the same npc (docs/NPC_EDITOR_DESIGN.md 3.5). */
#define SESSION_NPCDOCS 4

static struct
{
    struct EditorUi* ui;
    struct Editor_NpcDoc doc;
} g_npcdocs[SESSION_NPCDOCS];

static struct Editor_NpcDoc*
session_npcdoc(
    struct EditorUi* ui,
    const char* npc)
{
    int spare = -1;

    for( int i = 0; i < SESSION_NPCDOCS; i++ )
    {
        if( g_npcdocs[i].ui == ui )
        {
            if( g_npcdocs[i].doc.open && strcmp(g_npcdocs[i].doc.name, npc) == 0 )
                return &g_npcdocs[i].doc;
            spare = i;
        }
        else if( spare < 0 && !g_npcdocs[i].ui )
            spare = i;
    }
    if( spare < 0 )
        spare = 0;
    g_npcdocs[spare].ui = ui;
    if( !Editor_NpcDocOpen(ui, &g_npcdocs[spare].doc, npc) )
        return NULL;
    return &g_npcdocs[spare].doc;
}

static int
session_movement(
    void* user_data,
    const char* npc,
    struct Editor_NpcMovement* out,
    char* target,
    size_t target_size)
{
    struct EditorUi* ui = user_data;
    struct Editor_NpcDoc* doc;

    assert(ui);
    assert(npc);
    assert(out);
    doc = session_npcdoc(ui, npc);
    if( !doc || !Editor_NpcDocMovement(ui, doc, out) )
        return 0;
    if( target && target_size )
        (void)Editor_NpcDocWriteTarget(ui, doc, "patrol1", target, target_size);
    return 1;
}

static int
session_movement_set(
    void* user_data,
    const char* npc,
    const char* key,
    const char* value,
    const char* what)
{
    struct EditorUi* ui = user_data;
    struct Editor_NpcDoc* doc;
    const char* sub = "";
    char plain[EDITOR_CMD_NAME_MAX];

    assert(ui);
    assert(npc);
    assert(key);
    assert(value);
    doc = session_npcdoc(ui, npc);
    if( !doc )
        return -1;
    /* "param:huntrange" addresses a param row. */
    if( strncmp(key, "param:", 6) == 0 )
    {
        sub = key + 6;
        snprintf(plain, sizeof(plain), "param");
        key = plain;
    }
    return Editor_NpcDocSetField(ui, doc, key, sub, value, what);
}

static int
session_route_set(
    void* user_data,
    const char* npc,
    const struct Editor_NpcWaypoint* waypoints,
    int count,
    enum Editor_NpcMode mode,
    const char* what)
{
    struct EditorUi* ui = user_data;
    struct Editor_NpcDoc* doc;

    assert(ui);
    assert(npc);
    doc = session_npcdoc(ui, npc);
    if( !doc )
        return -1;
    return Editor_NpcDocSetRoute(ui, doc, waypoints, count, mode, what);
}

static void
session_tile_info(
    void* user_data,
    int x,
    int z,
    int level,
    struct Builder_TileInfo* out)
{
    struct EditorUi* ui = user_data;
    struct Editor_Square* square;

    assert(ui);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( ui->app_services.tile_flags
        && ui->app_services.tile_flags(ui->app_services.user_data, x, z, level, &out->collision) )
        out->in_scene = 1;
    if( ui->editor && level >= 0 && level < EDITOR_SQUARE_LEVELS && x >= 0 && z >= 0 )
    {
        square = Editor_DocFindSquare(&ui->editor->doc, x / EDITOR_SQUARE_X, z / EDITOR_SQUARE_Z);
        if( square && square->loaded )
        {
            out->in_doc = 1;
            out->settings = square->tiles[Editor_TileIndex(x % EDITOR_SQUARE_X, z % EDITOR_SQUARE_Z, level)].settings;
        }
    }
}

static int
session_path_steps(
    void* user_data,
    int ax,
    int az,
    int bx,
    int bz,
    int level)
{
    struct EditorUi* ui = user_data;

    assert(ui);
    if( !ui->app_services.path_steps )
        return -2;
    return ui->app_services.path_steps(ui->app_services.user_data, ax, az, bx, bz, level);
}

static int
session_spawns(
    void* user_data,
    const char* npc,
    int id,
    struct Builder_NpcSpawn* out,
    int max)
{
    struct EditorUi* ui = user_data;
    static struct MapEditorSpawnPreviewRow rows[MAP_EDITOR_SPAWN_PREVIEW_MAX];
    int n;

    assert(ui);
    assert(npc);
    assert(out || max == 0);
    if( !Editor_UiFindPanel(ui, "spawns") )
        return 0;
    n = Editor_Spawns_RowsOf(ui, npc, id, rows, MAP_EDITOR_SPAWN_PREVIEW_MAX);
    if( n > max )
        n = max;
    for( int i = 0; i < n; i++ )
    {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].x = rows[i].row.abs_x;
        out[i].z = rows[i].row.abs_z;
        out[i].level = rows[i].row.level;
        out[i].edited = rows[i].edited;
        out[i].scripted = rows[i].scripted;
        out[i].suppressed = rows[i].suppressed;
        snprintf(out[i].name, sizeof(out[i].name), "%s", rows[i].row.name);
    }
    return n;
}

static int
session_goto_tile(
    void* user_data,
    int x,
    int z,
    int level)
{
    struct EditorUi* ui = user_data;

    assert(ui);
    if( !ui->app_services.camera_to_tile )
        return 0;
    if( ui->app_services.camera_to_tile(ui->app_services.user_data, x, z, level) )
        return 1;
    /* Not loaded: open its square (keeping the rest) and let the caller ask again. */
    Editor_UiOpenSquare(ui, x / EDITOR_SQUARE_X, z / EDITOR_SQUARE_Z, "", -1, 1);
    return 0;
}

static void
session_spawn_row(
    const struct Builder_NpcSpawn* spawn,
    struct MapEditorSpawnPreviewRow* out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->row.name, sizeof(out->row.name), "%s", spawn->name);
    out->row.abs_x = spawn->x;
    out->row.abs_z = spawn->z;
    out->row.level = spawn->level;
    out->row.count = 1;
    out->edited = spawn->edited;
    out->scripted = spawn->scripted;
    out->suppressed = spawn->suppressed;
}

static int
session_spawn_select(
    void* user_data,
    const struct Builder_NpcSpawn* spawn)
{
    struct EditorUi* ui = user_data;
    struct MapEditorSpawnPreviewRow row;

    assert(ui);
    assert(spawn);
    if( !Editor_UiFindPanel(ui, "spawns") )
        return 0;
    session_spawn_row(spawn, &row);
    /* The panel lists the latched tile's square: latch the spawn's. */
    ui->tools.tile_valid = 1;
    ui->tools.tile_x = spawn->x;
    ui->tools.tile_z = spawn->z;
    ui->tools.tile_level = spawn->level;
    return Editor_Spawns_Select(ui, &row.row, 1) == 1;
}

static int
session_spawn_place(
    void* user_data,
    const char* npc,
    int x,
    int z,
    int level)
{
    struct EditorUi* ui = user_data;
    struct Editor_SpawnRow row;

    assert(ui);
    assert(npc);
    if( !Editor_UiFindPanel(ui, "spawns") )
        return 0;
    memset(&row, 0, sizeof(row));
    snprintf(row.name, sizeof(row.name), "%s", npc);
    row.abs_x = x;
    row.abs_z = z;
    row.level = level;
    row.count = 1;
    return Editor_Spawns_PlaceRow(ui, &row);
}

static int
session_spawn_delete(
    void* user_data,
    const struct Builder_NpcSpawn* spawn)
{
    struct EditorUi* ui = user_data;
    struct MapEditorSpawnPreviewRow row;

    assert(ui);
    assert(spawn);
    if( !Editor_UiFindPanel(ui, "spawns") )
        return 0;
    session_spawn_row(spawn, &row);
    return Editor_Spawns_DeleteRow(ui, &row);
}

static void
session_release(void* user_data)
{
    struct EditorUi* ui = user_data;

    assert(ui);
    for( int i = 0; i < SESSION_NPCDOCS; i++ )
        if( g_npcdocs[i].ui == ui )
        {
            Editor_NpcDocClose(&g_npcdocs[i].doc);
            g_npcdocs[i].ui = NULL;
        }
}

static void
session_photo_place(
    void* user_data,
    int panel)
{
    struct EditorUi* ui = user_data;

    assert(ui);
    if( ui->dock_x <= 0 || ui->chrome->surface_w <= ui->dock_x )
        return;
    /* Every other View-menu panel closes, so nothing draws over the one
     * photographed (the column's frames are drawn after it). */
    for( int i = 0; i < ui->panel_count; i++ )
    {
        const struct EditorUiPanel* other = &ui->panels[i];
        if( !other->title || other->chrome_panel < 0 || other->chrome_panel == panel )
            continue;
        if( strcmp(other->name, "world") == 0 || strcmp(other->name, "console") == 0
            || strcmp(other->name, "toolrail") == 0 || strcmp(other->name, "statusbar") == 0 )
            continue;
        if( Editor_UiPanelInStrip(ui, other->name) || ui->chrome->panels[other->chrome_panel].visible )
            (void)Editor_UiTogglePanel(ui, other->name);
    }
    ToriRSChrome_PanelSetFixedWidth(ui->chrome, panel, ui->chrome->surface_w - ui->dock_x);
    ToriRSChrome_PanelSetFixedHeight(ui->chrome, panel, ui->dock_bottom - ui->dock_top);
    ToriRSChrome_PanelMove(ui->chrome, panel, ui->dock_x, ui->dock_top);
    ui->chrome->panels[panel].user_placed = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, panel, 1);
}

static void
session_services(
    struct EditorUi* ui,
    struct Builder_Services* out)
{
    memset(out, 0, sizeof(*out));
    out->user_data = ui;
    out->submit.user_data = ui;
    out->submit.apply = session_apply;
    out->submit.stroke = session_stroke;
    out->submit.current = session_current;
    out->save = session_save;
    out->record = session_record;
    out->triggers = session_triggers;
    out->state_set = session_state_set;
    out->copy_record = session_copy_record;
    out->name_known = session_name_known;
    out->selection = session_selection;
    out->open_file = session_open_file;
    out->movement = session_movement;
    out->movement_set = session_movement_set;
    out->route_set = session_route_set;
    out->tile_info = session_tile_info;
    out->path_steps = session_path_steps;
    out->spawns = session_spawns;
    out->goto_tile = session_goto_tile;
    out->spawn_select = session_spawn_select;
    out->spawn_place = session_spawn_place;
    out->spawn_delete = session_spawn_delete;
    out->release = session_release;
    out->photo_place = session_photo_place;
}

void
Editor_LocBuilder_Register(struct EditorUi* ui)
{
    struct Builder_Services services;

    assert(ui);
    session_services(ui, &services);
    Builder_LocPanelRegister(ui, &services);
}

void
Editor_NpcBuilder_Register(struct EditorUi* ui)
{
    struct Builder_Services services;

    assert(ui);
    session_services(ui, &services);
    Builder_NpcPanelRegister(ui, &services);
}

void
Editor_ShellEditor_Register(struct EditorUi* ui)
{
    struct Builder_Services services;

    assert(ui);
    session_services(ui, &services);
    Builder_ShellPanelRegister(ui, &services);
}

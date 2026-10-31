/*
 * Areas: named areas of the world and named collections of squares
 * (editor/editor_areas.h, the AREAS document every window mirrors).
 *
 *   Areas tab        the list (name, level, rectangles); New from rect takes
 *                    the Rect tool's rectangle; Add rect (to the selected), Rename,
 *                    Set level, Delete, Go to (the camera to the area's
 *                    centre); Show outlines (default on) and Hide others
 *                    drive the world overlay (app_overlay.c draws what
 *                    Editor_Areas_Overlay answers)
 *   Collections tab  the list (name, squares); New from open squares, New
 *                    from picker selection, Add / Remove the current square
 *                    (the picker's focus, else the latched tile's square),
 *                    Rename, Delete, Open all squares (a confirm past
 *                    AREAS_OPEN_CONFIRM: the world shows 16 squares at once)
 *
 * Every change is one command on the authority (AREA_SET / AREA_REMOVE /
 * COLLECTION_SET / COLLECTION_REMOVE), named in the History by the codec
 * ("Mark area Lumbridge castle"), saved with File > Save like any document.
 * The panel parses the document only when its text revision moves.
 *
 * TORIRS_EDITOR_AREAS=<opts> (photographs, comma-separated): `dock` docks the
 * panel in the column under Terrain (`tall`: down over the picker too),
 * `tab=collections` picks the tab, `select=<area name>` selects an
 * area (and `goto` moves the camera to it), `collection=<name>` selects a
 * collection and filters the picker to it.
 */

#include "app.h"
#include "app/app_internal.h"
#include "edpanel_common.h"
#include "edpanel_picker.h"
#include "edpanels.h"
#include "editor/editor.h"
#include "editor/editor_areas.h"
#include "editor/editor_view.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AREAS_LIST_ROWS 5
#define AREAS_ROWS_MAX 256
#define AREAS_ROW_TEXT 120
/** Squares Open all opens without asking (the world load's own ceiling). */
#define AREAS_OPEN_CONFIRM 16
/** Frames Go to waits for the square it opened to load. */
#define AREAS_GOTO_WAIT_FRAMES 600

enum
{
    AREAS_TAB_AREAS = 0,
    AREAS_TAB_COLLECTIONS,
};

static const char* const k_level_rows[5] = { "all levels", "level 0", "level 1", "level 2", "level 3" };

struct AreasPanel
{
    int panel;
    int tabs;

    /* Areas tab */
    int row_rect;
    int list_areas;
    int input_area_name;
    int dropdown_level;
    int button_new_rect;
    int button_add_rect;
    int button_rename_area;
    int button_set_level;
    int button_delete_area;
    int button_goto;
    int check_outlines;
    int check_hide_others;

    /* Collections tab */
    int row_current;
    int list_colls;
    int input_coll_name;
    int button_new_open;
    int button_new_picker;
    int button_add_current;
    int button_remove_current;
    int button_rename_coll;
    int button_delete_coll;
    int button_open_all;

    int row_status;

    int confirm_panel;
    int confirm_row;
    int confirm_yes;
    int confirm_no;

    /* The parse of the mirror's AREAS text, keyed on its revision. */
    struct Editor_AreasFile file;
    const struct Editor_TextDoc* file_doc;
    uint32_t file_revision;
    int file_ok;
    /** Bumps whenever `file` is re-parsed: what the rows and the picker's
     *  collection dropdown rebuild on. */
    uint32_t serial;

    char area_rows[AREAS_ROWS_MAX][AREAS_ROW_TEXT];
    int area_row_count;
    char coll_rows[AREAS_ROWS_MAX][AREAS_ROW_TEXT];
    int coll_row_count;
    uint32_t built_serial;
    int built;

    char sel_area[EDITOR_AREA_NAME_MAX];
    char sel_coll[EDITOR_AREA_NAME_MAX];

    /* Open all: the squares still to hand the window, one per load. */
    char open_queue[EDITOR_COLLECTION_SQUARES_MAX][EDITOR_COLLECTION_KEY_MAX];
    int open_count;
    int open_next;
    char confirm_coll[EDITOR_AREA_NAME_MAX];

    /* Go to: a point waiting for its square to load. */
    int goto_pending;
    int goto_x;
    int goto_z;
    int goto_level;
    int goto_frames;

    char shown_rect[TORIRS_CHROME_LABEL_MAX];
    int env_done;
    int env_dock;
    /** `tall`: docked down to the surface's bottom, over the picker. */
    int env_tall;
};

static struct AreasPanel*
areas_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "areas");
}

/* ---- the document ------------------------------------------------------------- */

static struct Editor_TextDoc*
areas_doc(struct EditorUi* ui)
{
    struct Editor_DocHandle handle;

    if( !ui->editor )
        return NULL;
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_AREAS, EDITOR_AREAS_DOC_KEY, &handle) || !handle.text
        || !handle.text->loaded )
        return NULL;
    return handle.text;
}

/** The parse, current with the mirror's text. NULL without the document (or
 *  when it does not parse -- said once). */
static const struct Editor_AreasFile*
areas_refresh(
    struct EditorUi* ui,
    struct AreasPanel* self)
{
    struct Editor_TextDoc* doc = areas_doc(ui);

    if( !doc )
    {
        if( self->file_doc )
        {
            Editor_AreasFree(&self->file);
            self->file_doc = NULL;
            self->file_ok = 0;
            self->serial++;
        }
        return NULL;
    }
    if( doc != self->file_doc || doc->revision != self->file_revision )
    {
        struct Editor_AreasParseResult parsed = Editor_AreasParse(&self->file, doc->text, doc->length);
        if( !parsed.ok )
            Editor_UiLog(ui, "editor: %s line %d does not parse; areas are read only until it is fixed",
                EDITOR_AREAS_FILE_PATH, parsed.line);
        self->file_doc = doc;
        self->file_revision = doc->revision;
        self->file_ok = parsed.ok;
        self->serial++;
    }
    return self->file_ok ? &self->file : NULL;
}

static const struct Editor_Area*
areas_selected_area(struct AreasPanel* self)
{
    int line;
    if( !self->file_ok || !self->sel_area[0] )
        return NULL;
    line = Editor_AreasFindArea(&self->file, self->sel_area);
    return line >= 0 ? self->file.lines[line].area : NULL;
}

static const struct Editor_Collection*
areas_selected_coll(struct AreasPanel* self)
{
    int line;
    if( !self->file_ok || !self->sel_coll[0] )
        return NULL;
    line = Editor_AreasFindCollection(&self->file, self->sel_coll);
    return line >= 0 ? self->file.lines[line].collection : NULL;
}

/** Submit one record command; the console says what it was. */
static int
areas_submit(
    struct EditorUi* ui,
    const struct Editor_Cmd* command)
{
    char label[EDITOR_HISTORY_LABEL_MAX];

    Editor_AreasCmdLabel(command, label, sizeof(label));
    if( !Editor_Apply(ui->editor, command) )
    {
        Editor_UiLog(ui, "editor: the server refused '%s'", label);
        return 0;
    }
    Editor_UiLog(ui, "editor: %s (unsaved until Save)", label);
    return 1;
}

/** 0 with a console line when there is no document to edit. */
static const struct Editor_AreasFile*
areas_editable(
    struct EditorUi* ui,
    struct AreasPanel* self)
{
    const struct Editor_AreasFile* file;

    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: no editor session");
        return NULL;
    }
    file = areas_refresh(ui, self);
    if( !file )
        Editor_UiLog(ui, "editor: this server has no areas document (or %s does not parse)", EDITOR_AREAS_FILE_PATH);
    return file;
}

static const char*
areas_name_input(
    struct EditorUi* ui,
    int widget)
{
    const char* text = ToriRSChrome_Text(ui->chrome, widget);
    return text ? text : "";
}

/* ---- areas ------------------------------------------------------------------------ */

/** The Rect tool's rectangle, normalised, any size. 0 when none is latched. */
static int
areas_tool_rect(
    struct EditorUi* ui,
    struct Editor_AreaRect* out,
    int* out_level)
{
    const struct Editor_TileRect* r = &ui->tools.rect;

    if( r->valid <= 0 )
        return 0;
    out->x0 = r->x0 < r->x1 ? r->x0 : r->x1;
    out->x1 = r->x0 < r->x1 ? r->x1 : r->x0;
    out->z0 = r->z0 < r->z1 ? r->z0 : r->z1;
    out->z1 = r->z0 < r->z1 ? r->z1 : r->z0;
    *out_level = r->level >= 0 && r->level <= 3 ? r->level : 0;
    return out->x0 >= 0 && out->z0 >= 0;
}

int
Editor_Areas_NewFromRect(
    struct EditorUi* ui,
    const char* name)
{
    struct AreasPanel* self;
    const struct Editor_AreasFile* file;
    struct Editor_Area area;
    struct Editor_AreaRect rect;
    struct Editor_Cmd command;
    int level;

    assert(ui);
    self = areas_self(ui);
    assert(self);
    if( !areas_tool_rect(ui, &rect, &level) )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_status, "no rectangle: arm the Rect tool and drag one");
        Editor_UiLog(ui, "editor: New from rect needs a rectangle: arm the Rect tool and drag one in the world");
        return 0;
    }
    file = areas_editable(ui, self);
    if( !file )
        return 0;

    memset(&area, 0, sizeof(area));
    area.colour = -1;
    area.level = level;
    area.rects[0] = rect;
    area.rect_count = 1;
    if( name && name[0] )
        snprintf(area.name, sizeof(area.name), "%s", name);
    else
    {
        /* "Area <n>", the first n not taken. */
        for( int n = Editor_AreasAreaCount(file) + 1;; n++ )
        {
            snprintf(area.name, sizeof(area.name), "Area %d", n);
            if( Editor_AreasFindArea(file, area.name) < 0 )
                break;
        }
    }
    if( Editor_AreasFindArea(file, area.name) >= 0 )
    {
        Editor_UiLog(ui, "editor: an area named '%s' already exists", area.name);
        return 0;
    }
    if( !Editor_AreaSetCommand(&command, file, NULL, &area) )
    {
        Editor_UiLog(ui, "editor: '%s' is not a name an area can have (no '|', no edge spaces)", area.name);
        return 0;
    }
    if( !areas_submit(ui, &command) )
        return 0;
    snprintf(self->sel_area, sizeof(self->sel_area), "%s", area.name);
    ToriRSChrome_SetText(ui->chrome, self->input_area_name, area.name);
    return 1;
}

/** SET the selected area to `edited` (named `edited->name`). */
static int
areas_set_selected(
    struct EditorUi* ui,
    struct AreasPanel* self,
    const struct Editor_Area* edited)
{
    const struct Editor_AreasFile* file = areas_editable(ui, self);
    struct Editor_Cmd command;
    char old_name[EDITOR_AREA_NAME_MAX];

    if( !file )
        return 0;
    snprintf(old_name, sizeof(old_name), "%s", self->sel_area);
    if( strcmp(old_name, edited->name) != 0 && Editor_AreasFindArea(file, edited->name) >= 0 )
    {
        Editor_UiLog(ui, "editor: an area named '%s' already exists", edited->name);
        return 0;
    }
    if( !Editor_AreaSetCommand(&command, file, old_name, edited) )
    {
        Editor_UiLog(ui, "editor: nothing to change on '%s' (or the result does not fit a row)", old_name);
        return 0;
    }
    if( !areas_submit(ui, &command) )
        return 0;
    snprintf(self->sel_area, sizeof(self->sel_area), "%s", edited->name);
    return 1;
}

int
Editor_Areas_AddRect(struct EditorUi* ui)
{
    struct AreasPanel* self;
    const struct Editor_Area* area;
    struct Editor_Area edited;
    struct Editor_AreaRect rect;
    int level;

    assert(ui);
    self = areas_self(ui);
    assert(self);
    areas_refresh(ui, self);
    area = areas_selected_area(self);
    if( !area )
    {
        Editor_UiLog(ui, "editor: select an area first");
        return 0;
    }
    if( !areas_tool_rect(ui, &rect, &level) )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_status, "no rectangle: arm the Rect tool and drag one");
        Editor_UiLog(ui, "editor: Add rect needs a rectangle: arm the Rect tool and drag one in the world");
        return 0;
    }
    edited = *area;
    if( !Editor_AreaAddRect(&edited, rect.x0, rect.z0, rect.x1, rect.z1) )
    {
        Editor_UiLog(ui, "editor: '%s' holds %d rectangles at most", area->name, EDITOR_AREA_RECTS_MAX);
        return 0;
    }
    return areas_set_selected(ui, self, &edited);
}

int
Editor_Areas_Rename(
    struct EditorUi* ui,
    const char* new_name)
{
    struct AreasPanel* self;
    const struct Editor_Area* area;
    struct Editor_Area edited;

    assert(ui);
    assert(new_name);
    self = areas_self(ui);
    assert(self);
    areas_refresh(ui, self);
    area = areas_selected_area(self);
    if( !area )
    {
        Editor_UiLog(ui, "editor: select an area first");
        return 0;
    }
    edited = *area;
    snprintf(edited.name, sizeof(edited.name), "%s", new_name);
    return areas_set_selected(ui, self, &edited);
}

int
Editor_Areas_SetLevel(
    struct EditorUi* ui,
    int level)
{
    struct AreasPanel* self;
    const struct Editor_Area* area;
    struct Editor_Area edited;

    assert(ui);
    assert(level >= -1);
    assert(level <= 3);
    self = areas_self(ui);
    assert(self);
    areas_refresh(ui, self);
    area = areas_selected_area(self);
    if( !area )
    {
        Editor_UiLog(ui, "editor: select an area first");
        return 0;
    }
    edited = *area;
    edited.level = level;
    return areas_set_selected(ui, self, &edited);
}

static int
areas_delete_area(
    struct EditorUi* ui,
    struct AreasPanel* self)
{
    const struct Editor_AreasFile* file = areas_editable(ui, self);
    struct Editor_Cmd command;

    if( !file )
        return 0;
    if( !self->sel_area[0] || !Editor_AreaRemoveCommand(&command, file, self->sel_area) )
    {
        Editor_UiLog(ui, "editor: select an area first");
        return 0;
    }
    if( !areas_submit(ui, &command) )
        return 0;
    self->sel_area[0] = '\0';
    return 1;
}

/** Put the App's free camera over absolute tile (x, z): 0 when that tile is
 *  not in the loaded scene (the caller opens its square and retries). */
static int
areas_camera_to(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct App_WorldView* view;
    struct EditorViewCamera pose;
    int scene_x;
    int scene_z;
    int extent;

    if( !app->world || !app->frame_view )
        return 0;
    view = app->frame_view;
    scene_x = (tile_x - app->world->_base_tile_x) * 128 + 64;
    scene_z = (tile_z - app->world->_base_tile_z) * 128 + 64;
    extent = app->world->_scene_size * 128;
    if( scene_x < 0 || scene_z < 0 || scene_x >= extent || scene_z >= extent )
        return 0;
    memset(&pose, 0, sizeof(pose));
    pose.x = tile_x * 128 + 64;
    pose.z = tile_z * 128 + 64;
    pose.level = level;
    pose.yaw = view->world_camera.yaw & 0x7ff;
    pose.pitch = app_world_clamp_pitch(app, view->world_camera.pitch > 0 ? view->world_camera.pitch : 383);
    pose.zoom = 2600;
    EditorView_PoseEye(&pose, app->world->_base_tile_x * 128, app->world->_base_tile_z * 128,
        app_world_height(app, scene_x, scene_z, level), &view->world_camera_pos);
    view->world_camera.pitch = pose.pitch;
    view->camera_unlocked = 1;
    app->need_redraw = 1;
    return 1;
}

int
Editor_Areas_CameraTo(
    struct EditorUi* ui,
    int tile_x,
    int tile_z,
    int level)
{
    assert(ui);
    assert(ui->app);
    return areas_camera_to(ui->app, tile_x, tile_z, level);
}

int
Editor_Areas_GoTo(struct EditorUi* ui)
{
    struct AreasPanel* self;
    const struct Editor_Area* area;
    int x;
    int z;

    assert(ui);
    self = areas_self(ui);
    assert(self);
    areas_refresh(ui, self);
    area = areas_selected_area(self);
    if( !area )
    {
        Editor_UiLog(ui, "editor: select an area first");
        return 0;
    }
    Editor_AreaCentre(area, &x, &z);
    if( !ui->app )
    {
        Editor_UiLog(ui, "editor: Go to %s (%d,%d) needs a world in this window", area->name, x, z);
        return 0;
    }
    if( areas_camera_to(ui->app, x, z, area->level < 0 ? 0 : area->level) )
    {
        Editor_UiLog(ui, "editor: camera at %s (%d,%d)", area->name, x, z);
        return 1;
    }
    /* Not loaded: open its square beside the others, then move. */
    if( !ui->hooks.open_square )
    {
        Editor_UiLog(ui, "editor: %s (%d,%d) is not loaded and this window cannot open squares", area->name, x, z);
        return 0;
    }
    ui->hooks.open_square(ui->hooks_user, x / 64, z / 64, "", 1);
    self->goto_pending = 1;
    self->goto_x = x;
    self->goto_z = z;
    self->goto_level = area->level < 0 ? 0 : area->level;
    self->goto_frames = 0;
    Editor_UiLog(ui, "editor: loading m%d_%d to go to %s", x / 64, z / 64, area->name);
    return 1;
}

/* ---- collections -------------------------------------------------------------------- */

/** "m50_50" or "ported/<lane>/maps/m50_50" of the current square: the picker's
 *  focus, else the latched tile's square. */
static int
areas_current_square(
    struct EditorUi* ui,
    char out[EDITOR_COLLECTION_KEY_MAX])
{
    char key[EDITOR_DOC_KEY_MAX];

    if( Editor_SquarePicker_SelectionKey(ui, key, sizeof(key)) && strlen(key) < EDITOR_COLLECTION_KEY_MAX )
    {
        snprintf(out, EDITOR_COLLECTION_KEY_MAX, "%s", key);
        return 1;
    }
    if( ui->tools.tile_valid )
    {
        snprintf(out, EDITOR_COLLECTION_KEY_MAX, "m%d_%d", ui->tools.tile_x / 64, ui->tools.tile_z / 64);
        return 1;
    }
    return 0;
}

static int
areas_new_collection(
    struct EditorUi* ui,
    struct AreasPanel* self,
    struct Editor_Collection* coll,
    const char* from)
{
    const struct Editor_AreasFile* file = areas_editable(ui, self);
    const char* name = areas_name_input(ui, self->input_coll_name);
    struct Editor_Cmd command;

    if( !file )
        return 0;
    if( coll->square_count == 0 )
    {
        Editor_UiLog(ui, "editor: no %s to collect", from);
        return 0;
    }
    if( !coll->name[0] )
    {
        if( name[0] && Editor_AreasFindCollection(file, name) < 0 )
            snprintf(coll->name, sizeof(coll->name), "%s", name);
        else
            for( int n = Editor_AreasCollectionCount(file) + 1;; n++ )
            {
                snprintf(coll->name, sizeof(coll->name), "Collection %d", n);
                if( Editor_AreasFindCollection(file, coll->name) < 0 )
                    break;
            }
    }
    if( !Editor_CollectionSetCommand(&command, file, NULL, coll) )
    {
        Editor_UiLog(ui, "editor: '%s' is taken or not a name a collection can have", coll->name);
        return 0;
    }
    if( !areas_submit(ui, &command) )
        return 0;
    snprintf(self->sel_coll, sizeof(self->sel_coll), "%s", coll->name);
    ToriRSChrome_SetText(ui->chrome, self->input_coll_name, coll->name);
    return 1;
}

int
Editor_Areas_CollectionFromOpen(
    struct EditorUi* ui,
    const char* name)
{
    struct AreasPanel* self;
    struct Editor_Collection* coll;
    int result;
    int skipped = 0;

    assert(ui);
    self = areas_self(ui);
    assert(self);
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: no editor session");
        return 0;
    }
    coll = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*coll));
    assert(coll);
    if( name )
        snprintf(coll->name, sizeof(coll->name), "%s", name);
    for( int i = 0; i < ui->editor->doc.square_count; i++ )
    {
        const struct Editor_Square* square = &ui->editor->doc.squares[i];
        const char* storage = ui->editor->doc.square_storage[i];
        char key[EDITOR_DOC_KEY_MAX + EDITOR_DOC_STORAGE_MAX];
        if( !square->loaded )
            continue;
        snprintf(key, sizeof(key), "%s%sm%d_%d", storage, storage[0] ? "/" : "", square->map_x, square->map_z);
        if( !Editor_CollectionAdd(coll, key) )
            skipped++;
    }
    if( skipped )
        Editor_UiLog(ui, "editor: %d open square%s did not fit the collection", skipped, skipped == 1 ? "" : "s");
    result = areas_new_collection(ui, self, coll, "open squares");
    ToriMem_Free(ToriMem_Editor(), coll);
    return result;
}

int
Editor_Areas_CollectionFromPicker(
    struct EditorUi* ui,
    const char* name)
{
    struct AreasPanel* self;
    const struct Editor_Picker* picker;
    struct Editor_Collection* coll;
    int result;
    int skipped = 0;

    assert(ui);
    self = areas_self(ui);
    assert(self);
    picker = edp_squares_picker(ui);
    if( !picker )
    {
        Editor_UiLog(ui, "editor: the square picker has no atlas yet");
        return 0;
    }
    coll = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*coll));
    assert(coll);
    if( name )
        snprintf(coll->name, sizeof(coll->name), "%s", name);
    for( int i = 0; i < picker->square_count; i++ )
    {
        char key[EDITOR_DOC_KEY_MAX];
        if( !picker->selected[i] )
            continue;
        Editor_PickerSquareKey(picker, i, key);
        if( !Editor_CollectionAdd(coll, key) )
            skipped++;
    }
    if( skipped )
        Editor_UiLog(ui, "editor: %d selected square%s did not fit the collection", skipped, skipped == 1 ? "" : "s");
    result = areas_new_collection(ui, self, coll, "selected squares in the picker (shift-click some)");
    ToriMem_Free(ToriMem_Editor(), coll);
    return result;
}

/** SET the selected collection to `edited`. */
static int
areas_set_coll(
    struct EditorUi* ui,
    struct AreasPanel* self,
    const struct Editor_Collection* edited)
{
    const struct Editor_AreasFile* file = areas_editable(ui, self);
    struct Editor_Cmd command;
    char old_name[EDITOR_AREA_NAME_MAX];

    if( !file )
        return 0;
    snprintf(old_name, sizeof(old_name), "%s", self->sel_coll);
    if( strcmp(old_name, edited->name) != 0 && Editor_AreasFindCollection(file, edited->name) >= 0 )
    {
        Editor_UiLog(ui, "editor: a collection named '%s' already exists", edited->name);
        return 0;
    }
    if( !Editor_CollectionSetCommand(&command, file, old_name, edited) )
    {
        Editor_UiLog(ui, "editor: nothing to change on '%s' (or the result does not fit a row)", old_name);
        return 0;
    }
    if( !areas_submit(ui, &command) )
        return 0;
    snprintf(self->sel_coll, sizeof(self->sel_coll), "%s", edited->name);
    return 1;
}

/** Add (`add` 1) or remove the current square from the selected collection. */
static int
areas_coll_current(
    struct EditorUi* ui,
    struct AreasPanel* self,
    int add)
{
    const struct Editor_Collection* coll;
    struct Editor_Collection* edited;
    char key[EDITOR_COLLECTION_KEY_MAX];
    int result = 0;

    areas_refresh(ui, self);
    coll = areas_selected_coll(self);
    if( !coll )
    {
        Editor_UiLog(ui, "editor: select a collection first");
        return 0;
    }
    if( !areas_current_square(ui, key) )
    {
        Editor_UiLog(ui, "editor: no current square: click one in the Squares picker or latch a tile");
        return 0;
    }
    edited = ToriMem_Alloc(ToriMem_Editor(), sizeof(*edited));
    assert(edited);
    *edited = *coll;
    if( add ? !Editor_CollectionAdd(edited, key) : !Editor_CollectionRemove(edited, key) )
        Editor_UiLog(ui, add ? "editor: %s is already in %s, or the collection is full"
                             : "editor: %s is not in %s",
            key, coll->name);
    else
        result = areas_set_coll(ui, self, edited);
    ToriMem_Free(ToriMem_Editor(), edited);
    return result;
}

int
Editor_Areas_CollectionAddCurrent(
    struct EditorUi* ui,
    int add)
{
    struct AreasPanel* self;

    assert(ui);
    self = areas_self(ui);
    assert(self);
    return areas_coll_current(ui, self, add);
}

static int
areas_rename_coll(
    struct EditorUi* ui,
    struct AreasPanel* self,
    const char* new_name)
{
    const struct Editor_Collection* coll;
    struct Editor_Collection* edited;
    int result;

    areas_refresh(ui, self);
    coll = areas_selected_coll(self);
    if( !coll )
    {
        Editor_UiLog(ui, "editor: select a collection first");
        return 0;
    }
    edited = ToriMem_Alloc(ToriMem_Editor(), sizeof(*edited));
    assert(edited);
    *edited = *coll;
    snprintf(edited->name, sizeof(edited->name), "%s", new_name);
    result = areas_set_coll(ui, self, edited);
    ToriMem_Free(ToriMem_Editor(), edited);
    return result;
}

static int
areas_delete_coll(
    struct EditorUi* ui,
    struct AreasPanel* self)
{
    const struct Editor_AreasFile* file = areas_editable(ui, self);
    struct Editor_Cmd command;

    if( !file )
        return 0;
    if( !self->sel_coll[0] || !Editor_CollectionRemoveCommand(&command, file, self->sel_coll) )
    {
        Editor_UiLog(ui, "editor: select a collection first");
        return 0;
    }
    if( !areas_submit(ui, &command) )
        return 0;
    self->sel_coll[0] = '\0';
    return 1;
}

/** Queue every square of `coll` to open: each as a document in this mirror
 *  now, and in the world one load at a time (the window holds one pending
 *  open). */
static int
areas_open_all(
    struct EditorUi* ui,
    struct AreasPanel* self,
    const struct Editor_Collection* coll)
{
    int opened = 0;

    self->open_count = 0;
    self->open_next = 0;
    for( int i = 0; i < coll->square_count; i++ )
    {
        if( edp_ensure_doc(ui, EDITOR_DOC_SQUARE, coll->squares[i]) )
            opened++;
        else
            Editor_UiLog(ui, "editor: could not open %s", coll->squares[i]);
        snprintf(self->open_queue[self->open_count++], EDITOR_COLLECTION_KEY_MAX, "%s", coll->squares[i]);
    }
    Editor_UiLog(ui, "editor: opened %d of %d squares of %s%s", opened, coll->square_count, coll->name,
        coll->square_count > AREAS_OPEN_CONFIRM ? "; the world shows 16 at once" : "");
    return opened;
}

int
Editor_Areas_OpenAll(
    struct EditorUi* ui,
    int confirmed)
{
    struct AreasPanel* self;
    const struct Editor_Collection* coll;
    char line[TORIRS_CHROME_LABEL_MAX];

    assert(ui);
    self = areas_self(ui);
    assert(self);
    areas_refresh(ui, self);
    coll = areas_selected_coll(self);
    if( !coll )
    {
        Editor_UiLog(ui, "editor: select a collection first");
        return 0;
    }
    if( coll->square_count > AREAS_OPEN_CONFIRM && !confirmed )
    {
        snprintf(self->confirm_coll, sizeof(self->confirm_coll), "%s", coll->name);
        snprintf(line, sizeof(line), "Open all %d squares of %s?", coll->square_count, coll->name);
        ToriRSChrome_SetText(ui->chrome, self->confirm_row, line);
        ToriRSChrome_PanelSetVisible(ui->chrome, self->confirm_panel, 1);
        return -1;
    }
    return areas_open_all(ui, self, coll);
}

/** Hand the window the next queued square once the last one is in. */
static void
areas_pump_open(
    struct EditorUi* ui,
    struct AreasPanel* self)
{
    while( self->open_next < self->open_count )
    {
        char storage[EDITOR_DOC_STORAGE_MAX];
        char bare[EDITOR_DOC_KEY_MAX];
        int map_x = 0;
        int map_z = 0;
        const char* key = self->open_queue[self->open_next];

        if( ui->app )
        {
            struct App* app = ui->app;
            if( app->world_load_inflight || app->editor_panel.sq_open_pending )
                return;
        }
        self->open_next++;
        if( !Editor_DocSplitSquareKey(key, storage, bare) || !Editor_SquareKeyParse(bare, &map_x, &map_z) )
            continue;
        if( !ui->hooks.open_square )
        {
            Editor_UiLog(ui, "editor: this window cannot open squares in the world");
            self->open_next = self->open_count;
            return;
        }
        ui->hooks.open_square(ui->hooks_user, map_x, map_z, storage, 1);
        /* One per frame with a world (each is a load); every one at once
         * without (the hook only records). */
        if( ui->app )
            return;
    }
}

/* ---- the picker's collection filter -------------------------------------------------- */

const struct Editor_AreasFile*
Editor_Areas_File(
    struct EditorUi* ui,
    uint32_t* out_serial)
{
    struct AreasPanel* self;
    const struct Editor_AreasFile* file;

    assert(ui);
    self = areas_self(ui);
    if( !self )
        return NULL;
    file = areas_refresh(ui, self);
    if( out_serial )
        *out_serial = self->serial;
    return file;
}

/* ---- the world overlay ------------------------------------------------------------------ */

int
Editor_Areas_Overlay(
    struct EditorUi* ui,
    struct Editor_AreaOverlay* out,
    int max)
{
    struct AreasPanel* self;
    const struct Editor_AreasFile* file;
    int n = 0;
    int nth = -1;
    int hide_others;

    assert(ui);
    assert(out || max == 0);
    self = areas_self(ui);
    if( !self || !ToriRSChrome_Checked(ui->chrome, self->check_outlines) )
        return 0;
    file = areas_refresh(ui, self);
    if( !file )
        return 0;
    hide_others = ToriRSChrome_Checked(ui->chrome, self->check_hide_others) && self->sel_area[0];
    for( int i = 0; i < file->line_count && n < max; i++ )
    {
        const struct Editor_Area* area = file->lines[i].area;
        int selected;
        uint32_t rgb;
        if( !area )
            continue;
        nth++;
        selected = strcmp(area->name, self->sel_area) == 0;
        if( hide_others && !selected )
            continue;
        rgb = Editor_AreaPaletteRgb(Editor_AreaColourIndex(area, nth));
        for( int r = 0; r < area->rect_count && n < max; r++ )
        {
            struct Editor_AreaOverlay* o = &out[n++];
            o->x0 = area->rects[r].x0;
            o->z0 = area->rects[r].z0;
            o->x1 = area->rects[r].x1;
            o->z1 = area->rects[r].z1;
            o->level = area->level;
            o->rgb = rgb;
            o->selected = selected;
            o->first = r == 0;
            snprintf(o->label, sizeof(o->label), "%s", r == 0 ? area->name : "");
        }
    }
    return n;
}

/* ---- the panel ------------------------------------------------------------------------- */

static void
areas_rebuild_rows(
    struct EditorUi* ui,
    struct AreasPanel* self)
{
    const struct Editor_AreasFile* file = self->file_ok ? &self->file : NULL;
    int sel_area = -1;
    int sel_coll = -1;

    self->area_row_count = 0;
    self->coll_row_count = 0;
    for( int i = 0; file && i < file->line_count; i++ )
    {
        const struct Editor_AreasLine* line = &file->lines[i];
        if( line->area && self->area_row_count < AREAS_ROWS_MAX )
        {
            char level[8];
            if( line->area->level < 0 )
                snprintf(level, sizeof(level), "all");
            else
                snprintf(level, sizeof(level), "L%d", line->area->level);
            if( strcmp(line->area->name, self->sel_area) == 0 )
                sel_area = self->area_row_count;
            snprintf(self->area_rows[self->area_row_count++], AREAS_ROW_TEXT, "%s  %s  %d rect%s", line->area->name,
                level, line->area->rect_count, line->area->rect_count == 1 ? "" : "s");
        }
        else if( line->collection && self->coll_row_count < AREAS_ROWS_MAX )
        {
            if( strcmp(line->collection->name, self->sel_coll) == 0 )
                sel_coll = self->coll_row_count;
            snprintf(self->coll_rows[self->coll_row_count++], AREAS_ROW_TEXT, "%s  %d square%s",
                line->collection->name, line->collection->square_count,
                line->collection->square_count == 1 ? "" : "s");
        }
    }
    /* A selection whose record went (deleted, renamed elsewhere) clears. */
    if( sel_area < 0 )
        self->sel_area[0] = '\0';
    if( sel_coll < 0 )
        self->sel_coll[0] = '\0';
    ToriRSChrome_VListSetCount(ui->chrome, self->list_areas, self->area_row_count);
    ToriRSChrome_VListSetCount(ui->chrome, self->list_colls, self->coll_row_count);
    ToriRSChrome_VListSetSelected(ui->chrome, self->list_areas, sel_area);
    ToriRSChrome_VListSetSelected(ui->chrome, self->list_colls, sel_coll);
    {
        char status[TORIRS_CHROME_LABEL_MAX];
        if( !ui->editor )
            snprintf(status, sizeof(status), "no editor session");
        else if( !self->file_doc )
            snprintf(status, sizeof(status), "this server has no areas document");
        else if( !file )
            snprintf(status, sizeof(status), "%s does not parse: read only", EDITOR_AREAS_FILE_PATH);
        else
            snprintf(status, sizeof(status), "%d area%s, %d collection%s%s", self->area_row_count,
                self->area_row_count == 1 ? "" : "s", self->coll_row_count, self->coll_row_count == 1 ? "" : "s",
                self->file_doc->dirty ? "  (unsaved)" : "");
        ToriRSChrome_SetText(ui->chrome, self->row_status, status);
    }
    self->built_serial = self->serial;
    self->built = 1;
}

static void
areas_fill_rows(
    struct EditorUi* ui,
    int list,
    char (*rows)[AREAS_ROW_TEXT],
    int count)
{
    int first;
    int shown;

    if( !ToriRSChrome_VListWindow(ui->chrome, list, &first, &shown) )
        return;
    for( int row = first; row < first + shown && row < count; row++ )
        ToriRSChrome_VListSetRow(ui->chrome, list, row, rows[row]);
}

static void
areas_status_rect(
    struct EditorUi* ui,
    struct AreasPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    struct Editor_AreaRect rect;
    int level;

    if( areas_tool_rect(ui, &rect, &level) )
        snprintf(line, sizeof(line), "rect %d,%d .. %d,%d  L%d  (%dx%d)", rect.x0, rect.z0, rect.x1, rect.z1, level,
            rect.x1 - rect.x0 + 1, rect.z1 - rect.z0 + 1);
    else
        snprintf(line, sizeof(line), "no rectangle: arm the Rect tool and drag one");
    if( strcmp(line, self->shown_rect) != 0 )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_rect, line);
        snprintf(self->shown_rect, sizeof(self->shown_rect), "%s", line);
    }
    {
        char key[EDITOR_COLLECTION_KEY_MAX];
        if( areas_current_square(ui, key) )
            snprintf(line, sizeof(line), "current square %s", key);
        else
            snprintf(line, sizeof(line), "no current square: click one in Squares");
        ToriRSChrome_SetText(ui->chrome, self->row_current, line);
    }
}

static int
areas_select_area(
    struct EditorUi* ui,
    struct AreasPanel* self,
    const char* name)
{
    const struct Editor_Area* area;

    snprintf(self->sel_area, sizeof(self->sel_area), "%s", name);
    area = areas_selected_area(self);
    if( !area )
    {
        self->sel_area[0] = '\0';
        return 0;
    }
    ToriRSChrome_SetText(ui->chrome, self->input_area_name, area->name);
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_level, area->level + 1);
    self->built = 0;
    return 1;
}

static int
areas_select_coll(
    struct EditorUi* ui,
    struct AreasPanel* self,
    const char* name)
{
    snprintf(self->sel_coll, sizeof(self->sel_coll), "%s", name);
    if( !areas_selected_coll(self) )
    {
        self->sel_coll[0] = '\0';
        return 0;
    }
    ToriRSChrome_SetText(ui->chrome, self->input_coll_name, name);
    self->built = 0;
    return 1;
}

/** The panel docked for a photograph: over the column between the Terrain
 *  panel and the Squares picker when both are docked there, else floating at
 *  the world's top-left; marked as placed so the layout keeps it. */
static void
areas_env_dock(
    struct EditorUi* ui,
    struct AreasPanel* self)
{
    struct EditorUiPanel const* terrain = Editor_UiFindPanel(ui, "terrain");
    struct EditorUiPanel const* squares = Editor_UiFindPanel(ui, "squares");
    struct ToriRSChromePanel const* t = terrain ? &ui->chrome->panels[terrain->chrome_panel] : NULL;
    struct ToriRSChromePanel const* q = squares ? &ui->chrome->panels[squares->chrome_panel] : NULL;
    int x = ui->world.x + edp_px(ui, 8);
    int y = ui->world.y + edp_px(ui, 8);
    int w = ui->dock_w > 0 ? ui->dock_w : edp_px(ui, EDP_COL_W + 60);
    int h = ui->world.h / 2;

    if( t && q && t->visible && q->visible && ui->dock_w > 0 && t->fixed_h > 0
        && q->y > t->y + t->fixed_h + edp_px(ui, 160) )
    {
        x = ui->dock_x;
        y = t->y + t->fixed_h;
        h = (self->env_tall ? ui->chrome->surface_h : q->y) - y;
    }
    ToriRSChrome_PanelSetFixedWidth(ui->chrome, self->panel, w);
    ToriRSChrome_PanelSetFixedHeight(ui->chrome, self->panel, h > edp_px(ui, 160) ? h : edp_px(ui, 160));
    ToriRSChrome_PanelMove(ui->chrome, self->panel, x, y);
    ui->chrome->panels[self->panel].user_placed = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    self->built = 0;
}

/** TORIRS_EDITOR_AREAS, once the document is in (see the header). */
static void
areas_env(
    struct EditorUi* ui,
    struct AreasPanel* self)
{
    const char* env = getenv("TORIRS_EDITOR_AREAS");
    char opts[256];
    char* next;
    int want_goto = 0;

    self->env_done = 1;
    if( !env || !env[0] )
        return;
    snprintf(opts, sizeof(opts), "%s", env);
    /* Comma-separated; split by hand (no strtok_r on every platform). */
    for( char* tok = opts; tok; tok = next )
    {
        next = strchr(tok, ',');
        if( next )
            *next++ = '\0';
        if( strcmp(tok, "dock") == 0 )
            self->env_dock = 1; /* placed once the layout ran (tick) */
        else if( strcmp(tok, "tall") == 0 )
            self->env_tall = 1;
        else if( strcmp(tok, "tab=collections") == 0 )
            ToriRSChrome_PanelSetActiveTab(ui->chrome, self->panel, AREAS_TAB_COLLECTIONS);
        else if( strcmp(tok, "tab=areas") == 0 )
            ToriRSChrome_PanelSetActiveTab(ui->chrome, self->panel, AREAS_TAB_AREAS);
        else if( strncmp(tok, "select=", 7) == 0 )
        {
            if( !areas_select_area(ui, self, tok + 7) )
                Editor_UiLog(ui, "editor: TORIRS_EDITOR_AREAS: no area named '%s'", tok + 7);
        }
        else if( strncmp(tok, "collection=", 11) == 0 )
        {
            if( !areas_select_coll(ui, self, tok + 11) )
                Editor_UiLog(ui, "editor: TORIRS_EDITOR_AREAS: no collection named '%s'", tok + 11);
            else
                Editor_SquarePicker_SetCollection(ui, tok + 11);
        }
        else if( strcmp(tok, "goto") == 0 )
            want_goto = 1;
    }
    if( want_goto )
        (void)Editor_Areas_GoTo(ui);
    Editor_UiLog(ui, "editor: TORIRS_EDITOR_AREAS=%s", env);
}

static void
areas_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct AreasPanel* self = opaque;

    areas_refresh(ui, self);
    if( !self->env_done && self->file_doc && (!ui->app || ((struct App*)ui->app)->world) )
        areas_env(ui, self);
    /* A few frames after the layout, so the column has its final boxes. */
    if( self->env_dock && ui->laid_out && ++self->env_dock > 4 )
    {
        areas_env_dock(ui, self);
        self->env_dock = 0;
    }
    areas_pump_open(ui, self);
    if( self->goto_pending && ui->app )
    {
        if( areas_camera_to(ui->app, self->goto_x, self->goto_z, self->goto_level) )
            self->goto_pending = 0;
        else if( ++self->goto_frames > AREAS_GOTO_WAIT_FRAMES )
        {
            Editor_UiLog(ui, "editor: Go to gave up: %d,%d never loaded", self->goto_x, self->goto_z);
            self->goto_pending = 0;
        }
    }
    if( !ui->chrome->panels[self->panel].visible )
        return;
    if( !self->built || self->built_serial != self->serial )
        areas_rebuild_rows(ui, self);
    areas_status_rect(ui, self);
    areas_fill_rows(ui, self->list_areas, self->area_rows, self->area_row_count);
    areas_fill_rows(ui, self->list_colls, self->coll_rows, self->coll_row_count);
}

/** The nth record of a kind's name, from a list row. */
static const char*
areas_row_name(
    struct AreasPanel* self,
    int is_area,
    int row)
{
    int line;
    if( !self->file_ok || row < 0 )
        return NULL;
    line = is_area ? Editor_AreasNthArea(&self->file, row) : Editor_AreasNthCollection(&self->file, row);
    if( line < 0 )
        return NULL;
    return is_area ? self->file.lines[line].area->name : self->file.lines[line].collection->name;
}

static int
areas_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct AreasPanel* self = opaque;

    if( widget == self->tabs || widget == self->input_area_name || widget == self->input_coll_name
        || widget == self->dropdown_level || widget == self->check_outlines || widget == self->check_hide_others )
        return 1;
    if( widget == self->list_areas || widget == self->list_colls )
    {
        int const is_area = widget == self->list_areas;
        const char* name;
        areas_refresh(ui, self);
        name = areas_row_name(self, is_area, ToriRSChrome_VListSelected(ui->chrome, widget));
        if( name )
        {
            char copy[EDITOR_AREA_NAME_MAX];
            snprintf(copy, sizeof(copy), "%s", name);
            if( is_area )
                areas_select_area(ui, self, copy);
            else
                areas_select_coll(ui, self, copy);
        }
        return 1;
    }
    if( widget == self->button_new_rect )
        (void)Editor_Areas_NewFromRect(ui, areas_name_input(ui, self->input_area_name));
    else if( widget == self->button_add_rect )
        (void)Editor_Areas_AddRect(ui);
    else if( widget == self->button_rename_area )
        (void)Editor_Areas_Rename(ui, areas_name_input(ui, self->input_area_name));
    else if( widget == self->button_set_level )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_level);
        (void)Editor_Areas_SetLevel(ui, row >= 1 && row <= 4 ? row - 1 : -1);
    }
    else if( widget == self->button_delete_area )
        (void)areas_delete_area(ui, self);
    else if( widget == self->button_goto )
        (void)Editor_Areas_GoTo(ui);
    else if( widget == self->button_new_open )
        (void)Editor_Areas_CollectionFromOpen(ui, NULL);
    else if( widget == self->button_new_picker )
        (void)Editor_Areas_CollectionFromPicker(ui, NULL);
    else if( widget == self->button_add_current )
        (void)areas_coll_current(ui, self, 1);
    else if( widget == self->button_remove_current )
        (void)areas_coll_current(ui, self, 0);
    else if( widget == self->button_rename_coll )
        (void)areas_rename_coll(ui, self, areas_name_input(ui, self->input_coll_name));
    else if( widget == self->button_delete_coll )
        (void)areas_delete_coll(ui, self);
    else if( widget == self->button_open_all )
        (void)Editor_Areas_OpenAll(ui, 0);
    else if( widget == self->confirm_yes || widget == self->confirm_no )
    {
        ToriRSChrome_PanelSetVisible(ui->chrome, self->confirm_panel, 0);
        if( widget == self->confirm_yes && strcmp(self->confirm_coll, self->sel_coll) == 0 )
            (void)Editor_Areas_OpenAll(ui, 1);
    }
    else
        return 0;
    return 1;
}

static void
areas_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct AreasPanel* self = opaque;
    if( !visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->confirm_panel, 0);
    self->built = 0;
}

static void
areas_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct AreasPanel* self = opaque;
    (void)ui;
    Editor_AreasFree(&self->file);
    ToriMem_Free(ToriMem_Editor(), self);
}

/* ---- entry points ----------------------------------------------------------------------- */

int
Editor_Areas_Select(
    struct EditorUi* ui,
    const char* name)
{
    struct AreasPanel* self;

    assert(ui);
    assert(name);
    self = areas_self(ui);
    assert(self);
    areas_refresh(ui, self);
    return areas_select_area(ui, self, name);
}

const char*
Editor_Areas_Selected(struct EditorUi* ui)
{
    struct AreasPanel* self;

    assert(ui);
    self = areas_self(ui);
    assert(self);
    return self->sel_area;
}

int
Editor_Areas_SelectCollection(
    struct EditorUi* ui,
    const char* name)
{
    struct AreasPanel* self;

    assert(ui);
    assert(name);
    self = areas_self(ui);
    assert(self);
    areas_refresh(ui, self);
    return areas_select_coll(ui, self, name);
}

const char*
Editor_Areas_SelectedCollection(struct EditorUi* ui)
{
    struct AreasPanel* self;

    assert(ui);
    self = areas_self(ui);
    assert(self);
    return self->sel_coll;
}

void
Editor_Areas_Show(struct EditorUi* ui)
{
    struct AreasPanel* self;

    assert(ui);
    self = areas_self(ui);
    assert(self);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    self->built = 0;
}

void
Editor_Areas_Register(struct EditorUi* ui)
{
    struct AreasPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    Editor_AreasInit(&self->file);

    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8),
        edp_px(ui, EDP_TOP_Y), edp_px(ui, EDP_COL_W + 60), "Areas");
    assert(self->panel >= 0);
    {
        static const char* const tabs[] = { "Areas", "Collections" };
        self->tabs = ToriRSChrome_Tabs(chrome, self->panel, tabs, 2, AREAS_TAB_AREAS);
    }

    ToriRSChrome_PanelBeginTab(chrome, self->panel, AREAS_TAB_AREAS);
    self->list_areas = ToriRSChrome_VList(chrome, self->panel, "", AREAS_LIST_ROWS, 0);
    assert(self->list_areas >= 0);
    ToriRSChrome_SetTooltip(chrome, self->list_areas, "name, level, rectangles; click to select");
    self->row_rect = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_area_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_area_name, "for New from rect and Rename; no '|'");
    self->button_new_rect = ToriRSChrome_Button(chrome, self->panel, "New from rect");
    ToriRSChrome_SetTooltip(chrome, self->button_new_rect, "Name the Rect tool's rectangle (on its level)");
    self->button_add_rect = ToriRSChrome_Button(chrome, self->panel, "Add rect");
    ToriRSChrome_SetTooltip(chrome, self->button_add_rect, "Add the Rect tool's rectangle to the selected area");
    self->button_rename_area = ToriRSChrome_Button(chrome, self->panel, "Rename");
    self->dropdown_level = ToriRSChrome_Dropdown(chrome, self->panel, "Level", k_level_rows, 5, 1);
    self->button_set_level = ToriRSChrome_Button(chrome, self->panel, "Set level");
    self->button_delete_area = ToriRSChrome_Button(chrome, self->panel, "Delete");
    self->button_goto = ToriRSChrome_Button(chrome, self->panel, "Go to");
    ToriRSChrome_SetTooltip(chrome, self->button_goto, "Move the camera to the area's centre");
    self->check_outlines = ToriRSChrome_Checkbox(chrome, self->panel, "Show outlines", 1);
    self->check_hide_others = ToriRSChrome_Checkbox(chrome, self->panel, "Hide others", 0);

    ToriRSChrome_PanelBeginTab(chrome, self->panel, AREAS_TAB_COLLECTIONS);
    self->list_colls = ToriRSChrome_VList(chrome, self->panel, "", AREAS_LIST_ROWS, 0);
    assert(self->list_colls >= 0);
    ToriRSChrome_SetTooltip(chrome, self->list_colls, "name, squares; click to select");
    self->row_current = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_coll_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    self->button_new_open = ToriRSChrome_Button(chrome, self->panel, "From open");
    ToriRSChrome_SetTooltip(chrome, self->button_new_open, "New collection of the squares open in this window");
    self->button_new_picker = ToriRSChrome_Button(chrome, self->panel, "From picker");
    ToriRSChrome_SetTooltip(chrome, self->button_new_picker, "New collection of the squares shift-clicked in Squares");
    self->button_add_current = ToriRSChrome_Button(chrome, self->panel, "Add current");
    ToriRSChrome_SetTooltip(chrome, self->button_add_current, "Add the current square (picker focus, else the latched tile)");
    self->button_remove_current = ToriRSChrome_Button(chrome, self->panel, "Remove current");
    self->button_rename_coll = ToriRSChrome_Button(chrome, self->panel, "Rename");
    self->button_delete_coll = ToriRSChrome_Button(chrome, self->panel, "Delete");
    self->button_open_all = ToriRSChrome_Button(chrome, self->panel, "Open all");
    ToriRSChrome_SetTooltip(chrome, self->button_open_all, "Each as a document, and into the world");

    ToriRSChrome_PanelBeginTab(chrome, self->panel, -1);
    self->row_status = ToriRSChrome_Label(chrome, self->panel, "");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    self->confirm_panel = ToriRSChrome_ModalAdd(chrome, "Open all squares", edp_px(ui, 260));
    assert(self->confirm_panel >= 0);
    self->confirm_row = ToriRSChrome_Label(chrome, self->confirm_panel, "");
    ToriRSChrome_Label(chrome, self->confirm_panel, "Each opens as a document; the world shows 16 at once.");
    self->confirm_yes = ToriRSChrome_Button(chrome, self->confirm_panel, "Open them");
    self->confirm_no = ToriRSChrome_Button(chrome, self->confirm_panel, "Cancel");
    ToriRSChrome_PanelSetVisible(chrome, self->confirm_panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "areas";
    desc.title = "Areas";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = areas_tick;
    desc.activate = areas_activate;
    desc.set_visible = areas_set_visible;
    desc.destroy = areas_destroy;
    Editor_UiAddPanel(ui, &desc);
}

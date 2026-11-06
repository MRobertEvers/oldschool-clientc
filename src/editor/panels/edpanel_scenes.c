/*
 * Scenes (workbench 6.5, design #lanes "Scenes"; module "scenes", View >
 * Scenes): named snapshots of the session's state -- the open squares, the
 * World's camera, the pinned level and Above, the lane, the var preview and
 * the play account -- so a builder can leave an encounter half-tuned, fix a
 * door across the map and come back to exactly that. The SCENES document
 * (editor/editor_scenes.h, editor/scenes.txt) is shared by the Client like
 * the AREAS one: every window mirrors it from the start (editor.c), so a
 * scene saved in one window is in every window's list after a pump.
 *
 *   the list   "editor/scenes.txt | 5 scenes", a Find box (R3) and a VLIST of
 *              "name | m50_50 m50_51 | L1 above | working"; a click picks a
 *              row (its name goes to the Name box), a second activation of
 *              the picked row -- Enter, a double-click -- enters it;
 *   Holds      chips: squares, camera, level, var preview, account, lane --
 *              what Save scene and Update capture (lit: kept);
 *   Save scene (Ctrl+Shift+K) the state as a new scene named in Name (or
 *              "scene-<n>"); Update rewrites the picked one from the state,
 *              keeping the parts the chips leave out; Rename takes Name;
 *              Delete. Each is ONE undo step (Editor_ScenePlan*, one
 *              stroke) and the document is saved at once (a scene is
 *              session state, not content: it survives a restart without
 *              Save all); the status bar confirms;
 *   Enter      through the save guard (EDITOR_UI_GUARD_ENTER_SCENE: the
 *              squares replace the open set): opens the squares, pins the
 *              level and Above, publishes STATE_VAR_PREVIEW from its preview
 *              list, switches the lane when it names another (staged for
 *              the next boot: Editor_Lanes_Switch), teleports the account
 *              through the live link when the lane is up (Editor_Live_
 *              Teleport, the Live panel's ::tele), and puts the World's
 *              camera at the scene's orbit pose once its squares have loaded
 *              (app_services.world_camera_set). One status sentence says
 *              what it did and what it could not.
 *
 * The command palette (workbench 6.8) lists scenes under their own group
 * through Editor_ScenesList. Nothing is parsed on a quiet frame: the list is
 * rebuilt when the document's revision or the filter moves.
 */

#include "edpanel_chips.h"
#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/builder/builder_shell.h"
#include "editor/editor.h"
#include "editor/editor_scenes.h"
#include "editor/editor_view.h"
#include "editor/live/live_panel.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENES_LIST_ROWS 8
#define SCENES_SHOWN_MAX EDITOR_SCENES_MAX
/** How long Enter waits for the squares before it says the camera could not
 *  be put, on the chrome's clock. */
#define SCENES_CAMERA_WAIT_MS 15000

/** The Holds chips, in the design's order, and the segments each keeps. */
enum
{
    SCENES_HOLD_SQUARES = 0,
    SCENES_HOLD_CAMERA,
    SCENES_HOLD_LEVEL,
    SCENES_HOLD_PREVIEW,
    SCENES_HOLD_ACCOUNT,
    SCENES_HOLD_LANE,
    SCENES_HOLDS,
};

static const char* const k_hold_labels[SCENES_HOLDS] = {
    "squares", "camera", "level", "var preview", "account", "lane",
};
static const unsigned k_hold_segments[SCENES_HOLDS] = {
    EDITOR_SCENE_SEG_SQUARES, EDITOR_SCENE_SEG_CAMERA,  EDITOR_SCENE_SEG_LEVEL,
    EDITOR_SCENE_SEG_PREVIEW, EDITOR_SCENE_SEG_ACCOUNT, EDITOR_SCENE_SEG_LANE,
};

struct ScenesPanel
{
    int panel;
    int row_count;
    int input_find;
    int list;
    int input_name;
    struct Edp_ChipStrip holds;
    int button_save;
    int button_enter;
    int button_update;
    int button_rename;
    int button_delete;
    int row_detail;

    /** Lit Holds chips. */
    int hold[SCENES_HOLDS];

    struct Editor_ScenesFile file;
    int file_ok;
    const struct Editor_TextDoc* file_doc;
    uint32_t file_revision;
    int file_valid;
    char shown_filter[TORIRS_CHROME_INPUT_MAX];
    /** Rows on screen: the nth scene of `file`. */
    int rows[SCENES_SHOWN_MAX];
    int listed;
    int total;
    /** The row the last activation picked (a second one enters it). */
    int picked_row;
    char picked_name[EDITOR_SCENE_NAME_MAX];

    /** STATE_VAR_PREVIEW as last heard or published. */
    int32_t preview[TORIRSMAPED_STATE_VALUES_MAX];
    int preview_count;

    /** Enter's Above, set again for a few ticks: a level pinned behind the
     *  menu bar's back makes it follow the cap from the old pin first
     *  (edp_level_follow_vis), which would undo an Above set with it. */
    int above_pending;
    int above_want;
    /** Enter's camera, put once the squares have loaded. */
    int camera_pending;
    struct EditorViewCamera camera;
    struct Editor_Scene camera_scene;
    int64_t camera_since_ms;

    uint32_t chord_serial;
};

static struct ScenesPanel*
scenes_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "scenes");
}

static int
scenes_contains(
    const char* text,
    const char* filter)
{
    size_t const n = strlen(filter);

    if( n == 0 )
        return 1;
    for( const char* at = text; *at; at++ )
    {
        size_t i = 0;
        while( i < n && at[i] )
        {
            char a = at[i];
            char b = filter[i];
            if( a >= 'A' && a <= 'Z' )
                a = (char)(a - 'A' + 'a');
            if( b >= 'A' && b <= 'Z' )
                b = (char)(b - 'A' + 'a');
            if( a != b )
                break;
            i++;
        }
        if( i == n )
            return 1;
    }
    return 0;
}

/* ---- the document --------------------------------------------------------------- */

static struct Editor_TextDoc*
scenes_doc(struct EditorUi* ui)
{
    struct Editor_DocHandle handle;

    if( !ui->editor )
        return NULL;
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_SCENES, EDITOR_SCENES_DOC_KEY, &handle) || !handle.text
        || !handle.text->loaded )
        return NULL;
    return handle.text;
}

/** The parse, current with the mirror's text. NULL without the document (or
 *  when it does not parse -- said once per revision). */
static const struct Editor_ScenesFile*
scenes_refresh(
    struct EditorUi* ui,
    struct ScenesPanel* self)
{
    struct Editor_TextDoc* doc = scenes_doc(ui);

    if( !doc )
    {
        if( self->file_doc )
        {
            Editor_ScenesFree(&self->file);
            self->file_doc = NULL;
            self->file_ok = 0;
            self->file_valid = 0;
        }
        return NULL;
    }
    if( doc != self->file_doc || doc->revision != self->file_revision )
    {
        struct Editor_ScenesParseResult const parsed = Editor_ScenesParse(&self->file, doc->text, doc->length);
        if( !parsed.ok )
            Editor_UiLog(ui, "editor: %s line %d does not parse; scenes are read only until it is fixed",
                EDITOR_SCENES_FILE_PATH, parsed.line);
        self->file_doc = doc;
        self->file_revision = doc->revision;
        self->file_ok = parsed.ok;
        self->file_valid = 0;
    }
    return self->file_ok ? &self->file : NULL;
}

/** 0 with a status sentence when there is no document to edit. */
static const struct Editor_ScenesFile*
scenes_editable(
    struct EditorUi* ui,
    struct ScenesPanel* self,
    const char* verb)
{
    const struct Editor_ScenesFile* file;

    if( !ui->editor )
    {
        Editor_UiStatus(ui, "%s refused: no editor session", verb);
        return NULL;
    }
    file = scenes_refresh(ui, self);
    if( !file )
        Editor_UiStatus(ui, "%s refused: this server has no scenes document (or %s does not parse)", verb,
            EDITOR_SCENES_FILE_PATH);
    return file;
}

/** Apply one verb's commands as ONE undo step named `label`, then save the
 *  document. 1 when every command was taken. */
static int
scenes_submit(
    struct EditorUi* ui,
    const struct Editor_Cmd* commands,
    int count,
    const char* label)
{
    int applied = 0;

    assert(commands);
    assert(count > 0);
    if( !ui->tools.stroke_open )
    {
        Editor_StrokeBegin(ui->editor);
        Editor_HistoryBreak(&ui->editor->history, label);
    }
    for( int i = 0; i < count; i++ )
        applied += Editor_Apply(ui->editor, &commands[i]) ? 1 : 0;
    if( !ui->tools.stroke_open )
        Editor_StrokeEnd(ui->editor);
    if( applied < count )
    {
        Editor_UiStatus(ui, "%s refused: the authority took %d of %d edits", label, applied, count);
        return 0;
    }
    /* A scene is session state: it survives a restart without Save all. */
    (void)Editor_SaveDoc(ui->editor, EDITOR_DOC_SCENES, EDITOR_SCENES_DOC_KEY);
    return 1;
}

/* ---- the state ----------------------------------------------------------------- */

/** The window's state as a scene named `name`: the segments `mask` asks for
 *  that the window has. */
static void
scenes_capture(
    struct EditorUi* ui,
    struct ScenesPanel* self,
    const char* name,
    unsigned mask,
    struct Editor_Scene* out)
{
    Editor_SceneInitRow(out, name);
    if( (mask & EDITOR_SCENE_SEG_SQUARES) && ui->editor )
    {
        struct Editor_Doc* doc = &ui->editor->doc;
        for( int i = 0; i < doc->square_count && out->square_count < EDITOR_SCENE_SQUARES_MAX; i++ )
        {
            const struct Editor_Square* square = &doc->squares[i];
            const char* storage;
            if( !square->loaded )
                continue;
            storage = Editor_DocSquareStorage(doc, square);
            if( storage && storage[0] )
                snprintf(out->squares[out->square_count], sizeof(out->squares[0]), "%s/m%d_%d", storage,
                    square->map_x, square->map_z);
            else
                snprintf(out->squares[out->square_count], sizeof(out->squares[0]), "m%d_%d", square->map_x,
                    square->map_z);
            out->square_count++;
        }
        if( out->square_count > 0 )
            out->segments |= EDITOR_SCENE_SEG_SQUARES;
    }
    if( (mask & EDITOR_SCENE_SEG_CAMERA) && ui->app_services.world_camera )
    {
        struct EditorViewCamera camera;
        int const level = ui->tools.edit_level >= 0 ? ui->tools.edit_level : 0;
        if( ui->app_services.world_camera(ui->app_services.user_data, level, &camera) && camera.x >= 0
            && camera.z >= 0 && camera.x <= EDITOR_SCENE_TILE_MAX * 128 && camera.z <= EDITOR_SCENE_TILE_MAX * 128
            && camera.level >= 0 && camera.level <= 3 )
        {
            out->camera.x = camera.x;
            out->camera.z = camera.z;
            out->camera.level = camera.level;
            out->camera.yaw = camera.yaw & 2047;
            out->camera.pitch = camera.pitch & 2047;
            out->camera.zoom = camera.zoom < 0 ? 0 : camera.zoom > EDITOR_SCENE_ZOOM_MAX ? EDITOR_SCENE_ZOOM_MAX
                                                                                          : camera.zoom;
            out->segments |= EDITOR_SCENE_SEG_CAMERA;
        }
    }
    if( mask & EDITOR_SCENE_SEG_LEVEL )
    {
        out->level = ui->tools.edit_level;
        out->above = out->level >= 0 && edp_level_showing_above(ui);
        out->segments |= EDITOR_SCENE_SEG_LEVEL;
    }
    if( (mask & EDITOR_SCENE_SEG_LANE) && ui->lane.known )
    {
        snprintf(out->lane, sizeof(out->lane), "%s", ui->lane.name);
        out->segments |= EDITOR_SCENE_SEG_LANE;
    }
    if( (mask & EDITOR_SCENE_SEG_PREVIEW) && self->preview_count > 0 )
    {
        struct Builder_PreviewVar vars[BUILDER_PREVIEW_MAX];
        int const n = Builder_PreviewDecode(self->preview, self->preview_count, vars, BUILDER_PREVIEW_MAX);
        for( int i = 0; i < n && out->preview_count < EDITOR_SCENE_PREVIEW_MAX; i++ )
        {
            struct Editor_ScenePreview* entry = &out->preview[out->preview_count++];
            snprintf(entry->var, sizeof(entry->var), "%s%d", vars[i].kind == BUILDER_PREVIEW_VARBIT ? "varb" : "varp",
                vars[i].id);
            entry->value = vars[i].value;
        }
        if( out->preview_count > 0 )
            out->segments |= EDITOR_SCENE_SEG_PREVIEW;
    }
    if( mask & EDITOR_SCENE_SEG_ACCOUNT )
    {
        const struct Editor_LiveModel* live = Editor_LivePanelModel(ui);
        if( live && live->play_account[0] && live->play_x >= 0 && live->play_z >= 0 && live->play_level >= 0 )
        {
            snprintf(out->account, sizeof(out->account), "%s", live->play_account);
            out->account_x = live->play_x;
            out->account_z = live->play_z;
            out->account_level = live->play_level;
            out->segments |= EDITOR_SCENE_SEG_ACCOUNT;
        }
    }
    /* A part that does not read as the grammar's is left out, never cut. */
    if( !Editor_SceneValid(out) )
        Editor_SceneClear(out, EDITOR_SCENE_SEG_ACCOUNT);
}

static unsigned
scenes_held(const struct ScenesPanel* self)
{
    unsigned mask = 0;
    for( int i = 0; i < SCENES_HOLDS; i++ )
        if( self->hold[i] )
            mask |= k_hold_segments[i];
    return mask;
}

static const char*
scenes_name_box(
    struct EditorUi* ui,
    struct ScenesPanel* self)
{
    const char* text = ToriRSChrome_Text(ui->chrome, self->input_name);
    return text ? text : "";
}

/* ---- the list ------------------------------------------------------------------- */

static void
scenes_rebuild(
    struct EditorUi* ui,
    struct ScenesPanel* self,
    const char* filter)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    int first;
    int shown;

    self->listed = 0;
    self->total = self->file_ok ? Editor_ScenesCount(&self->file) : 0;
    for( int i = 0; i < self->total && self->listed < SCENES_SHOWN_MAX; i++ )
        if( scenes_contains(Editor_ScenesName(&self->file, i), filter) )
            self->rows[self->listed++] = i;
    self->file_valid = 1;
    snprintf(self->shown_filter, sizeof(self->shown_filter), "%s", filter);
    ToriRSChrome_VListSetCount(ui->chrome, self->list, self->listed);
    if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        for( int r = first; r < first + shown; r++ )
            ToriRSChrome_VListSetRow(ui->chrome, self->list, r, "");
    if( !self->file_ok && !scenes_doc(ui) )
        snprintf(line, sizeof(line), "No scenes document on this server");
    else if( self->total == 0 )
        snprintf(line, sizeof(line), "No scenes yet: Save scene keeps what you look at");
    else if( filter[0] )
        snprintf(line, sizeof(line), "%d of %d scenes match \"%.20s\"", self->listed, self->total, filter);
    else
        snprintf(line, sizeof(line), "editor/scenes.txt | %d scene%s", self->total, self->total == 1 ? "" : "s");
    ToriRSChrome_SetText(ui->chrome, self->row_count, line);
    if( self->picked_row >= self->listed )
        self->picked_row = -1;
}

static void
scenes_row_text(
    struct ScenesPanel* self,
    int row,
    char* out,
    size_t out_size)
{
    struct Editor_Scene scene;
    char summary[160];
    const char* name = Editor_ScenesName(&self->file, self->rows[row]);

    out[0] = '\0';
    if( !name || !Editor_ScenesGet(&self->file, name, &scene) )
        return;
    Editor_SceneSummary(&scene, summary, sizeof(summary));
    snprintf(out, out_size, "%s | %s", name, summary);
}

static void
scenes_holds(
    struct EditorUi* ui,
    struct ScenesPanel* self)
{
    edp_chips_begin(&self->holds);
    for( int i = 0; i < SCENES_HOLDS; i++ )
    {
        char tip[EDP_CHIP_TIP_MAX];
        snprintf(tip, sizeof(tip), "Save and Update %s the %s", self->hold[i] ? "keep" : "leave out", k_hold_labels[i]);
        (void)edp_chips_push(&self->holds, k_hold_labels[i], tip, self->hold[i], 0);
    }
    edp_chips_end(ui, &self->holds);
}

static void
scenes_chords(
    struct EditorUi* ui,
    struct ScenesPanel* self)
{
    edp_button_chord(ui, self->button_save, EDITOR_HK_FILE_SAVE_SCENE);
    self->chord_serial = ui->hotkeys.serial;
}

static void
scenes_detail(
    struct EditorUi* ui,
    struct ScenesPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    struct Editor_Scene scene;

    line[0] = '\0';
    if( self->picked_name[0] && self->file_ok && Editor_ScenesGet(&self->file, self->picked_name, &scene) )
    {
        int at = snprintf(line, sizeof(line), "Holds:");
        for( int i = 0; i < SCENES_HOLDS && at < (int)sizeof(line); i++ )
            if( scene.segments & k_hold_segments[i] )
                at += snprintf(line + at, sizeof(line) - (size_t)at, " %s", k_hold_labels[i]);
        if( (scene.segments & EDITOR_SCENE_SEG_NOTE) && at < (int)sizeof(line) )
            at += snprintf(line + at, sizeof(line) - (size_t)at, " note");
        (void)at;
    }
    ToriRSChrome_SetText(ui->chrome, self->row_detail, line);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_enter, !line[0]);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_update, !line[0]);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_rename, !line[0]);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_delete, !line[0]);
}

/* ---- Enter ---------------------------------------------------------------------- */

/** The picker's storage index of `storage` ("" is the content tree: -1). */
static int
scenes_storage_index(
    struct EditorUi* ui,
    const char* storage)
{
    char name[EDITOR_DOC_STORAGE_MAX];

    if( !storage[0] )
        return -1;
    for( int i = 0; i < 64; i++ )
    {
        if( !Editor_Squares_StorageName(ui, i, name) )
            break;
        if( strcmp(name, storage) == 0 )
            return i;
    }
    return -1;
}

/** Every square of `scene` loaded in this window's mirror. */
static int
scenes_squares_loaded(
    struct EditorUi* ui,
    const struct Editor_Scene* scene)
{
    struct Editor_Doc* doc;

    if( !(scene->segments & EDITOR_SCENE_SEG_SQUARES) )
        return 1;
    if( !ui->editor )
        return 0;
    doc = &ui->editor->doc;
    for( int k = 0; k < scene->square_count; k++ )
    {
        char storage[EDITOR_DOC_STORAGE_MAX];
        char bare[EDITOR_DOC_KEY_MAX];
        int map_x;
        int map_z;
        int found = 0;
        if( !Editor_DocSplitSquareKey(scene->squares[k], storage, bare) || !Editor_SquareKeyParse(bare, &map_x, &map_z) )
            continue;
        for( int i = 0; i < doc->square_count && !found; i++ )
        {
            const struct Editor_Square* square = &doc->squares[i];
            const char* held = Editor_DocSquareStorage(doc, square);
            found = square->loaded && square->map_x == map_x && square->map_z == map_z
                    && strcmp(held ? held : "", storage) == 0;
        }
        if( !found )
            return 0;
    }
    return 1;
}

/** Enter's camera: put once the world is quiet and the scene's squares are
 *  in; said once it is, or once it could not be. */
static void
scenes_camera_tick(
    struct EditorUi* ui,
    struct ScenesPanel* self)
{
    if( !self->camera_pending )
        return;
    if( ui->app_services.world_busy && ui->app_services.world_busy(ui->app_services.user_data) )
        return;
    if( scenes_squares_loaded(ui, &self->camera_scene)
        && ui->app_services.world_camera_set(ui->app_services.user_data, &self->camera) )
    {
        self->camera_pending = 0;
        Editor_UiLog(ui, "scenes: %s: the camera is over %d,%d level %d", self->camera_scene.name,
            (int)(self->camera.x / 128), (int)(self->camera.z / 128), (int)self->camera.level);
        return;
    }
    if( ui->chrome->now_ms - self->camera_since_ms >= SCENES_CAMERA_WAIT_MS )
    {
        self->camera_pending = 0;
        Editor_UiStatus(ui, "Enter %s: the camera was not put: %d,%d is not in the squares this window loaded",
            self->camera_scene.name, (int)(self->camera.x / 128), (int)(self->camera.z / 128));
    }
}

int
Editor_Scenes_EnterNow(
    struct EditorUi* ui,
    const char* name)
{
    char name_copy[EDITOR_SCENE_NAME_MAX];
    struct ScenesPanel* self;
    const struct Editor_ScenesFile* file;
    struct Editor_Scene scene;
    char said[EDITOR_UI_CONSOLE_LINE_MAX];
    int at;
    int opened = 0;

    assert(ui);
    assert(name);
    snprintf(name_copy, sizeof(name_copy), "%s", name);
    name = name_copy;
    self = scenes_self(ui);
    if( !self )
    {
        Editor_UiStatus(ui, "Enter refused: this window has no Scenes panel");
        return 0;
    }
    file = scenes_editable(ui, self, "Enter");
    if( !file )
        return 0;
    if( !Editor_ScenesGet(file, name, &scene) )
    {
        Editor_UiStatus(ui, "Enter refused: there is no scene %s", name);
        return 0;
    }
    at = snprintf(said, sizeof(said), "Entered %s:", name);

    if( scene.segments & EDITOR_SCENE_SEG_SQUARES )
    {
        for( int k = 0; k < scene.square_count; k++ )
        {
            char storage[EDITOR_DOC_STORAGE_MAX];
            char bare[EDITOR_DOC_KEY_MAX];
            int map_x;
            int map_z;
            if( !Editor_DocSplitSquareKey(scene.squares[k], storage, bare)
                || !Editor_SquareKeyParse(bare, &map_x, &map_z) )
                continue;
            Editor_UiOpenSquare(ui, map_x, map_z, storage, scenes_storage_index(ui, storage), opened > 0);
            opened++;
        }
        at += snprintf(said + at, sizeof(said) - (size_t)at, " %d square%s,", opened, opened == 1 ? "" : "s");
    }
    if( scene.segments & EDITOR_SCENE_SEG_LEVEL )
    {
        edp_level_set(ui, scene.level);
        if( scene.level >= 0 )
        {
            (void)edp_level_show_above(ui, scene.above);
            self->above_pending = 3;
            self->above_want = scene.above;
        }
        if( scene.level < 0 )
            at += snprintf(said + at, sizeof(said) - (size_t)at, " level Auto,");
        else
            at += snprintf(said + at, sizeof(said) - (size_t)at, " level %d%s,", scene.level,
                scene.above ? " above" : "");
    }
    if( scene.segments & EDITOR_SCENE_SEG_PREVIEW )
    {
        struct Builder_PreviewVar vars[BUILDER_PREVIEW_MAX];
        int32_t values[TORIRSMAPED_STATE_VALUES_MAX];
        int n = 0;
        int count;
        for( int i = 0; i < scene.preview_count && n < BUILDER_PREVIEW_MAX; i++ )
        {
            int kind;
            int id;
            if( !Builder_VarNameId(scene.preview[i].var, &kind, &id) )
                continue;
            vars[n].kind = kind;
            vars[n].id = id;
            vars[n].value = scene.preview[i].value;
            n++;
        }
        count = Builder_PreviewEncode(vars, n, values, TORIRSMAPED_STATE_VALUES_MAX);
        if( Editor_UiPublish(ui, TORIRSMAPED_STATE_VAR_PREVIEW, values, count) )
        {
            memcpy(self->preview, values, sizeof(values[0]) * (size_t)count);
            self->preview_count = count;
        }
        at += snprintf(said + at, sizeof(said) - (size_t)at, " preview of %d var%s,", n, n == 1 ? "" : "s");
    }
    if( (scene.segments & EDITOR_SCENE_SEG_LANE) && ui->lane.known && strcmp(scene.lane, ui->lane.name) != 0 )
    {
        if( strcmp(scene.lane, ui->lane.next_boot) == 0 )
            at += snprintf(said + at, sizeof(said) - (size_t)at, " lane %s at the next boot,", scene.lane);
        else if( Editor_Lanes_Switch(ui, scene.lane) )
            at += snprintf(said + at, sizeof(said) - (size_t)at, " lane %s from the next boot,", scene.lane);
        else
            at += snprintf(said + at, sizeof(said) - (size_t)at, " lane %s not switched,", scene.lane);
    }
    if( scene.segments & EDITOR_SCENE_SEG_ACCOUNT )
    {
        int const sent = Editor_Live_Teleport(ui, scene.account, scene.account_x, scene.account_z, scene.account_level);
        at += snprintf(said + at, sizeof(said) - (size_t)at, sent > 0 ? " %s teleported," : " %s not moved (the live lane is down),",
            scene.account);
    }
    if( scene.segments & EDITOR_SCENE_SEG_CAMERA )
    {
        if( !ui->app_services.world_camera_set )
            at += snprintf(said + at, sizeof(said) - (size_t)at, " no camera here,");
        else
        {
            memset(&self->camera, 0, sizeof(self->camera));
            self->camera.x = scene.camera.x;
            self->camera.z = scene.camera.z;
            self->camera.level = scene.camera.level;
            self->camera.yaw = scene.camera.yaw;
            self->camera.pitch = scene.camera.pitch;
            self->camera.zoom = scene.camera.zoom;
            self->camera_scene = scene;
            self->camera_pending = 1;
            self->camera_since_ms = ui->chrome->now_ms;
            at += snprintf(said + at, sizeof(said) - (size_t)at, " camera,");
        }
    }
    if( at > 0 && at < (int)sizeof(said) && said[at - 1] == ',' )
        said[at - 1] = '\0';
    if( strcmp(said + strlen(said) - 1, ":") == 0 )
        snprintf(said, sizeof(said), "Entered %s: it holds nothing to restore", name);
    Editor_UiStatus(ui, "%s", said);
    /* The camera goes now when the squares were open already. */
    scenes_camera_tick(ui, self);
    return 1;
}

int
Editor_Scenes_Enter(
    struct EditorUi* ui,
    const char* name)
{
    char name_copy[EDITOR_SCENE_NAME_MAX];
    struct ScenesPanel* self;
    const struct Editor_ScenesFile* file;
    struct Editor_Scene scene;
    struct EditorUiGuard guard;

    assert(ui);
    assert(name);
    snprintf(name_copy, sizeof(name_copy), "%s", name);
    name = name_copy;
    self = scenes_self(ui);
    if( !self )
    {
        Editor_UiStatus(ui, "Enter refused: this window has no Scenes panel");
        return 0;
    }
    file = scenes_editable(ui, self, "Enter");
    if( !file )
        return 0;
    if( !Editor_ScenesGet(file, name, &scene) )
    {
        Editor_UiStatus(ui, "Enter refused: there is no scene %s", name);
        return 0;
    }
    if( !(scene.segments & EDITOR_SCENE_SEG_SQUARES) )
        return Editor_Scenes_EnterNow(ui, name);
    /* Its squares replace the open set: unsaved edits are asked about. */
    memset(&guard, 0, sizeof(guard));
    guard.action = EDITOR_UI_GUARD_ENTER_SCENE;
    snprintf(guard.scene, sizeof(guard.scene), "%s", name);
    (void)Editor_UiGuarded(ui, &guard);
    return 1;
}

/* ---- the verbs ------------------------------------------------------------------ */

int
Editor_Scenes_Save(
    struct EditorUi* ui,
    const char* name)
{
    char name_copy[EDITOR_SCENE_NAME_MAX];
    struct ScenesPanel* self;
    const struct Editor_ScenesFile* file;
    struct Editor_Scene scene;
    struct Editor_Cmd* commands;
    char generated[EDITOR_SCENE_NAME_MAX];
    char label[EDITOR_HISTORY_LABEL_MAX];
    char summary[160];
    int capacity;
    int n;

    assert(ui);
    self = scenes_self(ui);
    if( !self )
    {
        Editor_UiStatus(ui, "Save scene refused: this window has no Scenes panel");
        return 0;
    }
    file = scenes_editable(ui, self, "Save scene");
    if( !file )
        return 0;
    if( !ui->editor->writable )
    {
        Editor_UiStatus(ui, "Save scene refused: this session is read-only");
        return 0;
    }
    if( !name )
        name = scenes_name_box(ui, self);
    snprintf(name_copy, sizeof(name_copy), "%s", name);
    name = name_copy;
    if( !name[0] )
    {
        for( int i = 1; i < 10000; i++ )
        {
            snprintf(generated, sizeof(generated), "scene-%d", i);
            if( !Editor_ScenesHas(file, generated) )
                break;
        }
        name = generated;
    }
    if( !Editor_SceneNameValid(name) )
    {
        Editor_UiStatus(ui, "Save scene refused: '%.40s' is not a scene name (no spaces, no '|')", name);
        return 0;
    }
    if( Editor_ScenesHas(file, name) )
    {
        Editor_UiStatus(ui, "Save scene refused: %s exists; Update rewrites it", name);
        return 0;
    }
    scenes_capture(ui, self, name, scenes_held(self), &scene);
    commands = edp_scratch(&capacity);
    n = Editor_ScenePlanSave(commands, capacity < EDITOR_SCENE_PLAN_MAX ? capacity : EDITOR_SCENE_PLAN_MAX, file,
        &scene);
    if( n == 0 )
    {
        Editor_UiStatus(ui, "Save scene refused: %s does not fit a line (too many squares?)", name);
        return 0;
    }
    snprintf(label, sizeof(label), "Save scene %s", name);
    if( !scenes_submit(ui, commands, n, label) )
        return 0;
    snprintf(self->picked_name, sizeof(self->picked_name), "%s", name);
    Editor_SceneSummary(&scene, summary, sizeof(summary));
    Editor_UiStatus(ui, "Saved scene %s: %s", name, summary);
    return 1;
}

int
Editor_Scenes_Update(
    struct EditorUi* ui,
    const char* name)
{
    char name_copy[EDITOR_SCENE_NAME_MAX];
    struct ScenesPanel* self;
    const struct Editor_ScenesFile* file;
    struct Editor_Scene scene;
    struct Editor_Scene now;
    struct Editor_Cmd* commands;
    char label[EDITOR_HISTORY_LABEL_MAX];
    int capacity;
    int n;

    assert(ui);
    assert(name);
    snprintf(name_copy, sizeof(name_copy), "%s", name);
    name = name_copy;
    self = scenes_self(ui);
    if( !self )
        return 0;
    file = scenes_editable(ui, self, "Update");
    if( !file )
        return 0;
    if( !Editor_ScenesGet(file, name, &scene) )
    {
        Editor_UiStatus(ui, "Update refused: there is no scene %s", name);
        return 0;
    }
    /* The held parts the window has replace the scene's; the rest stay. */
    scenes_capture(ui, self, name, scenes_held(self), &now);
    Editor_SceneMerge(&scene, &now);
    commands = edp_scratch(&capacity);
    n = Editor_ScenePlanSave(commands, capacity < EDITOR_SCENE_PLAN_MAX ? capacity : EDITOR_SCENE_PLAN_MAX, file,
        &scene);
    if( n == 0 )
    {
        Editor_UiStatus(ui, "Update refused: scene %s already holds this state", name);
        return 0;
    }
    snprintf(label, sizeof(label), "Update scene %s", name);
    if( !scenes_submit(ui, commands, n, label) )
        return 0;
    Editor_UiStatus(ui, "Updated scene %s (%d line%s rewritten)", name, n, n == 1 ? "" : "s");
    return 1;
}

int
Editor_Scenes_Rename(
    struct EditorUi* ui,
    const char* name,
    const char* new_name)
{
    char name_copy[EDITOR_SCENE_NAME_MAX];
    char new_copy[EDITOR_SCENE_NAME_MAX];
    struct ScenesPanel* self;
    const struct Editor_ScenesFile* file;
    struct Editor_Cmd* commands;
    char label[EDITOR_HISTORY_LABEL_MAX];
    int capacity;
    int n;

    assert(ui);
    assert(name);
    assert(new_name);
    snprintf(name_copy, sizeof(name_copy), "%s", name);
    snprintf(new_copy, sizeof(new_copy), "%s", new_name);
    name = name_copy;
    new_name = new_copy;
    self = scenes_self(ui);
    if( !self )
        return 0;
    file = scenes_editable(ui, self, "Rename");
    if( !file )
        return 0;
    if( !Editor_SceneNameValid(new_name) || Editor_ScenesHas(file, new_name) || !Editor_ScenesHas(file, name) )
    {
        Editor_UiStatus(ui, "Rename refused: %s", !Editor_ScenesHas(file, name) ? "there is no such scene"
                                                  : Editor_ScenesHas(file, new_name)
                                                      ? "that name is taken; type a new one in Name"
                                                      : "type a scene name in Name (no spaces, no '|')");
        return 0;
    }
    commands = edp_scratch(&capacity);
    n = Editor_ScenePlanRename(commands, capacity < EDITOR_SCENE_PLAN_MAX ? capacity : EDITOR_SCENE_PLAN_MAX, file,
        name, new_name);
    if( n == 0 )
    {
        Editor_UiStatus(ui, "Rename refused: a line of %s would be too long", name);
        return 0;
    }
    snprintf(label, sizeof(label), "Rename scene %s to %s", name, new_name);
    if( !scenes_submit(ui, commands, n, label) )
        return 0;
    snprintf(self->picked_name, sizeof(self->picked_name), "%s", new_name);
    Editor_UiStatus(ui, "Renamed scene %s to %s", name, new_name);
    return 1;
}

int
Editor_Scenes_Delete(
    struct EditorUi* ui,
    const char* name)
{
    char name_copy[EDITOR_SCENE_NAME_MAX];
    struct ScenesPanel* self;
    const struct Editor_ScenesFile* file;
    struct Editor_Cmd* commands;
    char label[EDITOR_HISTORY_LABEL_MAX];
    int capacity;
    int n;

    assert(ui);
    assert(name);
    snprintf(name_copy, sizeof(name_copy), "%s", name);
    name = name_copy;
    self = scenes_self(ui);
    if( !self )
        return 0;
    file = scenes_editable(ui, self, "Delete");
    if( !file )
        return 0;
    commands = edp_scratch(&capacity);
    n = Editor_ScenePlanDelete(commands, capacity < EDITOR_SCENE_PLAN_MAX ? capacity : EDITOR_SCENE_PLAN_MAX, file,
        name);
    if( n == 0 )
    {
        Editor_UiStatus(ui, "Delete refused: there is no scene %s", name);
        return 0;
    }
    snprintf(label, sizeof(label), "Delete scene %s", name);
    if( !scenes_submit(ui, commands, n, label) )
        return 0;
    self->picked_name[0] = '\0';
    Editor_UiStatus(ui, "Deleted scene %s (Undo brings it back)", name);
    return 1;
}

int
Editor_ScenesList(
    struct EditorUi* ui,
    struct Editor_SceneList* out)
{
    struct Editor_DocHandle handle;
    struct Editor_ScenesFile file;
    int total;

    assert(ui);
    assert(out);
    out->count = 0;
    out->more = 0;
    if( !ui->editor || !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_SCENES, EDITOR_SCENES_DOC_KEY, &handle)
        || !handle.text || !handle.text->loaded )
        return 0;
    Editor_ScenesInit(&file);
    if( !Editor_ScenesParse(&file, handle.text->text, handle.text->length).ok )
    {
        Editor_ScenesFree(&file);
        return 0;
    }
    total = Editor_ScenesCount(&file);
    for( int i = 0; i < total; i++ )
    {
        struct Editor_Scene scene;
        struct Editor_SceneListRow* row;
        if( out->count == EDITOR_SCENES_LIST_MAX )
        {
            out->more = total - i;
            break;
        }
        row = &out->rows[out->count++];
        snprintf(row->name, sizeof(row->name), "%s", Editor_ScenesName(&file, i));
        (void)Editor_ScenesGet(&file, row->name, &scene);
        Editor_SceneSummary(&scene, row->summary, sizeof(row->summary));
    }
    Editor_ScenesFree(&file);
    return out->count;
}

int
Editor_Scenes_AddInStroke(
    struct EditorUi* ui,
    const struct Editor_Scene* scene)
{
    struct Editor_DocHandle handle;
    struct Editor_ScenesFile file;
    struct Editor_Cmd commands[EDITOR_SCENE_PLAN_MAX];
    int n = 0;
    int applied = 0;

    assert(ui);
    assert(scene);
    assert(ui->editor);
    /* The caller's stroke is what makes this one step with its own edits. */
    assert(ui->editor->history_stroke);
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_SCENES, EDITOR_SCENES_DOC_KEY, &handle) || !handle.text
        || !handle.text->loaded )
        return 0;
    Editor_ScenesInit(&file);
    if( Editor_ScenesParse(&file, handle.text->text, handle.text->length).ok && !Editor_ScenesHas(&file, scene->name) )
        n = Editor_ScenePlanSave(commands, EDITOR_SCENE_PLAN_MAX, &file, scene);
    Editor_ScenesFree(&file);
    for( int i = 0; i < n; i++ )
        applied += Editor_Apply(ui->editor, &commands[i]) ? 1 : 0;
    return n > 0 && applied == n;
}

/* ---- the module ------------------------------------------------------------------ */

static void
scenes_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct ScenesPanel* self = opaque;
    const char* filter;
    int first;
    int shown;
    uint32_t revision;
    const struct Editor_TextDoc* doc;

    scenes_camera_tick(ui, self);
    if( self->above_pending > 0 )
    {
        self->above_pending--;
        if( ui->tools.edit_level >= 0 && edp_level_showing_above(ui) != self->above_want )
            (void)edp_level_show_above(ui, self->above_want);
    }
    if( self->chord_serial != ui->hotkeys.serial )
        scenes_chords(ui, self);
    if( !ui->editor || !ui->chrome->panels[self->panel].visible )
        return;
    doc = self->file_doc;
    revision = self->file_revision;
    (void)scenes_refresh(ui, self);
    filter = ToriRSChrome_Text(ui->chrome, self->input_find);
    if( !filter )
        filter = "";
    if( !self->file_valid || doc != self->file_doc || revision != self->file_revision
        || strcmp(filter, self->shown_filter) != 0 )
    {
        scenes_rebuild(ui, self, filter);
        scenes_detail(ui, self);
    }
    edp_chips_hover(ui, &self->holds);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->listed; r++ )
    {
        char text[300];
        scenes_row_text(self, r, text, sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
    }
}

static const char*
scenes_picked(
    struct EditorUi* ui,
    struct ScenesPanel* self,
    const char* verb)
{
    if( !self->picked_name[0] )
    {
        Editor_UiStatus(ui, "%s refused: pick a scene in the list first", verb);
        return NULL;
    }
    return self->picked_name;
}

static int
scenes_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct ScenesPanel* self = opaque;
    const char* name;

    if( widget == self->list )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, self->list);
        const char* picked;
        if( row < 0 || row >= self->listed )
            return 1;
        picked = Editor_ScenesName(&self->file, self->rows[row]);
        if( !picked )
            return 1;
        /* A second activation of the picked row (Enter, a double-click)
         * enters it; the first picks it. */
        if( row == self->picked_row && strcmp(picked, self->picked_name) == 0 )
            (void)Editor_Scenes_Enter(ui, picked);
        else
        {
            self->picked_row = row;
            snprintf(self->picked_name, sizeof(self->picked_name), "%s", picked);
            ToriRSChrome_SetText(ui->chrome, self->input_name, picked);
        }
        scenes_detail(ui, self);
        return 1;
    }
    if( widget == self->button_save )
    {
        (void)Editor_Scenes_Save(ui, NULL);
        scenes_detail(ui, self);
        return 1;
    }
    if( widget == self->button_enter )
    {
        if( (name = scenes_picked(ui, self, "Enter")) )
            (void)Editor_Scenes_Enter(ui, name);
        return 1;
    }
    if( widget == self->button_update )
    {
        if( (name = scenes_picked(ui, self, "Update")) )
            (void)Editor_Scenes_Update(ui, name);
        return 1;
    }
    if( widget == self->button_rename )
    {
        char old_name[EDITOR_SCENE_NAME_MAX];
        if( !(name = scenes_picked(ui, self, "Rename")) )
            return 1;
        snprintf(old_name, sizeof(old_name), "%s", name);
        (void)Editor_Scenes_Rename(ui, old_name, scenes_name_box(ui, self));
        scenes_detail(ui, self);
        return 1;
    }
    if( widget == self->button_delete )
    {
        char old_name[EDITOR_SCENE_NAME_MAX];
        if( !(name = scenes_picked(ui, self, "Delete")) )
            return 1;
        snprintf(old_name, sizeof(old_name), "%s", name);
        (void)Editor_Scenes_Delete(ui, old_name);
        scenes_detail(ui, self);
        return 1;
    }
    if( widget == self->input_find || widget == self->input_name )
        return 1;
    return 0;
}

static int
scenes_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct ScenesPanel* self = opaque;
    int on_x = 0;
    int index;

    if( event->widget != self->holds.graph )
        return 0;
    if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS )
        return 1;
    index = edp_chips_key_of(&self->holds, event->key, &on_x);
    if( index >= 0 && index < SCENES_HOLDS )
    {
        self->hold[index] = !self->hold[index];
        scenes_holds(ui, self);
    }
    return 1;
}

static void
scenes_on_state(
    struct EditorUi* ui,
    void* opaque,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct ScenesPanel* self = opaque;

    (void)ui;
    if( key != TORIRSMAPED_STATE_VAR_PREVIEW || count < 0 || count > TORIRSMAPED_STATE_VALUES_MAX )
        return;
    if( count > 0 )
        memcpy(self->preview, values, sizeof(values[0]) * (size_t)count);
    self->preview_count = count;
}

/** The editor opened or closed: the panel starts (and stays) hidden until
 *  View > Scenes opens it, like the Live and Lanes panels;
 *  TORIRS_EDITOR_SCENES=show (photographs) opens it with the editor. */
static void
scenes_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct ScenesPanel* self = opaque;
    const char* value = getenv("TORIRS_EDITOR_SCENES");
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, visible && value && strcmp(value, "show") == 0);
}

static void
scenes_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct ScenesPanel* self = opaque;
    (void)ui;
    Editor_ScenesFree(&self->file);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_Scenes_Register(struct EditorUi* ui)
{
    struct ScenesPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    if( !ui->editor )
        return;
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    Editor_ScenesInit(&self->file);
    self->picked_row = -1;
    for( int i = 0; i < SCENES_HOLDS; i++ )
        self->hold[i] = 1;

    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8),
        edp_px(ui, 420), edp_px(ui, 280), "Scenes");
    assert(self->panel >= 0);
    self->row_count = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "part of a scene's name");
    self->list = ToriRSChrome_VList(chrome, self->panel, "", SCENES_LIST_ROWS, 0);
    ToriRSChrome_SetTooltip(chrome, self->list, "Click picks a scene; Enter or a second click enters it");
    self->input_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_name, "Save scene's and Rename's name; empty: scene-<n>");
    edp_chips_init(ui, &self->holds, self->panel, 2);
    self->button_save = ToriRSChrome_Button(chrome, self->panel, "Save scene");
    ToriRSChrome_SetTooltip(chrome, self->button_save, "Keep what you look at as a new scene (the lit Holds)");
    self->button_enter = ToriRSChrome_Button(chrome, self->panel, "Enter");
    ToriRSChrome_SetTooltip(chrome, self->button_enter, "Open its squares, camera, level, preview, lane, account");
    self->button_update = ToriRSChrome_Button(chrome, self->panel, "Update");
    ToriRSChrome_SetTooltip(chrome, self->button_update, "Rewrite the picked scene from what you look at now");
    self->button_rename = ToriRSChrome_Button(chrome, self->panel, "Rename");
    ToriRSChrome_SetTooltip(chrome, self->button_rename, "The picked scene takes the name in Name");
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete");
    ToriRSChrome_SetTooltip(chrome, self->button_delete, "Delete the picked scene (Undo brings it back)");
    self->row_detail = ToriRSChrome_Label(chrome, self->panel, "");
    scenes_chords(ui, self);
    scenes_holds(ui, self);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "scenes";
    desc.title = "Scenes";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = scenes_tick;
    desc.activate = scenes_activate;
    desc.event = scenes_event;
    desc.on_state = scenes_on_state;
    desc.set_visible = scenes_set_visible;
    desc.destroy = scenes_destroy;
    Editor_UiAddPanel(ui, &desc);
}

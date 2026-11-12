/*
 * The Camera path panel (module "campath", View > Camera path; action
 * view.campath): a cutscene's scripted camera authored by flying the World
 * camera, kept in editor/campaths.txt (editor/editor_campath.h has the
 * document, the maths and the script) and handed to the script's author as a
 * proc on the clipboard.
 *
 *   header     above the tabs: the shown path's count and length, and Path
 *              (the document's paths)
 *   Keyframes  the path's keyframes as rows (eye tile + height, look tile +
 *              height, cut or glide, hold). Add keyframe fits one to what
 *              the World camera shows NOW and puts the camera on it, so
 *              what is on screen after the press is what the game will
 *              frame (its eye and look point stand on tile centres) -- with
 *              no path yet it makes one, so the first press needs nothing
 *              typed; Update keyframe re-fits the selected one; Go to puts
 *              the camera on it; Delete keyframe; Move up / Move down. The
 *              selected keyframe's Move (Cut / Glide), Speed (Slow / Medium
 *              / Fast, or the raw Eye rate and Look rate pairs) and Hold
 *              (ticks); a typed field is applied by Enter or by leaving it.
 *              The row under the list says how long the glide takes and
 *              whether the hold cuts it short. Preview: the World camera
 *              flies the path on the client's own easing (Editor_CamSim, 50
 *              cycles a second) and returns; Stop
 *   Setup      Name + New path / Rename / Delete path; Coordinates (World
 *              tiles, or Instance-relative to the Template square's
 *              south-west tile, for a path that plays inside a runtime map
 *              instance); Wrap in a cutscene frame (the proc fades, locks
 *              and unlocks as general/scripts/cutscene_frame.rs2
 *              prescribes); Show the path in the World
 *   Script     the proc as Copy script will put it on the clipboard, in a
 *              read-only view; Send to play client: the path played on the
 *              lane's play account now, a ::cam cheat per keyframe and
 *              ::camreset after, through the Live panel's cheat channel -- a
 *              framing judged in the real client without a RUN
 *
 * The three tabs are the three things done with a path, each without a
 * scroll: on one page the panel was 1,100 px in an 856 px column.
 *
 * Every edit is ONE undo step on the TEXT document (TEXT_SPLICE commands
 * planned from the old text to the new), saved by Save all, lane-owned.
 * Every refusal says why through Editor_UiStatus. The selected path is drawn
 * in the World (app_editor_campath.c asks Editor_CamPath_Marks).
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/editor.h"
#include "editor/editor_campath.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_stamps.h"
#include "editor/live/live_panel.h"
#include "mem/torirs_mem.h"
#include "platform/platform_clipboard.h"
#include "torirsmaped/torirs_maped_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAMPATH_KEY_ROWS 6
/** The script view's rows (the chrome's row pool is shared by every list of
 *  every panel: ten, as the Triggers panel's snippet takes) and the lines of
 *  the longest proc: three a keyframe and the frame's. */
#define CAMPATH_SCRIPT_ROWS 10
#define CAMPATH_SCRIPT_LINES_MAX (EDITOR_CAMPATH_KEYS_MAX * 3 + 24)
/** A cut's hold when nothing says otherwise: 2.4 s. */
#define CAMPATH_CUT_HOLD 4
/** Ticks a new glide holds past its arrival. */
#define CAMPATH_GLIDE_REST 2
/** Cycles a glide is stepped before it is called one that never arrives:
 *  five minutes. */
#define CAMPATH_GLIDE_MAX_CYCLES 15000
/** Cycles one frame steps the Preview at most: a hitch does not fast-forward
 *  the flight past a second. */
#define CAMPATH_PREVIEW_CATCH_UP 50

enum
{
    CAMPATH_TAB_KEYS = 0,
    CAMPATH_TAB_SETUP,
    CAMPATH_TAB_SCRIPT,
    CAMPATH_TAB_COUNT
};

static const char* const k_tabs[CAMPATH_TAB_COUNT] = { "Keyframes", "Setup", "Script" };
static const char* const k_coords[2] = { "World tiles", "Instance-relative" };
static const char* const k_moves[2] = { "Cut", "Glide" };

struct CamPathPanel
{
    int panel;
    int row_title;
    int dropdown_path;
    int tabs;
    int input_name;
    int button_new;
    int button_rename;
    int button_delete;
    int dropdown_coords;
    int input_template;
    int check_frame;
    int list_keys;
    int button_add;
    int button_update;
    int button_goto;
    int button_delete_key;
    int button_up;
    int button_down;
    int row_key;
    int dropdown_move;
    int dropdown_speed;
    int input_eye_rate;
    int input_look_rate;
    int input_hold;
    int check_show;
    int button_preview;
    int button_stop;
    int button_send;
    int code_script;
    int button_copy;

    /* The document, parsed when its text revision moves. */
    struct Editor_CamPathsFile file;
    const struct Editor_TextDoc* file_doc;
    uint32_t file_revision;
    int file_ok;
    int file_line;
    /** Bumps whenever what the widgets show may have moved: the document,
     *  the shown path, the selected keyframe. */
    uint32_t serial;

    /* The shown path: its name, and the path as the document holds it. */
    char path_name[EDITOR_CAMPATH_NAME_MAX];
    struct Editor_CamPath path;
    int path_valid;
    /** The selected keyframe, -1 none. */
    int selected;

    char names[EDITOR_CAMPATHS_MAX][EDITOR_CAMPATH_NAME_MAX];
    const char* options[EDITOR_CAMPATHS_MAX];
    int name_count;
    /** The path whose name the Name field was last given: the field follows
     *  the shown path (Rename starts from its name) and is the map maker's
     *  to type in between. */
    char named[EDITOR_CAMPATH_NAME_MAX];
    uint32_t built_serial;
    int built;
    int built_dirty;

    /* The leg's fields as last filled, and what the row under the list was
     * computed for (a glide's cycles are a few thousand steps: not a
     * frame's work). */
    uint32_t filled_serial;
    int filled;
    char filled_eye_rate[24];
    char filled_look_rate[24];
    char filled_hold[16];
    char filled_template[16];
    uint32_t leg_serial;
    uint32_t leg_cmd_seq;
    int leg_known;

    /* The script as Copy script would copy it. */
    char script[EDITOR_CAMPATH_SCRIPT_MAX];
    size_t script_length;
    int script_line_start[CAMPATH_SCRIPT_LINES_MAX];
    int script_line_count;
    int script_unlock;
    uint32_t script_serial;
    uint32_t script_cmd_seq;
    int script_known;

    /* Preview: the simulated client camera, the cycle it is on, the
     * keyframe whose commands come next and the cycle they come at. */
    int playing;
    struct Editor_CamSim sim;
    struct Editor_CamEye before;
    int play_cycle;
    int play_key;
    int play_key_cycle;
    int64_t play_ms;
    int play_carry_ms;

    /* Send to play client: the keyframe whose cheat goes next, and when. */
    int sending;
    int send_key;
    int64_t send_ms;
    char send_account[32];

    /** The panel was open on the last frame: it opens with the editor. */
    int wanted;
    uint32_t chord_serial;

    /** Where Triggers stood when this panel was last put beside it (x, y,
     *  width; campath_beside_triggers), so it is moved once per place. */
    int beside_x;
    int beside_y;
    int beside_w;
};

static struct CamPathPanel*
campath_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "campath");
}

/* ---- the World camera ----------------------------------------------------------- */

/** 1 when this window has a World camera to read, put and measure under. */
static int
campath_world(const struct EditorUi* ui)
{
    return ui->app_services.world_eye && ui->app_services.world_eye_set && ui->app_services.world_ground;
}

/** The level heights are measured on: the pinned edit level, else 0 -- in
 *  the game it is the level the player stands on. */
static int
campath_level(const struct EditorUi* ui)
{
    return ui->tools.edit_level >= 0 && ui->tools.edit_level <= 3 ? ui->tools.edit_level : 0;
}

/** Where the game stands and looks for `key`, the pitch held to the range
 *  the script opens for it. 0 when its tiles are not in the open squares. */
static int
campath_key_eye(
    struct EditorUi* ui,
    const struct Editor_CamKey* key,
    struct Editor_CamEye* out)
{
    if( !Editor_CamKeyEye(key, ui->app_services.world_ground, ui->app_services.user_data, out) )
        return 0;
    out->pitch = Editor_CamPitchClamp(out->pitch, 1);
    return 1;
}

/** 1 when the script must unlock the camera's pitch for `path`: a keyframe
 *  looks steeper or flatter than the locked range, or cannot be measured
 *  (its square is not open, and a needless unlock costs nothing). */
static int
campath_needs_unlock(
    struct EditorUi* ui,
    const struct Editor_CamPath* path)
{
    if( !campath_world(ui) )
        return 1;
    for( int i = 0; i < path->key_count; i++ )
    {
        struct Editor_CamEye eye;
        if( !Editor_CamKeyEye(&path->keys[i], ui->app_services.world_ground, ui->app_services.user_data, &eye)
            || Editor_CamPitchNeedsUnlock(eye.pitch) )
            return 1;
    }
    return 0;
}

/* ---- the document ------------------------------------------------------------- */

static struct Editor_TextDoc*
campath_text(struct EditorUi* ui)
{
    struct Editor_DocHandle handle;

    if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, EDITOR_CAMPATHS_DOC_KEY) )
        return NULL;
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, EDITOR_CAMPATHS_DOC_KEY, &handle) || !handle.text
        || !handle.text->loaded )
        return NULL;
    return handle.text;
}

/** The shown path read out of the file again; the selection kept in range. */
static void
campath_reload(struct CamPathPanel* self)
{
    self->path_valid = self->file_ok && self->path_name[0]
                       && Editor_CamPathsGet(&self->file, self->path_name, &self->path);
    if( !self->path_valid )
    {
        memset(&self->path, 0, sizeof(self->path));
        self->selected = -1;
    }
    else if( self->selected >= self->path.key_count )
        self->selected = self->path.key_count - 1;
    self->serial++;
}

/** Re-parse the document when its text moved. 1 when it parses. */
static int
campath_refresh(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    const struct Editor_TextDoc* doc;

    if( !ui->editor )
        return 0;
    doc = campath_text(ui);
    if( !doc )
    {
        if( self->file_doc )
        {
            Editor_CamPathsFree(&self->file);
            self->file_doc = NULL;
            self->file_ok = 0;
            campath_reload(self);
        }
        return 0;
    }
    if( doc != self->file_doc || doc->revision != self->file_revision )
    {
        struct Editor_CamPathsParseResult const r = Editor_CamPathsParse(&self->file, doc->text, doc->length);
        self->file_doc = doc;
        self->file_revision = doc->revision;
        self->file_ok = r.ok;
        self->file_line = r.line;
        /* The path shown went (an undo, a delete in another window): the
         * first one takes its place. */
        if( self->file_ok && Editor_CamPathsCount(&self->file) > 0 )
        {
            struct Editor_CamPath probe;
            if( !self->path_name[0] || !Editor_CamPathsGet(&self->file, self->path_name, &probe) )
                snprintf(self->path_name, sizeof(self->path_name), "%s", Editor_CamPathsName(&self->file, 0));
        }
        campath_reload(self);
    }
    return self->file_ok;
}

/** 0 with a sentence when the document cannot be edited. */
static int
campath_editable(
    struct EditorUi* ui,
    struct CamPathPanel* self,
    const char* verb)
{
    if( !ui->editor )
    {
        Editor_UiStatus(ui, "%s refused: this window has no editor session", verb);
        return 0;
    }
    if( campath_refresh(ui, self) )
        return 1;
    if( self->file_doc )
        Editor_UiStatus(ui, "%s refused: %s line %d does not parse; fix it by hand first", verb, EDITOR_CAMPATHS_FILE_PATH,
            self->file_line);
    else
        Editor_UiStatus(ui, "%s refused: this session cannot open %s", verb, EDITOR_CAMPATHS_FILE_PATH);
    return 0;
}

/** Splice the document to `text` as one undo step named `what`. 1 when it
 *  landed (or nothing changed). */
static int
campath_write_text(
    struct EditorUi* ui,
    const char* text,
    size_t length,
    const char* what)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    struct Editor_TextDoc* doc = campath_text(ui);
    int count;

    if( !doc )
        return 0;
    count = Editor_StampSplices(EDITOR_CAMPATHS_DOC_KEY, doc->text ? doc->text : "", doc->length, text, length, commands,
        capacity);
    if( count < 0 )
        return 0;
    if( count == 0 )
        return 1;
    return edp_apply(ui, commands, count, what) == count;
}

/** Write `path` in place of `old_name` (NULL: a new path). 1 when it landed;
 *  0 with a sentence. */
static int
campath_write(
    struct EditorUi* ui,
    struct CamPathPanel* self,
    const char* old_name,
    const struct Editor_CamPath* path,
    const char* verb)
{
    size_t length = 0;
    char* text;
    int ok;

    if( !Editor_CamPathsTextWith(&self->file, old_name, path, NULL, 0, &length) )
    {
        Editor_UiStatus(ui, "%s refused: path %s is not one the file can hold, or another path has that name", verb,
            path->name);
        return 0;
    }
    text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(text);
    Editor_CamPathsTextWith(&self->file, old_name, path, text, length + 1, &length);
    ok = campath_write_text(ui, text, length, verb);
    ToriMem_Free(ToriMem_Editor(), text);
    if( !ok )
        Editor_UiStatus(ui, "%s refused: the authority did not take the edit (a read-only session?)", verb);
    (void)campath_refresh(ui, self);
    return ok;
}

/** A copy of the shown path to edit (heap: a path is 48 keyframes), or NULL
 *  with a sentence. */
static struct Editor_CamPath*
campath_edit_copy(
    struct EditorUi* ui,
    struct CamPathPanel* self,
    const char* verb)
{
    struct Editor_CamPath* copy;

    if( !campath_editable(ui, self, verb) )
        return NULL;
    if( !self->path_valid )
    {
        Editor_UiStatus(ui, "%s refused: no path is shown; Add keyframe makes one, or Setup > New path", verb);
        return NULL;
    }
    copy = ToriMem_Alloc(ToriMem_Editor(), sizeof(*copy));
    assert(copy);
    *copy = self->path;
    return copy;
}

/** The selected keyframe's index, or -1 with a sentence. */
static int
campath_picked(
    struct EditorUi* ui,
    const struct CamPathPanel* self,
    const char* verb)
{
    if( !self->path_valid || self->selected < 0 || self->selected >= self->path.key_count )
    {
        Editor_UiStatus(ui, "%s refused: select a keyframe in the list first", verb);
        return -1;
    }
    return self->selected;
}

static void
campath_select(
    struct EditorUi* ui,
    struct CamPathPanel* self,
    int index)
{
    self->selected = index;
    ToriRSChrome_VListSetSelected(ui->chrome, self->list_keys, index);
    self->serial++;
}

/* ---- what the widgets show ------------------------------------------------------- */

/** "m16_101" for a square. */
static void
campath_square_text(
    int mx,
    int mz,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "m%d_%d", mx, mz);
}

/** "m16_101" into a square; 0 when it is not one. */
static int
campath_square_parse(
    const char* text,
    int* out_mx,
    int* out_mz)
{
    char tail = '\0';
    int mx = -1;
    int mz = -1;

    if( !text || sscanf(text, " m%d_%d %c", &mx, &mz, &tail) != 2 || mx < 0 || mz < 0 || mx > 255 || mz > 255 )
        return 0;
    *out_mx = mx;
    *out_mz = mz;
    return 1;
}

/** "5,5" (or "5 5") into a rate pair; 0 when it is not one a glide holds. */
static int
campath_rates_parse(
    const char* text,
    int* out_rate,
    int* out_rate2)
{
    char tail = '\0';
    int rate = -1;
    int rate2 = -1;

    if( !text || (sscanf(text, " %d , %d %c", &rate, &rate2, &tail) != 2 && sscanf(text, " %d %d %c", &rate, &rate2, &tail) != 2) )
        return 0;
    if( rate < 0 || rate > EDITOR_CAMPATH_RATE_MAX || rate2 < 0 || rate2 >= EDITOR_CAMPATH_SNAP_RATE2 )
        return 0;
    *out_rate = rate;
    *out_rate2 = rate2;
    return 1;
}

/** The path dropdown, the header's controls, the title and the list's
 *  count: rebuilt when the document or the shown path moved. */
static void
campath_build(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    int const dirty = self->file_doc ? self->file_doc->dirty : 0;
    int selected = -1;

    if( self->built && self->built_serial == self->serial && self->built_dirty == dirty )
        return;
    self->built = 1;
    self->built_serial = self->serial;
    self->built_dirty = dirty;

    self->name_count = 0;
    for( int i = 0; self->file_ok && i < Editor_CamPathsCount(&self->file) && self->name_count < EDITOR_CAMPATHS_MAX; i++ )
    {
        snprintf(self->names[self->name_count], sizeof(self->names[0]), "%s", Editor_CamPathsName(&self->file, i));
        if( strcmp(self->names[self->name_count], self->path_name) == 0 )
            selected = self->name_count;
        self->options[self->name_count] = self->names[self->name_count];
        self->name_count++;
    }
    if( self->name_count == 0 )
    {
        snprintf(self->names[0], sizeof(self->names[0]), "(none: name one, New path)");
        self->options[0] = self->names[0];
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_path, self->options, 1, 0);
    }
    else
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_path, self->options, self->name_count,
            selected < 0 ? 0 : selected);

    if( self->path_valid )
    {
        int const ticks = Editor_CamPathTicks(&self->path);
        snprintf(line, sizeof(line), "%d keyframe%s | %d ticks (%d.%d s)%s", self->path.key_count,
            self->path.key_count == 1 ? "" : "s", ticks, ticks * 6 / 10, ticks * 6 % 10, dirty ? " | unsaved" : "");
    }
    else if( !self->file_doc )
        snprintf(line, sizeof(line), "no %s in this session", EDITOR_CAMPATHS_FILE_PATH);
    else if( !self->file_ok )
        snprintf(line, sizeof(line), "%s line %d does not parse: read only", EDITOR_CAMPATHS_FILE_PATH, self->file_line);
    else
        snprintf(line, sizeof(line), "no path yet: frame a shot, press Add keyframe");
    ToriRSChrome_SetText(ui->chrome, self->row_title, line);

    if( self->path_valid && strcmp(self->named, self->path.name) != 0 )
    {
        snprintf(self->named, sizeof(self->named), "%s", self->path.name);
        ToriRSChrome_SetText(ui->chrome, self->input_name, self->named);
    }
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_coords, self->path_valid && self->path.instance);
    ToriRSChrome_SetChecked(ui->chrome, self->check_frame, self->path_valid && self->path.frame);
    self->filled_template[0] = '\0';
    if( self->path_valid && self->path.instance )
        campath_square_text(self->path.origin_mx, self->path.origin_mz, self->filled_template,
            sizeof(self->filled_template));
    ToriRSChrome_SetText(ui->chrome, self->input_template, self->filled_template);

    ToriRSChrome_VListSetCount(ui->chrome, self->list_keys, self->path_valid ? self->path.key_count : 0);
    ToriRSChrome_VListSetSelected(ui->chrome, self->list_keys, self->selected);
    {
        int first;
        int shown;
        /* Every visible row is restated: a reorder keeps the count. */
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_keys, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list_keys, r, "");
    }
}

/** The leg's controls from the selected keyframe, when it or the document
 *  moved. */
static void
campath_fill(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    const struct Editor_CamKey* key;
    int const has = self->path_valid && self->selected >= 0 && self->selected < self->path.key_count;

    if( self->filled && self->filled_serial == self->serial )
        return;
    self->filled = 1;
    self->filled_serial = self->serial;
    self->filled_eye_rate[0] = '\0';
    self->filled_look_rate[0] = '\0';
    self->filled_hold[0] = '\0';
    if( has )
    {
        key = &self->path.keys[self->selected];
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_move, key->glide ? 1 : 0);
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_speed, Editor_CamSpeedOf(key));
        if( key->glide )
        {
            snprintf(self->filled_eye_rate, sizeof(self->filled_eye_rate), "%d,%d", key->move_rate, key->move_rate2);
            snprintf(self->filled_look_rate, sizeof(self->filled_look_rate), "%d,%d", key->look_rate, key->look_rate2);
        }
        snprintf(self->filled_hold, sizeof(self->filled_hold), "%d", key->hold);
    }
    ToriRSChrome_SetText(ui->chrome, self->input_eye_rate, self->filled_eye_rate);
    ToriRSChrome_SetText(ui->chrome, self->input_look_rate, self->filled_look_rate);
    ToriRSChrome_SetText(ui->chrome, self->input_hold, self->filled_hold);
    ToriRSChrome_SetDisabled(ui->chrome, self->dropdown_move, !has);
    ToriRSChrome_SetDisabled(ui->chrome, self->dropdown_speed, !has);
    ToriRSChrome_SetDisabled(ui->chrome, self->input_eye_rate, !has);
    ToriRSChrome_SetDisabled(ui->chrome, self->input_look_rate, !has);
    ToriRSChrome_SetDisabled(ui->chrome, self->input_hold, !has);
}

/** The row under the list: the selected keyframe's angles as the game will
 *  hold them, and how its glide fits its hold. */
static void
campath_leg_row(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    int const has = self->path_valid && self->selected >= 0 && self->selected < self->path.key_count;
    const struct Editor_CamKey* key = has ? &self->path.keys[self->selected] : NULL;
    struct Editor_CamEye eye;
    uint32_t const seq = ui->editor ? ui->editor->last_cmd_seq : 0;
    int const number = self->selected + 1;

    if( self->leg_known && self->leg_serial == self->serial && self->leg_cmd_seq == seq )
        return;
    self->leg_known = 1;
    self->leg_serial = self->serial;
    self->leg_cmd_seq = seq;
    if( !has )
        snprintf(line, sizeof(line), "%s",
            self->path_valid && self->path.key_count > 0 ? "select a keyframe to retime or re-frame it"
            : self->path_valid                            ? "fly the World camera to a framing, Add keyframe"
                                                          : "");
    else if( !campath_world(ui) )
        snprintf(line, sizeof(line), "Key %d: this window has no World to measure it in", number);
    else if( !campath_key_eye(ui, key, &eye) )
        snprintf(line, sizeof(line), "Key %d is outside the open squares: open m%d_%d", number, key->eye_x / 64,
            key->eye_z / 64);
    else if( !key->glide )
        snprintf(line, sizeof(line), "Key %d: pitch %d, yaw %d; a cut", number, eye.pitch, eye.yaw);
    else if( self->selected == 0 )
        snprintf(line, sizeof(line), "Key 1 glides in from the player's own camera");
    else
    {
        struct Editor_CamEye from;
        int cycles = -1;
        int ticks;

        if( campath_key_eye(ui, &self->path.keys[self->selected - 1], &from) )
            cycles = Editor_CamGlideCycles(&from, key, ui->app_services.world_ground, ui->app_services.user_data, 1,
                CAMPATH_GLIDE_MAX_CYCLES);
        ticks = cycles >= 0 ? Editor_CamCyclesToTicks(cycles) : 0;
        if( cycles < 0 )
            snprintf(line, sizeof(line), "Key %d: this glide never arrives: raise a rate", number);
        else if( ticks > key->hold )
            snprintf(line, sizeof(line), "Key %d: the glide takes %d ticks, the hold cuts it at %d", number, ticks,
                key->hold);
        else
            snprintf(line, sizeof(line), "Key %d: pitch %d, yaw %d; glides in %d ticks (%d.%d s)", number, eye.pitch,
                eye.yaw, ticks, cycles * 2 / 100, cycles * 2 / 10 % 10);
    }
    ToriRSChrome_SetText(ui->chrome, self->row_key, line);
}

/** The script view's visible lines. */
static void
campath_script_fill(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    int first;
    int shown;

    if( !ToriRSChrome_VListWindow(ui->chrome, self->code_script, &first, &shown) )
        return;
    for( int l = first; l < first + shown && l < self->script_line_count; l++ )
    {
        char line[TORIRS_CHROME_INPUT_MAX];
        size_t const start = (size_t)self->script_line_start[l];
        size_t stop = l + 1 < self->script_line_count ? (size_t)self->script_line_start[l + 1] : self->script_length;
        while( stop > start && self->script[stop - 1] == '\n' )
            stop--;
        snprintf(line, sizeof(line), "%.*s", (int)(stop - start), self->script + start);
        ToriRSChrome_CodeSetLine(ui->chrome, self->code_script, l, line);
    }
}

/** Render the shown path's proc and cut it into the view's lines, when the
 *  path or the terrain under it moved. */
static void
campath_script_render(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    uint32_t const seq = ui->editor ? ui->editor->last_cmd_seq : 0;

    if( self->script_known && self->script_serial == self->serial && self->script_cmd_seq == seq )
        return;
    self->script_known = 1;
    self->script_serial = self->serial;
    self->script_cmd_seq = seq;
    self->script_length = 0;
    self->script_line_count = 0;
    self->script[0] = '\0';
    if( self->path_valid && self->path.key_count > 0 )
    {
        self->script_unlock = campath_needs_unlock(ui, &self->path);
        self->script_length = Editor_CamPathScript(&self->path, self->script_unlock, self->script, sizeof(self->script));
        assert(self->script_length < sizeof(self->script));
        for( size_t at = 0; at < self->script_length; )
        {
            const char* newline = memchr(self->script + at, '\n', self->script_length - at);
            assert(self->script_line_count < CAMPATH_SCRIPT_LINES_MAX);
            self->script_line_start[self->script_line_count++] = (int)at;
            if( !newline )
                break;
            at = (size_t)(newline - self->script) + 1;
        }
    }
    ToriRSChrome_VListSetCount(ui->chrome, self->code_script, self->script_line_count);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_copy, self->script_line_count == 0);
    campath_script_fill(ui, self);
}

/* ---- Preview ------------------------------------------------------------------- */

static void
campath_preview_end(
    struct EditorUi* ui,
    struct CamPathPanel* self,
    const char* said)
{
    if( !self->playing )
        return;
    self->playing = 0;
    /* Back where the map maker left it, as cam_reset gives the player's
     * camera back. */
    if( ui->app_services.world_eye_set )
        (void)ui->app_services.world_eye_set(ui->app_services.user_data, &self->before);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_stop, 1);
    self->leg_known = 0;
    if( said )
        Editor_UiStatus(ui, "%s; the World camera is back where it was", said);
}

/** Keyframe `index`'s commands, as the client takes the two packets. */
static int
campath_preview_key(
    struct EditorUi* ui,
    struct CamPathPanel* self,
    int index)
{
    const struct Editor_CamKey* key = &self->path.keys[index];
    struct Editor_CamEye eye;
    int look_x;
    int look_y;
    int look_z;
    int const cut = !key->glide;

    if( !Editor_CamKeyEye(key, ui->app_services.world_ground, ui->app_services.user_data, &eye)
        || !Editor_CamKeyLookPoint(key, ui->app_services.world_ground, ui->app_services.user_data, &look_x, &look_y, &look_z) )
        return 0;
    Editor_CamSimMoveTo(&self->sim, eye.x, eye.y, eye.z, cut ? EDITOR_CAMPATH_CUT_RATE : key->move_rate,
        cut ? EDITOR_CAMPATH_CUT_RATE : key->move_rate2);
    Editor_CamSimLookAt(&self->sim, look_x, look_y, look_z, cut ? EDITOR_CAMPATH_CUT_RATE : key->look_rate,
        cut ? EDITOR_CAMPATH_CUT_RATE : key->look_rate2);
    return 1;
}

static void
campath_preview_start(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    struct Editor_CamEye probe;

    campath_preview_end(ui, self, NULL);
    if( !campath_world(ui) )
    {
        Editor_UiStatus(ui, "Preview refused: this window has no World camera to fly");
        return;
    }
    if( !self->path_valid || self->path.key_count == 0 )
    {
        Editor_UiStatus(ui, "Preview refused: the path has no keyframe yet; fly the camera to a framing and press Add keyframe");
        return;
    }
    for( int i = 0; i < self->path.key_count; i++ )
        if( !campath_key_eye(ui, &self->path.keys[i], &probe) )
        {
            Editor_UiStatus(ui, "Preview refused: keyframe %d stands at %d,%d, outside the open squares; open m%d_%d too",
                i + 1, self->path.keys[i].eye_x, self->path.keys[i].eye_z, self->path.keys[i].eye_x / 64,
                self->path.keys[i].eye_z / 64);
            return;
        }
    if( !ui->app_services.world_eye(ui->app_services.user_data, &self->before) )
    {
        Editor_UiStatus(ui, "Preview refused: the World has no camera yet");
        return;
    }
    /* The script unlocks the pitch exactly when the path needs it. */
    Editor_CamSimInit(&self->sim, &self->before, campath_needs_unlock(ui, &self->path));
    self->playing = 1;
    self->play_cycle = 0;
    self->play_key = 0;
    self->play_key_cycle = 0;
    self->play_ms = ui->chrome->now_ms;
    self->play_carry_ms = 0;
    ToriRSChrome_SetDisabled(ui->chrome, self->button_stop, 0);
    {
        int const ticks = Editor_CamPathTicks(&self->path) - (self->path.frame ? 9 : 0);
        Editor_UiStatus(ui, "Preview: %s, %d keyframe%s, %d.%d s, on the game's own easing%s", self->path.name,
            self->path.key_count, self->path.key_count == 1 ? "" : "s", ticks * 6 / 10, ticks * 6 % 10,
            self->path.keys[0].glide ? " (keyframe 1 glides in from where the camera is now)" : "");
    }
}

/** One frame of the flight: the cycles the clock says have passed. */
static void
campath_preview_tick(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    int64_t const now = ui->chrome->now_ms;
    int cycles;

    if( !self->playing )
        return;
    self->play_carry_ms += (int)(now - self->play_ms);
    self->play_ms = now;
    cycles = self->play_carry_ms / EDITOR_CAMPATH_CYCLE_MS;
    self->play_carry_ms %= EDITOR_CAMPATH_CYCLE_MS;
    if( cycles > CAMPATH_PREVIEW_CATCH_UP )
        cycles = CAMPATH_PREVIEW_CATCH_UP;
    for( int c = 0; c < cycles; c++ )
    {
        while( self->play_key < self->path.key_count && self->play_cycle >= self->play_key_cycle )
        {
            if( !campath_preview_key(ui, self, self->play_key) )
            {
                campath_preview_end(ui, self, "Preview stopped: a keyframe left the open squares");
                return;
            }
            self->play_key_cycle += self->path.keys[self->play_key].hold * EDITOR_CAMPATH_CYCLES_PER_TICK;
            self->play_key++;
        }
        if( self->play_key >= self->path.key_count && self->play_cycle >= self->play_key_cycle )
        {
            char said[96];
            snprintf(said, sizeof(said), "Preview finished: %d keyframe%s in %d.%d s", self->path.key_count,
                self->path.key_count == 1 ? "" : "s", self->play_cycle * 2 / 100, self->play_cycle * 2 / 10 % 10);
            campath_preview_end(ui, self, said);
            return;
        }
        Editor_CamSimStep(&self->sim);
        self->play_cycle++;
    }
    if( !ui->app_services.world_eye_set(ui->app_services.user_data, &self->sim.eye) )
    {
        campath_preview_end(ui, self, "Preview stopped: the flight left the open squares");
        return;
    }
    if( ui->app_services.request_redraw )
        ui->app_services.request_redraw(ui->app_services.user_data);
}

/* ---- Send to play client ---------------------------------------------------------- */

static void
campath_send_start(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    const struct Editor_LiveModel* live = Editor_LivePanelModel(ui);

    self->sending = 0;
    if( !self->path_valid || self->path.key_count == 0 )
    {
        Editor_UiStatus(ui, "Send to play client refused: the path has no keyframe yet");
        return;
    }
    /* A play account outlives the link (a lane stopped, a server gone): ask
     * whether the cheat could go now, not whether it ever went. */
    if( !live || !live->play_account[0] || live->state != TORIRSMAPED_LIVE_ATTACHED )
    {
        Editor_UiStatus(ui, "Send to play client refused: no lane is live (%s); press RUN (Ctrl+Enter) first, then send again",
            live ? ToriRSMapEd_LiveStateName(live->state) : "no Live panel");
        return;
    }
    snprintf(self->send_account, sizeof(self->send_account), "%s", live->play_account);
    self->sending = 1;
    self->send_key = 0;
    self->send_ms = ui->chrome->now_ms;
    Editor_UiStatus(ui, "Sending %s to %s: a ::cam per keyframe, then ::camreset; stand the player near %d,%d first%s",
        self->path.name, self->send_account, self->path.keys[0].look_x, self->path.keys[0].look_z,
        campath_needs_unlock(ui, &self->path) ? " (::cam cannot unlock the pitch: a steep shot reads flatter)" : "");
}

static void
campath_send_tick(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    char line[128];

    if( !self->sending || ui->chrome->now_ms < self->send_ms )
        return;
    if( !self->path_valid || self->send_key > self->path.key_count )
    {
        self->sending = 0;
        return;
    }
    if( self->send_key == self->path.key_count )
    {
        (void)Editor_Live_Cheat(ui, self->send_account, "camreset");
        self->sending = 0;
        Editor_UiStatus(ui, "Sent %s to %s: %d keyframe%s and the reset", self->path.name, self->send_account,
            self->path.key_count, self->path.key_count == 1 ? "" : "s");
        return;
    }
    Editor_CamKeyCheat(&self->path.keys[self->send_key], line, sizeof(line));
    if( Editor_Live_Cheat(ui, self->send_account, line) <= 0 )
    {
        self->sending = 0;
        Editor_UiStatus(ui, "Send to play client stopped at keyframe %d: the live lane did not take the cheat (see the console)",
            self->send_key + 1);
        return;
    }
    /* The next keyframe a hold of game ticks later, as the proc's p_delay
     * would send it. */
    self->send_ms = ui->chrome->now_ms + (int64_t)self->path.keys[self->send_key].hold * 600;
    self->send_key++;
}

/* ---- the verbs: paths ------------------------------------------------------------- */

static void
campath_show(
    struct CamPathPanel* self,
    const char* name)
{
    snprintf(self->path_name, sizeof(self->path_name), "%s", name);
    self->selected = -1;
    campath_reload(self);
}

static void
campath_new(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    const char* name = ToriRSChrome_Text(ui->chrome, self->input_name);
    struct Editor_CamPath* path;
    struct Editor_CamPath probe;

    if( !name || !Editor_CamPathNameValid(name) )
    {
        Editor_UiStatus(ui, "New path refused: \"%.40s\" is not a path name; it names a proc: a lower-case letter, then "
                            "letters, digits and _ (under %d)",
            name ? name : "", EDITOR_CAMPATH_NAME_MAX);
        return;
    }
    if( !campath_editable(ui, self, "New path") )
        return;
    if( Editor_CamPathsGet(&self->file, name, &probe) )
    {
        Editor_UiStatus(ui, "New path refused: a path named %s exists; choose another name", name);
        return;
    }
    path = ToriMem_Alloc(ToriMem_Editor(), sizeof(*path));
    assert(path);
    Editor_CamPathInit(path, name);
    if( campath_write(ui, self, NULL, path, "new camera path") )
    {
        campath_show(self, name);
        Editor_UiStatus(ui, "New camera path %s: fly the World camera to the first framing and press Add keyframe", name);
    }
    ToriMem_Free(ToriMem_Editor(), path);
}

static void
campath_rename(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    const char* name = ToriRSChrome_Text(ui->chrome, self->input_name);
    struct Editor_CamPath* path = campath_edit_copy(ui, self, "Rename");
    char old[EDITOR_CAMPATH_NAME_MAX];

    if( !path )
        return;
    if( !name || !Editor_CamPathNameValid(name) )
        Editor_UiStatus(ui, "Rename refused: \"%.40s\" is not a path name (a lower-case letter, then letters, digits and _)",
            name ? name : "");
    else
    {
        snprintf(old, sizeof(old), "%s", path->name);
        snprintf(path->name, sizeof(path->name), "%s", name);
        if( campath_write(ui, self, old, path, "rename camera path") )
        {
            int const selected = self->selected;
            campath_show(self, name);
            self->selected = selected;
            Editor_UiStatus(ui, "Renamed camera path %s to %s: its proc is now %s_camera; copy the script again", old, name,
                name);
        }
    }
    ToriMem_Free(ToriMem_Editor(), path);
}

static void
campath_delete(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    struct Editor_CamPath* path = campath_edit_copy(ui, self, "Delete path");
    size_t length = 0;
    char* text;

    if( !path )
        return;
    if( Editor_CamPathsTextWithout(&self->file, path->name, NULL, 0, &length) )
    {
        text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
        assert(text);
        Editor_CamPathsTextWithout(&self->file, path->name, text, length + 1, &length);
        if( campath_write_text(ui, text, length, "delete camera path") )
        {
            Editor_UiStatus(ui, "Deleted camera path %s and its %d keyframe%s (Ctrl+Z brings it back)", path->name,
                path->key_count, path->key_count == 1 ? "" : "s");
            self->path_name[0] = '\0';
        }
        else
            Editor_UiStatus(ui, "Delete path refused: the authority did not take the edit (a read-only session?)");
        ToriMem_Free(ToriMem_Editor(), text);
        /* The refresh shows the first path left. */
        self->file_doc = NULL;
        (void)campath_refresh(ui, self);
    }
    ToriMem_Free(ToriMem_Editor(), path);
}

/** Coordinates and the Template square. */
static void
campath_set_coords(
    struct EditorUi* ui,
    struct CamPathPanel* self,
    int instance)
{
    struct Editor_CamPath* path = campath_edit_copy(ui, self, "Coordinates");
    const char* typed = ToriRSChrome_Text(ui->chrome, self->input_template);
    char square[16];

    if( !path )
    {
        self->built = 0;
        return;
    }
    if( instance )
    {
        /* The typed square; else the one the path stands in. */
        if( !campath_square_parse(typed, &path->origin_mx, &path->origin_mz) )
        {
            if( typed && typed[0] )
            {
                Editor_UiStatus(ui, "Template refused: \"%.20s\" is not a square; type the copied block's south-west square, "
                                    "like m16_101",
                    typed);
                ToriMem_Free(ToriMem_Editor(), path);
                self->built = 0;
                return;
            }
            if( path->key_count == 0 )
            {
                Editor_UiStatus(ui, "Instance-relative refused: type the template's south-west square in Template first "
                                    "(like m16_101), or add a keyframe and it is taken from that");
                ToriMem_Free(ToriMem_Editor(), path);
                self->built = 0;
                return;
            }
            path->origin_mx = path->keys[0].eye_x / 64;
            path->origin_mz = path->keys[0].eye_z / 64;
        }
    }
    path->instance = instance;
    campath_square_text(path->origin_mx, path->origin_mz, square, sizeof(square));
    if( campath_write(ui, self, path->name, path, "camera path coordinates") )
    {
        if( instance )
            Editor_UiStatus(ui, "%s is instance-relative: offsets from %s's south-west tile, and the proc takes the "
                                "instance's $handle",
                path->name, square);
        else
            Editor_UiStatus(ui, "%s is in world tiles: the proc plays where the path was framed", path->name);
    }
    self->built = 0;
    ToriMem_Free(ToriMem_Editor(), path);
}

static void
campath_set_frame(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    struct Editor_CamPath* path = campath_edit_copy(ui, self, "Wrap in a cutscene frame");

    if( !path )
    {
        self->built = 0;
        return;
    }
    path->frame = ToriRSChrome_Checked(ui->chrome, self->check_frame) ? 1 : 0;
    if( campath_write(ui, self, path->name, path, "camera path frame") )
        Editor_UiStatus(ui, "%s", path->frame ? "The proc now fades out, locks the HUD, plays, unlocks and fades in "
                                                "(general/scripts/cutscene_frame.rs2)"
                                              : "The proc is the bare camera moves: the caller frames them");
    self->built = 0;
    ToriMem_Free(ToriMem_Editor(), path);
}

/* ---- the verbs: keyframes ---------------------------------------------------------- */

/** Fit a keyframe to the World camera into `key` (its timing left alone). 0
 *  with a sentence. */
static int
campath_fit(
    struct EditorUi* ui,
    const char* verb,
    struct Editor_CamKey* key,
    struct Editor_CamFitNote* note)
{
    struct Editor_CamEye eye;
    enum Editor_CamFit fit;

    if( !campath_world(ui) || !ui->app_services.world_eye(ui->app_services.user_data, &eye) )
    {
        Editor_UiStatus(ui, "%s refused: this window has no World camera to take a framing from", verb);
        return 0;
    }
    fit = Editor_CamKeyFromEye(&eye, campath_level(ui), ui->app_services.world_ground, ui->app_services.user_data, key,
        note);
    if( fit == EDITOR_CAM_FIT_LOOKS_UP )
        Editor_UiStatus(ui, "%s refused: the camera looks above the horizon, which the game's scripted camera cannot; tilt "
                            "it down",
            verb);
    else if( fit != EDITOR_CAM_FIT_OK )
        Editor_UiStatus(ui, "%s refused: the camera is outside the open squares, so its height above the ground cannot be "
                            "measured; fly it over an open square",
            verb);
    return fit == EDITOR_CAM_FIT_OK;
}

/** Put the World camera where the game will stand for `key`, and say how
 *  far that is from where the map maker had it. */
static void
campath_snap(
    struct EditorUi* ui,
    const struct Editor_CamKey* key,
    const struct Editor_CamFitNote* note,
    char* out,
    size_t out_size)
{
    struct Editor_CamEye eye;

    out[0] = '\0';
    if( !campath_key_eye(ui, key, &eye) || !ui->app_services.world_eye_set(ui->app_services.user_data, &eye) )
        return;
    /* Short: the status line holds 159 bytes and the keyframe comes first. */
    if( note->steep )
        snprintf(out, out_size, "; straight down has no yaw in game: it looks one tile on");
    else if( note->eye_raised )
        snprintf(out, out_size, "; the eye was under the ground: raised onto it");
    else if( note->yaw_error > 2 || note->pitch_error > 2 )
        snprintf(out, out_size, "; framing snapped to tile centres: yaw %d, pitch %d off", note->yaw_error,
            note->pitch_error);
    else
        snprintf(out, out_size, "; the World shows the game's framing");
}

/** A name for the path Add keyframe makes when none is shown: what Name
 *  holds when that is a free path name, else the first free of cutscene,
 *  cutscene_2, ... (the proc is then cutscene_camera). */
static void
campath_fresh_name(
    struct EditorUi* ui,
    struct CamPathPanel* self,
    char out[EDITOR_CAMPATH_NAME_MAX])
{
    const char* typed = ToriRSChrome_Text(ui->chrome, self->input_name);
    struct Editor_CamPath* probe = ToriMem_Alloc(ToriMem_Editor(), sizeof(*probe));

    assert(probe);
    if( typed && Editor_CamPathNameValid(typed) && !Editor_CamPathsGet(&self->file, typed, probe) )
        snprintf(out, EDITOR_CAMPATH_NAME_MAX, "%s", typed);
    else
    {
        snprintf(out, EDITOR_CAMPATH_NAME_MAX, "cutscene");
        for( int n = 2; Editor_CamPathsGet(&self->file, out, probe); n++ )
            snprintf(out, EDITOR_CAMPATH_NAME_MAX, "cutscene_%d", n);
    }
    ToriMem_Free(ToriMem_Editor(), probe);
}

static void
campath_add(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    struct Editor_CamPath* path;
    struct Editor_CamFitNote note;
    struct Editor_CamKey key;
    char snapped[80];
    int made = 0;
    int at;

    if( !campath_editable(ui, self, "Add keyframe") )
        return;
    path = ToriMem_Alloc(ToriMem_Editor(), sizeof(*path));
    assert(path);
    if( self->path_valid )
        *path = self->path;
    else
    {
        /* No path yet: the first keyframe makes one, so a map maker frames
         * and presses before naming anything. */
        char name[EDITOR_CAMPATH_NAME_MAX];
        campath_fresh_name(ui, self, name);
        Editor_CamPathInit(path, name);
        made = 1;
    }
    if( path->key_count >= EDITOR_CAMPATH_KEYS_MAX )
    {
        Editor_UiStatus(ui, "Add keyframe refused: a path holds %d keyframes; start a second path for the rest",
            EDITOR_CAMPATH_KEYS_MAX);
        ToriMem_Free(ToriMem_Editor(), path);
        return;
    }
    memset(&key, 0, sizeof(key));
    Editor_CamSpeedApply(EDITOR_CAM_SPEED_MEDIUM, &key);
    if( !campath_fit(ui, "Add keyframe", &key, &note) )
    {
        ToriMem_Free(ToriMem_Editor(), path);
        return;
    }
    /* After the selected keyframe, else at the end. */
    at = self->selected >= 0 && self->selected < path->key_count ? self->selected + 1 : path->key_count;
    /* The first keyframe cuts (the scene opens on it); a later one glides
     * from the one before and holds until it has arrived and rested. */
    key.glide = at > 0;
    key.hold = CAMPATH_CUT_HOLD;
    if( key.glide )
    {
        struct Editor_CamEye from;
        int cycles = -1;
        if( campath_key_eye(ui, &path->keys[at - 1], &from) )
            cycles = Editor_CamGlideCycles(&from, &key, ui->app_services.world_ground, ui->app_services.user_data, 1,
                CAMPATH_GLIDE_MAX_CYCLES);
        if( cycles >= 0 )
            key.hold = Editor_CamCyclesToTicks(cycles) + CAMPATH_GLIDE_REST;
        if( key.hold > EDITOR_CAMPATH_HOLD_MAX )
            key.hold = EDITOR_CAMPATH_HOLD_MAX;
    }
    memmove(&path->keys[at + 1], &path->keys[at], sizeof(path->keys[0]) * (size_t)(path->key_count - at));
    path->keys[at] = key;
    path->key_count++;
    if( campath_write(ui, self, made ? NULL : path->name, path, "add camera keyframe") )
    {
        if( made )
            campath_show(self, path->name);
        campath_select(ui, self, at);
        campath_snap(ui, &key, &note, snapped, sizeof(snapped));
        Editor_UiStatus(ui, "Added keyframe %d (%s, hold %d): eye %d,%d +%d looks at %d,%d +%d%s", at + 1,
            key.glide ? "glide Medium" : "cut", key.hold, key.eye_x, key.eye_z, key.eye_height, key.look_x, key.look_z,
            key.look_height, snapped);
        if( made )
            Editor_UiLog(ui, "camera path: made path %s for it (its proc is %s_camera; Setup > Rename changes the name)",
                path->name, path->name);
    }
    ToriMem_Free(ToriMem_Editor(), path);
}

static void
campath_update(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    struct Editor_CamPath* path;
    struct Editor_CamFitNote note;
    char snapped[80];
    int const at = campath_picked(ui, self, "Update keyframe");

    if( at < 0 )
        return;
    path = campath_edit_copy(ui, self, "Update keyframe");
    if( !path )
        return;
    if( campath_fit(ui, "Update keyframe", &path->keys[at], &note)
        && campath_write(ui, self, path->name, path, "update camera keyframe") )
    {
        const struct Editor_CamKey* key = &path->keys[at];
        campath_snap(ui, key, &note, snapped, sizeof(snapped));
        Editor_UiStatus(ui, "Updated keyframe %d: eye %d,%d +%d looks at %d,%d +%d%s", at + 1, key->eye_x, key->eye_z,
            key->eye_height, key->look_x, key->look_z, key->look_height, snapped);
    }
    ToriMem_Free(ToriMem_Editor(), path);
}

static void
campath_goto(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    const struct Editor_CamKey* key;
    struct Editor_CamEye eye;
    int const at = campath_picked(ui, self, "Go to");

    if( at < 0 )
        return;
    key = &self->path.keys[at];
    if( !campath_world(ui) )
        Editor_UiStatus(ui, "Go to refused: this window has no World camera to put");
    else if( !campath_key_eye(ui, key, &eye) || !ui->app_services.world_eye_set(ui->app_services.user_data, &eye) )
        Editor_UiStatus(ui, "Go to refused: keyframe %d stands at %d,%d, outside the open squares; open m%d_%d first", at + 1,
            key->eye_x, key->eye_z, key->eye_x / 64, key->eye_z / 64);
    else
        Editor_UiStatus(ui, "The World camera is on keyframe %d: eye %d,%d +%d, pitch %d, yaw %d; re-frame and press Update "
                            "keyframe",
            at + 1, key->eye_x, key->eye_z, key->eye_height, eye.pitch, eye.yaw);
}

static void
campath_delete_key(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    struct Editor_CamPath* path;
    int const at = campath_picked(ui, self, "Delete keyframe");

    if( at < 0 )
        return;
    path = campath_edit_copy(ui, self, "Delete keyframe");
    if( !path )
        return;
    memmove(&path->keys[at], &path->keys[at + 1], sizeof(path->keys[0]) * (size_t)(path->key_count - at - 1));
    path->key_count--;
    if( campath_write(ui, self, path->name, path, "delete camera keyframe") )
    {
        campath_select(ui, self, at < path->key_count ? at : path->key_count - 1);
        Editor_UiStatus(ui, "Deleted keyframe %d of %s (Ctrl+Z brings it back)", at + 1, path->name);
    }
    ToriMem_Free(ToriMem_Editor(), path);
}

static void
campath_move_key(
    struct EditorUi* ui,
    struct CamPathPanel* self,
    int delta)
{
    const char* verb = delta < 0 ? "Move up" : "Move down";
    struct Editor_CamPath* path;
    struct Editor_CamKey swap;
    int const at = campath_picked(ui, self, verb);

    if( at < 0 )
        return;
    if( at + delta < 0 || at + delta >= self->path.key_count )
    {
        Editor_UiStatus(ui, "%s refused: keyframe %d is already the %s", verb, at + 1, delta < 0 ? "first" : "last");
        return;
    }
    path = campath_edit_copy(ui, self, verb);
    if( !path )
        return;
    swap = path->keys[at];
    path->keys[at] = path->keys[at + delta];
    path->keys[at + delta] = swap;
    if( campath_write(ui, self, path->name, path, "reorder camera keyframes") )
    {
        campath_select(ui, self, at + delta);
        Editor_UiStatus(ui, "Keyframe %d is now keyframe %d", at + 1, at + delta + 1);
    }
    ToriMem_Free(ToriMem_Editor(), path);
}

/**
 * The leg's controls into the selected keyframe: `changed` is the widget
 * that moved (a dropdown, or an input committed or left). 1 when it was
 * written.
 */
static int
campath_apply_leg(
    struct EditorUi* ui,
    struct CamPathPanel* self,
    int changed)
{
    struct Editor_CamPath* path;
    struct Editor_CamKey* key;
    const char* text;
    int ok = 1;
    int raised = 0;
    int const at = campath_picked(ui, self, "Retime");

    if( at < 0 )
    {
        self->filled = 0;
        return 0;
    }
    path = campath_edit_copy(ui, self, "Retime");
    if( !path )
    {
        self->filled = 0;
        return 0;
    }
    key = &path->keys[at];
    if( changed == self->dropdown_move )
        key->glide = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_move) == 1;
    else if( changed == self->dropdown_speed )
    {
        int const speed = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_speed);
        if( speed >= 0 && speed < EDITOR_CAM_SPEED_PRESETS )
            Editor_CamSpeedApply(speed, key);
        else
        {
            Editor_UiStatus(ui, "Custom: type the pairs in Eye rate and Look rate (flat rate, then per-1000 of what is left)");
            ok = 0;
        }
    }
    else if( changed == self->input_eye_rate || changed == self->input_look_rate )
    {
        int const eye = changed == self->input_eye_rate;
        text = ToriRSChrome_Text(ui->chrome, changed);
        if( campath_rates_parse(text, eye ? &key->move_rate : &key->look_rate, eye ? &key->move_rate2 : &key->look_rate2) )
            key->glide = 1;
        else
        {
            Editor_UiStatus(ui, "%s refused: \"%.20s\" is not a glide's pair; type two numbers like 5,5: the first 0..255, "
                                "the second 0..99 (100 and up is a cut)",
                eye ? "Eye rate" : "Look rate", text ? text : "");
            ok = 0;
        }
    }
    else
    {
        char tail = '\0';
        int hold = 0;
        assert(changed == self->input_hold);
        text = ToriRSChrome_Text(ui->chrome, changed);
        if( text && sscanf(text, " %d %c", &hold, &tail) == 1 && hold >= 1 && hold <= EDITOR_CAMPATH_HOLD_MAX )
            key->hold = hold;
        else
        {
            Editor_UiStatus(ui, "Hold refused: \"%.20s\" is not a count of game ticks; type 1..%d (a tick is 0.6 s)",
                text ? text : "", EDITOR_CAMPATH_HOLD_MAX);
            ok = 0;
        }
    }
    /* A slower move that no longer arrives inside its hold would be cut off
     * by the next keyframe: the hold grows to fit it, as Add keyframe sized
     * it. A Hold typed by hand is the map maker's (the row under the list
     * says when it cuts the glide). Keyframe 1 glides from the player's own
     * camera, which nobody can measure here. */
    if( ok && changed != self->input_hold && key->glide && at > 0 && campath_world(ui) )
    {
        struct Editor_CamEye from;
        int cycles = -1;
        if( campath_key_eye(ui, &path->keys[at - 1], &from) )
            cycles = Editor_CamGlideCycles(&from, key, ui->app_services.world_ground, ui->app_services.user_data, 1,
                CAMPATH_GLIDE_MAX_CYCLES);
        if( cycles >= 0 && Editor_CamCyclesToTicks(cycles) > key->hold )
        {
            key->hold = Editor_CamCyclesToTicks(cycles) + CAMPATH_GLIDE_REST;
            if( key->hold > EDITOR_CAMPATH_HOLD_MAX )
                key->hold = EDITOR_CAMPATH_HOLD_MAX;
            raised = 1;
        }
    }
    if( ok && memcmp(key, &self->path.keys[at], sizeof(*key)) != 0 )
    {
        ok = campath_write(ui, self, path->name, path, "retime camera keyframe");
        if( ok )
        {
            char row[TORIRS_CHROME_INPUT_MAX];
            Editor_CamKeyRowText(&path->keys[at], at, row, sizeof(row));
            Editor_UiStatus(ui, "Keyframe %s%s", row, raised ? "; hold raised so the glide arrives" : "");
        }
    }
    /* Whatever happened, the controls say what the keyframe now holds. */
    self->filled = 0;
    ToriMem_Free(ToriMem_Editor(), path);
    return ok;
}

/** A typed field that no longer says what the keyframe holds, and is not
 *  being typed in: applied, so a value is never lost for want of Enter. */
static void
campath_apply_left_fields(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    int const focused = ToriRSChrome_InputFocused(ui->chrome);
    const struct
    {
        int widget;
        const char* filled;
    } fields[3] = {
        { self->input_eye_rate, self->filled_eye_rate },
        { self->input_look_rate, self->filled_look_rate },
        { self->input_hold, self->filled_hold },
    };

    /* The Template square, the same way: typed and left, it is the path's. */
    {
        const char* text = ToriRSChrome_Text(ui->chrome, self->input_template);
        if( self->built && self->path_valid && focused != self->input_template && text
            && strcmp(text, self->filled_template) != 0 )
        {
            if( text[0] )
                campath_set_coords(ui, self, 1);
            else
                self->built = 0;
            return;
        }
    }
    if( !self->filled || !self->path_valid || self->selected < 0 )
        return;
    for( int i = 0; i < 3; i++ )
    {
        const char* text = ToriRSChrome_Text(ui->chrome, fields[i].widget);
        if( focused != fields[i].widget && text && strcmp(text, fields[i].filled) != 0 )
        {
            (void)campath_apply_leg(ui, self, fields[i].widget);
            return;
        }
    }
}

static void
campath_copy(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    if( !self->path_valid || self->path.key_count == 0 )
    {
        Editor_UiStatus(ui, "Copy script refused: the path has no keyframe yet; fly the camera to a framing and press Add "
                            "keyframe");
        return;
    }
    campath_script_render(ui, self);
    if( !PlatformClipboard_SetText(self->script) )
    {
        Editor_UiStatus(ui, "Copy script refused: the system clipboard did not take it (the proc is in the panel's view)");
        return;
    }
    Editor_UiStatus(ui, "Copied [proc,%s_camera] (%d lines%s): paste it into your quest's .rs2 and call ~%s_camera%s;",
        self->path.name, self->script_line_count, self->script_unlock ? ", pitch unlocked" : "", self->path.name,
        self->path.instance ? "($handle)" : "");
    for( int i = 0; self->path.instance && i < self->path.key_count; i++ )
        if( self->path.keys[i].eye_x < self->path.origin_mx * 64 || self->path.keys[i].eye_z < self->path.origin_mz * 64
            || self->path.keys[i].look_x < self->path.origin_mx * 64 || self->path.keys[i].look_z < self->path.origin_mz * 64 )
        {
            Editor_UiLog(ui, "camera path: keyframe %d is west or south of m%d_%d's south-west tile: its offset is negative, "
                             "which no instance holds; set Template to the block's south-west square",
                i + 1, self->path.origin_mx, self->path.origin_mz);
            break;
        }
}

/* ---- the module ----------------------------------------------------------------- */

static void
campath_chords(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    self->chord_serial = ui->hotkeys.serial;
    edp_button_chord(ui, self->button_add, EDITOR_HK_CAMPATH_ADD);
    edp_button_chord(ui, self->button_update, EDITOR_HK_CAMPATH_UPDATE);
    edp_button_chord(ui, self->button_goto, EDITOR_HK_CAMPATH_GOTO);
    edp_button_chord(ui, self->button_preview, EDITOR_HK_CAMPATH_PREVIEW);
    edp_button_chord(ui, self->button_copy, EDITOR_HK_CAMPATH_COPY);
    edp_button_chord(ui, self->button_delete_key, EDITOR_HK_CAMPATH_DELETE_KEY);
    edp_button_chord(ui, self->button_up, EDITOR_HK_CAMPATH_MOVE_UP);
    edp_button_chord(ui, self->button_down, EDITOR_HK_CAMPATH_MOVE_DOWN);
    edp_button_chord(ui, self->button_send, EDITOR_HK_CAMPATH_SEND);
}

/** A window's box where it stands now: its position (moved since the last
 *  Build when the layout flew it out this tick) at its last measured size.
 *  0 before its first Build. */
static int
campath_window_box(
    const struct ToriRSChrome* chrome,
    int panel,
    struct ToriRSChromeRect* out)
{
    const struct ToriRSChromePanel* p = &chrome->panels[panel];

    out->x = p->x;
    out->y = p->y;
    out->w = p->fixed_w > 0 ? p->fixed_w : p->last_rect.w;
    out->h = p->last_rect.h;
    return out->w > 0 && out->h > 0;
}

/**
 * Camera path and Triggers are used together -- Copy script here, New script
 * file and Open in editor there -- and both fly out against the column at
 * its full height, so whichever opened second covered the other whole, and
 * a press on Copy script answered "Triggers is under that point"
 * (2026-11-11). While both are shown, neither was placed or maximised by
 * hand and they overlap, this panel moves against Triggers' side
 * (Editor_UiPanelOpenBeside), once per place Triggers takes. A few fields a
 * frame: no tree walk.
 */
static void
campath_beside_triggers(
    struct EditorUi* ui,
    struct CamPathPanel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    struct EditorUiPanel* triggers = Editor_UiFindPanel(ui, "triggers");
    const struct ToriRSChromePanel* mine = &chrome->panels[self->panel];
    struct ToriRSChromeRect a;
    struct ToriRSChromeRect b;
    int t;

    /* A harness that registers this panel alone has no Triggers. */
    if( !triggers || triggers->chrome_panel < 0 )
        return;
    t = triggers->chrome_panel;
    if( !chrome->panels[t].visible || chrome->panels[t].user_placed || chrome->panels[t].maximised
        || mine->user_placed || mine->maximised )
        return;
    if( !campath_window_box(chrome, self->panel, &a) || !campath_window_box(chrome, t, &b) )
        return;
    if( a.x >= b.x + b.w || b.x >= a.x + a.w || a.y >= b.y + b.h || b.y >= a.y + a.h )
    {
        self->beside_w = 0;
        return;
    }
    /* Already moved for this place (on a surface too narrow for both, the
     * move could not clear it: once is enough). */
    if( self->beside_x == b.x && self->beside_y == b.y && self->beside_w == b.w )
        return;
    self->beside_x = b.x;
    self->beside_y = b.y;
    self->beside_w = b.w;
    Editor_UiPanelOpenBeside(ui, "campath", "triggers");
}

static void
campath_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct CamPathPanel* self = opaque;
    int first;
    int shown;

    /* A flight and a send go on with the panel closed or behind a tab. */
    campath_preview_tick(ui, self);
    campath_send_tick(ui, self);
    if( self->chord_serial != ui->hotkeys.serial )
        campath_chords(ui, self);
    /* Open when the editor opens again only if it was open. */
    self->wanted = ui->chrome->panels[self->panel].visible;
    if( !self->wanted )
        return;
    campath_beside_triggers(ui, self);
    (void)campath_refresh(ui, self);

    /* The list's selection is the selected keyframe: a click, an arrow key.
     * A field typed for the old one is applied to it first. */
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_keys);
        int const count = self->path_valid ? self->path.key_count : 0;
        int const wanted = row >= 0 && row < count ? row : -1;
        if( wanted != self->selected && self->built && self->built_serial == self->serial )
        {
            campath_apply_left_fields(ui, self);
            self->selected = wanted < (self->path_valid ? self->path.key_count : 0) ? wanted : -1;
            self->serial++;
        }
    }
    campath_build(ui, self);
    campath_apply_left_fields(ui, self);
    campath_fill(ui, self);
    campath_leg_row(ui, self);
    campath_script_render(ui, self);
    campath_script_fill(ui, self);

    if( self->path_valid && ToriRSChrome_VListWindow(ui->chrome, self->list_keys, &first, &shown) )
        for( int r = first; r < first + shown && r < self->path.key_count; r++ )
        {
            char text[TORIRS_CHROME_INPUT_MAX];
            Editor_CamKeyRowText(&self->path.keys[r], r, text, sizeof(text));
            ToriRSChrome_VListSetRow(ui->chrome, self->list_keys, r, text);
        }
}

static int
campath_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct CamPathPanel* self = opaque;

    if( widget == self->dropdown_path )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        campath_preview_end(ui, self, "Preview stopped");
        if( choice >= 0 && choice < self->name_count && self->file_ok && Editor_CamPathsCount(&self->file) > 0 )
        {
            campath_show(self, self->names[choice]);
            ToriRSChrome_VListSetSelected(ui->chrome, self->list_keys, -1);
        }
        return 1;
    }
    if( widget == self->tabs || widget == self->list_keys || widget == self->input_name || widget == self->check_show
        || widget == self->code_script )
        return 1;
    if( widget == self->button_stop )
    {
        campath_preview_end(ui, self, "Preview stopped");
        return 1;
    }
    if( widget == self->button_preview )
    {
        campath_preview_start(ui, self);
        return 1;
    }
    if( widget == self->button_send )
    {
        campath_send_start(ui, self);
        return 1;
    }
    if( widget == self->button_copy )
    {
        campath_copy(ui, self);
        return 1;
    }
    if( widget == self->button_goto )
    {
        campath_preview_end(ui, self, NULL);
        campath_goto(ui, self);
        return 1;
    }
    if( widget != self->button_new && widget != self->button_rename && widget != self->button_delete
        && widget != self->dropdown_coords && widget != self->input_template && widget != self->check_frame
        && widget != self->button_add && widget != self->button_update && widget != self->button_delete_key
        && widget != self->button_up && widget != self->button_down && widget != self->dropdown_move
        && widget != self->dropdown_speed && widget != self->input_eye_rate && widget != self->input_look_rate
        && widget != self->input_hold )
        return 0;

    /* Every edit ends a flight first: the camera it would read is the
     * simulated one. */
    campath_preview_end(ui, self, NULL);
    if( widget == self->button_new )
        campath_new(ui, self);
    else if( widget == self->button_rename )
        campath_rename(ui, self);
    else if( widget == self->button_delete )
        campath_delete(ui, self);
    else if( widget == self->dropdown_coords )
        campath_set_coords(ui, self, ToriRSChrome_DropdownSelected(ui->chrome, widget) == 1);
    else if( widget == self->input_template )
        campath_set_coords(ui, self, 1);
    else if( widget == self->check_frame )
        campath_set_frame(ui, self);
    else if( widget == self->button_add )
        campath_add(ui, self);
    else if( widget == self->button_update )
        campath_update(ui, self);
    else if( widget == self->button_delete_key )
        campath_delete_key(ui, self);
    else if( widget == self->button_up )
        campath_move_key(ui, self, -1);
    else if( widget == self->button_down )
        campath_move_key(ui, self, 1);
    else
        (void)campath_apply_leg(ui, self, widget);
    return 1;
}

static void
campath_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct CamPathPanel* self = opaque;

    if( visible && !self->wanted )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    if( !visible )
        campath_preview_end(ui, self, NULL);
    self->built = 0;
}

static void
campath_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct CamPathPanel* self = opaque;

    (void)ui;
    Editor_CamPathsFree(&self->file);
    ToriMem_Free(ToriMem_Editor(), self);
}

static void
campath_tip(
    struct EditorUi* ui,
    int widget,
    const char* tip)
{
    ToriRSChrome_SetTooltip(ui->chrome, widget, tip);
}

void
Editor_CamPath_Register(struct EditorUi* ui)
{
    static const char* speeds[EDITOR_CAM_SPEED_PRESETS + 1];
    struct CamPathPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    Editor_CamPathsInit(&self->file);
    self->selected = -1;
    /* 420 px, Triggers' width: a keyframe row with both heights fits (at 360
     * the look height was cut off the right edge). */
    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 40),
        edp_px(ui, 90), edp_px(ui, 420), "Camera path");
    assert(self->panel >= 0);

    /* Above the tabs, always: what the path is and which one is shown. */
    self->row_title = ToriRSChrome_Label(chrome, self->panel, "");
    self->dropdown_path = ToriRSChrome_Dropdown(chrome, self->panel, "Path", NULL, 0, -1);
    campath_tip(ui, self->dropdown_path, "the path shown; Setup makes, renames and deletes them");
    self->tabs = ToriRSChrome_Tabs(chrome, self->panel, k_tabs, CAMPATH_TAB_COUNT, CAMPATH_TAB_KEYS);

    /* Keyframes: the loop a path is made in -- frame, add, time, watch. */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, CAMPATH_TAB_KEYS);
    self->list_keys = ToriRSChrome_VList(chrome, self->panel, "Keyframe list", CAMPATH_KEY_ROWS, 0);
    assert(self->list_keys >= 0);
    campath_tip(ui, self->list_keys, "move (cut or a glide's speed), hold ticks, eye, look");
    self->button_add = ToriRSChrome_Button(chrome, self->panel, "Add keyframe");
    campath_tip(ui, self->button_add, "a keyframe of what the World camera shows now");
    self->button_update = ToriRSChrome_Button(chrome, self->panel, "Update keyframe");
    campath_tip(ui, self->button_update, "the selected keyframe takes the World camera's framing");
    self->button_goto = ToriRSChrome_Button(chrome, self->panel, "Go to");
    campath_tip(ui, self->button_goto, "put the World camera on the selected keyframe");
    self->button_delete_key = ToriRSChrome_Button(chrome, self->panel, "Delete keyframe");
    self->button_up = ToriRSChrome_Button(chrome, self->panel, "Move up");
    campath_tip(ui, self->button_up, "the selected keyframe plays one earlier");
    self->button_down = ToriRSChrome_Button(chrome, self->panel, "Move down");
    campath_tip(ui, self->button_down, "the selected keyframe plays one later");
    self->row_key = ToriRSChrome_Label(chrome, self->panel, "");
    self->dropdown_move = ToriRSChrome_Dropdown(chrome, self->panel, "Move", k_moves, 2, 0);
    campath_tip(ui, self->dropdown_move, "Cut: there at once. Glide: it eases there");
    for( int i = 0; i <= EDITOR_CAM_SPEED_PRESETS; i++ )
        speeds[i] = Editor_CamSpeedName(i);
    self->dropdown_speed = ToriRSChrome_Dropdown(chrome, self->panel, "Speed", speeds, EDITOR_CAM_SPEED_PRESETS + 1, 1);
    campath_tip(ui, self->dropdown_speed, "a glide's rates: 2,2 / 5,5 / 10,10, or your own below");
    self->input_eye_rate = ToriRSChrome_TextInput(chrome, self->panel, "Eye rate", "");
    campath_tip(ui, self->input_eye_rate, "cam_moveto's pair: units a cycle, per-1000 of the rest");
    self->input_look_rate = ToriRSChrome_TextInput(chrome, self->panel, "Look rate", "");
    campath_tip(ui, self->input_look_rate, "cam_lookat's pair: angle units a cycle, per-1000");
    self->input_hold = ToriRSChrome_TextInput(chrome, self->panel, "Hold (ticks)", "");
    campath_tip(ui, self->input_hold, "game ticks (0.6 s) until the next keyframe starts");
    self->button_preview = ToriRSChrome_Button(chrome, self->panel, "Preview");
    campath_tip(ui, self->button_preview, "fly the World camera along the path, as the game will");
    self->button_stop = ToriRSChrome_Button(chrome, self->panel, "Stop");
    ToriRSChrome_SetDisabled(chrome, self->button_stop, 1);

    /* Setup: what a path is called and how its proc is written. */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, CAMPATH_TAB_SETUP);
    self->input_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    campath_tip(ui, self->input_name, "it names the proc: a letter, then letters, digits, _");
    self->button_new = ToriRSChrome_Button(chrome, self->panel, "New path");
    self->button_rename = ToriRSChrome_Button(chrome, self->panel, "Rename");
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete path");
    campath_tip(ui, self->button_delete, "the path and its keyframes go; Ctrl+Z brings them back");
    self->dropdown_coords = ToriRSChrome_Dropdown(chrome, self->panel, "Coordinates", k_coords, 2, 0);
    campath_tip(ui, self->dropdown_coords, "Instance-relative plays inside a runtime map instance");
    self->input_template = ToriRSChrome_TextInput(chrome, self->panel, "Template", "");
    campath_tip(ui, self->input_template, "the copied block's south-west square, like m16_101");
    self->check_frame = ToriRSChrome_Checkbox(chrome, self->panel, "Wrap in a cutscene frame", 0);
    campath_tip(ui, self->check_frame, "fade out, lock, HUD lock; then unlock and fade in");
    self->check_show = ToriRSChrome_Checkbox(chrome, self->panel, "Show the path in the World", 1);
    campath_tip(ui, self->check_show, "the eye's track, each keyframe and where it looks");

    /* Script: the proc, in the chrome's read-only multi-line view (as the
     * Triggers panel shows a snippet), and the two ways out of the editor. */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, CAMPATH_TAB_SCRIPT);
    self->code_script = ToriRSChrome_Code(chrome, self->panel, "", CAMPATH_SCRIPT_ROWS, 0);
    assert(self->code_script >= 0);
    self->button_copy = ToriRSChrome_Button(chrome, self->panel, "Copy script");
    campath_tip(ui, self->button_copy, "the proc above to the clipboard: paste it in your .rs2");
    ToriRSChrome_SetDisabled(chrome, self->button_copy, 1);
    self->button_send = ToriRSChrome_Button(chrome, self->panel, "Send to play client");
    campath_tip(ui, self->button_send, "play it on the live play account now (::cam cheats)");
    ToriRSChrome_PanelBeginTab(chrome, self->panel, -1);

    /* A column of commands: each plate takes the row, so a caption and the
     * keycap it wears once bound both fit. */
    {
        int const buttons[] = { self->button_add, self->button_update, self->button_goto, self->button_delete_key,
            self->button_up, self->button_down, self->button_preview, self->button_stop, self->button_new,
            self->button_rename, self->button_delete, self->button_copy, self->button_send };
        for( size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++ )
            ToriRSChrome_ButtonSetFill(chrome, buttons[i], 1);
    }
    campath_chords(ui, self);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "campath";
    desc.title = "Camera path";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = campath_tick;
    desc.activate = campath_activate;
    desc.set_visible = campath_set_visible;
    desc.destroy = campath_destroy;
    Editor_UiAddPanel(ui, &desc);
}

/* ---- entry points ------------------------------------------------------------------ */

int
Editor_CamPath_Hotkey(
    struct EditorUi* ui,
    int action)
{
    struct CamPathPanel* self;

    assert(ui);
    assert(action >= EDITOR_HK_CAMPATH_ADD);
    assert(action <= EDITOR_HK_CAMPATH_SEND);
    self = campath_self(ui);
    if( !self )
    {
        Editor_UiStatus(ui, "%s refused: this window has no Camera path panel", Editor_HotkeyInfo(action)->name);
        return 1;
    }
    /* The verbs read the shown path: the panel's own view of the document. */
    (void)campath_refresh(ui, self);
    if( action == EDITOR_HK_CAMPATH_PREVIEW )
    {
        if( self->playing )
            campath_preview_end(ui, self, "Preview stopped");
        else
            campath_preview_start(ui, self);
        return 1;
    }
    /* Copy and Send read the path, not the camera: a flight goes on. */
    if( action == EDITOR_HK_CAMPATH_COPY )
    {
        campath_copy(ui, self);
        return 1;
    }
    if( action == EDITOR_HK_CAMPATH_SEND )
    {
        campath_send_start(ui, self);
        return 1;
    }
    campath_preview_end(ui, self, NULL);
    if( action == EDITOR_HK_CAMPATH_ADD )
        campath_add(ui, self);
    else if( action == EDITOR_HK_CAMPATH_UPDATE )
        campath_update(ui, self);
    else if( action == EDITOR_HK_CAMPATH_GOTO )
        campath_goto(ui, self);
    else if( action == EDITOR_HK_CAMPATH_DELETE_KEY )
        campath_delete_key(ui, self);
    else if( action == EDITOR_HK_CAMPATH_MOVE_UP )
        campath_move_key(ui, self, -1);
    else
    {
        assert(action == EDITOR_HK_CAMPATH_MOVE_DOWN);
        campath_move_key(ui, self, 1);
    }
    return 1;
}

int
Editor_CamPath_Marks(
    struct EditorUi* ui,
    struct Editor_CamPathMark* out,
    int max)
{
    struct CamPathPanel* self;
    int count = 0;

    assert(ui);
    assert(out);
    assert(max > 0);
    self = campath_self(ui);
    /* Not during a flight: the marks would stand in the shot being judged. */
    if( !self || !self->path_valid || self->playing || !campath_world(ui) || !ui->chrome->panels[self->panel].visible
        || !ToriRSChrome_Checked(ui->chrome, self->check_show) )
        return 0;
    for( int i = 0; i < self->path.key_count && count < max; i++ )
    {
        const struct Editor_CamKey* key = &self->path.keys[i];
        struct Editor_CamPathMark* mark = &out[count];
        struct Editor_CamEye eye;

        if( !Editor_CamKeyEye(key, ui->app_services.world_ground, ui->app_services.user_data, &eye)
            || !Editor_CamKeyLookPoint(key, ui->app_services.world_ground, ui->app_services.user_data, &mark->look_x,
                &mark->look_y, &mark->look_z)
            || !ui->app_services.world_ground(ui->app_services.user_data, eye.x, eye.z, key->level, &mark->ground_y) )
            continue;
        mark->eye_x = eye.x;
        mark->eye_y = eye.y;
        mark->eye_z = eye.z;
        mark->index = i;
        mark->selected = i == self->selected;
        mark->glide = key->glide;
        count++;
    }
    return count;
}

int
Editor_CamPath_Widgets(
    struct EditorUi* ui,
    struct Editor_CamPathWidgets* out)
{
    const struct CamPathPanel* self;

    assert(ui);
    assert(out);
    self = campath_self(ui);
    if( !self )
        return 0;
    out->panel = self->panel;
    out->row_title = self->row_title;
    out->dropdown_path = self->dropdown_path;
    out->tabs = self->tabs;
    out->input_name = self->input_name;
    out->button_new = self->button_new;
    out->button_rename = self->button_rename;
    out->button_delete = self->button_delete;
    out->dropdown_coords = self->dropdown_coords;
    out->input_template = self->input_template;
    out->check_frame = self->check_frame;
    out->list_keys = self->list_keys;
    out->button_add = self->button_add;
    out->button_update = self->button_update;
    out->button_goto = self->button_goto;
    out->button_delete_key = self->button_delete_key;
    out->button_up = self->button_up;
    out->button_down = self->button_down;
    out->row_key = self->row_key;
    out->dropdown_move = self->dropdown_move;
    out->dropdown_speed = self->dropdown_speed;
    out->input_eye_rate = self->input_eye_rate;
    out->input_look_rate = self->input_look_rate;
    out->input_hold = self->input_hold;
    out->check_show = self->check_show;
    out->button_preview = self->button_preview;
    out->button_stop = self->button_stop;
    out->button_send = self->button_send;
    out->code_script = self->code_script;
    out->button_copy = self->button_copy;
    return 1;
}

const char*
Editor_CamPath_Script(struct EditorUi* ui)
{
    struct CamPathPanel* self;

    assert(ui);
    self = campath_self(ui);
    if( !self )
        return "";
    (void)campath_refresh(ui, self);
    campath_script_render(ui, self);
    return self->script;
}

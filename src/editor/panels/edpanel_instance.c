/*
 * The instance helper (design section 13; workbench 6.5). One modal, opened
 * from the picker's detail pane (Instance...), that does what was New
 * square four times and Copy to by hand:
 *
 *   Source       the picker's selection (shift-clicked squares, else the
 *                focus), the open set, or a collection of the AREAS
 *                document; what to copy (terrain, locs, spawns, areas) and
 *                which levels.
 *   Destination  typed ("m16_101", the box's south-west square) or Find
 *                free..., the lowest free rectangle of the source's size in
 *                instance space with no square in any storage
 *                (Editor_InstanceFindFree), marked in the picker.
 *   Storage      maps/ or a lane's ported/<lane>/maps, as New square.
 *   Name         the .wma's area, the trigger's directory, a collection.
 *   Map area     worldmap/areas/<name>.wma, or none (hidden).
 *   Entry        a teleport trigger, a runtime map instance (only when the
 *                lane's server reports TORIRSMAPED_CAP_MAP_INSTANCE, which
 *                today none does: the chip is disabled and says why), none;
 *                the trigger's subject loc and its landing coord.
 *
 * Create is ONE undo step on the authority: a stroke holding a DOC_CREATE
 * per square (the copy template, TORIRSMAPED_DOC_CREATE_IN_STROKE), the
 * commands that trim the copy to what was asked, a SPAWN batch rebased to
 * the new absolute tiles, the areas rebased and a collection naming the
 * squares, the .wma and the .rs2 as TEXT_SPLICE syncs (builder_text.h, the
 * builders' text document path). Nothing is written until Save. Every
 * refusal says why in the dialog's check row and the status bar.
 */

#include "edpanel_instance.h"

#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/builder/builder_text.h"
#include "editor/editor.h"
#include "editor/editor_areas.h"
#include "editor/editor_doc.h"
#include "editor/editor_wma.h"
#include "edpanel_ops.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The planner's instance space is the catalog's (the atlas flags its rows
 * TORIRSMAPED_SQUARE_INSTANCE by the same bounds). */
_Static_assert(EDITOR_INSTANCE_X0 == TORIRSMAPED_INSTANCE_X0, "instance space x0");
_Static_assert(EDITOR_INSTANCE_X1 == TORIRSMAPED_INSTANCE_X1, "instance space x1");
_Static_assert(EDITOR_INSTANCE_Z0 == TORIRSMAPED_INSTANCE_Z0, "instance space z0");
_Static_assert(EDITOR_INSTANCE_Z1 == TORIRSMAPED_INSTANCE_Z1, "instance space z1");

#define INSTANCE_SOURCE_ROWS 24
#define INSTANCE_STORAGE_ROWS 34
/** Spawn rows one Create copies. */
#define INSTANCE_SPAWNS_MAX 1024

enum
{
    INSTANCE_GROUP_MAP = 31,
    INSTANCE_GROUP_ENTRY = 32,
};

static const char* const k_level_rows[5] = { "Levels 0..3", "Level 0 only", "Level 1 only", "Level 2 only",
    "Level 3 only" };

struct InstancePanel
{
    int panel;
    int row_intro;
    int dropdown_source;
    int row_source;
    int check_terrain;
    int check_locs;
    int check_spawns;
    int check_areas;
    int dropdown_levels;
    int input_dest;
    int button_find;
    int row_free;
    int dropdown_storage;
    int input_lane;
    int input_name;
    int radio_map[2];
    int radio_entry[3];
    int input_loc;
    int input_coord;
    int row_check;
    int button_create;
    int button_cancel;

    /** Source rows: 0 the selection, 1 the open set, then collections. */
    char source_labels[INSTANCE_SOURCE_ROWS][TORIRS_CHROME_LABEL_MAX];
    const char* source_rows[INSTANCE_SOURCE_ROWS];
    char source_collection[INSTANCE_SOURCE_ROWS][EDITOR_AREA_NAME_MAX];
    int source_count;

    char storage_labels[INSTANCE_STORAGE_ROWS][TORIRS_CHROME_LABEL_MAX];
    const char* storage_rows[INSTANCE_STORAGE_ROWS];
    int storage_index[INSTANCE_STORAGE_ROWS];
    int storage_count;

    int shown;
    struct Editor_InstanceForm checked;
    int checked_valid;
    int ok;
    /** The last Create's tally, for the status line and the tests. */
    int created_squares;
    int created_spawns;
};

static struct InstancePanel*
instance_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "instance");
}

/* ---- the form, pure ------------------------------------------------------------- */

void
Editor_InstanceDisplayName(
    const char* name,
    char* out,
    size_t out_size)
{
    size_t i = 0;

    assert(name);
    assert(out);
    assert(out_size > 0);
    for( ; name[i] && i + 1 < out_size; i++ )
    {
        char c = name[i] == '_' ? ' ' : name[i];
        if( i == 0 && c >= 'a' && c <= 'z' )
            c = (char)(c - 'a' + 'A');
        out[i] = c;
    }
    out[i] = '\0';
}

static const char*
instance_storage_label(const char* storage)
{
    return (storage[0] == '\0' || strcmp(storage, "maps") == 0) ? "maps/" : storage;
}

int
Editor_InstanceValidate(
    const struct Editor_Picker* picker,
    const struct Editor_InstanceForm* form,
    struct Editor_InstanceCheck* check)
{
    const char* keys[EDITOR_INSTANCE_SQUARES_MAX];
    struct Editor_InstancePlan* plan;
    int storage_index = -1;

    assert(picker);
    assert(form);
    assert(check);

    memset(check, 0, sizeof(*check));
    plan = &check->plan;

    /* The source. */
    if( form->source_count <= 0 )
    {
        snprintf(check->message, sizeof(check->message),
            "pick the source squares in the picker first (Shift+click adds one)");
        return 0;
    }
    if( form->source_count > EDITOR_INSTANCE_SQUARES_MAX )
    {
        snprintf(check->message, sizeof(check->message), "%d source squares: an instance copies at most %d",
            form->source_count, EDITOR_INSTANCE_SQUARES_MAX);
        return 0;
    }
    for( int i = 0; i < form->source_count; i++ )
        keys[i] = form->sources[i];
    if( !Editor_InstancePlanInit(plan, keys, form->source_count) )
    {
        snprintf(check->message, sizeof(check->message), "the source names a square twice, or not a square");
        return 0;
    }
    assert(form->level_lo >= 0);
    assert(form->level_hi < EDITOR_SQUARE_LEVELS);
    assert(form->level_lo <= form->level_hi);
    plan->level_lo = form->level_lo;
    plan->level_hi = form->level_hi;

    /* The destination: every square it makes free in every storage. */
    if( form->dst_x < 0 || form->dst_z < 0 )
    {
        snprintf(check->message, sizeof(check->message), "type the destination (m16_101) or press Find free...");
        return 0;
    }
    if( form->dst_x + plan->w > 256 || form->dst_z + plan->h > 256 )
    {
        snprintf(check->message, sizeof(check->message), "a %dx%d box at m%d_%d leaves the world (0..255)", plan->w,
            plan->h, form->dst_x, form->dst_z);
        return 0;
    }
    plan->dst_x0 = form->dst_x;
    plan->dst_z0 = form->dst_z;
    for( int i = 0; i < plan->count; i++ )
    {
        int x;
        int z;
        int taken;
        Editor_InstanceDest(plan, i, &x, &z);
        taken = Editor_PickerFind(picker, x, z, -1);
        if( taken >= 0 )
        {
            const struct Editor_PickerSquare* row = &picker->squares[taken];
            const char* area =
                row->primary_area >= 0 ? picker->areas[row->primary_area].display_name : "unmapped";
            snprintf(check->message, sizeof(check->message), "m%d_%d is taken (%s, %s): pick another destination", x,
                z, instance_storage_label(picker->storages[row->storage]), area[0] ? area : "unmapped");
            return 0;
        }
    }

    /* The storage. */
    if( form->storage < 0 )
    {
        if( !Editor_NewSquareLaneValid(form->new_lane) )
        {
            snprintf(check->message, sizeof(check->message), "name the new lane: letters, digits, '_' or '-'");
            return 0;
        }
        snprintf(check->storage, sizeof(check->storage), "ported/%s/maps", form->new_lane);
        storage_index = Editor_PickerStorageIndex(picker, check->storage);
        check->creates_lane = storage_index < 0;
    }
    else
    {
        if( form->storage >= picker->storage_count )
        {
            snprintf(check->message, sizeof(check->message), "pick where the squares are stored");
            return 0;
        }
        snprintf(check->storage, sizeof(check->storage), "%s", picker->storages[form->storage]);
        if( strcmp(check->storage, "maps") == 0 )
            check->storage[0] = '\0';
    }
    if( !Editor_DocStorageValid(check->storage) )
    {
        snprintf(check->message, sizeof(check->message), "not a storage the tree accepts");
        return 0;
    }

    /* The name. */
    if( !Editor_InstanceNameValid(form->name) )
    {
        snprintf(check->message, sizeof(check->message),
            "name the instance: a-z, 0-9 and '_' (the .wma, the script directory)");
        return 0;
    }

    /* The entry. */
    if( form->entry == EDITOR_INSTANCE_ENTRY_RUNTIME && !form->runtime_capable )
    {
        snprintf(check->message, sizeof(check->message),
            "runtime map instance: the lane's server does not report map instances; use a teleport trigger");
        return 0;
    }
    if( form->entry == EDITOR_INSTANCE_ENTRY_RUNTIME && (plan->w > 2 || plan->h > 2) )
    {
        snprintf(check->message, sizeof(check->message),
            "runtime map instance: %dx%d squares is more than map_instance_alloc's 2x2", plan->w, plan->h);
        return 0;
    }
    Editor_InstanceEntryTile(plan, &check->entry_level, &check->entry_x, &check->entry_z);
    if( form->entry != EDITOR_INSTANCE_ENTRY_NONE )
    {
        if( !Editor_InstanceLocNameValid(form->loc) )
        {
            snprintf(check->message, sizeof(check->message),
                "name the loc whose first option enters (oploc1): a-z, 0-9, '_'");
            return 0;
        }
        if( form->coord[0] )
        {
            int level;
            int x;
            int z;
            int inside = 0;
            if( !Editor_InstanceCoordParse(form->coord, &level, &x, &z) )
            {
                snprintf(check->message, sizeof(check->message),
                    "the landing coord is 0_16_101_32_32 or 'x z [level]': %.40s", form->coord);
                return 0;
            }
            for( int i = 0; i < plan->count && !inside; i++ )
            {
                int mx;
                int mz;
                Editor_InstanceDest(plan, i, &mx, &mz);
                inside = x / EDITOR_SQUARE_X == mx && z / EDITOR_SQUARE_Z == mz;
            }
            if( !inside || level < plan->level_lo || level > plan->level_hi )
            {
                snprintf(check->message, sizeof(check->message),
                    "the landing tile %d,%d L%d is not in the instance's squares and levels", x, z, level);
                return 0;
            }
            check->entry_level = level;
            check->entry_x = x;
            check->entry_z = z;
        }
    }

    {
        char coord[EDITOR_INSTANCE_COORD_MAX];
        Editor_InstanceCoordText(check->entry_level, check->entry_x, check->entry_z, coord);
        snprintf(check->message, sizeof(check->message), "creates %d square%s at m%d_%d in %s%s%s%s%s%s",
            plan->count, plan->count == 1 ? "" : "s", plan->dst_x0, plan->dst_z0,
            instance_storage_label(check->storage), check->creates_lane ? " (new lane)" : "",
            form->map_area ? ", a .wma" : "", form->entry != EDITOR_INSTANCE_ENTRY_NONE ? ", a trigger to " : "",
            form->entry != EDITOR_INSTANCE_ENTRY_NONE ? coord : "", "; unsaved until Save");
    }
    check->ok = 1;
    return 1;
}

/* ---- the widgets ---------------------------------------------------------------- */

static int
instance_runtime_capable(struct EditorUi* ui)
{
    return ui->editor && (ui->editor->capabilities & TORIRSMAPED_CAP_MAP_INSTANCE) != 0;
}

/** "m16_101" (or "16 101", "16,101") -> the square; 0 when it is not one. */
static int
instance_parse_square(
    const char* text,
    int* out_x,
    int* out_z)
{
    char key[EDITOR_DOC_KEY_MAX];
    int x;
    int z;
    char extra;

    if( Editor_SquareKeyParse(text, out_x, out_z) )
        return 1;
    if( sscanf(text, " %d %d %c", &x, &z, &extra) == 2 || sscanf(text, " %d , %d %c", &x, &z, &extra) == 2 )
    {
        if( x < 0 || x > 255 || z < 0 || z > 255 )
            return 0;
        Editor_SquareKey(key, x, z);
        return Editor_SquareKeyParse(key, out_x, out_z);
    }
    return 0;
}

/** The source rows' keys as the form wants them. */
static int
instance_source_keys(
    struct EditorUi* ui,
    struct InstancePanel* self,
    char (*out)[EDITOR_DOC_KEY_MAX],
    int max)
{
    int const row = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_source);
    const struct Editor_AreasFile* areas;
    int line;
    const struct Editor_Collection* collection;
    int n = 0;

    if( row <= 0 )
        return Editor_SquarePicker_Sources(ui, EDITOR_SQUARE_PICKER_SELECTION, out, max);
    if( row == 1 )
        return Editor_SquarePicker_Sources(ui, EDITOR_SQUARE_PICKER_OPEN_SET, out, max);
    if( row >= self->source_count )
        return 0;
    areas = Editor_Areas_File(ui, NULL);
    if( !areas )
        return 0;
    line = Editor_AreasFindCollection(areas, self->source_collection[row]);
    if( line < 0 )
        return 0;
    collection = areas->lines[line].collection;
    for( int i = 0; i < collection->square_count && n < max; i++ )
        snprintf(out[n++], EDITOR_DOC_KEY_MAX, "%s", collection->squares[i]);
    /* More than an instance takes is still said: the form refuses it. */
    return collection->square_count > max ? max + 1 : n;
}

/** A source key's (z, x) as one number, for the south-west-first order the
 *  plan uses (Editor_InstancePlanInit); a key that is not a square last. */
static int
instance_key_order(const char* key)
{
    const char* slash = strrchr(key, '/');
    int x;
    int z;
    if( !Editor_SquareKeyParse(slash ? slash + 1 : key, &x, &z) )
        return 1 << 20;
    return z * 256 + x;
}

static void
instance_read(
    struct EditorUi* ui,
    struct InstancePanel* self,
    struct Editor_InstanceForm* form)
{
    char sources[EDITOR_INSTANCE_SQUARES_MAX + 1][EDITOR_DOC_KEY_MAX];
    int row;
    int n;

    memset(form, 0, sizeof(*form));
    n = instance_source_keys(ui, self, sources, EDITOR_INSTANCE_SQUARES_MAX);
    form->source_count = n;
    for( int i = 0; i < n && i < EDITOR_INSTANCE_SQUARES_MAX; i++ )
        snprintf(form->sources[i], sizeof(form->sources[i]), "%s", sources[i]);
    for( int i = 1; i < n && i < EDITOR_INSTANCE_SQUARES_MAX; i++ )
        for( int j = i; j > 0 && instance_key_order(form->sources[j - 1]) > instance_key_order(form->sources[j]); j-- )
        {
            char swap[EDITOR_DOC_KEY_MAX];
            memcpy(swap, form->sources[j], sizeof(swap));
            memcpy(form->sources[j], form->sources[j - 1], sizeof(swap));
            memcpy(form->sources[j - 1], swap, sizeof(swap));
        }
    form->copy = (ToriRSChrome_Checked(ui->chrome, self->check_terrain) ? EDITOR_INSTANCE_COPY_TERRAIN : 0u)
                 | (ToriRSChrome_Checked(ui->chrome, self->check_locs) ? EDITOR_INSTANCE_COPY_LOCS : 0u)
                 | (ToriRSChrome_Checked(ui->chrome, self->check_spawns) ? EDITOR_INSTANCE_COPY_SPAWNS : 0u)
                 | (ToriRSChrome_Checked(ui->chrome, self->check_areas) ? EDITOR_INSTANCE_COPY_AREAS : 0u);
    row = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_levels);
    if( row >= 1 && row <= 4 )
        form->level_lo = form->level_hi = row - 1;
    else
    {
        form->level_lo = 0;
        form->level_hi = EDITOR_SQUARE_LEVELS - 1;
    }
    if( !instance_parse_square(ToriRSChrome_Text(ui->chrome, self->input_dest), &form->dst_x, &form->dst_z) )
        form->dst_x = form->dst_z = -1;
    row = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_storage);
    form->storage = row >= 0 && row < self->storage_count ? self->storage_index[row] : 1 << 20;
    snprintf(form->new_lane, sizeof(form->new_lane), "%s", ToriRSChrome_Text(ui->chrome, self->input_lane));
    snprintf(form->name, sizeof(form->name), "%s", ToriRSChrome_Text(ui->chrome, self->input_name));
    form->map_area = ToriRSChrome_Checked(ui->chrome, self->radio_map[0]);
    form->entry = EDITOR_INSTANCE_ENTRY_TELEPORT;
    for( int i = 0; i < 3; i++ )
        if( ToriRSChrome_Checked(ui->chrome, self->radio_entry[i]) )
            form->entry = (enum Editor_InstanceEntry)i;
    snprintf(form->loc, sizeof(form->loc), "%s", ToriRSChrome_Text(ui->chrome, self->input_loc));
    snprintf(form->coord, sizeof(form->coord), "%s", ToriRSChrome_Text(ui->chrome, self->input_coord));
    form->runtime_capable = instance_runtime_capable(ui);
}

static void
instance_layout(
    struct EditorUi* ui,
    struct InstancePanel* self,
    const struct Editor_InstanceForm* form)
{
    int const row = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_storage);
    int const new_lane = row >= 0 && row < self->storage_count && self->storage_index[row] < 0;
    int const has_entry = form->entry != EDITOR_INSTANCE_ENTRY_NONE;

    ToriRSChrome_SetHidden(ui->chrome, self->input_lane, !new_lane);
    ToriRSChrome_SetHidden(ui->chrome, self->input_loc, !has_entry);
    ToriRSChrome_SetHidden(ui->chrome, self->input_coord, !has_entry);
}

/** The source row: the keys, fitted by the chrome (the whole list in the
 *  tooltip when it is long). */
static void
instance_source_row(
    struct EditorUi* ui,
    struct InstancePanel* self,
    const struct Editor_InstanceForm* form)
{
    char line[TORIRS_CHROME_INPUT_MAX];
    char tip[TORIRS_CHROME_LABEL_MAX];
    size_t at = 0;

    line[0] = '\0';
    for( int i = 0; i < form->source_count && i < EDITOR_INSTANCE_SQUARES_MAX; i++ )
    {
        int const n = snprintf(line + at, sizeof(line) - at, "%s%s", i ? " " : "", form->sources[i]);
        if( n < 0 || (size_t)n >= sizeof(line) - at )
            break;
        at += (size_t)n;
    }
    if( form->source_count == 0 )
        snprintf(line, sizeof(line), "no squares: select some in the picker");
    ToriRSChrome_SetText(ui->chrome, self->row_source, line);
    snprintf(tip, sizeof(tip), "%d source square%s", form->source_count, form->source_count == 1 ? "" : "s");
    ToriRSChrome_SetTooltip(ui->chrome, self->row_source, tip);
    snprintf(tip, sizeof(tip), "New instance from %d square%s", form->source_count, form->source_count == 1 ? "" : "s");
    ToriRSChrome_SetText(ui->chrome, self->row_intro, tip);
}

static void
instance_check(
    struct EditorUi* ui,
    struct InstancePanel* self,
    int force)
{
    struct Editor_InstanceForm form;
    struct Editor_InstanceCheck check;
    const struct Editor_Picker* picker;

    instance_read(ui, self, &form);
    if( !force && self->checked_valid && memcmp(&form, &self->checked, sizeof(form)) == 0 )
        return;
    self->checked = form;
    self->checked_valid = 1;
    instance_layout(ui, self, &form);
    instance_source_row(ui, self, &form);

    picker = edp_squares_picker(ui);
    if( !picker )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_check, "no square atlas: is the server reachable?");
        ToriRSChrome_SetDisabled(ui->chrome, self->button_create, 1);
        self->ok = 0;
        return;
    }
    self->ok = Editor_InstanceValidate(picker, &form, &check);
    ToriRSChrome_SetText(ui->chrome, self->row_check, check.message);
    ToriRSChrome_SetColor(ui->chrome, self->row_check, self->ok ? 0x9fdf8f : 0xff7f5f);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_create, !self->ok);
}

static void
instance_fill_sources(
    struct EditorUi* ui,
    struct InstancePanel* self)
{
    char keys[EDITOR_INSTANCE_SQUARES_MAX][EDITOR_DOC_KEY_MAX];
    const struct Editor_AreasFile* areas = Editor_Areas_File(ui, NULL);
    int n = 0;

    snprintf(self->source_labels[n], TORIRS_CHROME_LABEL_MAX, "Picker selection (%d)",
        Editor_SquarePicker_Sources(ui, EDITOR_SQUARE_PICKER_SELECTION, keys, EDITOR_INSTANCE_SQUARES_MAX));
    self->source_collection[n][0] = '\0';
    n++;
    snprintf(self->source_labels[n], TORIRS_CHROME_LABEL_MAX, "Open set (%d)",
        Editor_SquarePicker_Sources(ui, EDITOR_SQUARE_PICKER_OPEN_SET, keys, EDITOR_INSTANCE_SQUARES_MAX));
    self->source_collection[n][0] = '\0';
    n++;
    for( int c = 0; areas && c < Editor_AreasCollectionCount(areas) && n < INSTANCE_SOURCE_ROWS; c++ )
    {
        const struct Editor_Collection* collection = areas->lines[Editor_AreasNthCollection(areas, c)].collection;
        snprintf(self->source_labels[n], TORIRS_CHROME_LABEL_MAX, "collection: %.40s", collection->name);
        snprintf(self->source_collection[n], EDITOR_AREA_NAME_MAX, "%s", collection->name);
        n++;
    }
    for( int i = 0; i < n; i++ )
        self->source_rows[i] = self->source_labels[i];
    self->source_count = n;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_source, self->source_rows, n, 0);
}

static void
instance_fill_storages(
    struct EditorUi* ui,
    struct InstancePanel* self)
{
    const struct Editor_Picker* picker = edp_squares_picker(ui);
    int selected = 0;
    int n = 0;

    for( int i = 0; picker && i < picker->storage_count && n < INSTANCE_STORAGE_ROWS - 1; i++ )
    {
        const char* s = picker->storages[i];
        if( strcmp(s, "maps") == 0 || s[0] == '\0' )
        {
            snprintf(self->storage_labels[n], TORIRS_CHROME_LABEL_MAX, "maps/ (content tree)");
            selected = n;
        }
        else
            snprintf(self->storage_labels[n], TORIRS_CHROME_LABEL_MAX, "%.56s/", s);
        self->storage_rows[n] = self->storage_labels[n];
        self->storage_index[n] = i;
        n++;
    }
    snprintf(self->storage_labels[n], TORIRS_CHROME_LABEL_MAX, "New lane...");
    self->storage_rows[n] = self->storage_labels[n];
    self->storage_index[n] = -1;
    n++;
    self->storage_count = n;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_storage, self->storage_rows, n, selected);
}

/** The atlas's squares as (x, z) pairs, every storage (what Find free must
 *  not land on). Returns the count; *out is ToriMem_Editor's. */
static int
instance_taken(
    const struct Editor_Picker* picker,
    int** out)
{
    int* pairs = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * 2 * (size_t)(picker->square_count + 1));
    assert(pairs);
    for( int i = 0; i < picker->square_count; i++ )
    {
        pairs[i * 2] = picker->squares[i].map_x;
        pairs[i * 2 + 1] = picker->squares[i].map_z;
    }
    *out = pairs;
    return picker->square_count;
}

int
Editor_Instance_FindFree(struct EditorUi* ui)
{
    struct InstancePanel* self;
    struct Editor_InstanceForm form;
    struct Editor_InstancePlan plan;
    const struct Editor_Picker* picker;
    const char* keys[EDITOR_INSTANCE_SQUARES_MAX];
    char text[48];
    int* taken;
    int count;
    int x;
    int z;
    int found;

    assert(ui);
    self = instance_self(ui);
    assert(self);
    picker = edp_squares_picker(ui);
    if( !picker )
    {
        Editor_UiStatus(ui, "Find free refused: the picker has no atlas yet");
        return 0;
    }
    instance_read(ui, self, &form);
    if( form.source_count <= 0 || form.source_count > EDITOR_INSTANCE_SQUARES_MAX )
    {
        Editor_UiStatus(ui, "Find free refused: pick 1 to %d source squares first", EDITOR_INSTANCE_SQUARES_MAX);
        return 0;
    }
    for( int i = 0; i < form.source_count; i++ )
        keys[i] = form.sources[i];
    if( !Editor_InstancePlanInit(&plan, keys, form.source_count) )
    {
        Editor_UiStatus(ui, "Find free refused: the source names a square twice, or not a square");
        return 0;
    }
    count = instance_taken(picker, &taken);
    found = Editor_InstanceFindFree(taken, count, plan.w, plan.h, &x, &z);
    snprintf(text, sizeof(text), "instance space 0..19 x 100..255 has %d free",
        Editor_InstanceFreeCount(taken, count));
    ToriRSChrome_SetText(ui->chrome, self->row_free, text);
    ToriMem_Free(ToriMem_Editor(), taken);
    if( !found )
    {
        Editor_UiStatus(ui, "Find free: no free %dx%d rectangle is left in instance space", plan.w, plan.h);
        return 0;
    }
    snprintf(text, sizeof(text), "m%d_%d", x, z);
    ToriRSChrome_SetText(ui->chrome, self->input_dest, text);
    Editor_SquarePicker_Mark(ui, x, z, plan.w, plan.h);
    Editor_UiStatus(ui, "Find free: m%d_%d .. m%d_%d is free in every storage (marked in the picker)", x, z,
        x + plan.w - 1, z + plan.h - 1);
    instance_check(ui, self, 1);
    return 1;
}

void
Editor_Instance_Open(struct EditorUi* ui)
{
    struct InstancePanel* self;
    char name[EDITOR_INSTANCE_NAME_MAX];

    assert(ui);
    self = instance_self(ui);
    if( !self )
    {
        Editor_UiLog(ui, "editor: no instance helper in this window");
        return;
    }
    instance_fill_sources(ui, self);
    instance_fill_storages(ui, self);
    ToriRSChrome_SetChecked(ui->chrome, self->check_terrain, 1);
    ToriRSChrome_SetChecked(ui->chrome, self->check_locs, 1);
    ToriRSChrome_SetChecked(ui->chrome, self->check_spawns, 1);
    ToriRSChrome_SetChecked(ui->chrome, self->check_areas, 0);
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_levels, k_level_rows, 5, 0);
    ToriRSChrome_SetText(ui->chrome, self->input_dest, "");
    ToriRSChrome_SetText(ui->chrome, self->input_lane, "");
    ToriRSChrome_SetText(ui->chrome, self->row_free, "");
    snprintf(name, sizeof(name), "%s", ToriRSChrome_Text(ui->chrome, self->input_name));
    ToriRSChrome_SetText(ui->chrome, self->input_coord, "");
    ToriRSChrome_RadioSelect(ui->chrome, self->radio_map[0]);
    ToriRSChrome_RadioSelect(ui->chrome, self->radio_entry[EDITOR_INSTANCE_ENTRY_TELEPORT]);
    /* The runtime chip: only a lane whose server says it takes map
     * instances (TORIRSMAPED_CAP_MAP_INSTANCE; none does yet, see
     * torirs_maped.h). Disabled, it says why. */
    ToriRSChrome_SetDisabled(ui->chrome, self->radio_entry[EDITOR_INSTANCE_ENTRY_RUNTIME], !instance_runtime_capable(ui));
    ToriRSChrome_SetTooltip(ui->chrome, self->radio_entry[EDITOR_INSTANCE_ENTRY_RUNTIME],
        instance_runtime_capable(ui) ? "Build a runtime copy with map_instance_*"
                                     : "The lane's server reports no map instances");
    Editor_SquarePicker_Mark(ui, 0, 0, 0, 0);
    self->shown = 1;
    self->checked_valid = 0;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    instance_check(ui, self, 1);
}

static void
instance_close(
    struct EditorUi* ui,
    struct InstancePanel* self)
{
    self->shown = 0;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    Editor_SquarePicker_Mark(ui, 0, 0, 0, 0);
}

/* ---- Create ------------------------------------------------------------------- */

/** builder_text.h's submitter over this window's session (as the builders'
 *  own, builder_session.c): the commands to the authority, the mirror's
 *  text of a document. The stroke is the Create's own, already open. */
static int
instance_submit_apply(
    void* user_data,
    const struct Editor_Cmd* command)
{
    struct EditorUi* ui = user_data;
    return Editor_Apply(ui->editor, command);
}

static void
instance_submit_stroke(
    void* user_data,
    int begin)
{
    (void)user_data;
    (void)begin;
    /* Every sync here runs inside the Create's stroke (in_stroke 1). */
    assert(0);
}

static const char*
instance_submit_current(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key,
    size_t* out_length)
{
    struct EditorUi* ui = user_data;
    struct Editor_DocHandle handle;

    assert(key);
    assert(out_length);
    *out_length = 0;
    Editor_PumpFacts(ui->editor);
    if( !edp_ensure_doc(ui, kind, key) || !Editor_DocFind(&ui->editor->doc, kind, key, &handle) || !handle.text )
        return NULL;
    *out_length = handle.text->length;
    return handle.text->text ? handle.text->text : "";
}

/** The mirror's text of a TEXT document, opened if need be; NULL when the
 *  authority will not open it. */
static const char*
instance_text(
    struct EditorUi* ui,
    const char* key,
    size_t* out_length)
{
    return instance_submit_current(ui, EDITOR_DOC_TEXT, key, out_length);
}

/** The spawn rows standing in source square `index`: the edited file's
 *  rows, then the generated file's that no edited suppression removes. */
static int
instance_source_spawns(
    struct EditorUi* ui,
    const struct Editor_InstancePlan* plan,
    int index,
    struct Editor_SpawnRow* out,
    int max)
{
    char key[EDITOR_DOC_KEY_MAX];
    struct Editor_DocHandle handle;
    const struct Editor_SpawnFile* edited = NULL;
    struct Editor_SpawnFile generated;
    const char* text;
    size_t length = 0;
    int n = 0;

    Editor_SquareKey(key, plan->sources[index].map_x, plan->sources[index].map_z);
    if( edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key) && Editor_DocFind(&ui->editor->doc, EDITOR_DOC_SPAWN, key, &handle)
        && handle.spawn )
        edited = &handle.spawn->file;
    for( int l = 0; edited && l < edited->line_count && n < max; l++ )
        if( edited->lines[l].kind == EDITOR_SPAWN_LINE_ROW && !edited->lines[l].row.suppress )
            out[n++] = edited->lines[l].row;

    Editor_SpawnGeneratedKey(key, plan->sources[index].map_x, plan->sources[index].map_z);
    text = instance_text(ui, key, &length);
    if( !text || length == 0 )
        return n;
    Editor_SpawnInit(&generated);
    if( Editor_SpawnParse(&generated, text, length).ok )
    {
        for( int l = 0; l < generated.line_count && n < max; l++ )
        {
            struct Editor_SpawnRow row;
            if( generated.lines[l].kind != EDITOR_SPAWN_LINE_ROW || generated.lines[l].row.suppress )
                continue;
            row = generated.lines[l].row;
            row.suppress = 1;
            if( edited && Editor_SpawnFind(edited, &row) >= 0 )
                continue;
            row.suppress = 0;
            out[n++] = row;
        }
    }
    else
        Editor_UiLog(ui, "editor: instance: %s does not parse; its spawns are not copied", key);
    Editor_SpawnFree(&generated);
    return n;
}

/** The AREAS document's areas lying wholly inside the source squares, moved
 *  and named "<instance>: <area>", then a collection `name` of the new
 *  squares. Returns the commands applied. */
static int
instance_areas(
    struct EditorUi* ui,
    const struct Editor_InstanceCheck* check,
    const char* name,
    int copy_areas)
{
    const struct Editor_InstancePlan* plan = &check->plan;
    const struct Editor_AreasFile* file = Editor_Areas_File(ui, NULL);
    struct Editor_Collection collection;
    struct Editor_Cmd command;
    int applied = 0;
    int areas;

    if( !file )
        return 0;
    areas = Editor_AreasAreaCount(file);
    for( int a = 0; copy_areas && a < areas; a++ )
    {
        const struct Editor_Area* area;
        struct Editor_Area moved;
        int inside = 1;

        file = Editor_Areas_File(ui, NULL);
        if( !file || a >= Editor_AreasAreaCount(file) )
            break;
        area = file->lines[Editor_AreasNthArea(file, a)].area;
        if( strncmp(area->name, name, strlen(name)) == 0 && area->name[strlen(name)] == ':' )
            continue; /* one this helper made */
        memset(&moved, 0, sizeof(moved));
        snprintf(moved.name, sizeof(moved.name), "%.20s: %.40s", name, area->name);
        moved.level = area->level;
        moved.colour = area->colour;
        snprintf(moved.note, sizeof(moved.note), "%s", area->note);
        for( int r = 0; r < area->rect_count && inside; r++ )
        {
            int x0;
            int z0;
            int x1;
            int z1;
            inside = Editor_InstanceRebaseTile(plan, area->rects[r].x0, area->rects[r].z0, &x0, &z0)
                     && Editor_InstanceRebaseTile(plan, area->rects[r].x1, area->rects[r].z1, &x1, &z1)
                     && Editor_InstanceSourceOfTile(plan, area->rects[r].x0, area->rects[r].z0)
                            == Editor_InstanceSourceOfTile(plan, area->rects[r].x1, area->rects[r].z1)
                     && Editor_AreaAddRect(&moved, x0, z0, x1, z1);
        }
        if( !inside || moved.rect_count == 0 )
            continue;
        if( Editor_AreaSetCommand(&command, file, NULL, &moved) && Editor_Apply(ui->editor, &command) )
            applied++;
    }

    file = Editor_Areas_File(ui, NULL);
    if( !file )
        return applied;
    memset(&collection, 0, sizeof(collection));
    snprintf(collection.name, sizeof(collection.name), "%s", name);
    for( int i = 0; i < plan->count; i++ )
    {
        char key[EDITOR_DOC_KEY_MAX * 2];
        char bare[EDITOR_DOC_KEY_MAX];
        int x;
        int z;
        Editor_InstanceDest(plan, i, &x, &z);
        Editor_SquareKey(bare, x, z);
        if( check->storage[0] )
            snprintf(key, sizeof(key), "%s/%s", check->storage, bare);
        else
            snprintf(key, sizeof(key), "%s", bare);
        (void)Editor_CollectionAdd(&collection, key);
    }
    snprintf(collection.note, sizeof(collection.note), "instance: made by the instance helper");
    if( Editor_CollectionSetCommand(&command, file, NULL, &collection) && Editor_Apply(ui->editor, &command) )
        applied++;
    return applied;
}

/* TODO(workbench 6.5, scenes): the SCENES document (editor/scenes.txt,
 * design section 12) is another slice's and is not in this tree yet. When it
 * lands, this records a scene "instance: <name>" holding the new squares, the
 * camera at the landing tile and its level, through that document's SET
 * command, inside the Create's stroke (so it undoes with the rest). Until
 * then it records nothing, and the collection `name` is what finds the
 * instance again (the picker's Find "instance:<name>"). */
static int
instance_scene_hook(
    struct EditorUi* ui,
    const struct Editor_InstanceCheck* check,
    const char* name)
{
    (void)ui;
    (void)check;
    (void)name;
    return 0;
}

/** Refusals that need the documents: a .wma or trigger file that already
 *  has text, a collection of that name. 1 when Create may go on. */
static int
instance_documents_free(
    struct EditorUi* ui,
    struct InstancePanel* self,
    const struct Editor_InstanceForm* form,
    char* message,
    size_t message_size)
{
    char key[EDITOR_DOC_KEY_MAX];
    size_t length = 0;
    const struct Editor_AreasFile* areas;

    (void)self;
    if( form->map_area )
    {
        Editor_WmaDocKey(key, sizeof(key), form->name);
        if( !instance_text(ui, key, &length) )
        {
            snprintf(message, message_size, "the server will not open worldmap/areas/%s.wma", form->name);
            return 0;
        }
        if( length > 0 )
        {
            snprintf(message, message_size, "worldmap/areas/%s.wma already exists: pick another name", form->name);
            return 0;
        }
    }
    if( form->entry != EDITOR_INSTANCE_ENTRY_NONE )
    {
        Editor_InstanceTriggerKey(form->name, key);
        if( !instance_text(ui, key, &length) )
        {
            snprintf(message, message_size, "the server will not open %s", key + 4);
            return 0;
        }
        if( length > 0 )
        {
            snprintf(message, message_size, "server/scripts/edited/%s/ already has its script: pick another name",
                form->name);
            return 0;
        }
    }
    areas = Editor_Areas_File(ui, NULL);
    if( areas && Editor_AreasFindCollection(areas, form->name) >= 0 )
    {
        snprintf(message, message_size, "a collection named %s exists: pick another name", form->name);
        return 0;
    }
    return 1;
}

static void
instance_refuse(
    struct EditorUi* ui,
    struct InstancePanel* self,
    const char* message)
{
    ToriRSChrome_SetText(ui->chrome, self->row_check, message);
    ToriRSChrome_SetColor(ui->chrome, self->row_check, 0xff7f5f);
    Editor_UiStatus(ui, "Create instance refused: %s", message);
}

int
Editor_Instance_Create(struct EditorUi* ui)
{
    struct InstancePanel* self;
    struct Editor_InstanceForm form;
    struct Editor_InstanceCheck check;
    const struct Editor_Picker* picker;
    struct Builder_Submit submit;
    struct Editor_SpawnRow* spawns;
    char message[EDITOR_INSTANCE_MESSAGE_MAX];
    char verb[32];
    int capacity;
    struct Editor_Cmd* commands;
    int failed = 0;
    int spawn_count = 0;
    int applied_before;

    assert(ui);
    self = instance_self(ui);
    assert(self);
    picker = edp_squares_picker(ui);
    instance_read(ui, self, &form);
    if( !picker || !Editor_InstanceValidate(picker, &form, &check) )
    {
        instance_check(ui, self, 1);
        Editor_UiStatus(ui, "Create instance refused: %s",
            picker ? check.message : "the picker has no atlas yet");
        return 0;
    }
    if( !ui->editor || !ui->editor->host_open )
    {
        instance_refuse(ui, self, "no editor session");
        return 0;
    }
    if( !(ui->editor->capabilities & TORIRSMAPED_CAP_DOC_CREATE_STROKE) )
    {
        instance_refuse(ui, self, "this server cannot make squares inside one undo step (an older ToriRSMapEd)");
        return 0;
    }
    if( ui->tools.stroke_open )
    {
        instance_refuse(ui, self, "a stroke is open; finish it first");
        return 0;
    }
    if( !instance_documents_free(ui, self, &form, message, sizeof(message)) )
    {
        instance_refuse(ui, self, message);
        return 0;
    }

    /* Everything the copy reads, open in the mirror before the stroke. */
    for( int i = 0; i < check.plan.count; i++ )
        if( !edp_ensure_doc(ui, EDITOR_DOC_SQUARE, check.plan.sources[i].key) )
        {
            snprintf(message, sizeof(message), "the server will not open %s", check.plan.sources[i].key);
            instance_refuse(ui, self, message);
            return 0;
        }
    spawns = ToriMem_Alloc(ToriMem_Editor(), sizeof(*spawns) * INSTANCE_SPAWNS_MAX);
    assert(spawns);
    for( int i = 0; (form.copy & EDITOR_INSTANCE_COPY_SPAWNS) && i < check.plan.count; i++ )
        spawn_count += instance_source_spawns(ui, &check.plan, i, spawns + spawn_count,
            INSTANCE_SPAWNS_MAX - spawn_count);
    for( int i = 0; i < spawn_count; i++ )
        if( !Editor_InstanceRebaseSpawn(&check.plan, &spawns[i], &spawns[i]) )
            spawns[i].name[0] = '\0';
    for( int i = 0; i < spawn_count; i++ )
    {
        char key[EDITOR_DOC_KEY_MAX];
        if( !spawns[i].name[0] )
            continue;
        Editor_SpawnDocKeyAt(key, spawns[i].abs_x, spawns[i].abs_z);
        (void)edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key);
    }

    memset(&submit, 0, sizeof(submit));
    submit.user_data = ui;
    submit.apply = instance_submit_apply;
    submit.stroke = instance_submit_stroke;
    submit.current = instance_submit_current;

    commands = edp_scratch(&capacity);
    applied_before = Editor_HistoryAppliedCommands(Editor_GetHistory(ui->editor));
    snprintf(verb, sizeof(verb), "instance %.22s", form.name);
    Editor_StrokeBegin(ui->editor);
    Editor_HistoryName(ui->editor, verb);
    self->created_squares = 0;
    self->created_spawns = 0;

    /* The squares: DOC_CREATE (the copy template) inside the stroke, then
     * what trims each to the Copy row. */
    for( int i = 0; i < check.plan.count && !failed; i++ )
    {
        struct EditorHost_DocCreate request;
        struct Editor_Square* created;
        struct Editor_Square* source;
        int x;
        int z;
        int n;

        Editor_InstanceDest(&check.plan, i, &x, &z);
        memset(&request, 0, sizeof(request));
        request.kind = EDITOR_DOC_SQUARE;
        Editor_SquareKey(request.key, x, z);
        snprintf(request.storage, sizeof(request.storage), "%s", check.storage);
        request.template_kind = Editor_InstanceTemplate(form.copy);
        snprintf(request.source_key, sizeof(request.source_key), "%s", check.plan.sources[i].key);
        request.join_stroke = 1;
        if( Editor_CreateSquare(ui->editor, &request) != EDITOR_HOST_OK )
        {
            snprintf(message, sizeof(message), "the server would not create %s (it exists there, or is open)",
                request.key);
            failed = 1;
            break;
        }
        self->created_squares++;
        created = Editor_DocFindSquare(&ui->editor->doc, x, z);
        source = Editor_DocFindSquare(&ui->editor->doc, check.plan.sources[i].map_x, check.plan.sources[i].map_z);
        assert(created);
        if( !(form.copy & EDITOR_INSTANCE_COPY_TERRAIN) && (form.copy & EDITOR_INSTANCE_COPY_LOCS) && !source )
        {
            snprintf(message, sizeof(message), "%s is not open in this window", check.plan.sources[i].key);
            failed = 1;
            break;
        }
        n = Editor_InstanceCopyCommands(&check.plan, form.copy, created, source, NULL, 0);
        if( n > capacity )
        {
            snprintf(message, sizeof(message), "%s needs %d trim commands, more than one action holds (%d)",
                request.key, n, capacity);
            failed = 1;
            break;
        }
        n = Editor_InstanceCopyCommands(&check.plan, form.copy, created, source, commands, capacity);
        for( int c = 0; c < n && !failed; c++ )
            if( !Editor_Apply(ui->editor, &commands[c]) )
            {
                snprintf(message, sizeof(message), "the authority refused the copy of %s", request.key);
                failed = 1;
            }
    }

    /* The spawns, rebased to the new absolute tiles. */
    for( int i = 0; i < spawn_count && !failed; i++ )
    {
        struct Editor_Cmd command;
        if( !spawns[i].name[0] )
            continue;
        Editor_SpawnCommand(&command, &spawns[i], 1);
        if( Editor_Apply(ui->editor, &command) )
            self->created_spawns++;
        else
        {
            snprintf(message, sizeof(message), "the authority refused the spawn %.40s", spawns[i].name);
            failed = 1;
        }
    }
    ToriMem_Free(ToriMem_Editor(), spawns);

    if( !failed )
        (void)instance_areas(ui, &check, form.name, (form.copy & EDITOR_INSTANCE_COPY_AREAS) != 0);

    /* The map area and the trigger, through the builders' text path. */
    if( !failed && form.map_area )
    {
        char key[EDITOR_DOC_KEY_MAX];
        char external[EDITOR_WMA_EXTERNAL_MAX];
        char text[1024];
        size_t length;
        Editor_WmaDocKey(key, sizeof(key), form.name);
        Editor_InstanceDisplayName(form.name, external, sizeof(external));
        length = Editor_WmaNewAreaText(form.name, external, check.plan.dst_x0, check.plan.dst_z0,
            check.plan.dst_x0 + check.plan.w - 1, check.plan.dst_z0 + check.plan.h - 1, check.plan.level_lo,
            check.plan.level_hi - check.plan.level_lo + 1, text, sizeof(text));
        assert(length < sizeof(text));
        if( Builder_TextSync(&submit, key, text, length, 1) == BUILDER_SYNC_REFUSED )
        {
            snprintf(message, sizeof(message), "the authority refused worldmap/areas/%s.wma", form.name);
            failed = 1;
        }
    }
    if( !failed && form.entry != EDITOR_INSTANCE_ENTRY_NONE )
    {
        char key[EDITOR_DOC_KEY_MAX];
        char text[4096];
        size_t length;
        Editor_InstanceTriggerKey(form.name, key);
        length = Editor_InstanceTriggerText(&check.plan, form.name, form.loc, form.entry, check.entry_level,
            check.entry_x, check.entry_z, text, sizeof(text));
        assert(length < sizeof(text));
        if( Builder_TextSync(&submit, key, text, length, 1) == BUILDER_SYNC_REFUSED )
        {
            snprintf(message, sizeof(message), "the authority refused %s", key + 4);
            failed = 1;
        }
    }
    if( !failed )
        (void)instance_scene_hook(ui, &check, form.name);
    Editor_StrokeEnd(ui->editor);

    if( failed )
    {
        /* Half an instance is not an instance: the step goes back whole. */
        if( Editor_HistoryAppliedCommands(Editor_GetHistory(ui->editor)) > applied_before )
            (void)Editor_Undo(ui->editor);
        instance_refuse(ui, self, message);
        Editor_SquarePicker_Refresh(ui);
        return 0;
    }

    Editor_UiStatus(ui, "Created instance %s: %d square%s at m%d_%d, %d spawn%s%s%s (unsaved until Save); Ctrl+Z undoes it",
        form.name, self->created_squares, self->created_squares == 1 ? "" : "s", check.plan.dst_x0, check.plan.dst_z0,
        self->created_spawns, self->created_spawns == 1 ? "" : "s", form.map_area ? ", a .wma" : "",
        form.entry != EDITOR_INSTANCE_ENTRY_NONE ? ", a trigger" : "");
    instance_close(ui, self);
    Editor_SquarePicker_Refresh(ui);
    Editor_SquarePicker_Mark(ui, check.plan.dst_x0, check.plan.dst_z0, check.plan.w, check.plan.h);
    if( ui->hooks.open_square )
    {
        int x;
        int z;
        Editor_InstanceDest(&check.plan, 0, &x, &z);
        ui->hooks.open_square(ui->hooks_user, x, z, check.storage, 1);
    }
    return 1;
}

int
Editor_Instance_Widget(
    struct EditorUi* ui,
    enum Editor_InstanceWidget which)
{
    struct InstancePanel* self;

    assert(ui);
    self = instance_self(ui);
    assert(self);
    switch( which )
    {
    case EDITOR_INSTANCE_W_PANEL:
        return self->panel;
    case EDITOR_INSTANCE_W_SOURCE:
        return self->dropdown_source;
    case EDITOR_INSTANCE_W_SOURCE_ROW:
        return self->row_source;
    case EDITOR_INSTANCE_W_TERRAIN:
        return self->check_terrain;
    case EDITOR_INSTANCE_W_LOCS:
        return self->check_locs;
    case EDITOR_INSTANCE_W_SPAWNS:
        return self->check_spawns;
    case EDITOR_INSTANCE_W_AREAS:
        return self->check_areas;
    case EDITOR_INSTANCE_W_LEVELS:
        return self->dropdown_levels;
    case EDITOR_INSTANCE_W_DEST:
        return self->input_dest;
    case EDITOR_INSTANCE_W_FIND:
        return self->button_find;
    case EDITOR_INSTANCE_W_FREE:
        return self->row_free;
    case EDITOR_INSTANCE_W_STORAGE:
        return self->dropdown_storage;
    case EDITOR_INSTANCE_W_LANE:
        return self->input_lane;
    case EDITOR_INSTANCE_W_NAME:
        return self->input_name;
    case EDITOR_INSTANCE_W_MAP_ADD:
        return self->radio_map[0];
    case EDITOR_INSTANCE_W_MAP_NONE:
        return self->radio_map[1];
    case EDITOR_INSTANCE_W_ENTRY_TELEPORT:
        return self->radio_entry[EDITOR_INSTANCE_ENTRY_TELEPORT];
    case EDITOR_INSTANCE_W_ENTRY_RUNTIME:
        return self->radio_entry[EDITOR_INSTANCE_ENTRY_RUNTIME];
    case EDITOR_INSTANCE_W_ENTRY_NONE:
        return self->radio_entry[EDITOR_INSTANCE_ENTRY_NONE];
    case EDITOR_INSTANCE_W_LOC:
        return self->input_loc;
    case EDITOR_INSTANCE_W_COORD:
        return self->input_coord;
    case EDITOR_INSTANCE_W_CHECK:
        return self->row_check;
    case EDITOR_INSTANCE_W_CREATE:
        return self->button_create;
    case EDITOR_INSTANCE_W_CANCEL:
        return self->button_cancel;
    }
    assert(0);
    return -1;
}

/* ---- the module ------------------------------------------------------------------ */

static void
instance_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct InstancePanel* self = opaque;

    if( !self->shown )
        return;
    if( !ui->chrome->panels[self->panel].visible )
    {
        self->shown = 0; /* Escape or the close box */
        Editor_SquarePicker_Mark(ui, 0, 0, 0, 0);
        return;
    }
    instance_check(ui, self, 0);
}

static int
instance_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct InstancePanel* self = opaque;

    if( widget == self->button_create )
    {
        (void)Editor_Instance_Create(ui);
        return 1;
    }
    if( widget == self->button_cancel )
    {
        instance_close(ui, self);
        return 1;
    }
    if( widget == self->button_find )
    {
        (void)Editor_Instance_FindFree(ui);
        return 1;
    }
    if( widget == self->radio_entry[EDITOR_INSTANCE_ENTRY_RUNTIME] && !instance_runtime_capable(ui) )
    {
        Editor_UiStatus(ui, "Runtime map instance unavailable: the lane's server reports no map instances");
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_entry[EDITOR_INSTANCE_ENTRY_TELEPORT]);
        instance_check(ui, self, 1);
        return 1;
    }
    if( widget == self->dropdown_source || widget == self->check_terrain || widget == self->check_locs
        || widget == self->check_spawns || widget == self->check_areas || widget == self->dropdown_levels
        || widget == self->input_dest || widget == self->dropdown_storage || widget == self->input_lane
        || widget == self->input_name || widget == self->radio_map[0] || widget == self->radio_map[1]
        || widget == self->radio_entry[0] || widget == self->radio_entry[1] || widget == self->radio_entry[2]
        || widget == self->input_loc || widget == self->input_coord )
    {
        instance_check(ui, self, 1);
        return 1;
    }
    return 0;
}

static void
instance_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Instance_Register(struct EditorUi* ui)
{
    struct InstancePanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->panel = ToriRSChrome_ModalAdd(chrome, "New instance", edp_px(ui, 360));
    assert(self->panel >= 0);
    self->row_intro = ToriRSChrome_Label(chrome, self->panel, "New instance");
    self->dropdown_source = ToriRSChrome_Dropdown(chrome, self->panel, "Source", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_source, "The picker's selection, the open set or a collection");
    self->row_source = ToriRSChrome_Label(chrome, self->panel, "");
    self->check_terrain = ToriRSChrome_Checkbox(chrome, self->panel, "Copy terrain", 1);
    self->check_locs = ToriRSChrome_Checkbox(chrome, self->panel, "Copy locs", 1);
    self->check_spawns = ToriRSChrome_Checkbox(chrome, self->panel, "Copy spawns", 1);
    self->check_areas = ToriRSChrome_Checkbox(chrome, self->panel, "Copy named areas", 0);
    ToriRSChrome_SetTooltip(chrome, self->check_areas, "AREAS rectangles inside the source, moved");
    self->dropdown_levels = ToriRSChrome_Dropdown(chrome, self->panel, "Levels", k_level_rows, 5, 0);
    self->input_dest = ToriRSChrome_TextInput(chrome, self->panel, "Destination", "");
    ToriRSChrome_SetTooltip(chrome, self->input_dest, "The south-west square: m16_101");
    self->button_find = ToriRSChrome_Button(chrome, self->panel, "Find free...");
    ToriRSChrome_SetTooltip(chrome, self->button_find, "Lowest free box in instance space");
    self->row_free = ToriRSChrome_Label(chrome, self->panel, "");
    self->dropdown_storage = ToriRSChrome_Dropdown(chrome, self->panel, "Stored in", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_storage, "maps/ or a lane's ported/<lane>/maps");
    self->input_lane = ToriRSChrome_TextInput(chrome, self->panel, "Lane", "");
    self->input_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_name, "Area name, script directory, collection");
    self->radio_map[0] = ToriRSChrome_Radio(chrome, self->panel, "Map area: add a .wma", INSTANCE_GROUP_MAP, 1);
    self->radio_map[1] = ToriRSChrome_Radio(chrome, self->panel, "Map area: none (hidden)", INSTANCE_GROUP_MAP, 0);
    self->radio_entry[EDITOR_INSTANCE_ENTRY_TELEPORT] =
        ToriRSChrome_Radio(chrome, self->panel, "Entry: teleport trigger", INSTANCE_GROUP_ENTRY, 1);
    self->radio_entry[EDITOR_INSTANCE_ENTRY_RUNTIME] =
        ToriRSChrome_Radio(chrome, self->panel, "Entry: runtime map instance", INSTANCE_GROUP_ENTRY, 0);
    self->radio_entry[EDITOR_INSTANCE_ENTRY_NONE] =
        ToriRSChrome_Radio(chrome, self->panel, "Entry: none", INSTANCE_GROUP_ENTRY, 0);
    self->input_loc = ToriRSChrome_TextInput(chrome, self->panel, "Trigger loc", "");
    ToriRSChrome_SetTooltip(chrome, self->input_loc, "[oploc1,<loc>] p_telejump's in");
    self->input_coord = ToriRSChrome_TextInput(chrome, self->panel, "Lands at", "");
    ToriRSChrome_SetTooltip(chrome, self->input_coord, "0_16_101_32_32; empty: first square's middle");
    self->row_check = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_create = ToriRSChrome_Button(chrome, self->panel, "Create");
    self->button_cancel = ToriRSChrome_Button(chrome, self->panel, "Cancel");
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "instance";
    desc.title = NULL;
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = instance_tick;
    desc.activate = instance_activate;
    desc.destroy = instance_destroy;
    Editor_UiAddPanel(ui, &desc);
}

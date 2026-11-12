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
 *   Entry        a runtime map instance (the default when the lane's server
 *                reports TORIRSMAPED_CAP_MAP_INSTANCE: each player who
 *                enters gets a private copy stamped from the template), a
 *                teleport trigger (everyone into the template itself), none;
 *                the trigger's subject loc and its landing coord, an exit
 *                (its loc and the real-map tile it returns to) and an npc
 *                added on entering a runtime copy.
 *   Where        two sentences under the fields: where the template is and
 *                how to open it, and what happens at play time.
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
#include "editor/editor_scenes.h"
#include "editor/editor_view.h"
#include "editor/editor_wma.h"
#include "edpanel_ops.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"

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
/** Frames the World must have been still before Create's status sentence is
 *  said (the square it opens says its own first). */
#define INSTANCE_STATUS_SETTLE_FRAMES 6

enum
{
    INSTANCE_GROUP_MAP = 31,
    INSTANCE_GROUP_ENTRY = 32,
};

enum
{
    INSTANCE_FACT_LOC = 0,
    INSTANCE_FACT_EXIT,
    INSTANCE_FACT_NPC,
    INSTANCE_FACT_COUNT,
};

struct InstanceNameFact
{
    /** The name the answer is for; "" = nothing asked yet. */
    char name[EDITOR_INSTANCE_LOC_MAX];
    /** enum Editor_InstanceNameState. */
    int state;
    /** The record's id, or -1. */
    int id;
    /** A TAKEN or CATEGORY loc's trigger: "<rel>:<line>". */
    char where[EDITOR_INSTANCE_WHERE_FILE_MAX];
    /** A CATEGORY loc's category ("dairy_churn"), else "". */
    char category[EDITOR_INSTANCE_LOC_MAX];
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
    int input_exit;
    int input_exit_coord;
    int input_npc;
    int input_npc_coord;
    /** Where the template is, and what happens at play time. */
    int row_where[2];
    int row_check;
    /** A name the catalog does not have yet: said, not refused. */
    int row_note;
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

    /** The catalog's word on the trigger loc, the exit loc and the npc,
     *  kept while the name stands (a lookup is a round trip, and the form
     *  is read every frame the dialog is up). */
    struct InstanceNameFact facts[INSTANCE_FACT_COUNT];
    /** The loc selected in the World when the dialog opened: its type and
     *  tile, what "Returns to" defaults to when it is the trigger loc. */
    int selected_loc_id;
    int selected_level;
    int selected_x;
    int selected_z;

    int shown;
    struct Editor_InstanceForm checked;
    int checked_valid;
    int ok;
    /** Create's status sentence, held until the template has opened in the
     *  World ("" = none waiting), and the quiet frames still to pass. */
    char status_pending[EDITOR_INSTANCE_WHERE_MAX];
    int status_wait;
    /** The last Create's tally, for the status line and the tests. */
    int created_squares;
    int created_spawns;
    /** The scene "instance:<name>" went into the step. */
    int created_scene;
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

/** 1 when absolute tile (x, z) on `level` is one the plan's destination
 *  squares and levels hold. */
static int
instance_tile_in_template(
    const struct Editor_InstancePlan* plan,
    int level,
    int x,
    int z)
{
    if( level < plan->level_lo || level > plan->level_hi )
        return 0;
    for( int i = 0; i < plan->count; i++ )
    {
        int mx;
        int mz;
        Editor_InstanceDest(plan, i, &mx, &mz);
        if( x / EDITOR_SQUARE_X == mx && z / EDITOR_SQUARE_Z == mz )
            return 1;
    }
    return 0;
}

/** The exit rows: its loc, and the real-map tile it returns to (typed, else
 *  the trigger loc's own tile). 1 good or no exit asked; 0 with the reason
 *  in check->message. */
static int
instance_validate_exit(
    const struct Editor_InstanceForm* form,
    struct Editor_InstanceCheck* check)
{
    int same_loc;

    if( !form->exit_loc[0] )
        return 1;
    if( !Editor_InstanceLocNameValid(form->exit_loc) )
    {
        snprintf(check->message, sizeof(check->message),
            "the exit loc is a loc's name (a-z, 0-9, '_'); leave it empty for no exit");
        return 0;
    }
    same_loc = strcmp(form->exit_loc, form->loc) == 0;
    if( same_loc && form->entry != EDITOR_INSTANCE_ENTRY_RUNTIME )
    {
        snprintf(check->message, sizeof(check->message),
            "a teleport entry needs another loc for its exit than %.40s (a runtime map instance can use the one loc)",
            form->loc);
        return 0;
    }
    if( !same_loc && form->exit_state == EDITOR_INSTANCE_NAME_TAKEN )
    {
        snprintf(check->message, sizeof(check->message),
            "%.40s already has an [oploc1] (%.80s): pick an exit loc with none, or edit that script", form->exit_loc,
            form->exit_where);
        return 0;
    }
    if( form->exit_coord[0] )
    {
        if( !Editor_InstanceCoordParse(form->exit_coord, &check->exit_level, &check->exit_x, &check->exit_z) )
        {
            snprintf(check->message, sizeof(check->message), "Returns to is 0_50_50_22_18 or 'x z [level]': %.40s",
                form->exit_coord);
            return 0;
        }
    }
    else if( form->home_known )
    {
        check->exit_level = form->home_level;
        check->exit_x = form->home_x;
        check->exit_z = form->home_z;
    }
    else
    {
        snprintf(check->message, sizeof(check->message),
            "type where the exit returns to (Returns to: x z [level]): %.40s is not placed in the source squares",
            form->loc);
        return 0;
    }
    if( Editor_InstanceInSpace(check->exit_x / EDITOR_SQUARE_X, check->exit_z / EDITOR_SQUARE_Z) )
    {
        snprintf(check->message, sizeof(check->message),
            "Returns to %d,%d is in instance space: the exit goes back to a tile of the real map", check->exit_x,
            check->exit_z);
        return 0;
    }
    check->has_exit = 1;
    return 1;
}

/** The npc added on entering: a runtime copy's, on a tile of the template
 *  (typed, else two tiles north of the landing). 1 good or none asked; 0
 *  with the reason in check->message. */
static int
instance_validate_npc(
    const struct Editor_InstanceForm* form,
    struct Editor_InstanceCheck* check)
{
    const struct Editor_InstancePlan* plan = &check->plan;

    if( !form->npc[0] )
        return 1;
    if( form->entry != EDITOR_INSTANCE_ENTRY_RUNTIME )
    {
        snprintf(check->message, sizeof(check->message),
            "Npc on enter is for a runtime map instance; in the shared template place the npc with the Spawns tool");
        return 0;
    }
    if( !Editor_InstanceLocNameValid(form->npc) )
    {
        snprintf(check->message, sizeof(check->message),
            "Npc on enter is an npc's name (a-z, 0-9, '_'); leave it empty for none");
        return 0;
    }
    if( form->npc_coord[0] )
    {
        if( !Editor_InstanceCoordParse(form->npc_coord, &check->npc_level, &check->npc_x, &check->npc_z) )
        {
            snprintf(check->message, sizeof(check->message), "Npc at is 0_16_101_32_34 or 'x z [level]': %.40s",
                form->npc_coord);
            return 0;
        }
        if( !Editor_InstanceTemplateTile(plan, check->npc_x, check->npc_z, &check->npc_x, &check->npc_z)
            || !instance_tile_in_template(plan, check->npc_level, check->npc_x, check->npc_z) )
        {
            snprintf(check->message, sizeof(check->message),
                "Npc at %d,%d L%d is not in the copy: type an offset in it (32 34), a tile of the source "
                "squares, or of the template", check->npc_x, check->npc_z, check->npc_level);
            return 0;
        }
    }
    else
    {
        /* Beside the player, not under them: two tiles north, or south at
         * the square's north edge. */
        int const local_z = check->entry_z % EDITOR_SQUARE_Z;
        check->npc_level = check->entry_level;
        check->npc_x = check->entry_x;
        check->npc_z = check->entry_z + (local_z + 2 < EDITOR_SQUARE_Z ? 2 : -2);
    }
    check->has_npc = 1;
    return 1;
}

/** What Create would make that will not work as it stands, said and not
 *  refused: a name the lane's catalog does not have (a record made this
 *  session is not indexed until it is saved; Check scripts fails until it
 *  is), else a loc whose category's trigger the new one takes over, else a
 *  trigger loc with no first option for a player to click. */
static void
instance_validate_note(
    const struct Editor_InstanceForm* form,
    struct Editor_InstanceCheck* check)
{
    const char* missing[3];
    const char* kinds[3];
    const char* no_option = NULL;
    int const own_exit = check->has_exit && strcmp(form->exit_loc, form->loc) != 0;
    int n = 0;

    check->note[0] = '\0';
    if( form->entry == EDITOR_INSTANCE_ENTRY_NONE )
        return;
    if( form->loc_state == EDITOR_INSTANCE_NAME_ABSENT )
    {
        kinds[n] = "loc";
        missing[n++] = form->loc;
    }
    if( own_exit && form->exit_state == EDITOR_INSTANCE_NAME_ABSENT )
    {
        kinds[n] = "loc";
        missing[n++] = form->exit_loc;
    }
    if( check->has_npc && form->npc_state == EDITOR_INSTANCE_NAME_ABSENT )
    {
        kinds[n] = "npc";
        missing[n++] = form->npc;
    }
    if( n > 0 )
    {
        snprintf(check->note, sizeof(check->note),
            "note: this lane has no %s named %.40s%s%s%s%.40s yet; Check scripts will say so until it does", kinds[0],
            missing[0], n > 1 ? ", no " : "", n > 1 ? kinds[1] : "", n > 1 ? " " : "", n > 1 ? missing[1] : "");
        return;
    }
    /* A loc whose category's trigger answers its first option: the
     * helper's own [oploc1,<loc>] wins over that for every such loc placed
     * anywhere -- right for a loc made with Copy to new (it inherits the
     * category), wrong for the game's own churn or range. Said, not refused. */
    if( form->loc_state == EDITOR_INSTANCE_NAME_CATEGORY
        || (own_exit && form->exit_state == EDITOR_INSTANCE_NAME_CATEGORY) )
    {
        int const of_loc = form->loc_state == EDITOR_INSTANCE_NAME_CATEGORY;
        const char* const loc = of_loc ? form->loc : form->exit_loc;
        snprintf(check->note, sizeof(check->note),
            "note: %.24s's op1 has a category trigger [oploc1,_%.24s] (%.60s): this replaces it for every %.24s; Copy "
            "to new for your own",
            loc, of_loc ? form->loc_category : form->exit_category, of_loc ? form->loc_where : form->exit_where, loc);
        return;
    }
    if( form->loc_state == EDITOR_INSTANCE_NAME_NO_OPTION )
        no_option = form->loc;
    else if( own_exit && form->exit_state == EDITOR_INSTANCE_NAME_NO_OPTION )
        no_option = form->exit_loc;
    if( no_option )
        snprintf(check->note, sizeof(check->note),
            "note: %.40s has no first option (op1), so a player cannot click it: give it one in the Record panel "
            "(op1=Enter)",
            no_option);
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
    if( !Editor_InstanceSourceCompact(plan, check->message, sizeof(check->message)) )
        return 0;

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
            inside = Editor_InstanceTemplateTile(plan, x, z, &x, &z);
            if( !inside || level < plan->level_lo || level > plan->level_hi )
            {
                snprintf(check->message, sizeof(check->message),
                    "Lands at %d,%d L%d is not in the copy: type an offset in it (32 20), a tile of the source "
                    "squares, or of the template", x, z, level);
                return 0;
            }
            check->entry_level = level;
            check->entry_x = x;
            check->entry_z = z;
        }
        if( form->loc_state == EDITOR_INSTANCE_NAME_TAKEN )
        {
            snprintf(check->message, sizeof(check->message),
                "%.40s already has an [oploc1] (%.80s): pick a loc with none, or edit that script", form->loc,
                form->loc_where);
            return 0;
        }
        if( !instance_validate_exit(form, check) || !instance_validate_npc(form, check) )
            return 0;
    }

    {
        char coord[EDITOR_INSTANCE_COORD_MAX];
        char home[EDITOR_INSTANCE_COORD_MAX];
        char exit_text[EDITOR_INSTANCE_COORD_MAX + 16];
        char npc_text[EDITOR_INSTANCE_LOC_MAX + 8];

        Editor_InstanceCoordText(check->entry_level, check->entry_x, check->entry_z, coord);
        Editor_InstanceCoordText(check->exit_level, check->exit_x, check->exit_z, home);
        exit_text[0] = '\0';
        npc_text[0] = '\0';
        if( check->has_exit )
            snprintf(exit_text, sizeof(exit_text), ", an exit to %s", home);
        if( check->has_npc )
            snprintf(npc_text, sizeof(npc_text), ", npc %.40s", form->npc);
        snprintf(check->message, sizeof(check->message), "creates %d square%s at m%d_%d in %s%s%s%s%s%s%s%s",
            plan->count, plan->count == 1 ? "" : "s", plan->dst_x0, plan->dst_z0,
            instance_storage_label(check->storage), check->creates_lane ? " (new lane)" : "",
            form->map_area ? ", a .wma" : "", form->entry != EDITOR_INSTANCE_ENTRY_NONE ? ", a trigger to " : "",
            form->entry != EDITOR_INSTANCE_ENTRY_NONE ? coord : "", exit_text, npc_text, "; unsaved until Save");
    }
    instance_validate_note(form, check);
    check->ok = 1;
    return 1;
}

void
Editor_InstanceScriptOf(
    const struct Editor_InstanceForm* form,
    const struct Editor_InstanceCheck* check,
    struct Editor_InstanceScript* out)
{
    assert(form);
    assert(check);
    assert(out);
    assert(check->ok);
    assert(form->entry != EDITOR_INSTANCE_ENTRY_NONE);

    memset(out, 0, sizeof(*out));
    out->entry = form->entry;
    snprintf(out->loc, sizeof(out->loc), "%s", form->loc);
    out->level = check->entry_level;
    out->x = check->entry_x;
    out->z = check->entry_z;
    out->spawns = (form->copy & EDITOR_INSTANCE_COPY_SPAWNS) != 0;
    if( check->has_exit )
    {
        snprintf(out->exit_loc, sizeof(out->exit_loc), "%s", form->exit_loc);
        out->exit_level = check->exit_level;
        out->exit_x = check->exit_x;
        out->exit_z = check->exit_z;
    }
    if( check->has_npc )
    {
        snprintf(out->npc, sizeof(out->npc), "%s", form->npc);
        out->npc_level = check->npc_level;
        out->npc_x = check->npc_x;
        out->npc_z = check->npc_z;
    }
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

/**
 * The name of a loc record's category ("dairy_churn"), into `out`; "" when
 * it has none. A dumped record states it as the category's id (category=1175),
 * which the catalog's category pack names.
 */
static void
instance_loc_category(
    struct EditorUi* ui,
    const struct EditorHost_CatalogRecord* record,
    char* out,
    size_t out_size)
{
    const char* value = "";
    char* end = NULL;
    long id;

    out[0] = '\0';
    for( int f = 0; f < record->field_count; f++ )
        if( strcmp(record->fields[f].key, "category") == 0 )
            value = record->fields[f].value ? record->fields[f].value : "";
    if( !value[0] || strcmp(value, "default") == 0 || strcmp(value, "null") == 0 )
        return;
    id = strtol(value, &end, 10);
    if( end && end != value && *end == '\0' )
    {
        struct EditorHost_CatalogRecord category;
        memset(&category, 0, sizeof(category));
        if( id >= 0
            && Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_CATEGORY, (int)id, "", &category)
                   == EDITOR_HOST_OK
            && category.name )
            snprintf(out, out_size, "%s", category.name);
        Editor_HostCatalogRecordFree(&category);
    }
    else
        snprintf(out, out_size, "%s", value);
    if( !Editor_InstanceLocNameValid(out) )
        out[0] = '\0';
}

/**
 * What the lane's catalog says of `name` (a record of `kind`), asked once
 * per name: KNOWN with its id, ABSENT, and for a loc (`trigger` 1) TAKEN
 * when the tree already declares its [oploc1,<name>], or CATEGORY when it
 * declares its category's [oploc1,_<category>], with where. UNCHECKED for a
 * name that is not one, or in a window whose server has no catalog.
 */
static const struct InstanceNameFact*
instance_name_fact(
    struct EditorUi* ui,
    struct InstancePanel* self,
    int slot,
    uint32_t kind,
    const char* name,
    int trigger)
{
    struct InstanceNameFact* fact = &self->facts[slot];
    struct EditorHost_CatalogRecord record;
    enum EditorHost_Status status;

    if( fact->name[0] && strcmp(fact->name, name) == 0 )
        return fact;
    memset(fact, 0, sizeof(*fact));
    fact->id = -1;
    if( !Editor_InstanceLocNameValid(name) || !ui->editor || !ui->editor->host_open
        || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return fact;
    snprintf(fact->name, sizeof(fact->name), "%s", name);
    memset(&record, 0, sizeof(record));
    status = Editor_HostMapEdCatalogRecord(&ui->editor->host, kind, -1, name, &record);
    if( status == EDITOR_HOST_OK )
    {
        fact->state = EDITOR_INSTANCE_NAME_KNOWN;
        fact->id = record.id;
        /* [oploc1,<loc>] answers the loc's first option: one with none
         * (op1 unset, "default" in a dumped record) is never clicked. */
        if( trigger )
        {
            const char* op1 = "";
            for( int f = 0; f < record.field_count; f++ )
                if( strcmp(record.fields[f].key, "op1") == 0 )
                    op1 = record.fields[f].value ? record.fields[f].value : "";
            if( !op1[0] || strcmp(op1, "default") == 0 || strcmp(op1, "hidden") == 0 )
                fact->state = EDITOR_INSTANCE_NAME_NO_OPTION;
        }
    }
    else if( status == EDITOR_HOST_ABSENT )
        fact->state = EDITOR_INSTANCE_NAME_ABSENT;
    if( trigger && status == EDITOR_HOST_OK )
        instance_loc_category(ui, &record, fact->category, sizeof(fact->category));
    Editor_HostCatalogRecordFree(&record);
    if( trigger )
    {
        struct Editor_ScriptRef refs[16];
        int n = Editor_Triggers_ForSubject(ui, name, refs, 16);
        for( int i = 0; i < n; i++ )
            if( strcmp(refs[i].kind, "oploc1") == 0 )
            {
                fact->state = EDITOR_INSTANCE_NAME_TAKEN;
                snprintf(fact->where, sizeof(fact->where), "%.80s:%d", refs[i].rel, refs[i].line);
                break;
            }
        /* A category's trigger, [oploc1,_<category>], answers every loc of
         * that category -- the churn, the range, the bank booth. A trigger
         * of the loc's own name wins over it, so the helper's would quietly
         * take that op away from every such loc in the game. */
        if( fact->state != EDITOR_INSTANCE_NAME_TAKEN && fact->category[0] )
        {
            char subject[EDITOR_INSTANCE_LOC_MAX + 1];
            snprintf(subject, sizeof(subject), "_%s", fact->category);
            n = Editor_Triggers_ForSubject(ui, subject, refs, 16);
            for( int i = 0; i < n; i++ )
                if( strcmp(refs[i].kind, "oploc1") == 0 )
                {
                    fact->state = EDITOR_INSTANCE_NAME_CATEGORY;
                    snprintf(fact->where, sizeof(fact->where), "%.80s:%d", refs[i].rel, refs[i].line);
                    break;
                }
        }
        if( fact->state != EDITOR_INSTANCE_NAME_CATEGORY )
            fact->category[0] = '\0';
    }
    return fact;
}

/**
 * The trigger loc's own tile, for an empty "Returns to": the loc selected in
 * the World when the dialog opened, when it is that loc; else its first
 * placement in a source square this window has open (south-west first, the
 * form's order). 0 when neither knows.
 */
static int
instance_home_tile(
    struct EditorUi* ui,
    struct InstancePanel* self,
    const struct Editor_InstanceForm* form,
    int loc_id,
    int* out_level,
    int* out_x,
    int* out_z)
{
    if( loc_id < 0 )
        return 0;
    if( self->selected_loc_id == loc_id )
    {
        *out_level = self->selected_level;
        *out_x = self->selected_x;
        *out_z = self->selected_z;
        return 1;
    }
    for( int i = 0; ui->editor && i < form->source_count && i < EDITOR_INSTANCE_SQUARES_MAX; i++ )
    {
        const char* slash = strrchr(form->sources[i], '/');
        const struct Editor_Square* square;
        int map_x;
        int map_z;

        if( !Editor_SquareKeyParse(slash ? slash + 1 : form->sources[i], &map_x, &map_z) )
            continue;
        square = Editor_DocFindSquare(&ui->editor->doc, map_x, map_z);
        if( !square || !square->loaded )
            continue;
        for( int l = 0; l < square->loc_count; l++ )
            if( square->locs[l].loc_id == loc_id )
            {
                *out_level = square->locs[l].level;
                *out_x = map_x * EDITOR_SQUARE_X + square->locs[l].x;
                *out_z = map_z * EDITOR_SQUARE_Z + square->locs[l].z;
                return 1;
            }
    }
    return 0;
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
    if( form->entry == EDITOR_INSTANCE_ENTRY_NONE )
        return;

    /* The exit, the npc (a runtime copy's: the row is hidden otherwise, and
     * what it last held is not read), and what the catalog says of each. */
    snprintf(form->exit_loc, sizeof(form->exit_loc), "%s", ToriRSChrome_Text(ui->chrome, self->input_exit));
    if( form->exit_loc[0] )
        snprintf(form->exit_coord, sizeof(form->exit_coord), "%s",
            ToriRSChrome_Text(ui->chrome, self->input_exit_coord));
    if( form->entry == EDITOR_INSTANCE_ENTRY_RUNTIME )
        snprintf(form->npc, sizeof(form->npc), "%s", ToriRSChrome_Text(ui->chrome, self->input_npc));
    if( form->npc[0] )
        snprintf(form->npc_coord, sizeof(form->npc_coord), "%s",
            ToriRSChrome_Text(ui->chrome, self->input_npc_coord));
    {
        const struct InstanceNameFact* fact =
            instance_name_fact(ui, self, INSTANCE_FACT_LOC, TORIRSMAPED_CATALOG_LOC, form->loc, 1);
        form->loc_state = fact->state;
        snprintf(form->loc_where, sizeof(form->loc_where), "%s", fact->where);
        snprintf(form->loc_category, sizeof(form->loc_category), "%s", fact->category);
        form->home_known =
            instance_home_tile(ui, self, form, fact->id, &form->home_level, &form->home_x, &form->home_z);
    }
    if( form->exit_loc[0] )
    {
        const struct InstanceNameFact* fact =
            instance_name_fact(ui, self, INSTANCE_FACT_EXIT, TORIRSMAPED_CATALOG_LOC, form->exit_loc, 1);
        form->exit_state = fact->state;
        snprintf(form->exit_where, sizeof(form->exit_where), "%s", fact->where);
        snprintf(form->exit_category, sizeof(form->exit_category), "%s", fact->category);
    }
    if( form->npc[0] )
        form->npc_state =
            instance_name_fact(ui, self, INSTANCE_FACT_NPC, TORIRSMAPED_CATALOG_NPC, form->npc, 0)->state;
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
    int const runtime = form->entry == EDITOR_INSTANCE_ENTRY_RUNTIME;

    ToriRSChrome_SetHidden(ui->chrome, self->input_lane, !new_lane);
    ToriRSChrome_SetHidden(ui->chrome, self->input_loc, !has_entry);
    ToriRSChrome_SetHidden(ui->chrome, self->input_coord, !has_entry);
    /* A row that does not apply is not shown: the exit's tile only with an
     * exit loc, the npc only for a runtime copy, its tile only with an npc. */
    ToriRSChrome_SetHidden(ui->chrome, self->input_exit, !has_entry);
    ToriRSChrome_SetHidden(ui->chrome, self->input_exit_coord, !has_entry || !form->exit_loc[0]);
    ToriRSChrome_SetHidden(ui->chrome, self->input_npc, !runtime);
    ToriRSChrome_SetHidden(ui->chrome, self->input_npc_coord, !runtime || !form->npc[0]);
}

/** The two sentences under the fields: where the template is and what
 *  happens at play time, for the plan as far as the form states it. */
static void
instance_where_rows(
    struct EditorUi* ui,
    struct InstancePanel* self,
    const struct Editor_InstanceForm* form,
    const struct Editor_InstanceCheck* check)
{
    char where[EDITOR_INSTANCE_WHERE_MAX];
    char stamp[EDITOR_INSTANCE_WHERE_MAX];

    if( check && check->plan.count > 0 && form->dst_x >= 0 && Editor_InstanceNameValid(form->name) )
    {
        /* The form's destination, whether or not the check got as far as
         * taking it (a taken square is still where the form points). */
        struct Editor_InstancePlan plan = check->plan;
        plan.dst_x0 = form->dst_x;
        plan.dst_z0 = form->dst_z;
        Editor_InstanceWhereText(&plan, form->name, form->entry, where, stamp);
    }
    else
    {
        /* Before there is a destination and a name: what the dialog makes. */
        struct Editor_InstancePlan none;
        char unused[EDITOR_INSTANCE_WHERE_MAX];
        memset(&none, 0, sizeof(none));
        none.w = none.h = 1;
        snprintf(where, sizeof(where),
            "Template: a copy of the source squares in instance space (map squares x %d..%d, z %d..%d), which no "
            "world-map area claims.",
            EDITOR_INSTANCE_X0, EDITOR_INSTANCE_X1, EDITOR_INSTANCE_Z0, EDITOR_INSTANCE_Z1);
        Editor_InstanceWhereText(&none, "x", form->entry, unused, stamp);
    }
    ToriRSChrome_SetText(ui->chrome, self->row_where[0], where);
    ToriRSChrome_SetText(ui->chrome, self->row_where[1], stamp);
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
        /* No atlas yet: the catalog index is still being read (the first
         * minute or two after the editor starts), and the form is checked
         * again when it lands (the form is not remembered as checked). */
        ToriRSChrome_SetText(ui->chrome, self->row_check,
            "the square atlas is not read yet (the catalog index takes a minute or two after the editor starts): "
            "Create waits for it");
        ToriRSChrome_SetColor(ui->chrome, self->row_check, 0xff7f5f);
        ToriRSChrome_SetHidden(ui->chrome, self->row_note, 1);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_create, 1);
        instance_where_rows(ui, self, &form, NULL);
        self->checked_valid = 0;
        self->ok = 0;
        return;
    }
    self->ok = Editor_InstanceValidate(picker, &form, &check);
    ToriRSChrome_SetText(ui->chrome, self->row_check, check.message);
    ToriRSChrome_SetColor(ui->chrome, self->row_check, self->ok ? 0x9fdf8f : 0xff7f5f);
    ToriRSChrome_SetText(ui->chrome, self->row_note, self->ok ? check.note : "");
    ToriRSChrome_SetHidden(ui->chrome, self->row_note, !self->ok || !check.note[0]);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_create, !self->ok);
    instance_where_rows(ui, self, &form, &check);
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
    /* The picker's selection when it has one (its Instance... button, or
     * squares shift-clicked before File > New instance...), else the squares
     * that are open: the dialog never opens on "no squares" with a world up. */
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_source, self->source_rows, n,
        Editor_SquarePicker_Sources(ui, EDITOR_SQUARE_PICKER_SELECTION, keys, EDITOR_INSTANCE_SQUARES_MAX) > 0 ? 0 : 1);
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
    {
        char why[EDITOR_INSTANCE_MESSAGE_MAX];
        if( !Editor_InstanceSourceCompact(&plan, why, sizeof(why)) )
        {
            Editor_UiStatus(ui, "Find free refused: %s", why);
            instance_check(ui, self, 1);
            return 0;
        }
    }
    count = instance_taken(picker, &taken);
    found = Editor_InstanceFindFree(taken, count, plan.w, plan.h, &x, &z);
    snprintf(text, sizeof(text), "instance space 0..19 x 100..255 has %d free",
        Editor_InstanceFreeCount(taken, count));
    ToriRSChrome_SetText(ui->chrome, self->row_free, text);
    ToriRSChrome_SetHidden(ui->chrome, self->row_free, 0);
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

/**
 * The loc selected in the World as the trigger: its name typed into "Trigger
 * loc" and its tile remembered for "Returns to". Select the ladder, File >
 * New instance..., name it, Create. With no loc selected the field keeps
 * what it held.
 */
static void
instance_open_selection(
    struct EditorUi* ui,
    struct InstancePanel* self)
{
    const struct Editor_LocRef* first;
    struct EditorHost_CatalogRecord record;

    self->selected_loc_id = -1;
    if( ui->tools.loc_count <= 0 || !ui->editor || !ui->editor->host_open
        || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return;
    first = &ui->tools.locs[0];
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_LOC, first->loc_id, "", &record)
            == EDITOR_HOST_OK
        && record.name && Editor_InstanceLocNameValid(record.name) )
    {
        self->selected_loc_id = first->loc_id;
        self->selected_level = first->level;
        self->selected_x = first->map_x * EDITOR_SQUARE_X + first->x;
        self->selected_z = first->map_z * EDITOR_SQUARE_Z + first->z;
        ToriRSChrome_SetText(ui->chrome, self->input_loc, record.name);
    }
    Editor_HostCatalogRecordFree(&record);
}

void
Editor_Instance_Open(struct EditorUi* ui)
{
    struct InstancePanel* self;

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
    ToriRSChrome_SetHidden(ui->chrome, self->row_free, 1);
    ToriRSChrome_SetText(ui->chrome, self->input_coord, "");
    ToriRSChrome_SetText(ui->chrome, self->input_exit, "");
    ToriRSChrome_SetText(ui->chrome, self->input_exit_coord, "");
    ToriRSChrome_SetText(ui->chrome, self->input_npc, "");
    ToriRSChrome_SetText(ui->chrome, self->input_npc_coord, "");
    memset(self->facts, 0, sizeof(self->facts));
    instance_open_selection(ui, self);
    ToriRSChrome_RadioSelect(ui->chrome, self->radio_map[0]);
    /* The entry: a private copy per player wherever the lane's server takes
     * map instances (TORIRSMAPED_CAP_MAP_INSTANCE: this tree's torirsserver
     * over a content tree with the map_instance procs, torirs_maped_live.c),
     * else the teleport into the template, the runtime chip disabled and
     * saying why. */
    ToriRSChrome_RadioSelect(ui->chrome,
        self->radio_entry[instance_runtime_capable(ui) ? EDITOR_INSTANCE_ENTRY_RUNTIME : EDITOR_INSTANCE_ENTRY_TELEPORT]);
    ToriRSChrome_SetDisabled(ui->chrome, self->radio_entry[EDITOR_INSTANCE_ENTRY_RUNTIME], !instance_runtime_capable(ui));
    ToriRSChrome_SetTooltip(ui->chrome, self->radio_entry[EDITOR_INSTANCE_ENTRY_RUNTIME],
        instance_runtime_capable(ui) ? "Each player who enters gets a private copy of the template"
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

/* The scene "instance:<name>" (the SCENES document, editor_scenes.h): the new
 * squares, the pinned level and the camera on the landing tile (the
 * trigger's, else Editor_InstanceEntryTile's default), inside the Create's stroke so it undoes with the
 * rest. The view's yaw, pitch and zoom are the World's when the window has
 * one, else the editor's own defaults. Unsaved until Save, like every other
 * document of the instance. 1 recorded; 0 when the window has no scenes
 * document or the name is taken (the collection `name` still finds the
 * instance: the picker's Find "instance:<name>"). */
static int
instance_scene_hook(
    struct EditorUi* ui,
    const struct Editor_InstanceCheck* check,
    const char* name)
{
    struct Editor_Scene scene;
    char scene_name[EDITOR_SCENE_NAME_MAX];
    const struct Editor_InstancePlan* plan = &check->plan;
    int level = check->entry_level;
    int const x = check->entry_x;
    int const z = check->entry_z;

    if( snprintf(scene_name, sizeof(scene_name), "instance:%s", name) >= (int)sizeof(scene_name) )
        return 0;
    Editor_SceneInitRow(&scene, scene_name);
    for( int i = 0; i < plan->count && scene.square_count < EDITOR_SCENE_SQUARES_MAX; i++ )
    {
        char bare[EDITOR_DOC_KEY_MAX];
        int mx;
        int mz;
        Editor_InstanceDest(plan, i, &mx, &mz);
        Editor_SquareKey(bare, mx, mz);
        if( check->storage[0] )
            snprintf(scene.squares[scene.square_count], sizeof(scene.squares[0]), "%s/%s", check->storage, bare);
        else
            snprintf(scene.squares[scene.square_count], sizeof(scene.squares[0]), "%s", bare);
        scene.square_count++;
    }
    if( scene.square_count > 0 )
        scene.segments |= EDITOR_SCENE_SEG_SQUARES;
    if( level < 0 || level > 3 )
        level = 0;
    scene.level = level;
    scene.segments |= EDITOR_SCENE_SEG_LEVEL;
    if( x >= 0 && z >= 0 && x <= EDITOR_SCENE_TILE_MAX && z <= EDITOR_SCENE_TILE_MAX )
    {
        struct EditorViewCamera view;
        memset(&view, 0, sizeof(view));
        view.yaw = 0;
        view.pitch = 400;
        view.zoom = 1800;
        if( ui->app_services.world_camera )
            (void)ui->app_services.world_camera(ui->app_services.user_data, level, &view);
        scene.camera.x = x * 128 + 64;
        scene.camera.z = z * 128 + 64;
        scene.camera.level = level;
        scene.camera.yaw = view.yaw & 2047;
        scene.camera.pitch = view.pitch & 2047;
        scene.camera.zoom = view.zoom < 0 ? 0 : view.zoom > EDITOR_SCENE_ZOOM_MAX ? EDITOR_SCENE_ZOOM_MAX : view.zoom;
        scene.segments |= EDITOR_SCENE_SEG_CAMERA;
    }
    snprintf(scene.note, sizeof(scene.note), "made by the instance helper");
    scene.segments |= EDITOR_SCENE_SEG_NOTE;
    return Editor_Scenes_AddInStroke(ui, &scene);
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
    self->created_scene = 0;

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
        char text[8192];
        struct Editor_InstanceScript script;
        size_t length;
        Editor_InstanceTriggerKey(form.name, key);
        Editor_InstanceScriptOf(&form, &check, &script);
        length = Editor_InstanceTriggerText(&check.plan, form.name, &script, text, sizeof(text));
        assert(length < sizeof(text));
        if( Builder_TextSync(&submit, key, text, length, 1) == BUILDER_SYNC_REFUSED )
        {
            snprintf(message, sizeof(message), "the authority refused %s", key + 4);
            failed = 1;
        }
    }
    if( !failed )
        self->created_scene = instance_scene_hook(ui, &check, form.name);
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

    {
        /* What was made, where the template is and how it is stamped: the
         * Output console keeps all of it. */
        char where[EDITOR_INSTANCE_WHERE_MAX];
        char stamp[EDITOR_INSTANCE_WHERE_MAX];

        Editor_InstanceWhereText(&check.plan, form.name, form.entry, where, stamp);
        Editor_UiLog(ui,
            "Created instance %s: %d square%s at m%d_%d, %d spawn%s%s%s%s (unsaved until Save); Ctrl+Z undoes it",
            form.name, self->created_squares, self->created_squares == 1 ? "" : "s", check.plan.dst_x0,
            check.plan.dst_z0, self->created_spawns, self->created_spawns == 1 ? "" : "s",
            form.map_area ? ", a .wma" : "", form.entry != EDITOR_INSTANCE_ENTRY_NONE ? ", a trigger" : "",
            self->created_scene ? ", a scene" : "");
        if( form.entry != EDITOR_INSTANCE_ENTRY_NONE )
            Editor_UiLog(ui, "Its script: server/scripts/edited/%s/scripts/%s.rs2 (the Triggers panel opens it in your "
                             "editor)",
                form.name, form.name);
        Editor_UiLog(ui, "%s", where);
        Editor_UiLog(ui, "%s", stamp);
    }
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
    /* The status bar's sentence -- where the instance is templated and how
     * it is stamped -- is said by the tick once the template has opened in
     * the World: the open says a sentence of its own about the squares it
     * replaced, a frame or more from now, and the bar must be left on this
     * one. */
    Editor_InstanceStatusText(&check.plan, form.name, form.entry, self->status_pending);
    self->status_wait = INSTANCE_STATUS_SETTLE_FRAMES;
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
    case EDITOR_INSTANCE_W_EXIT:
        return self->input_exit;
    case EDITOR_INSTANCE_W_EXIT_COORD:
        return self->input_exit_coord;
    case EDITOR_INSTANCE_W_NPC:
        return self->input_npc;
    case EDITOR_INSTANCE_W_NPC_COORD:
        return self->input_npc_coord;
    case EDITOR_INSTANCE_W_WHERE_TEMPLATE:
        return self->row_where[0];
    case EDITOR_INSTANCE_W_WHERE_STAMP:
        return self->row_where[1];
    case EDITOR_INSTANCE_W_NOTE:
        return self->row_note;
    }
    assert(0);
    return -1;
}

/* ---- the module ------------------------------------------------------------------ */

/**
 * Keep the dialog inside the window. A content-sized panel is as tall as its
 * rows and never scrolls; with the exit, npc and note rows up it outgrew a
 * 900-pixel window and Create sat below the bottom edge, where nobody could
 * press it. Taller than the window, it is pinned to the window's height and
 * scrolls; shorter again, it goes back to its own height.
 */
static void
instance_fit(
    struct EditorUi* ui,
    struct InstancePanel* self)
{
    struct ToriRSChromePanel const* p = &ui->chrome->panels[self->panel];
    int const room = ui->chrome->surface_h - edp_px(ui, 16);
    int natural;

    if( ui->chrome->surface_h <= 0 || p->view_h <= 0 )
        return;
    /* The rows' height plus the panel's own title, padding and edges. */
    natural = p->content_h + (p->h - p->view_h);
    ToriRSChrome_PanelSetFixedHeight(ui->chrome, self->panel, natural > room ? room : 0);
}

static void
instance_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct InstancePanel* self = opaque;

    if( self->status_pending[0] )
    {
        /* Create's last word (Editor_Instance_Create): after the World has
         * stopped loading the template, and a few quiet frames more. */
        if( ui->app_services.world_busy && ui->app_services.world_busy(ui->app_services.user_data) )
            self->status_wait = INSTANCE_STATUS_SETTLE_FRAMES;
        else if( --self->status_wait <= 0 )
        {
            Editor_UiStatus(ui, "%s", self->status_pending);
            self->status_pending[0] = '\0';
        }
    }
    if( !self->shown )
        return;
    if( !ui->chrome->panels[self->panel].visible )
    {
        self->shown = 0; /* Escape or the close box */
        Editor_SquarePicker_Mark(ui, 0, 0, 0, 0);
        return;
    }
    instance_check(ui, self, 0);
    instance_fit(ui, self);
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
        || widget == self->input_loc || widget == self->input_coord || widget == self->input_exit
        || widget == self->input_exit_coord || widget == self->input_npc || widget == self->input_npc_coord )
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
    /* The runtime entry first: it is the default wherever the lane's server
     * takes map instances (Editor_Instance_Open). */
    self->radio_entry[EDITOR_INSTANCE_ENTRY_RUNTIME] =
        ToriRSChrome_Radio(chrome, self->panel, "Entry: runtime map instance", INSTANCE_GROUP_ENTRY, 0);
    self->radio_entry[EDITOR_INSTANCE_ENTRY_TELEPORT] =
        ToriRSChrome_Radio(chrome, self->panel, "Entry: teleport trigger", INSTANCE_GROUP_ENTRY, 1);
    ToriRSChrome_SetTooltip(chrome, self->radio_entry[EDITOR_INSTANCE_ENTRY_TELEPORT],
        "Everyone is teleported into the template itself");
    self->radio_entry[EDITOR_INSTANCE_ENTRY_NONE] =
        ToriRSChrome_Radio(chrome, self->panel, "Entry: none", INSTANCE_GROUP_ENTRY, 0);
    ToriRSChrome_SetTooltip(chrome, self->radio_entry[EDITOR_INSTANCE_ENTRY_NONE], "No script: the squares only");
    self->input_loc = ToriRSChrome_TextInput(chrome, self->panel, "Trigger loc", "");
    ToriRSChrome_SetTooltip(chrome, self->input_loc, "Its first option enters; select it in the World first");
    self->input_coord = ToriRSChrome_TextInput(chrome, self->panel, "Lands at", "");
    ToriRSChrome_SetTooltip(chrome, self->input_coord, "0_16_101_32_32; empty: first square's middle");
    self->input_exit = ToriRSChrome_TextInput(chrome, self->panel, "Exit loc", "");
    ToriRSChrome_SetTooltip(chrome, self->input_exit, "Its first option leaves; empty: no exit block");
    self->input_exit_coord = ToriRSChrome_TextInput(chrome, self->panel, "Returns to", "");
    ToriRSChrome_SetTooltip(chrome, self->input_exit_coord, "x z [level], real map; empty: the trigger loc's tile");
    self->input_npc = ToriRSChrome_TextInput(chrome, self->panel, "Npc on enter", "");
    ToriRSChrome_SetTooltip(chrome, self->input_npc, "Spawn npc on enter: its name, added to each copy; empty: none");
    self->input_npc_coord = ToriRSChrome_TextInput(chrome, self->panel, "Npc at", "");
    ToriRSChrome_SetTooltip(chrome, self->input_npc_coord, "A template tile; empty: 2 tiles north of the landing");
    /* Where the template is and what happens at play time (instance_where_rows). */
    self->row_where[0] = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_where[1] = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_check = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_note = ToriRSChrome_LabelColored(chrome, self->panel, "", 0xdfbf5f);
    ToriRSChrome_SetHidden(chrome, self->row_note, 1);
    self->button_create = ToriRSChrome_Button(chrome, self->panel, "Create");
    self->button_cancel = ToriRSChrome_Button(chrome, self->panel, "Cancel");
    /* Thirty rows: in a window shorter than they are, they scroll. */
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
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

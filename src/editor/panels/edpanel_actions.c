/*
 * The actions more than one control reaches: the loc selection's nudge,
 * rotate, reshape, duplicate and delete (menu bar, locs panel, arrow keys, R,
 * S), and the rectangle's copy, paste, fill and clear (menu bar, rectangle
 * panel). Each is: build the commands against the mirror (edpanel_ops.h),
 * submit them as one undo step, re-point the selection, say what happened.
 *
 * Also the square copy that brings its spawns (edp_square_create) and the
 * clearing of a placed scratch square's slot (edp_scratch_clear): the New
 * square dialog and the Square map's Place both end there.
 */

#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "engine/torirs_types.h"
#include "edpanel_ops_stamps.h"
#include "edpanels.h"
#include "editor/editor_lanerecs.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdio.h>
#include <string.h>

/* The clipboard outlives any one panel: copy in the menu, paste from a key. */
static struct Editor_TileClip* g_clip;

/** "Move" for "move": the verb as a status sentence starts it. */
static void
actions_verb(
    const char* what,
    char* out,
    size_t size)
{
    snprintf(out, size, "%s", what);
    if( out[0] >= 'a' && out[0] <= 'z' )
        out[0] = (char)(out[0] - 'a' + 'A');
}

/** Say what a selection op did beyond the obvious: whom a move swapped with,
 *  what a reshape or copy could not land on, or that nothing it names is
 *  there any more. Silence here is what made a refused nudge look stuck. */
static void
actions_report(
    struct EditorUi* ui,
    const char* what,
    const struct Editor_LocOpResult* result,
    int applied)
{
    if( result->swapped > 0 )
        Editor_UiLog(ui, "editor: %s: swapped places with loc %d%s (that tile already held one of that shape)", what,
            result->swapped_loc_id, result->swapped > 1 ? " and others" : "");
    if( result->blocked > 0 )
    {
        char verb[32];
        int wx;
        int wz;
        Editor_LocRefWorld(&result->blocked_at, &wx, &wz);
        actions_verb(what, verb, sizeof(verb));
        /* A refusal (R8): the status bar says it, the console keeps it. */
        Editor_UiStatus(ui, "%s refused: tile %d,%d L%d holds a %s of that shape (loc %d); %d loc%s left as %s", verb,
            wx, wz, result->blocked_at.level, Editor_LocShapeName(result->blocked_at.shape),
            result->blocked_at.loc_id, result->blocked, result->blocked == 1 ? "" : "s",
            result->blocked == 1 ? "it was" : "they were");
    }
    if( applied == 0 && result->blocked == 0 && result->command_count == 0 )
    {
        char verb[32];
        actions_verb(what, verb, sizeof(verb));
        Editor_UiStatus(ui, "%s refused: the selected loc%s no longer there (undone or deleted elsewhere)", verb,
            ui->tools.loc_count == 1 ? " is" : "s are");
    }
}

static void
actions_selection_moved(
    struct EditorUi* ui,
    const struct Editor_LocOpResult* result,
    int keep)
{
    if( !keep )
        return;
    ui->tools.loc_count = result->ref_count;
    memcpy(ui->tools.locs, result->refs, sizeof(result->refs[0]) * (size_t)result->ref_count);
    if( ui->hooks.selection_changed )
        ui->hooks.selection_changed(ui->hooks_user);
}

static int
actions_need_selection(
    struct EditorUi* ui,
    const char* what)
{
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: %s needs an editor session", what);
        return 0;
    }
    if( ui->tools.loc_count == 0 )
    {
        char verb[32];
        actions_verb(what, verb, sizeof(verb));
        Editor_UiStatus(ui, "%s refused: select a loc first (Shift+click adds more)", verb);
        return 0;
    }
    return 1;
}

/* Loc records this window's client asked its cache for and has not been
 * given: which, and since when on the chrome's clock (edp_loc_shapes notes
 * the first ask and forgets one that arrives). A record the cache holds
 * answers within a frame or two; one still absent ACTIONS_LOC_LOAD_MS later
 * is one the cache does not have. A few slots: the ghost asks about the pick
 * every frame while a selection asks about its own locs. */
#define ACTIONS_LOC_LOAD_MS 1500
#define ACTIONS_LOC_WAIT_SLOTS 4
static struct
{
    int valid;
    int loc_id;
    int64_t since_ms;
} g_loc_wait[ACTIONS_LOC_WAIT_SLOTS];
static int g_loc_wait_next;

/** How long loc `loc_id` has been waited for, in ms; the first call for it
 *  starts the wait (0). */
static int64_t
actions_loc_waited(
    const struct EditorUi* ui,
    int loc_id)
{
    int64_t const now = (int64_t)ui->chrome->now_ms;

    for( int i = 0; i < ACTIONS_LOC_WAIT_SLOTS; i++ )
        if( g_loc_wait[i].valid && g_loc_wait[i].loc_id == loc_id )
            return now - g_loc_wait[i].since_ms;
    g_loc_wait[g_loc_wait_next].valid = 1;
    g_loc_wait[g_loc_wait_next].loc_id = loc_id;
    g_loc_wait[g_loc_wait_next].since_ms = now;
    g_loc_wait_next = (g_loc_wait_next + 1) % ACTIONS_LOC_WAIT_SLOTS;
    return 0;
}

static void
actions_loc_arrived(int loc_id)
{
    for( int i = 0; i < ACTIONS_LOC_WAIT_SLOTS; i++ )
        if( g_loc_wait[i].valid && g_loc_wait[i].loc_id == loc_id )
            g_loc_wait[i].valid = 0;
}

/**
 * Refuse `verb` because loc `loc_id`'s record is not resident, saying which
 * of the two reasons it is (W6, 2026-11-09). While it may still be loading:
 * "still loading; try again in a moment", as before. Once it has been absent
 * ACTIONS_LOC_LOAD_MS: nobody has such a record -- not the cache, and not
 * the lane, whose own records (a loc made by "Copy to new") this window is
 * given from their text the moment they exist (editor/editor_lanerecs.h;
 * until 2026-11-11 it was not, and this sentence said "RUN builds it"). What
 * is left is an id whose record went: a copy that was undone, or a
 * placement of a record another lane made. A harness with no clock (now_ms
 * 0) keeps the first sentence.
 */
static void
actions_loc_absent(
    struct EditorUi* ui,
    const char* verb,
    int loc_id)
{
    char name[64];
    char gone[EDITOR_CMD_NAME_MAX];

    /* A copy undone is known at once: no wait, no "still loading". */
    if( Editor_LaneRecords_Gone(0, loc_id, gone, sizeof(gone)) )
    {
        Editor_UiStatus(ui, "%s refused: %.40s (loc %d) is gone: the Copy to new that made it was undone (Ctrl+Y)",
            verb, gone, loc_id);
        return;
    }
    if( actions_loc_waited(ui, loc_id) < ACTIONS_LOC_LOAD_MS )
    {
        Editor_UiStatus(ui, "%s refused: loc %d is still loading; try again in a moment", verb, loc_id);
        return;
    }
    /* Its name: the server's catalog knows a new record; else the Catalog
     * panel's own name for the pick ("#70000" until it has one). */
    snprintf(name, sizeof(name), "loc %d", loc_id);
    if( ui->tools.pick_kind == 0 && ui->tools.pick_id == loc_id && ui->tools.pick_name[0]
        && ui->tools.pick_name[0] != '#' )
        snprintf(name, sizeof(name), "%.28s (loc %d)", ui->tools.pick_name, loc_id);
    if( ui->editor && (ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        struct EditorHost_CatalogRecord record;
        memset(&record, 0, sizeof(record));
        if( Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_LOC, loc_id, "", &record)
                == EDITOR_HOST_OK
            && record.name && record.name[0] )
            snprintf(name, sizeof(name), "%.28s (loc %d)", record.name, loc_id);
        Editor_HostCatalogRecordFree(&record);
    }
    /* Inside EDITOR_UI_CONSOLE_LINE_MAX with the longest verb and name. */
    Editor_UiStatus(ui,
        "%s refused: %s has no record in the cache or in this lane (a Copy to new that was undone? Ctrl+Y redoes it)",
        verb, name);
}

static int
actions_reshape_to(
    struct EditorUi* ui,
    const char* what,
    const int* shapes,
    const int* rotations);

/** Result scratch: one op at a time, and it is ~8 KB. */
static struct Editor_LocOpResult g_result;

int
edp_locs_nudge(
    struct EditorUi* ui,
    int dx,
    int dz)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "nudge") )
        return 0;
    count = Editor_LocOpsNudge(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, dx, dz, commands, capacity, &g_result);
    applied = edp_apply(ui, commands, count, "nudge");
    actions_report(ui, "nudge", &g_result, applied);
    actions_selection_moved(ui, &g_result, applied > 0);
    return applied;
}

int
edp_locs_move_to(
    struct EditorUi* ui,
    int world_x,
    int world_z)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "move") )
        return 0;
    count = Editor_LocOpsMoveTo(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, world_x, world_z, commands, capacity,
        &g_result);
    applied = edp_apply(ui, commands, count, "move");
    actions_report(ui, "move", &g_result, applied);
    actions_selection_moved(ui, &g_result, applied > 0);
    return applied;
}

int
edp_locs_rotate(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "rotate") )
        return 0;
    count = Editor_LocOpsRotate(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, 1, commands, capacity, &g_result);
    applied = edp_apply(ui, commands, count, "rotate");
    actions_report(ui, "rotate", &g_result, applied);
    actions_selection_moved(ui, &g_result, applied > 0);
    return applied;
}

int
edp_locs_reshape(struct EditorUi* ui)
{
    int shapes[EDITOR_LOC_SELECTION_MAX];

    assert(ui);
    if( !actions_need_selection(ui, "reshape") )
        return 0;
    /* Each loc to its own next shape: a wall cycles its wall shapes, never
     * to one it has no model for. */
    for( int i = 0; i < ui->tools.loc_count; i++ )
    {
        uint32_t mask;
        shapes[i] = -1;
        if( !edp_loc_shapes(ui, ui->tools.locs[i].loc_id, &mask) )
            actions_loc_absent(ui, "Reshape", ui->tools.locs[i].loc_id);
        else if( mask != 0 )
            shapes[i] = Editor_LocShapeNext(mask, ui->tools.locs[i].shape);
        else
            Editor_UiStatus(ui, "Reshape refused: loc %d has no model for any shape", ui->tools.locs[i].loc_id);
    }
    return actions_reshape_to(ui, "reshape", shapes, NULL);
}

int
edp_locs_duplicate(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "duplicate") )
        return 0;
    /* One tile east: beside the original, so the copy is visible and the
     * arrow keys carry it from there. */
    count = Editor_LocOpsDuplicate(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, 1, 0, commands, capacity, &g_result);
    applied = edp_apply(ui, commands, count, "duplicate");
    actions_report(ui, "duplicate", &g_result, applied);
    actions_selection_moved(ui, &g_result, applied > 0);
    if( applied > 0 )
        Editor_UiLog(ui, "editor: duplicated %d loc(s); the copies are selected", applied);
    return applied;
}

void
edp_locs_say_deleted(
    struct EditorUi* ui,
    const struct Editor_LocRef* ref,
    int applied)
{
    struct ToriRS_Location* cfg = NULL;
    char name[64];
    int wx;
    int wz;

    assert(ui);
    assert(ref);
    assert(applied > 0);
    if( applied > 1 )
    {
        Editor_UiStatus(ui, "Deleted %d locs; Ctrl+Z undoes it", applied);
        return;
    }
    if( ui->app_services.loc_record )
        cfg = ui->app_services.loc_record(ui->app_services.user_data, ref->loc_id);
    if( cfg && cfg->name[0] )
        snprintf(name, sizeof(name), "%s", cfg->name);
    else
        snprintf(name, sizeof(name), "loc %d", ref->loc_id);
    Editor_LocRefWorld(ref, &wx, &wz);
    Editor_UiStatus(ui, "Deleted %s at %d,%d L%d; Ctrl+Z undoes it", name, wx, wz, ref->level);
}

int
edp_locs_delete(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "delete") )
        return 0;
    count = Editor_LocOpsDelete(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, commands, capacity, &g_result);
    if( count == 0 )
    {
        /* Every selected loc is "(gone)" in the Locs panel: undone, or
         * deleted from another window. Say so rather than do nothing. */
        Editor_UiLog(ui, "editor: delete: the selected loc%s no longer there (undone or deleted elsewhere)",
            ui->tools.loc_count == 1 ? " is" : "s are");
        return 0;
    }
    applied = edp_apply(ui, commands, count, "delete");
    if( applied > 0 )
    {
        Editor_UiLog(ui, "editor: deleted %d loc(s)", applied);
        edp_locs_say_deleted(ui, &ui->tools.locs[0], applied);
        ui->tools.loc_count = 0;
        if( ui->hooks.selection_changed )
            ui->hooks.selection_changed(ui->hooks_user);
    }
    return applied;
}

/* ---- a loc's shapes ------------------------------------------------------------ */

int
edp_place_fit_enabled(const struct EditorUi* ui)
{
    assert(ui);
    return ui->tools.place_fit;
}

void
edp_place_fit_set(
    struct EditorUi* ui,
    int on)
{
    assert(ui);
    /* The next tick publishes it with the rest of the tools. */
    ui->tools.place_fit = on ? 1 : 0;
}

int
edp_loc_shapes(
    struct EditorUi* ui,
    int loc_id,
    uint32_t* out_mask)
{
    struct ToriRS_Location* cfg;

    assert(ui);
    assert(out_mask);
    assert(loc_id >= 0);
    if( !ui->app_services.loc_record )
    {
        *out_mask = (1u << EDITOR_LOC_SHAPE_COUNT) - 1u;
        return 1;
    }
    cfg = ui->app_services.loc_record(ui->app_services.user_data, loc_id);
    if( !cfg )
    {
        if( ui->app_services.loc_request )
            ui->app_services.loc_request(ui->app_services.user_data, loc_id);
        /* The wait starts at the first ask, so a refusal can tell a record
         * still loading from one the cache will never give. */
        (void)actions_loc_waited(ui, loc_id);
        return 0;
    }
    actions_loc_arrived(loc_id);
    *out_mask = Editor_LocShapeMask(cfg);
    return 1;
}

/** "10 centrepiece, 0 wall, 2 wall L" into `out`. */
static void
actions_shape_list_text(
    uint32_t mask,
    char* out,
    size_t size)
{
    int shapes[EDITOR_LOC_SHAPE_COUNT];
    int const count = Editor_LocShapeList(mask, shapes);
    size_t used = 0;

    out[0] = '\0';
    for( int i = 0; i < count && used + 1 < size; i++ )
        used += (size_t)snprintf(out + used, size - used, "%s%d %s", i ? ", " : "", shapes[i],
            Editor_LocShapeName(shapes[i]));
    if( count == 0 )
        snprintf(out, size, "none");
}

int
edp_loc_shape_check(
    struct EditorUi* ui,
    int loc_id,
    int shape,
    const char* what)
{
    uint32_t mask;
    char list[160];
    char verb[32];

    assert(ui);
    assert(what);
    actions_verb(what, verb, sizeof(verb));
    if( !edp_loc_shapes(ui, loc_id, &mask) )
    {
        actions_loc_absent(ui, verb, loc_id);
        return 0;
    }
    if( shape >= 0 && shape < EDITOR_LOC_SHAPE_COUNT && (mask & (1u << shape)) )
        return 1;
    actions_shape_list_text(mask, list, sizeof(list));
    Editor_UiStatus(ui, "%s refused: loc %d has no model for shape %d (%s); its shapes: %s", verb, loc_id, shape,
        shape >= 0 && shape < EDITOR_LOC_SHAPE_COUNT ? Editor_LocShapeName(shape) : "?", list);
    return 0;
}

int
edp_place_pose_follow(struct EditorUi* ui)
{
    uint32_t mask;
    int first;

    assert(ui);
    if( ui->tools.pick_kind != 0 || ui->tools.pick_id < 0 )
        return 0;
    if( !edp_loc_shapes(ui, ui->tools.pick_id, &mask) || mask == 0 )
        return 0;
    if( ui->tools.place_shape >= 0 && ui->tools.place_shape < EDITOR_LOC_SHAPE_COUNT
        && (mask & (1u << ui->tools.place_shape)) )
        return 0;
    first = Editor_LocShapeFirst(mask);
    Editor_UiLog(ui, "editor: loc %d has no shape %d; the Place pose is %d %s", ui->tools.pick_id,
        ui->tools.place_shape, first, Editor_LocShapeName(first));
    ui->tools.place_shape = first;
    return 1;
}

void
edp_place_reshape(struct EditorUi* ui)
{
    uint32_t mask = 0;

    assert(ui);
    if( ui->tools.pick_kind == 0 && ui->tools.pick_id >= 0 && edp_loc_shapes(ui, ui->tools.pick_id, &mask) )
        ui->tools.place_shape = Editor_LocShapeNext(mask, ui->tools.place_shape);
    else
        ui->tools.place_shape = Editor_LocShapeCycle(ui->tools.place_shape);
    Editor_UiLog(ui, "editor: place shape %d %s", ui->tools.place_shape,
        ui->tools.place_shape >= 0 && ui->tools.place_shape < EDITOR_LOC_SHAPE_COUNT
            ? Editor_LocShapeName(ui->tools.place_shape)
            : "");
}

void
edp_place_rotate(struct EditorUi* ui)
{
    assert(ui);
    ui->tools.place_rotation = (ui->tools.place_rotation + 1) & 3;
    /* In the bar, as an armed stamp's turn is (2026-11-11: Q on a picked
     * bookcase left "Picked bookcase" there, as if the key had missed). */
    Editor_UiStatus(ui, "Place: turned to %d degrees; Q turns it on, a click puts it down",
        ui->tools.place_rotation * 90);
}

void
edp_place_pose_at(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level,
    int* out_shape,
    int* out_rotation)
{
    uint32_t mask;

    assert(ui);
    assert(out_shape);
    assert(out_rotation);
    *out_shape = ui->tools.place_shape;
    *out_rotation = ui->tools.place_rotation & 3;
    if( !ui->tools.place_fit || !ui->editor || ui->tools.pick_kind != 0 || ui->tools.pick_id < 0 )
        return;
    if( !edp_loc_shapes(ui, ui->tools.pick_id, &mask) )
        return;
    /* Decor turns to the wall its tile has (on it, or across an edge: a
     * room's walls) and, over the wall itself, takes the shape the wall's
     * width pushes out (edpanel_ops.h Editor_LocFitDecorPose). */
    if( Editor_LocFitDecorPose(&ui->editor->doc, mask, world_x, world_z, level, out_shape, out_rotation) )
        return;
    /* Only a wall pick fits: a centrepiece placed beside a wall of its own
     * loc stays the centrepiece it was asked to be. */
    if( *out_shape != 0 && *out_shape != 1 && *out_shape != 2 && *out_shape != 3 && *out_shape != 9 )
        return;
    (void)Editor_LocFitWall(
        &ui->editor->doc, ui->tools.pick_id, mask, world_x, world_z, level, out_shape, out_rotation);
}

/** Give the selection `shapes[i]` / `rotations[i]` (-1 keeps), one step. */
static int
actions_reshape_to(
    struct EditorUi* ui,
    const char* what,
    const int* shapes,
    const int* rotations)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    count = Editor_LocOpsReshapeTo(&ui->editor->doc, ui->tools.locs, ui->tools.loc_count, shapes, rotations, commands,
        capacity, &g_result);
    applied = edp_apply(ui, commands, count, what);
    actions_report(ui, what, &g_result, applied);
    actions_selection_moved(ui, &g_result, applied > 0);
    return applied;
}

int
edp_locs_set_shape(
    struct EditorUi* ui,
    int shape)
{
    int shapes[EDITOR_LOC_SELECTION_MAX];
    int any = 0;

    assert(ui);
    assert(shape >= 0);
    assert(shape < EDITOR_LOC_SHAPE_COUNT);
    if( !actions_need_selection(ui, "set shape") )
        return 0;
    for( int i = 0; i < ui->tools.loc_count; i++ )
    {
        shapes[i] = -1;
        if( ui->tools.locs[i].shape == shape )
            continue;
        if( !edp_loc_shape_check(ui, ui->tools.locs[i].loc_id, shape, "set shape") )
            continue;
        shapes[i] = shape;
        any = 1;
    }
    if( !any )
        return 0;
    return actions_reshape_to(ui, "reshape", shapes, NULL);
}

int
edp_locs_fit(struct EditorUi* ui)
{
    int shapes[EDITOR_LOC_SELECTION_MAX];
    int rotations[EDITOR_LOC_SELECTION_MAX];
    int fitted = 0;

    assert(ui);
    if( !actions_need_selection(ui, "fit") )
        return 0;
    for( int i = 0; i < ui->tools.loc_count; i++ )
    {
        const struct Editor_LocRef* ref = &ui->tools.locs[i];
        uint32_t mask;
        int wx;
        int wz;
        int shape = ref->shape;
        int rotation = ref->rotation;
        shapes[i] = -1;
        rotations[i] = -1;
        if( !edp_loc_shapes(ui, ref->loc_id, &mask) )
            continue;
        Editor_LocRefWorld(ref, &wx, &wz);
        if( !Editor_LocFitWall(&ui->editor->doc, ref->loc_id, mask, wx, wz, ref->level, &shape, &rotation) )
            continue;
        shapes[i] = shape;
        rotations[i] = rotation;
        fitted++;
    }
    if( fitted == 0 )
    {
        Editor_UiStatus(ui, "Fit refused: no wall of the same loc stands next to the selection to fit to");
        return 0;
    }
    return actions_reshape_to(ui, "fit to neighbours", shapes, rotations);
}

int
edp_locs_place(
    struct EditorUi* ui,
    int loc_id,
    int shape,
    int rotation,
    int world_x,
    int world_z,
    int level)
{
    struct Editor_Cmd command;
    int applied;

    assert(ui);
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: place needs an editor session");
        return 0;
    }
    if( loc_id < 0 )
    {
        Editor_UiStatus(ui, "Place refused: pick a loc in the catalog first");
        return 0;
    }
    if( !edp_loc_shape_check(ui, loc_id, shape, "place") )
        return 0;
    if( !Editor_LocOpsPlace(
            &ui->editor->doc, loc_id, shape, rotation, world_x, world_z, level, &command, 1,
            &g_result) )
    {
        Editor_UiStatus(ui, "Place refused: tile %d,%d L%d is in m%d_%d, which is not open", world_x, world_z, level,
            world_x / 64, world_z / 64);
        return 0;
    }
    applied = edp_apply(ui, &command, 1, "place");
    actions_selection_moved(ui, &g_result, applied > 0);
    /* A place onto a tile whose slot is taken REPLACES what stood there. It
     * did so without a word: a test crate took a rock's place on Gullwick
     * Isle and only a file diff showed it (2026-11-09). */
    if( applied > 0 && command.has_before && command.has_after
        && command.u.loc.before.loc_id != command.u.loc.after.loc_id )
        Editor_UiStatus(ui, "Placed loc %d at %d,%d L%d: it replaced loc %d there; Ctrl+Z brings that back",
            command.u.loc.after.loc_id, world_x, world_z, level, command.u.loc.before.loc_id);
    /* And a plain place says where it went: the status bar was blank after
     * one, and a program driving the editor (world_click's reply is this
     * sentence) could tell a place from a miss only by the unsaved count. */
    else if( applied > 0 )
        Editor_UiStatus(ui, "Placed loc %d at %d,%d L%d", loc_id, world_x, world_z, level);
    return applied;
}

/* ---- the Place drag ------------------------------------------------------------ */

/** The run planned against the document as it is now, with the pose the ghost
 *  shows at its first tile (Fit applied there). 0 when the pick's record is
 *  not resident yet (the plan is then empty: nothing drawn, nothing placed). */
static int
actions_run_plan(
    struct EditorUi* ui,
    struct Editor_PlaceRun* run)
{
    uint32_t mask;
    int shape;
    int rotation;

    run->plan.count = 0;
    run->plan.place_count = 0;
    run->plan.refused_count = 0;
    run->plan.first_refused = -1;
    if( !ui->editor || ui->tools.pick_kind != 0 || ui->tools.pick_id < 0 )
        return 0;
    if( !edp_loc_shapes(ui, ui->tools.pick_id, &mask) )
        return 0;
    edp_place_pose_at(ui, run->path.x[0], run->path.z[0], run->level, &shape, &rotation);
    if( shape < 0 || shape >= EDITOR_LOC_SHAPE_COUNT )
        return 0;
    Editor_LocRunPlanMake(
        &ui->editor->doc, &run->path, ui->tools.pick_id, mask, shape, rotation, run->level, ui->tools.place_fit,
        &run->plan);
    return 1;
}

/** The run's first refusal in the status bar, the first time the run has one. */
static void
actions_run_say(
    struct EditorUi* ui,
    struct Editor_PlaceRun* run)
{
    char sentence[EDITOR_UI_CONSOLE_LINE_MAX];

    if( run->said || run->path.count < 2 || run->plan.refused_count == 0 )
        return;
    if( !Editor_LocRunRefusal(&run->plan, sentence, sizeof(sentence)) )
        return;
    Editor_UiStatus(ui, "%s", sentence);
    run->said = 1;
}

int
edp_run_begin(
    struct EditorUi* ui,
    struct Editor_PlaceRun* run,
    int world_x,
    int world_z,
    int level)
{
    assert(ui);
    assert(run);
    run->active = 0;
    if( !ui->editor || ui->tools.pick_kind != 0 || ui->tools.pick_id < 0 )
        return 0;
    if( world_x < 0 || world_z < 0 )
        return 0;
    Editor_LocRunBegin(&run->path, world_x, world_z);
    run->level = level < 0 ? 0 : level >= EDITOR_SQUARE_LEVELS ? EDITOR_SQUARE_LEVELS - 1 : level;
    run->said = 0;
    run->active = 1;
    (void)actions_run_plan(ui, run);
    return 1;
}

int
edp_run_to(
    struct EditorUi* ui,
    struct Editor_PlaceRun* run,
    int world_x,
    int world_z,
    int constrain)
{
    assert(ui);
    assert(run);
    assert(run->active);
    if( !Editor_LocRunTo(&run->path, world_x, world_z, constrain) )
        return 0;
    (void)actions_run_plan(ui, run);
    actions_run_say(ui, run);
    return 1;
}

int
edp_run_end(
    struct EditorUi* ui,
    struct Editor_PlaceRun* run)
{
    static struct Editor_LocRef placed[EDITOR_LOC_SELECTION_MAX];
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    const char* what;
    int count;
    int applied;
    int n = 0;

    assert(ui);
    assert(run);
    assert(run->active);
    run->active = 0;
    if( run->path.count < 2 )
        return -1;
    /* Afresh: another window may have edited under the drag. */
    if( !actions_run_plan(ui, run) )
    {
        if( ui->tools.pick_kind == 0 && ui->tools.pick_id >= 0 )
        {
            uint32_t mask;
            /* The record is not there (yet, or at all); or it is, and the
             * pose shown is not one of its shapes. */
            if( !edp_loc_shapes(ui, ui->tools.pick_id, &mask) )
                actions_loc_absent(ui, "Place", ui->tools.pick_id);
            else
                Editor_UiStatus(ui, "Place refused: loc %d has no model for the pose shown; pick another shape",
                    ui->tools.pick_id);
        }
        return 0;
    }
    what = run->plan.wall ? "wall run" : "place run";
    actions_run_say(ui, run);
    count = Editor_LocRunCommands(&run->plan, commands, capacity);
    if( count == 0 )
    {
        if( run->plan.refused_count == 0 )
            Editor_UiLog(ui, "editor: %s: every piece of it is already there", what);
        return 0;
    }
    applied = edp_apply(ui, commands, count, what);
    if( applied == 0 )
        return 0;
    /* The pieces placed become the selection, as a single place's loc does. */
    for( int i = 0; i < run->plan.count && n < EDITOR_LOC_SELECTION_MAX; i++ )
    {
        const struct Editor_LocRunPiece* piece = &run->plan.pieces[i];
        if( piece->state != EDITOR_LOC_RUN_PLACE )
            continue;
        Editor_LocRefAt(&placed[n++], piece->world_x, piece->world_z, run->plan.level, piece->shape, run->plan.loc_id,
            piece->rotation & 3);
    }
    ui->tools.loc_count = n;
    memcpy(ui->tools.locs, placed, sizeof(placed[0]) * (size_t)n);
    if( ui->hooks.selection_changed )
        ui->hooks.selection_changed(ui->hooks_user);
    /* A clean run says so in the status bar (2026-11-11: a three-tile rug
     * said nothing there, the bar kept "Picked ..." and the run looked like
     * it had not taken). One with refusals leaves the bar its sentence of
     * why, said once during the drag. */
    if( run->plan.refused_count > 0 )
        Editor_UiLog(ui, "editor: %s: placed %d of %d tiles, %d refused; one undo step", what, run->plan.place_count,
            run->plan.count, run->plan.refused_count);
    else
        Editor_UiStatus(ui, "%c%s: placed %d of %d tiles; Ctrl+Z undoes it", what[0] - 'a' + 'A', what + 1,
            run->plan.place_count, run->plan.count);
    return applied;
}

void
edp_run_cancel(struct Editor_PlaceRun* run)
{
    assert(run);
    run->active = 0;
}

/* ---- the Select tool's sets ----------------------------------------------------- */

/** Editor_LocSizeFn over the App's loc records (one tile until resident). */
static int
actions_loc_size(
    void* user,
    int loc_id,
    int* out_size_x,
    int* out_size_z)
{
    struct EditorUi* ui = user;
    struct ToriRS_Location* cfg;

    if( !ui->app_services.loc_record )
        return 0;
    cfg = ui->app_services.loc_record(ui->app_services.user_data, loc_id);
    if( !cfg )
    {
        if( ui->app_services.loc_request )
            ui->app_services.loc_request(ui->app_services.user_data, loc_id);
        return 0;
    }
    *out_size_x = cfg->size_x;
    *out_size_z = cfg->size_z;
    return 1;
}

static const char* const k_select_mode_names[] = { "replaced", "added", "toggled", "subtracted" };

/** The merge, and its word: `beyond` locs the gesture found past what its
 *  buffer held count as left out too. */
static int
actions_select(
    struct EditorUi* ui,
    const struct Editor_LocRef* refs,
    int n,
    int beyond,
    enum Editor_LocSelectMode mode,
    const char* what)
{
    int const before = ui->tools.loc_count;
    int dropped = 0;

    ui->tools.loc_count = Editor_LocSelectionMerge(
        ui->tools.locs, ui->tools.loc_count, EDITOR_LOC_SELECTION_MAX, refs, n, mode, &dropped);
    dropped += beyond;
    if( ui->hooks.selection_changed )
        ui->hooks.selection_changed(ui->hooks_user);
    if( dropped > 0 )
    {
        char verb[32];
        actions_verb(what, verb, sizeof(verb));
        Editor_UiStatus(ui, "%s: %d loc%s left out; the selection holds %d at most", verb, dropped,
            dropped == 1 ? "" : "s", EDITOR_LOC_SELECTION_MAX);
    }
    Editor_UiLog(ui, "editor: %s: %d loc(s), %s; %d selected (was %d)", what, n + beyond,
        k_select_mode_names[(int)mode], ui->tools.loc_count, before);
    return ui->tools.loc_count;
}

int
edp_select_refs(
    struct EditorUi* ui,
    const struct Editor_LocRef* refs,
    int n,
    enum Editor_LocSelectMode mode,
    const char* what)
{
    assert(ui);
    assert(refs || n == 0);
    assert(what);
    return actions_select(ui, refs, n, 0, mode, what);
}

int
edp_select_rect(
    struct EditorUi* ui,
    const struct Editor_TileRect* rect,
    enum Editor_LocSelectMode mode)
{
    static struct Editor_LocRef found[EDITOR_LOC_SELECTION_MAX];
    struct Editor_TileRect r;
    int touching;
    int n;

    assert(ui);
    assert(rect);
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: marquee needs an editor session");
        return 0;
    }
    if( !Editor_TileRectNormalise(rect, &r) )
    {
        Editor_UiStatus(ui, "Select refused: a marquee covers 4096 tiles (64x64) at most");
        return ui->tools.loc_count;
    }
    touching = Editor_LocsTouchingRect(&ui->editor->doc, &r, actions_loc_size, ui, found, EDITOR_LOC_SELECTION_MAX);
    n = touching < EDITOR_LOC_SELECTION_MAX ? touching : EDITOR_LOC_SELECTION_MAX;
    return actions_select(ui, found, n, touching - n, mode, "marquee");
}

int
edp_select_same(
    struct EditorUi* ui,
    int loc_id,
    int level,
    enum Editor_LocSelectMode mode)
{
    static struct Editor_LocRef found[EDITOR_LOC_SELECTION_MAX];
    int total;
    int n;

    assert(ui);
    assert(loc_id >= 0);
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: select same needs an editor session");
        return 0;
    }
    total = Editor_LocsOfId(&ui->editor->doc, loc_id, level, found, EDITOR_LOC_SELECTION_MAX);
    if( total == 0 )
    {
        Editor_UiStatus(ui, "Select same: no loc %d on L%d in the open squares", loc_id, level);
        return ui->tools.loc_count;
    }
    n = total < EDITOR_LOC_SELECTION_MAX ? total : EDITOR_LOC_SELECTION_MAX;
    return actions_select(ui, found, n, total - n, mode, "select same");
}

/* ---- the rectangle ------------------------------------------------------------ */

static struct Editor_TileClip*
actions_clip(void)
{
    if( !g_clip )
    {
        g_clip = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*g_clip));
        assert(g_clip);
    }
    return g_clip;
}

int
edp_rect_clip_size(
    int* out_w,
    int* out_h)
{
    assert(out_w);
    assert(out_h);
    if( !g_clip || !g_clip->valid )
        return 0;
    *out_w = g_clip->w;
    *out_h = g_clip->h;
    return 1;
}

static int
actions_need_rect(
    struct EditorUi* ui,
    const char* what)
{
    struct Editor_TileRect r;

    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: %s needs an editor session", what);
        return 0;
    }
    if( !Editor_TileRectNormalise(&ui->tools.rect, &r) )
    {
        Editor_UiLog(
            ui, "editor: %s: drag a rectangle with the rectangle tool first (64x64 at most)", what);
        return 0;
    }
    return 1;
}

/* The Rectangle panel's "with spawns" box: Copy takes the spawns standing in
 * the rectangle and Paste stands them again. Here, not in the panel, because
 * Ctrl+C and Ctrl+V reach the clipboard without it. On until unticked: an
 * area copied to a new map is expected to bring its npcs (2026-11-09). */
static int g_rect_with_spawns = 1;

int
edp_rect_with_spawns(void)
{
    return g_rect_with_spawns;
}

void
edp_rect_set_with_spawns(int on)
{
    g_rect_with_spawns = on ? 1 : 0;
}

static const char*
actions_s(int n)
{
    return n == 1 ? "" : "s";
}

/** One undo step over several submissions: a paste's tiles, then its locs,
 *  then its spawns, each list built against what the one before left and
 *  each able to fill the scratch on its own (a 64 x 64 paste is 4096 tile
 *  commands, which used to leave no room for a single loc). The stroke opens
 *  at the first submission that has a command, so a step that changes
 *  nothing leaves no History row. */
struct ActionsStep
{
    const char* what;
    int was_open;
    int opened;
};

static void
actions_step_begin(
    struct EditorUi* ui,
    struct ActionsStep* step,
    const char* what)
{
    step->what = what;
    step->was_open = ui->tools.stroke_open;
    step->opened = 0;
}

static int
actions_step_apply(
    struct EditorUi* ui,
    struct ActionsStep* step,
    const struct Editor_Cmd* commands,
    int count)
{
    if( count <= 0 )
        return 0;
    if( !step->was_open && !step->opened )
    {
        Editor_StrokeBegin(ui->editor);
        Editor_HistoryName(ui->editor, step->what);
        ui->tools.stroke_open = 1;
        step->opened = 1;
    }
    return edp_apply(ui, commands, count, step->what);
}

static void
actions_step_end(
    struct EditorUi* ui,
    struct ActionsStep* step)
{
    if( !step->opened )
        return;
    ui->tools.stroke_open = step->was_open;
    Editor_StrokeEnd(ui->editor);
}

int
edp_rect_copy(struct EditorUi* ui)
{
    struct Editor_TileClip* clip;
    int copied;
    int locs;
    int spawns;

    assert(ui);
    if( !actions_need_rect(ui, "copy") )
        return 0;
    clip = actions_clip();
    copied = Editor_TileClipCopy(&ui->editor->doc, &ui->tools.rect, clip);
    /* The marquee is the unnamed stamp (design #placing): its locs and the
     * spawns standing in it travel with its tiles, pasted after them in the
     * same step. */
    locs = edp_stamp_clip_copy(ui, &ui->tools.rect, g_rect_with_spawns);
    spawns = edp_stamp_clip_spawns();
    if( copied == 0 )
        Editor_UiStatus(ui, "Copy refused: the rectangle is in no open square; open its square first");
    else if( !g_rect_with_spawns )
        Editor_UiStatus(ui, "Copied %dx%d tiles, %d loc%s; spawns stay behind (\"with spawns\" is off)", clip->w,
            clip->h, locs, actions_s(locs));
    else if( copied < clip->w * clip->h )
        Editor_UiStatus(ui, "Copied %dx%d tiles, %d loc%s, %d spawn%s; %d tiles are in squares that are not open",
            clip->w, clip->h, locs, actions_s(locs), spawns, actions_s(spawns), clip->w * clip->h - copied);
    else
        Editor_UiStatus(ui, "Copied %dx%d tiles, %d loc%s, %d spawn%s", clip->w, clip->h, locs, actions_s(locs),
            spawns, actions_s(spawns));
    return copied;
}

int
edp_rect_paste(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level)
{
    struct Editor_TileRect r;

    assert(ui);
    if( Editor_TileRectNormalise(&ui->tools.rect, &r) )
    {
        world_x = r.x0;
        world_z = r.z0;
        level = r.level;
    }
    return edp_rect_paste_at(ui, world_x, world_z, level);
}

int
edp_rect_paste_at(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    struct ActionsStep step;
    int count;
    int tiles;
    int locs;
    int spawns = 0;

    assert(ui);
    if( !ui->editor )
        return 0;
    if( !g_clip || !g_clip->valid )
    {
        Editor_UiStatus(ui, "Paste refused: nothing is copied yet; drag a rectangle and Copy it first");
        return 0;
    }
    /* The tile commands exactly as the tile paste makes them; the copied
     * locs after them, one place (a replace onto a taken key) each; then the
     * copied spawns, each an edited row of the square it lands in. Three
     * submissions, ONE undo step. */
    actions_step_begin(ui, &step, "paste");
    count = Editor_TileClipPaste(&ui->editor->doc, g_clip, world_x, world_z, level, commands, capacity);
    tiles = actions_step_apply(ui, &step, commands, count);
    count = edp_stamp_clip_paste(ui, world_x, world_z, level, commands, capacity);
    locs = actions_step_apply(ui, &step, commands, count);
    if( g_rect_with_spawns )
    {
        count = edp_stamp_clip_paste_spawns(ui, world_x, world_z, level, commands, capacity);
        spawns = actions_step_apply(ui, &step, commands, count);
        /* Pasted npcs are shown, or the paste reads as having dropped them. */
        if( spawns > 0 )
            edp_spawns_note_added(ui, commands, count);
    }
    actions_step_end(ui, &step);

    if( tiles + locs + spawns > 0 )
    {
        int const left = g_rect_with_spawns ? 0 : edp_stamp_clip_spawns();
        if( left > 0 )
            Editor_UiStatus(ui, "Pasted %dx%d at %d,%d L%d: %d tile%s, %d loc%s; %d spawn%s left out (\"with spawns\" is off)",
                g_clip->w, g_clip->h, world_x, world_z, level, tiles, actions_s(tiles), locs, actions_s(locs), left,
                actions_s(left));
        else
            Editor_UiStatus(ui, "Pasted %dx%d at %d,%d L%d: %d tile%s, %d loc%s, %d spawn%s; Ctrl+Z undoes it",
                g_clip->w, g_clip->h, world_x, world_z, level, tiles, actions_s(tiles), locs, actions_s(locs), spawns,
                actions_s(spawns));
    }
    else if( world_x < 0 || world_z < 0
             || !Editor_DocFindSquare(&ui->editor->doc, world_x / EDITOR_SQUARE_X, world_z / EDITOR_SQUARE_Z)
             || !Editor_DocFindSquare(&ui->editor->doc, world_x / EDITOR_SQUARE_X, world_z / EDITOR_SQUARE_Z)->loaded )
        Editor_UiStatus(ui, "Paste refused: %d,%d is not in an open square; open its square first", world_x, world_z);
    else
        Editor_UiStatus(ui, "Paste at %d,%d L%d: everything copied is already there; nothing changed", world_x,
            world_z, level);
    return tiles + locs + spawns;
}

int
edp_rect_fill(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_rect(ui, "fill") )
        return 0;
    count = Editor_TileRectFill(&ui->editor->doc, &ui->tools.rect, &ui->tools, commands, capacity);
    applied = edp_apply(ui, commands, count, "fill");
    /* What it laid, by the Palette's names: the picks are in another panel. */
    {
        char under[EDITOR_PALETTE_DESCRIBE_MAX];
        char over[EDITOR_PALETTE_DESCRIBE_MAX];

        Editor_Palette_Describe(ui, 0, ui->tools.underlay_id, under, sizeof(under));
        Editor_Palette_Describe(ui, 1, ui->tools.overlay_none ? 0 : ui->tools.overlay_id, over, sizeof(over));
        Editor_UiStatus(ui, "Fill: %d tile%s changed; underlay %s, overlay %s", applied, applied == 1 ? "" : "s",
            ui->tools.underlay_id ? under : "left as it was", ui->tools.overlay_none ? "taken off" : over);
    }
    return applied;
}

int
edp_rect_clear(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_rect(ui, "clear") )
        return 0;
    count = Editor_TileRectClear(&ui->editor->doc, &ui->tools.rect, commands, capacity);
    applied = edp_apply(ui, commands, count, "clear");
    Editor_UiLog(ui, "editor: cleared %d tile(s)", applied);
    return applied;
}

int
edp_rect_plateau(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int height;
    int count;
    int applied;

    assert(ui);
    if( !actions_need_rect(ui, "plateau") )
        return 0;
    /* The top: the latched tile's height (Select's click), else the
     * rectangle's first corner's. */
    if( !(ui->tools.tile_valid
            && Editor_OpsDerivedHeight(
                &ui->editor->doc, ui->tools.tile_x, ui->tools.tile_z, ui->tools.tile_level, &height))
        && !Editor_OpsDerivedHeight(&ui->editor->doc, ui->tools.rect.x0, ui->tools.rect.z0, ui->tools.rect.level,
            &height) )
    {
        Editor_UiStatus(ui, "Plateau refused: latch a tile at the height you want (Select), in an open square");
        return 0;
    }
    count = Editor_TileRectPlateau(
        &ui->editor->doc, &ui->tools.rect, height, ui->tools.plateau_skirt, commands, capacity);
    applied = edp_apply(ui, commands, count, "plateau");
    Editor_UiLog(ui, "editor: plateau over %d tile(s), skirt %d", applied, ui->tools.plateau_skirt);
    return applied;
}

int
edp_square_clear(
    struct EditorUi* ui,
    int map_x,
    int map_z)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    struct Editor_Square* square;
    int applied = 0;
    int const was_open = ui ? ui->tools.stroke_open : 0;

    assert(ui);
    if( !ui->editor )
        return 0;
    square = Editor_DocFindSquare(&ui->editor->doc, map_x, map_z);
    if( !square || !square->loaded )
    {
        Editor_UiLog(ui, "editor: open m%d_%d before clearing it", map_x, map_z);
        return 0;
    }

    /* One undo step for the whole square: four levels of tiles and every
     * loc, submitted in chunks the scratch holds. */
    if( !was_open )
        Editor_StrokeBegin(ui->editor);
    ui->tools.stroke_open = 1;
    for( int level = 0; level < EDITOR_SQUARE_LEVELS; level++ )
    {
        struct Editor_TileRect rect = { 1, map_x * 64, map_z * 64, map_x * 64 + 63,
                                        map_z * 64 + 63, level };
        int const count = Editor_TileRectClear(&ui->editor->doc, &rect, commands, capacity);
        applied += edp_apply(ui, commands, count, "clear square");
    }
    while( square->loc_count > 0 )
    {
        int count = 0;
        /* Last first: each delete removes by key, and the list shrinks. */
        for( int i = square->loc_count - 1; i >= 0 && count < capacity; i-- )
        {
            Editor_CmdInitSquare(&commands[count], EDITOR_CMD_LOC, map_x, map_z);
            commands[count].has_before = 1;
            commands[count].u.loc.before = square->locs[i];
            count++;
        }
        {
            int const before = square->loc_count;
            applied += edp_apply(ui, commands, count, "clear square");
            if( square->loc_count >= before )
                break; /* refused: do not spin */
        }
    }
    ui->tools.stroke_open = was_open;
    if( !was_open )
        Editor_StrokeEnd(ui->editor);
    Editor_UiLog(ui, "editor: cleared m%d_%d (%d edits, one undo step)", map_x, map_z, applied);
    return applied;
}

enum EditorHost_Status
edp_square_create(
    struct EditorUi* ui,
    const struct EditorHost_DocCreate* request,
    int with_spawns,
    int* out_spawns)
{
    struct EditorHost_DocCreate create;
    enum EditorHost_Status status;
    char storage[EDITOR_DOC_STORAGE_MAX];
    char bare[EDITOR_DOC_KEY_MAX];
    char verb[48];
    int capacity;
    struct Editor_Cmd* commands;
    int from_x = -1;
    int from_z = -1;
    int to_x = -1;
    int to_z = -1;
    int was_open;
    int joined;
    int count;

    assert(ui);
    assert(request);
    assert(out_spawns);
    assert(ui->editor);

    *out_spawns = 0;
    if( !with_spawns || request->template_kind != EDITOR_DOC_TEMPLATE_COPY
        || !Editor_DocSplitSquareKey(request->source_key, storage, bare)
        || !Editor_SquareKeyParse(bare, &from_x, &from_z) || !Editor_SquareKeyParse(request->key, &to_x, &to_z)
        || (from_x == to_x && from_z == to_z) )
        /* Nothing to carry: no source, or the same square in another
         * storage, whose spawn files (keyed by coordinates) it shares. */
        return Editor_CreateSquare(ui->editor, request);

    /* One undo step where the server lets a create join the client's stroke
     * (TORIRSMAPED_CAP_DOC_CREATE_STROKE): the copy and the spawns it brought
     * are undone together. An older server makes the square its own step and
     * the spawns the step after it. */
    create = *request;
    /* The spawn documents of both squares, before the stroke: the capture
     * reads the source's, the adds land in the destination's. */
    edp_square_spawn_docs(ui, from_x, from_z);
    edp_square_spawn_docs(ui, to_x, to_z);
    was_open = ui->tools.stroke_open;
    joined = (ui->editor->capabilities & TORIRSMAPED_CAP_DOC_CREATE_STROKE) != 0;
    if( joined )
    {
        if( !was_open )
        {
            snprintf(verb, sizeof(verb), "copy square to %.24s", request->key);
            Editor_StrokeBegin(ui->editor);
            Editor_HistoryName(ui->editor, verb);
        }
        ui->tools.stroke_open = 1;
        create.join_stroke = 1;
    }
    status = Editor_CreateSquare(ui->editor, &create);
    if( status == EDITOR_HOST_OK )
    {
        commands = edp_scratch(&capacity);
        count = edp_square_spawn_commands(ui, from_x, from_z, to_x, to_z, commands, capacity, NULL);
        *out_spawns = edp_apply(ui, commands, count, "copy spawns");
        if( *out_spawns > 0 )
            edp_spawns_note_added(ui, commands, count);
    }
    if( joined )
    {
        ui->tools.stroke_open = was_open;
        if( !was_open )
            Editor_StrokeEnd(ui->editor);
    }
    return status;
}

int
edp_scratch_clear(
    struct EditorUi* ui,
    int map_x,
    int map_z)
{
    struct Editor_DocHandle handle;
    char key[EDITOR_DOC_KEY_MAX];
    int capacity;
    struct Editor_Cmd* commands;
    int applied;
    int was_open;

    assert(ui);
    if( !ui->editor )
        return 0;
    was_open = ui->tools.stroke_open;
    if( !was_open )
    {
        Editor_StrokeBegin(ui->editor);
        Editor_HistoryName(ui->editor, "clear scratch square");
    }
    ui->tools.stroke_open = 1;
    applied = edp_square_clear(ui, map_x, map_z);
    /* The slot's edited spawn rows too: its spawns went to the placed square
     * (edp_square_create), and a row left here would be saved as a spawn at
     * a parking coordinate no map has. */
    Editor_SquareKey(key, map_x, map_z);
    commands = edp_scratch(&capacity);
    while( edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key) && Editor_DocFind(&ui->editor->doc, EDITOR_DOC_SPAWN, key, &handle)
           && handle.spawn->loaded )
    {
        const struct Editor_SpawnFile* file = &handle.spawn->file;
        int count = 0;
        int removed;

        for( int i = 0; i < file->line_count && count < capacity; i++ )
            if( file->lines[i].kind == EDITOR_SPAWN_LINE_ROW )
                Editor_SpawnCommand(&commands[count++], &file->lines[i].row, 0);
        if( count == 0 )
            break;
        removed = edp_apply(ui, commands, count, "clear scratch square");
        applied += removed;
        if( removed < count )
            break; /* refused: do not spin */
    }
    ui->tools.stroke_open = was_open;
    if( !was_open )
        Editor_StrokeEnd(ui->editor);
    return applied;
}

void
edp_actions_free(void)
{
    ToriMem_Free(ToriMem_Editor(), g_clip);
    g_clip = NULL;
    edp_stamps_free();
}

int
edp_locs_swap(struct EditorUi* ui)
{
    /* Same placements, the catalog's loc: one replace per loc, one stroke,
     * keeping tile, shape and rotation -- for each loc the pick has a model
     * for that shape. */
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count = 0;
    int applied;

    assert(ui);
    if( ui->tools.pick_kind != 0 || ui->tools.pick_id < 0 )
    {
        Editor_UiStatus(ui, "Swap refused: pick a replacement loc in the catalog first");
        return 0;
    }
    if( !ui->editor || ui->tools.loc_count == 0 )
    {
        Editor_UiStatus(ui, "Swap refused: select the locs to swap first");
        return 0;
    }
    for( int i = 0; i < ui->tools.loc_count && count < capacity; i++ )
    {
        struct Editor_LocRef* ref = &ui->tools.locs[i];
        struct Editor_Square* square = Editor_DocFindSquare(&ui->editor->doc, ref->map_x, ref->map_z);
        int const index = Editor_LocRefResolve(&ui->editor->doc, ref);
        if( index < 0 )
            continue;
        if( !edp_loc_shape_check(ui, ui->tools.pick_id, square->locs[index].shape, "swap") )
            continue;
        Editor_CmdInitSquare(&commands[count], EDITOR_CMD_LOC, ref->map_x, ref->map_z);
        commands[count].has_before = 1;
        commands[count].has_after = 1;
        commands[count].u.loc.before = square->locs[index];
        commands[count].u.loc.after = square->locs[index];
        commands[count].u.loc.after.loc_id = ui->tools.pick_id;
        count++;
    }
    applied = edp_apply(ui, commands, count, "swap");
    if( applied > 0 )
    {
        for( int i = 0; i < ui->tools.loc_count; i++ )
            if( Editor_LocRefResolve(&ui->editor->doc, &ui->tools.locs[i]) < 0 )
            {
                struct Editor_LocRef swapped = ui->tools.locs[i];
                swapped.loc_id = ui->tools.pick_id;
                if( Editor_LocRefResolve(&ui->editor->doc, &swapped) >= 0 )
                    ui->tools.locs[i] = swapped;
            }
        if( ui->hooks.selection_changed )
            ui->hooks.selection_changed(ui->hooks_user);
    }
    return applied;
}

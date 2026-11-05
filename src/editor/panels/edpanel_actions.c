/*
 * The actions more than one control reaches: the loc selection's nudge,
 * rotate, reshape, duplicate and delete (menu bar, locs panel, arrow keys, R,
 * S), and the rectangle's copy, paste, fill and clear (menu bar, rectangle
 * panel). Each is: build the commands against the mirror (edpanel_ops.h),
 * submit them as one undo step, re-point the selection, say what happened.
 */

#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "engine/torirs_types.h"
#include "mem/torirs_mem.h"

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
        if( edp_loc_shapes(ui, ui->tools.locs[i].loc_id, &mask) && mask != 0 )
            shapes[i] = Editor_LocShapeNext(mask, ui->tools.locs[i].shape);
        else
            Editor_UiStatus(
                ui, "Reshape refused: loc %d is still loading; try again in a moment", ui->tools.locs[i].loc_id);
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
        return 0;
    }
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
        Editor_UiStatus(ui, "%s refused: loc %d is still loading; try again in a moment", verb, loc_id);
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
    Editor_UiLog(ui, "editor: place rotation %d", ui->tools.place_rotation);
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
        &ui->editor->doc, &run->path, ui->tools.pick_id, mask, shape, rotation, run->level, &run->plan);
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
            Editor_UiStatus(
                ui, "Place refused: loc %d is still loading; try again in a moment", ui->tools.pick_id);
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
    if( run->plan.refused_count > 0 )
        Editor_UiLog(ui, "editor: %s: placed %d of %d tiles, %d refused; one undo step", what, run->plan.place_count,
            run->plan.count, run->plan.refused_count);
    else
        Editor_UiLog(ui, "editor: %s: placed %d of %d tiles; one undo step", what, run->plan.place_count,
            run->plan.count);
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

int
edp_rect_copy(struct EditorUi* ui)
{
    int copied;

    assert(ui);
    if( !actions_need_rect(ui, "copy") )
        return 0;
    copied = Editor_TileClipCopy(&ui->editor->doc, &ui->tools.rect, actions_clip());
    Editor_UiLog(ui, "editor: copied %d tile(s)", copied);
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
    int count;
    int applied;

    assert(ui);
    if( !ui->editor )
        return 0;
    if( !g_clip || !g_clip->valid )
    {
        Editor_UiLog(ui, "editor: nothing copied yet");
        return 0;
    }
    count = Editor_TileClipPaste(&ui->editor->doc, g_clip, world_x, world_z, level, commands, capacity);
    applied = edp_apply(ui, commands, count, "paste");
    Editor_UiLog(ui, "editor: pasted %dx%d, %d tile(s) changed", g_clip->w, g_clip->h, applied);
    return applied;
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
    Editor_UiLog(ui, "editor: filled %d tile(s)", applied);
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

void
edp_actions_free(void)
{
    ToriMem_Free(ToriMem_Editor(), g_clip);
    g_clip = NULL;
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

/*
 * The Place and Select tools' direct manipulation (workbench 6.3, design
 * section 7 "Placing and moving locs"), content-free: synthetic squares
 * written under the directory given, an embedded ToriRSMapEd serving them,
 * and the editor's own world-free halves (panels/edpanel_ops.h, the edp_run_*
 * and edp_select_* actions) driven the way the World's drag drives them:
 *
 *   ./editor_placement_test <tmp-content-dir>
 *
 *   ops        the run's path (a diagonal step is two orthogonal steps, a
 *              retrace takes the tail off, Shift keeps one axis), the shape
 *              layers against world.h's, the corner rule Fit shares, the
 *              footprint, the selection merge's four modes
 *   wall run   six tiles with two corners: the plan before release (an
 *              inside corner post 3, an outside L 2, at Fit's rotations;
 *              straight ends), nothing in the document until the release,
 *              then six locs, one undo step that takes all six (the .jl2
 *              byte-identical), redo, and the authority's square re-read by
 *              a second session identical to the mirror; a straight end of
 *              the same loc re-posed as the next run's corner
 *   tile run   a centrepiece run skipping a key another loc holds (refused,
 *              said once a run), keeping its own, and a closed square
 *   marquee    by footprint (a 2x3 loc, a turned one), level filtered, with
 *              replace / Shift add / Ctrl toggle / Alt subtract; select same
 *   fit        the Fit bit rides STATE_TOOLS to the Client's other window
 *   refusals   place / nudge / swap / reshape refused say why in the status
 *              bar
 *
 * Links the client's objects minus main.o, as test-editor-groups does.
 */

#include "editor/editor.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_jm2.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_ops.h"
#include "editor/panels/edpanels.h"
#include "engine/torirs_types.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/uitree_debug_overlay.h"
#include "world/world.h"

#include <rscache.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

/* m50_50 and m50_51 are open; m51_50 exists on disk and is never opened. */
#define BASE_X (50 * 64)
#define BASE_Z (50 * 64)

/* Synthetic loc records (the App's loc_record stand-in). */
#define LOC_WALL 1902
#define LOC_OTHER_WALL 1903
#define LOC_CRATE 500
#define LOC_TREE 600
#define LOC_POSTLESS_WALL 1904
#define LOC_DECOR 1905

static struct Editor g_editor;
static struct Editor g_peer;
static struct RSCache g_profile;
static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static struct ToriRSChrome g_peer_chrome;
static struct EditorUi g_peer_ui;
static int g_app_stand_in;

/* ---- fixture ------------------------------------------------------------------- */

static void
write_text(
    const char* root,
    const char* relative,
    const char* text)
{
    char path[2048];
    FILE* f;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    f = fopen(path, "wb");
    if( !f )
    {
        printf("FAIL: cannot write %s\n", path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
}

static void
make_dir(
    const char* root,
    const char* relative)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    mkdir(path, 0755);
}

static void
write_fixture(const char* root)
{
    make_dir(root, "");
    make_dir(root, "configs");
    make_dir(root, "maps");
    make_dir(root, "pack");
    make_dir(root, "editor");
    write_text(root, "configs/all.loc", "[test_wall]\nname=Wall\n\n[test_crate]\nname=Crate\n");
    write_text(root, "configs/all.loc.compack", "0=test_wall\n1=test_crate\n");
    write_text(root, "maps/m50_50.jm2", "==== MAP ====\n");
    write_text(root, "maps/m50_50.jl2", "==== LOC ====\n");
    write_text(root, "maps/m50_51.jm2", "==== MAP ====\n");
    write_text(root, "maps/m50_51.jl2", "==== LOC ====\n");
    write_text(root, "maps/m51_50.jm2", "==== MAP ====\n");
    write_text(root, "maps/m51_50.jl2", "==== LOC ====\n");
}

/* Records: the walls have models for 0, 1, 2, 3 and 9 (castlewall's set),
 * the postless wall for 0 and 2 only; the crate is a 2x3 centrepiece, the
 * tree a 1x1 one. */
static int k_wall_shapes[5] = { 0, 1, 2, 3, 9 };
static int k_decor_shapes[1] = { 4 };
static int k_postless_shapes[2] = { 0, 2 };
static int k_lengths[5] = { 1, 1, 1, 1, 1 };
static int k_model[1] = { 1 };
static int* k_models[5] = { k_model, k_model, k_model, k_model, k_model };
static struct ToriRS_Location g_wall;
static struct ToriRS_Location g_postless;
static struct ToriRS_Location g_decor;
static struct ToriRS_Location g_crate;
static struct ToriRS_Location g_tree;

static struct ToriRS_Location*
test_loc_record(
    void* user_data,
    int loc_id)
{
    (void)user_data;
    if( loc_id == LOC_WALL || loc_id == LOC_OTHER_WALL )
        return &g_wall;
    if( loc_id == LOC_POSTLESS_WALL )
        return &g_postless;
    if( loc_id == LOC_DECOR )
        return &g_decor;
    if( loc_id == LOC_CRATE )
        return &g_crate;
    if( loc_id == LOC_TREE )
        return &g_tree;
    return NULL;
}

static void
records_init(void)
{
    g_wall.shapes = k_wall_shapes;
    g_wall.models = k_models;
    g_wall.lengths = k_lengths;
    g_wall.shapes_and_model_count = 5;
    g_wall.size_x = 1;
    g_wall.size_z = 1;
    g_postless = g_wall;
    g_postless.shapes = k_postless_shapes;
    g_postless.shapes_and_model_count = 2;
    g_decor = g_wall;
    g_decor.shapes = k_decor_shapes;
    g_decor.shapes_and_model_count = 1;
    /* No shape list: a centrepiece only. */
    g_crate.models = k_models;
    g_crate.lengths = k_lengths;
    g_crate.shapes_and_model_count = 1;
    g_crate.size_x = 2;
    g_crate.size_z = 3;
    g_tree = g_crate;
    g_tree.size_x = 1;
    g_tree.size_z = 1;
}

static void
on_state(
    void* user,
    uint32_t key,
    const int32_t* values,
    int count)
{
    Editor_UiApplyState((struct EditorUi*)user, key, values, count);
}

static void
frame_of(
    struct ToriRSChrome* chrome,
    struct EditorUi* ui)
{
    ToriRSChrome_Build(chrome);
    Editor_UiTick(ui);
    ToriRSChrome_Build(chrome);
}

static struct Editor_Square*
square(
    struct Editor* editor,
    int map_x,
    int map_z)
{
    struct Editor_Square* sq = Editor_DocFindSquare(&editor->doc, map_x, map_z);
    return sq && sq->loaded ? sq : NULL;
}

static char*
jl2_of(const struct Editor_Square* sq)
{
    size_t const size = Editor_Jl2Emit(sq, NULL, 0);
    char* text = malloc(size + 1);
    assert(text);
    Editor_Jl2Emit(sq, text, size + 1);
    return text;
}

/** The loc of `loc_id` on world tile (x, z) level 0 with `shape`, or NULL. */
static const struct Editor_Loc*
loc_at(
    int loc_id,
    int world_x,
    int world_z,
    int level,
    int shape)
{
    struct Editor_Square* sq = square(&g_editor, world_x / 64, world_z / 64);
    int index;
    if( !sq )
        return NULL;
    index = Editor_SquareLocFind(sq, level, world_x % 64, world_z % 64, shape);
    if( index < 0 || sq->locs[index].loc_id != loc_id )
        return NULL;
    return &sq->locs[index];
}

static int
locs_of(
    int loc_id,
    int level)
{
    struct Editor_LocRef refs[4];
    return Editor_LocsOfId(&g_editor.doc, loc_id, level, refs, 0);
}

/** Place directly (a command through the authority), outside any test of the
 *  Place tool itself: the scene the gestures act on. */
static void
seed(
    int loc_id,
    int shape,
    int rotation,
    int world_x,
    int world_z,
    int level)
{
    struct Editor_Cmd command;
    struct Editor_LocOpResult result;
    int const n = Editor_LocOpsPlace(&g_editor.doc, loc_id, shape, rotation, world_x, world_z, level, &command, 1,
        &result);
    assert(n == 1);
    Editor_StrokeBegin(&g_editor);
    (void)Editor_Apply(&g_editor, &command);
    Editor_StrokeEnd(&g_editor);
}

/* ---- ops ------------------------------------------------------------------------- */

static void
test_path(void)
{
    struct Editor_LocRunPath path;

    Editor_LocRunBegin(&path, 100, 100);
    CHECK(Editor_LocRunTo(&path, 101, 101, 0) && path.count == 3 && path.x[1] == 101 && path.z[1] == 100
              && path.x[2] == 101 && path.z[2] == 101,
        "path: a diagonal step is two orthogonal steps, x then z (%d tiles)", path.count);
    CHECK(!Editor_LocRunTo(&path, 101, 101, 0), "path: the same tile again changes nothing");
    CHECK(Editor_LocRunTo(&path, 104, 101, 0) && path.count == 6, "path: a jump walks every tile (%d)", path.count);
    CHECK(Editor_LocRunTo(&path, 102, 101, 0) && path.count == 4 && path.x[3] == 102,
        "path: going back takes the tail off (%d tiles, last %d)", path.count, path.x[path.count - 1]);
    CHECK(Editor_LocRunTo(&path, 100, 100, 0) && path.count == 1, "path: back to the start is one tile (%d)",
        path.count);
    for( int i = 1; i < path.count; i++ )
    {
        int const d = abs(path.x[i] - path.x[i - 1]) + abs(path.z[i] - path.z[i - 1]);
        CHECK(d == 1, "path: step %d is one orthogonal tile", i);
    }
    /* Shift: one axis, the one the pointer moved further on. */
    Editor_LocRunBegin(&path, 100, 100);
    (void)Editor_LocRunTo(&path, 103, 101, 0);
    CHECK(Editor_LocRunTo(&path, 105, 102, 1) && path.count == 6 && path.z[5] == 100 && path.x[5] == 105,
        "path: constrained, the run is the straight line along x to 105,100 (%d tiles, last %d,%d)", path.count,
        path.x[path.count - 1], path.z[path.count - 1]);
    for( int i = 0; i < path.count; i++ )
        CHECK(path.z[i] == 100, "path: constrained tile %d stays on the axis", i);
    CHECK(Editor_LocRunTo(&path, 101, 106, 1) && path.count == 7 && path.x[6] == 100 && path.z[6] == 106,
        "path: constrained the other way it turns to the z axis (%d)", path.count);
    /* A run stops growing at its cap rather than overrunning. */
    Editor_LocRunBegin(&path, 0, 0);
    (void)Editor_LocRunTo(&path, EDITOR_LOC_RUN_MAX + 40, 0, 0);
    CHECK(path.count == EDITOR_LOC_RUN_MAX, "path: capped at %d tiles (%d)", EDITOR_LOC_RUN_MAX, path.count);
}

static void
test_rules(void)
{
    int w;
    int h;

    for( int s = 0; s < EDITOR_LOC_SHAPE_COUNT; s++ )
        CHECK(Editor_LocShapeLayer(s) == World_LocShapeToLayer(s), "rules: shape %d's layer is world.h's (%d vs %d)",
            s, Editor_LocShapeLayer(s), World_LocShapeToLayer(s));
    CHECK(Editor_LocWallCornerRotation(0, 1) == 0 && Editor_LocWallCornerRotation(1, 2) == 1
              && Editor_LocWallCornerRotation(2, 3) == 2 && Editor_LocWallCornerRotation(3, 0) == 3
              && Editor_LocWallCornerRotation(1, 0) == 0 && Editor_LocWallCornerRotation(0, 3) == 3,
        "rules: a corner of edges a and b is the r with {r, r+1} = {a, b}, in either order");
    Editor_LocFootprint(10, 0, 2, 3, &w, &h);
    CHECK(w == 2 && h == 3, "rules: a 2x3 centrepiece covers 2x3 (%dx%d)", w, h);
    Editor_LocFootprint(10, 1, 2, 3, &w, &h);
    CHECK(w == 3 && h == 2, "rules: turned a quarter, 3x2 (%dx%d)", w, h);
    Editor_LocFootprint(0, 1, 2, 3, &w, &h);
    CHECK(w == 1 && h == 1, "rules: a wall is one tile whatever its record says");
    CHECK(Editor_LocSelectModeOf(0, 0, 0) == EDITOR_LOC_SELECT_REPLACE
              && Editor_LocSelectModeOf(1, 0, 0) == EDITOR_LOC_SELECT_ADD
              && Editor_LocSelectModeOf(0, 1, 0) == EDITOR_LOC_SELECT_TOGGLE
              && Editor_LocSelectModeOf(1, 1, 1) == EDITOR_LOC_SELECT_SUBTRACT,
        "rules: none replaces, Shift adds, Ctrl toggles, Alt subtracts");
    /* The merge. */
    {
        struct Editor_LocRef sel[4];
        struct Editor_LocRef set[3];
        int count;
        int dropped;
        for( int i = 0; i < 3; i++ )
            Editor_LocRefAt(&set[i], 10 + i, 10, 0, 10, LOC_TREE, 0);
        count = Editor_LocSelectionMerge(sel, 0, 4, set, 2, EDITOR_LOC_SELECT_REPLACE, &dropped);
        CHECK(count == 2 && dropped == 0, "merge: replace takes the set (%d)", count);
        count = Editor_LocSelectionMerge(sel, count, 4, &set[1], 2, EDITOR_LOC_SELECT_ADD, &dropped);
        CHECK(count == 3 && sel[2].x == set[2].x, "merge: add skips what is there and appends the rest (%d)", count);
        count = Editor_LocSelectionMerge(sel, count, 4, set, 2, EDITOR_LOC_SELECT_TOGGLE, &dropped);
        CHECK(count == 1 && sel[0].x == set[2].x, "merge: toggle takes the two selected off (%d)", count);
        count = Editor_LocSelectionMerge(sel, count, 4, set, 2, EDITOR_LOC_SELECT_TOGGLE, &dropped);
        CHECK(count == 3, "merge: toggled again they come back (%d)", count);
        count = Editor_LocSelectionMerge(sel, count, 4, &set[2], 1, EDITOR_LOC_SELECT_SUBTRACT, &dropped);
        CHECK(count == 2 && Editor_LocSelectionMerge(sel, count, 2, &set[2], 1, EDITOR_LOC_SELECT_ADD, &dropped) == 2
                  && dropped == 1,
            "merge: subtract takes one off; a full selection counts what it could not hold");
    }
}

/* ---- the wall run ------------------------------------------------------------------ */

static void
arm_place(
    int loc_id,
    int shape,
    int rotation)
{
    g_ui.tools.tool = EDITOR_TOOL_LOC_PLACE;
    g_ui.tools.pick_kind = 0;
    g_ui.tools.pick_id = loc_id;
    g_ui.tools.place_shape = shape;
    g_ui.tools.place_rotation = rotation;
}

/* The Place tool's pose over a wall: with Fit on, straight decor takes the
 * on-wall shape, the one the wall's width pushes out. */
static void
test_decor_pose(void)
{
    static struct Editor_PlaceRun run;
    int const x0 = BASE_X + 50;
    int const z0 = BASE_Z + 4;
    int shape = -1;
    int rotation = -1;

    arm_place(LOC_WALL, 0, 1);
    edp_place_fit_set(&g_ui, 1);
    CHECK(edp_run_begin(&g_ui, &run, x0, z0, 0) == 1, "decor pose: the wall's run opens");
    (void)edp_run_to(&g_ui, &run, x0 + 1, z0, 0);
    CHECK(edp_run_end(&g_ui, &run) > 0, "decor pose: a wall stands there");

    arm_place(LOC_DECOR, 4, 1);
    edp_place_pose_at(&g_ui, x0, z0, 0, &shape, &rotation);
    CHECK(shape == 5 && rotation == 1, "decor pose: on the wall's edge it is shape 5 (%d rot %d)", shape, rotation);
    edp_place_pose_at(&g_ui, x0, z0 + 5, 0, &shape, &rotation);
    CHECK(shape == 4, "decor pose: on a bare tile it stays 4 (%d)", shape);
    edp_place_fit_set(&g_ui, 0);
    edp_place_pose_at(&g_ui, x0, z0, 0, &shape, &rotation);
    CHECK(shape == 4, "decor pose: Fit off places exactly the pose (%d)", shape);
    edp_place_fit_set(&g_ui, 1);
    CHECK(Editor_Undo(&g_editor) > 0, "decor pose: the wall comes back off");
}

static void
test_wall_run(void)
{
    static struct Editor_PlaceRun run;
    static const int k_shape[6] = { 0, 0, 3, 2, 0, 0 };
    static const int k_rotation[6] = { 1, 1, 0, 0, 1, 1 };
    struct Editor_Square* sq = square(&g_editor, 50, 50);
    const struct Editor_History* history = Editor_GetHistory(&g_editor);
    int const x0 = BASE_X + 10;
    int const z0 = BASE_Z + 10;
    char* before;
    char* after;
    int groups;
    int count_before;
    int applied;

    assert(sq);
    before = jl2_of(sq);
    count_before = sq->loc_count;
    groups = history->applied;
    arm_place(LOC_WALL, 0, 1);
    edp_place_fit_set(&g_ui, 1);

    /* East two, north one, east two: six tiles, the turns at the third and
     * fourth. The pose stands on the north edge: the left of east. */
    CHECK(edp_run_begin(&g_ui, &run, x0, z0, 0) == 1 && run.active, "wall run: the press opens a run");
    (void)edp_run_to(&g_ui, &run, x0 + 2, z0, 0);
    (void)edp_run_to(&g_ui, &run, x0 + 2, z0 + 1, 0);
    CHECK(edp_run_to(&g_ui, &run, x0 + 4, z0 + 1, 0) == 1, "wall run: the drag reaches the end");
    CHECK(run.plan.wall && run.plan.count == 6 && run.plan.place_count == 6 && run.plan.refused_count == 0,
        "wall run: six pieces planned, none refused (wall %d count %d place %d refused %d)", run.plan.wall,
        run.plan.count, run.plan.place_count, run.plan.refused_count);
    for( int i = 0; i < 6 && i < run.plan.count; i++ )
        CHECK(run.plan.pieces[i].shape == k_shape[i] && run.plan.pieces[i].rotation == k_rotation[i],
            "wall run: piece %d is shape %d rot %d (plan %d rot %d)", i, k_shape[i], k_rotation[i],
            run.plan.pieces[i].shape, run.plan.pieces[i].rotation);
    CHECK(sq->loc_count == count_before && history->applied == groups,
        "wall run: before release nothing is in the document (%d locs, %d groups)", sq->loc_count,
        history->applied);

    applied = edp_run_end(&g_ui, &run);
    CHECK(applied == 6 && !run.active && sq->loc_count == count_before + 6,
        "wall run: the release places the six (%d applied, %d locs)", applied, sq->loc_count);
    CHECK(history->applied == groups + 1, "wall run: as one undo step (%d groups, was %d)", history->applied,
        groups);
    CHECK(loc_at(LOC_WALL, x0 + 2, z0, 0, 3) && loc_at(LOC_WALL, x0 + 2, z0 + 1, 0, 2)
              && loc_at(LOC_WALL, x0, z0, 0, 0) && loc_at(LOC_WALL, x0 + 4, z0 + 1, 0, 0),
        "wall run: the post, the L and the straight ends are in the document");
    CHECK(g_ui.tools.loc_count == 6 && g_ui.tools.locs[2].shape == 3 && g_ui.tools.locs[3].shape == 2,
        "wall run: what it placed is the selection, by document address (%d)", g_ui.tools.loc_count);
    /* Fit's own answer at the L, with the run's neighbours as Fit reads
     * walls: a straight wall on the west edge south of it and one on the
     * north edge east of it make the same L at the same rotation. */
    {
        static struct Editor_Doc doc;
        struct Editor_Square* s;
        int shape = -1;
        int rotation = -1;
        struct Editor_Loc west = { LOC_WALL, 0, 0, 0, 12, 10 };
        struct Editor_Loc north = { LOC_WALL, 0, 1, 0, 13, 11 };
        memset(&doc, 0, sizeof(doc));
        s = Editor_DocOpenSquare(&doc, 50, 50);
        s->loaded = 1;
        (void)Editor_SquareLocAdd(s, &west);
        (void)Editor_SquareLocAdd(s, &north);
        CHECK(Editor_LocFitWall(&doc, LOC_WALL, 0x20fu, x0 + 2, z0 + 1, 0, &shape, &rotation) && shape == 2
                  && rotation == k_rotation[3],
            "wall run: Fit makes the same L there (%d rot %d)", shape, rotation);
        Editor_DocFree(&doc);
    }

    /* Decor on a wall's edge is the shape the wall's width pushes out. */
    {
        static struct Editor_Doc doc;
        struct Editor_Square* s;
        struct Editor_Loc straight = { LOC_WALL, 0, 1, 0, 13, 11 };
        struct Editor_Loc corner = { LOC_WALL, 2, 0, 0, 20, 20 };
        int shape;
        memset(&doc, 0, sizeof(doc));
        s = Editor_DocOpenSquare(&doc, 50, 50);
        s->loaded = 1;
        (void)Editor_SquareLocAdd(s, &straight);
        (void)Editor_SquareLocAdd(s, &corner);
        shape = 4;
        CHECK(Editor_LocFitDecor(&doc, 0x1f0u, x0 + 3, z0 + 1, 0, 1, &shape) && shape == 5,
            "decor fit: on a straight wall's edge, 4 becomes 5 (%d)", shape);
        shape = 4;
        CHECK(!Editor_LocFitDecor(&doc, 0x1f0u, x0 + 3, z0 + 1, 0, 0, &shape) && shape == 4,
            "decor fit: on another edge of the tile it stays 4 (%d)", shape);
        shape = 4;
        CHECK(!Editor_LocFitDecor(&doc, 0x1f0u, x0 + 4, z0 + 1, 0, 1, &shape) && shape == 4,
            "decor fit: with no wall on the tile it stays 4 (%d)", shape);
        shape = 4;
        CHECK(!Editor_LocFitDecor(&doc, 0x010u, x0 + 3, z0 + 1, 0, 1, &shape) && shape == 4,
            "decor fit: a loc with no shape 5 stays 4 (%d)", shape);
        shape = 7;
        CHECK(!Editor_LocFitDecor(&doc, 0x1f0u, x0 + 3, z0 + 1, 0, 1, &shape) && shape == 7,
            "decor fit: only a straight decor pose changes (%d)", shape);
        shape = 4;
        CHECK(Editor_LocFitDecor(&doc, 0x1f0u, x0 + 10, z0 + 10, 0, 1, &shape) && shape == 5,
            "decor fit: an L carries its second edge too (%d)", shape);
        shape = 4;
        CHECK(!Editor_LocFitDecor(&doc, 0x1f0u, x0 + 10, z0 + 10, 0, 2, &shape) && shape == 4,
            "decor fit: but not the edges it does not have (%d)", shape);
        Editor_DocFree(&doc);
    }

    /* A second session reading the authority's square: the same text the
     * mirror holds (the content-free stand-in for the STATIC rebake). */
    {
        struct EditorHost peer_host;
        struct Editor_Square* theirs;
        CHECK(Editor_HostOpenMapEdEmbedPeer(&peer_host, &g_editor.host, TORIRSMAPED_ROLE_GENERIC, 1),
            "wall run: a second session joins");
        Editor_OpenHost(&g_peer, &peer_host, "second window", &g_profile);
        CHECK(Editor_OpenDoc(&g_peer, EDITOR_DOC_SQUARE, "m50_50") && (theirs = square(&g_peer, 50, 50)) != NULL,
            "wall run: it opens m50_50");
        theirs = square(&g_peer, 50, 50);
        if( theirs )
        {
            char* mine = jl2_of(sq);
            char* authority = jl2_of(theirs);
            CHECK(strcmp(mine, authority) == 0, "wall run: the authority's square is the mirror's, byte for byte");
            free(mine);
            free(authority);
        }
    }

    CHECK(Editor_Undo(&g_editor) == 6 && sq->loc_count == count_before,
        "wall run: one undo takes all six back (%d locs)", sq->loc_count);
    after = jl2_of(sq);
    CHECK(strcmp(before, after) == 0, "wall run: and the .jl2 is byte-identical to before the run");
    free(after);
    CHECK(Editor_Redo(&g_editor) == 6 && sq->loc_count == count_before + 6, "wall run: redo puts the six back");

    /* The next run starts where this one ended and turns south: the end of
     * the first run, the same loc, is re-posed as the corner (an L outside
     * a right turn), not refused. */
    (void)edp_run_begin(&g_ui, &run, x0 + 3, z0 + 1, 0);
    (void)edp_run_to(&g_ui, &run, x0 + 4, z0 + 1, 0);
    (void)edp_run_to(&g_ui, &run, x0 + 4, z0 - 1, 0);
    CHECK(run.plan.count == 4 && run.plan.pieces[0].state == EDITOR_LOC_RUN_KEEP
              && run.plan.pieces[1].state == EDITOR_LOC_RUN_PLACE && run.plan.pieces[1].shape == 2
              && run.plan.pieces[1].rotation == 1 && run.plan.pieces[1].has_holder
              && run.plan.pieces[1].holder.shape == 0,
        "wall run: the old end is re-posed as an L rot 1 over the straight piece (state %d shape %d rot %d)",
        run.plan.count > 1 ? (int)run.plan.pieces[1].state : -1, run.plan.count > 1 ? run.plan.pieces[1].shape : -1,
        run.plan.count > 1 ? run.plan.pieces[1].rotation : -1);
    groups = history->applied;
    CHECK(edp_run_end(&g_ui, &run) == 4 && loc_at(LOC_WALL, x0 + 4, z0 + 1, 0, 2)
              && !loc_at(LOC_WALL, x0 + 4, z0 + 1, 0, 0) && history->applied == groups + 1,
        "wall run: the release swaps the straight end for the L (a remove and an add) and places the rest");
    CHECK(Editor_Undo(&g_editor) == 4 && loc_at(LOC_WALL, x0 + 4, z0 + 1, 0, 0),
        "wall run: whose undo puts the straight end back");

    /* A loc without the corner post leaves the inside corner empty. */
    arm_place(LOC_POSTLESS_WALL, 0, 1);
    (void)edp_run_begin(&g_ui, &run, x0, z0 + 20, 0);
    (void)edp_run_to(&g_ui, &run, x0 + 1, z0 + 20, 0);
    (void)edp_run_to(&g_ui, &run, x0 + 1, z0 + 21, 0);
    CHECK(run.plan.count == 3 && run.plan.pieces[1].state == EDITOR_LOC_RUN_NONE && run.plan.place_count == 2,
        "wall run: with no shape 3 the inside corner has no piece (state %d)",
        run.plan.count > 1 ? (int)run.plan.pieces[1].state : -1);
    edp_run_cancel(&run);
    CHECK(!run.active && !loc_at(LOC_POSTLESS_WALL, x0, z0 + 20, 0, 0), "wall run: a cancelled run placed nothing");

    /* A click is not a run: a press and release on one tile is -1. */
    arm_place(LOC_WALL, 0, 1);
    (void)edp_run_begin(&g_ui, &run, x0, z0 + 30, 0);
    CHECK(edp_run_end(&g_ui, &run) == -1 && !loc_at(LOC_WALL, x0, z0 + 30, 0, 0),
        "wall run: a press that stays on its tile is the click's to place");
    free(before);
}

/* ---- fitted ends, diagonal runs (walls and fences) -------------------------------- */

static void
test_fit_run(void)
{
    static struct Editor_PlaceRun run;
    struct Editor_Square* sq = square(&g_editor, 50, 50);
    int const x0 = BASE_X + 30;
    int const z0 = BASE_Z + 4;
    int count_before;

    assert(sq);
    /* A wall already there, along the north edge of (x0, z0 + 3). A run
     * north from (x0, z0), the pose on the west edge, ends on that tile: with
     * Fit its end is the L of west and north; without, the straight piece. */
    seed(LOC_WALL, 0, 1, x0, z0 + 3, 0);
    count_before = sq->loc_count;
    arm_place(LOC_WALL, 0, 0);
    edp_place_fit_set(&g_ui, 0);
    CHECK(edp_run_begin(&g_ui, &run, x0, z0, 0) == 1, "fit run: opens");
    (void)edp_run_to(&g_ui, &run, x0, z0 + 3, 0);
    CHECK(run.plan.wall && run.plan.count == 4 && run.plan.pieces[3].shape == 0 && run.plan.pieces[3].has_holder,
        "fit run: without Fit the end replaces the wall there (shape %d holder %d)", run.plan.pieces[3].shape,
        run.plan.pieces[3].has_holder);
    edp_run_cancel(&run);
    edp_place_fit_set(&g_ui, 1);
    CHECK(edp_run_begin(&g_ui, &run, x0, z0, 0) == 1, "fit run: opens again");
    (void)edp_run_to(&g_ui, &run, x0, z0 + 3, 0);
    CHECK(run.plan.count == 4 && run.plan.pieces[3].shape == 2 && run.plan.pieces[3].rotation == 0
              && run.plan.pieces[3].state == EDITOR_LOC_RUN_PLACE && run.plan.pieces[0].shape == 0
              && run.plan.pieces[0].rotation == 0,
        "fit run: with Fit the end is the L of west and north, rot 0 (shape %d rot %d state %d)",
        run.plan.pieces[3].shape, run.plan.pieces[3].rotation, run.plan.pieces[3].state);
    CHECK(edp_run_end(&g_ui, &run) == 5 && sq->loc_count == count_before + 3,
        "fit run: the release re-poses the end (a remove and an add) and places three (%d locs)", sq->loc_count);
    CHECK(loc_at(LOC_WALL, x0, z0 + 3, 0, 2) && !loc_at(LOC_WALL, x0, z0 + 3, 0, 0),
        "fit run: the L stands where the straight wall was");
    /* The same run again: the L already carries the west edge, so it is kept. */
    CHECK(edp_run_begin(&g_ui, &run, x0, z0, 0) == 1, "fit run: a third run");
    (void)edp_run_to(&g_ui, &run, x0, z0 + 3, 0);
    CHECK(run.plan.pieces[3].state == EDITOR_LOC_RUN_KEEP && run.plan.pieces[3].shape == 2
              && run.plan.place_count == 0,
        "fit run: an L already carrying the run's edge is kept, nothing to place (state %d place %d)",
        run.plan.pieces[3].state, run.plan.place_count);
    edp_run_cancel(&run);
    CHECK(Editor_Undo(&g_editor) > 0 && loc_at(LOC_WALL, x0, z0 + 3, 0, 0) && sq->loc_count == count_before,
        "fit run: one undo puts the straight wall back and takes the three off (%d locs)", sq->loc_count);
}

static void
test_diagonal_run(void)
{
    static struct Editor_PlaceRun run;
    struct Editor_Square* sq = square(&g_editor, 50, 50);
    int const x0 = BASE_X + 40;
    int const z0 = BASE_Z + 4;
    int count_before;

    assert(sq);
    count_before = sq->loc_count;
    arm_place(LOC_WALL, 9, 0);
    CHECK(edp_run_begin(&g_ui, &run, x0, z0, 0) == 1, "diagonal run: opens");
    /* Shift with the diagonal picked: constrain 2, the staircase to (+3, +3). */
    CHECK(edp_run_to(&g_ui, &run, x0 + 3, z0 + 3, 2) == 1 && run.path.count == 7 && run.path.x[1] == x0 + 1
              && run.path.z[1] == z0 && run.path.x[2] == x0 + 1 && run.path.z[2] == z0 + 1,
        "diagonal run: the staircase has seven tiles, x first (%d)", run.path.count);
    CHECK(run.plan.diagonal && !run.plan.wall && run.plan.place_count == 4 && run.plan.refused_count == 0,
        "diagonal run: four diagonal pieces planned (diagonal %d wall %d place %d)", run.plan.diagonal,
        run.plan.wall, run.plan.place_count);
    for( int i = 0; i < run.plan.count; i++ )
        CHECK((i & 1) ? run.plan.pieces[i].state == EDITOR_LOC_RUN_NONE
                      : run.plan.pieces[i].state == EDITOR_LOC_RUN_PLACE && run.plan.pieces[i].shape == 9
                            && run.plan.pieces[i].rotation == 0,
            "diagonal run: piece %d is %s (state %d shape %d rot %d)", i, (i & 1) ? "nothing" : "shape 9 rot 0",
            run.plan.pieces[i].state, run.plan.pieces[i].shape, run.plan.pieces[i].rotation);
    CHECK(edp_run_end(&g_ui, &run) == 4 && sq->loc_count == count_before + 4 && loc_at(LOC_WALL, x0 + 2, z0 + 2, 0, 9)
              && !loc_at(LOC_WALL, x0 + 1, z0, 0, 9),
        "diagonal run: the release lays the four on the diagonal (%d locs)", sq->loc_count);
    /* North-west / south-east (an x step and a z step of opposite signs): rotation 1. */
    CHECK(edp_run_begin(&g_ui, &run, x0 + 10, z0 + 3, 0) == 1, "diagonal run: a second, toward the south-east");
    (void)edp_run_to(&g_ui, &run, x0 + 12, z0, 2);
    CHECK(run.plan.diagonal && run.plan.place_count == 3 && run.plan.pieces[0].rotation == 1,
        "diagonal run: south-east is rotation 1 (diagonal %d place %d rot %d)", run.plan.diagonal,
        run.plan.place_count, run.plan.pieces[0].rotation);
    edp_run_cancel(&run);
    /* The diagonal pose on a plain path: the wall run it always was. */
    CHECK(edp_run_begin(&g_ui, &run, x0 + 10, z0 + 8, 0) == 1, "diagonal run: a straight drag");
    (void)edp_run_to(&g_ui, &run, x0 + 13, z0 + 8, 0);
    CHECK(!run.plan.diagonal && run.plan.wall, "diagonal run: a straight drag with the diagonal picked is a wall run");
    edp_run_cancel(&run);
    CHECK(Editor_Undo(&g_editor) > 0 && sq->loc_count == count_before, "diagonal run: one undo takes the four off");
}

/* ---- the brushes: roughen, scatter, plateau ------------------------------------- */

static int
derived(
    int world_x,
    int world_z)
{
    int h = 0;
    (void)Editor_OpsDerivedHeight(&g_editor.doc, world_x, world_z, 0, &h);
    return h;
}

static void
apply_all(
    const struct Editor_Cmd* commands,
    int count,
    const char* name)
{
    Editor_StrokeBegin(&g_editor);
    Editor_HistoryName(&g_editor, name);
    for( int i = 0; i < count; i++ )
        (void)Editor_Apply(&g_editor, &commands[i]);
    Editor_StrokeEnd(&g_editor);
}

static void
test_noise(void)
{
    static struct Editor_Cmd first[EDITOR_BRUSH_TILES_MAX];
    static struct Editor_Cmd again[EDITOR_BRUSH_TILES_MAX];
    struct Editor_BrushOp op;
    int const cx = BASE_X + 20;
    int const cz = BASE_Z + 40;
    int before[5][5];
    int n;
    int m;
    int moved = 0;
    int within = 1;
    int same = 1;

    memset(&op, 0, sizeof(op));
    op.tool = EDITOR_TOOL_NOISE;
    op.world_x = cx;
    op.world_z = cz;
    op.level = 0;
    op.brush.radius = 2;
    op.brush.falloff = EDITOR_BRUSH_FALLOFF_FLAT;
    op.brush.step = 3;
    op.noise_seed = 7;
    for( int dz = -2; dz <= 2; dz++ )
        for( int dx = -2; dx <= 2; dx++ )
            before[dz + 2][dx + 2] = derived(cx + dx, cz + dz);
    n = Editor_BrushCommands(&g_editor.doc, &op, first, EDITOR_BRUSH_TILES_MAX);
    m = Editor_BrushCommands(&g_editor.doc, &op, again, EDITOR_BRUSH_TILES_MAX);
    CHECK(n > 3 && n == m, "noise: the brush moves tiles, and the same seed plans the same (%d, %d)", n, m);
    for( int i = 0; i < n && i < m; i++ )
        if( first[i].u.tile.x != again[i].u.tile.x || first[i].u.tile.z != again[i].u.tile.z
            || first[i].u.tile.after.height != again[i].u.tile.after.height )
            same = 0;
    CHECK(same, "noise: tile for tile");
    apply_all(first, n, "roughen");
    for( int dz = -2; dz <= 2; dz++ )
        for( int dx = -2; dx <= 2; dx++ )
        {
            int const d = derived(cx + dx, cz + dz) - before[dz + 2][dx + 2];
            if( d != 0 )
                moved++;
            /* The step in units of 8, plus the ladder's snap: an absent
             * (generated) tile first lands on a representable height. */
            if( d < -(3 * 8 + 16) || d > 3 * 8 + 16 )
                within = 0;
        }
    CHECK(moved == n && within, "noise: %d tiles moved, none past the step (moved %d within %d)", n, moved, within);
    op.noise_seed = 8;
    m = Editor_BrushCommands(&g_editor.doc, &op, again, EDITOR_BRUSH_TILES_MAX);
    same = m == n;
    for( int i = 0; same && i < n; i++ )
        if( first[i].u.tile.after.height != again[i].u.tile.after.height )
            same = 0;
    CHECK(!same, "noise: another seed is another relief");
    CHECK(Editor_Undo(&g_editor) == n && derived(cx, cz) == before[2][2], "noise: one undo levels it again");
}

/* Roughen's relief is ground, not static: a tile goes the way its neighbours
 * go. Measured as the correlation of each tile with the next one along x --
 * near 0 for a draw per tile (what Roughen was), high for hills -- and the
 * relief keeps the step as a hill's height at every scale and detail. */
static double
relief_neighbour_correlation(
    uint32_t seed,
    int scale,
    int detail,
    double* out_peak)
{
    enum
    {
        SIDE = 96
    };
    double covariance = 0.0;
    double variance = 0.0;
    double peak = 0.0;

    for( int z = 0; z < SIDE; z++ )
        for( int x = 0; x < SIDE; x++ )
        {
            double const here = Editor_RoughenRelief(seed, BASE_X + x, BASE_Z + z, scale, detail);
            double const next = Editor_RoughenRelief(seed, BASE_X + x + 1, BASE_Z + z, scale, detail);
            covariance += here * next;
            variance += here * here;
            if( fabs(here) > peak )
                peak = fabs(here);
        }
    *out_peak = peak;
    return variance > 0.0 ? covariance / variance : 0.0;
}

static void
test_relief(void)
{
    static struct Editor_Cmd plan[EDITOR_BRUSH_TILES_MAX];
    static struct Editor_Cmd again[EDITOR_BRUSH_TILES_MAX];
    struct Editor_BrushOp op;
    double peak;
    double correlation;
    int const cx = BASE_X + 32;
    int const cz = BASE_Z + 20;
    int n;
    int m;
    int agree = 0;
    int pairs = 0;
    int same = 1;

    correlation = relief_neighbour_correlation(11, 1, 50, &peak);
    CHECK(fabs(correlation) < 0.1, "relief: scale 1 is a draw per tile (neighbour correlation %.3f)", correlation);
    correlation = relief_neighbour_correlation(11, 8, 50, &peak);
    CHECK(correlation > 0.7 && peak > 0.8 && peak <= 1.0,
        "relief: at 8 tiles a tile goes where its neighbour goes (correlation %.3f, peak %.3f)", correlation, peak);
    correlation = relief_neighbour_correlation(11, 8, 100, &peak);
    CHECK(correlation > 0.6 && correlation < relief_neighbour_correlation(11, 8, 0, &peak),
        "relief: detail roughens the same hills (correlation %.3f at 100)", correlation);
    correlation = relief_neighbour_correlation(11, 24, 50, &peak);
    CHECK(correlation > 0.95 && peak > 0.8, "relief: wider hills are smoother (correlation %.3f at 24, peak %.3f)",
        correlation, peak);
    CHECK(Editor_RoughenRelief(11, cx, cz, 0, 50) == Editor_RoughenRelief(11, cx, cz, EDITOR_ROUGHEN_SCALE_DEFAULT, 50)
              && Editor_RoughenRelief(11, cx, cz, 8, 50) != Editor_RoughenRelief(12, cx, cz, 8, 50),
        "relief: scale 0 reads as the default, and another seed is another relief");

    /* The brush lays it: under a wide flat brush with a tall step, tiles
     * that moved mostly moved the way the tile beside them did. */
    memset(&op, 0, sizeof(op));
    op.tool = EDITOR_TOOL_NOISE;
    op.world_x = cx;
    op.world_z = cz;
    op.level = 0;
    op.brush.radius = 8;
    op.brush.falloff = EDITOR_BRUSH_FALLOFF_FLAT;
    op.brush.step = 6;
    op.noise_seed = 21;
    op.noise_scale = 8;
    op.noise_detail = 50;
    n = Editor_BrushCommands(&g_editor.doc, &op, plan, EDITOR_BRUSH_TILES_MAX);
    m = Editor_BrushCommands(&g_editor.doc, &op, again, EDITOR_BRUSH_TILES_MAX);
    for( int i = 0; i < n && i < m; i++ )
        if( plan[i].u.tile.x != again[i].u.tile.x || plan[i].u.tile.z != again[i].u.tile.z
            || plan[i].u.tile.after.height != again[i].u.tile.after.height )
            same = 0;
    CHECK(n > 100 && n == m && same, "relief: the brush plans the same ground twice (%d, %d)", n, m);
    for( int i = 0; i + 1 < n; i++ )
    {
        /* Commands come out row by row: the next one on the same row is
         * the tile beside it. */
        if( plan[i].u.tile.z != plan[i + 1].u.tile.z || plan[i + 1].u.tile.x != plan[i].u.tile.x + 1 )
            continue;
        pairs++;
        if( (Editor_RoughenRelief(21, BASE_X + plan[i].u.tile.x, BASE_Z + plan[i].u.tile.z, 8, 50) > 0)
            == (Editor_RoughenRelief(21, BASE_X + plan[i + 1].u.tile.x, BASE_Z + plan[i + 1].u.tile.z, 8, 50) > 0) )
            agree++;
    }
    CHECK(pairs > 60 && agree * 10 >= pairs * 8, "relief: %d of %d neighbouring tiles go the same way", agree, pairs);

    /* Whole units from a fraction: a step of one still moves ground, where
     * rounding to the nearest left every tile under half a unit alone. */
    op.brush.step = 1;
    n = Editor_BrushCommands(&g_editor.doc, &op, plan, EDITOR_BRUSH_TILES_MAX);
    CHECK(n > 20, "relief: a step of one unit moves tiles too (%d)", n);

    /* One stroke lays the relief once. A drag applies the brush at every
     * tile it crosses, so a tile is under many applications; pushed by each,
     * a step of 6 under a radius-8 brush raised hills of a hundred units.
     * With the stroke remembered, no tile ends further than the step from
     * where it began, and going over the same ground again adds nothing. */
    {
        static struct Editor_RoughenStroke stroke;
        static int before[17][33];
        int worst = 0;
        int moved = 0;
        int again_total = 0;

        op.brush.step = 6;
        op.brush.falloff = EDITOR_BRUSH_FALLOFF_SMOOTH;
        op.noise_seed = 33;
        op.noise_stroke = &stroke;
        Editor_RoughenStrokeBegin(&stroke, op.noise_seed);
        for( int dz = -8; dz <= 8; dz++ )
            for( int dx = -8; dx <= 24; dx++ )
                before[dz + 8][dx + 8] = derived(cx + dx, cz + dz);
        Editor_StrokeBegin(&g_editor);
        Editor_HistoryName(&g_editor, "roughen");
        for( int pass = 0; pass < 2; pass++ )
            for( int step = 0; step <= 16; step++ )
            {
                op.world_x = cx + step;
                n = Editor_BrushCommands(&g_editor.doc, &op, plan, EDITOR_BRUSH_TILES_MAX);
                for( int i = 0; i < n; i++ )
                    (void)Editor_Apply(&g_editor, &plan[i]);
                Editor_RoughenStrokeNote(&stroke, &op, plan, n);
                if( pass == 1 )
                    again_total += n;
            }
        Editor_StrokeEnd(&g_editor);
        for( int dz = -8; dz <= 8; dz++ )
            for( int dx = -8; dx <= 24; dx++ )
            {
                int d = derived(cx + dx, cz + dz) - before[dz + 8][dx + 8];
                if( d < 0 )
                    d = -d;
                if( d > 0 )
                    moved++;
                if( d > worst )
                    worst = d;
            }
        /* The step in units of 8, plus the ladder's snap of a generated
         * tile onto a representable height. */
        CHECK(moved > 150 && worst <= 6 * 8 + 16,
            "relief: a 17-tile drag moves %d tiles and none further than the step (worst %d of %d)", moved, worst,
            6 * 8 + 16);
        CHECK(again_total == 0, "relief: the same stroke over the same ground again adds nothing (%d)", again_total);
        CHECK(Editor_Undo(&g_editor) > 150 && derived(cx, cz) == before[8][8], "relief: one undo takes the stroke back");
        op.world_x = cx;
        op.noise_stroke = NULL;
    }
}

/* Paint underlay's mix: under 100 the brush paints a seeded share of its
 * tiles (the dither the client's blend smooths); 100 is the flat paint. */
static void
test_mix(void)
{
    static struct Editor_Cmd full[EDITOR_BRUSH_TILES_MAX];
    static struct Editor_Cmd half[EDITOR_BRUSH_TILES_MAX];
    static struct Editor_Cmd again[EDITOR_BRUSH_TILES_MAX];
    struct Editor_BrushOp op;
    int n;
    int m;
    int k;
    int same = 1;
    int subset = 1;

    memset(&op, 0, sizeof(op));
    op.tool = EDITOR_TOOL_PAINT_UNDERLAY;
    op.world_x = BASE_X + 30;
    op.world_z = BASE_Z + 30;
    op.level = 0;
    op.brush.radius = 4;
    op.brush.falloff = EDITOR_BRUSH_FALLOFF_FLAT;
    op.underlay_id = 200;
    op.noise_seed = 5;
    op.paint_mix = 100;
    n = Editor_BrushCommands(&g_editor.doc, &op, full, EDITOR_BRUSH_TILES_MAX);
    CHECK(n >= 45, "mix: at 100 the whole disc is painted (%d)", n);
    op.paint_mix = 50;
    m = Editor_BrushCommands(&g_editor.doc, &op, half, EDITOR_BRUSH_TILES_MAX);
    CHECK(m > n / 5 && m < n * 4 / 5, "mix: at 50 about half of it (%d of %d)", m, n);
    for( int i = 0; i < m; i++ )
    {
        int found = 0;
        for( int j = 0; j < n; j++ )
            if( full[j].u.tile.x == half[i].u.tile.x && full[j].u.tile.z == half[i].u.tile.z )
                found = 1;
        if( !found || half[i].u.tile.after.underlay_id != 200 )
            subset = 0;
    }
    CHECK(subset, "mix: every tile painted is one of the disc's, with the underlay");
    k = Editor_BrushCommands(&g_editor.doc, &op, again, EDITOR_BRUSH_TILES_MAX);
    same = k == m;
    for( int i = 0; same && i < m; i++ )
        if( again[i].u.tile.x != half[i].u.tile.x || again[i].u.tile.z != half[i].u.tile.z )
            same = 0;
    CHECK(same, "mix: the same seed paints the same tiles (%d, %d)", m, k);
    op.noise_seed = 6;
    k = Editor_BrushCommands(&g_editor.doc, &op, again, EDITOR_BRUSH_TILES_MAX);
    same = k == m;
    for( int i = 0; same && i < m; i++ )
        if( again[i].u.tile.x != half[i].u.tile.x || again[i].u.tile.z != half[i].u.tile.z )
            same = 0;
    CHECK(!same, "mix: another seed is another dither");
    op.brush.falloff = EDITOR_BRUSH_FALLOFF_LINEAR;
    op.noise_seed = 5;
    k = Editor_BrushCommands(&g_editor.doc, &op, again, EDITOR_BRUSH_TILES_MAX);
    CHECK(k < m, "mix: the falloff thins the dither toward the rim (%d < %d)", k, m);
    apply_all(half, m, "paint underlay");
    CHECK(Editor_Undo(&g_editor) == m, "mix: one undo takes the dither off");
}

static void
test_scatter(void)
{
    static struct Editor_Cmd commands[EDITOR_BRUSH_TILES_MAX];
    struct Editor_Square* sq = square(&g_editor, 50, 50);
    struct Editor_ScatterOp op;
    int const cx = BASE_X + 44;
    int const cz = BASE_Z + 44;
    int count_before;
    int n;
    int m;
    int in_set = 1;
    int in_disc = 1;
    int overlap = 0;
    int turned = 0;

    assert(sq);
    count_before = sq->loc_count;
    memset(&op, 0, sizeof(op));
    op.world_x = cx;
    op.world_z = cz;
    op.level = 0;
    op.brush.radius = 3;
    op.brush.falloff = EDITOR_BRUSH_FALLOFF_FLAT;
    op.density = 100;
    op.rotate = 1;
    op.seed = 11;
    op.set.count = 2;
    op.set.loc_id[0] = LOC_TREE;
    op.set.shape[0] = 10;
    op.set.size_x[0] = 1;
    op.set.size_z[0] = 1;
    op.set.loc_id[1] = LOC_CRATE;
    op.set.shape[1] = 10;
    op.set.size_x[1] = 2;
    op.set.size_z[1] = 3;
    n = Editor_ScatterCommands(&g_editor.doc, &op, commands, EDITOR_BRUSH_TILES_MAX);
    CHECK(n >= 6 && n <= 37, "scatter: at 100%% the disc fills as far as footprints allow (%d)", n);
    for( int i = 0; i < n; i++ )
    {
        const struct Editor_Loc* loc = &commands[i].u.loc.after;
        int const dx = BASE_X + loc->x - cx;
        int const dz = BASE_Z + loc->z - cz;
        if( !commands[i].has_after || commands[i].has_before
            || (loc->loc_id != LOC_TREE && loc->loc_id != LOC_CRATE) || loc->shape != 10 )
            in_set = 0;
        if( dx * dx + dz * dz > 3 * 3 + 3 )
            in_disc = 0;
        if( loc->rotation )
            turned++;
        for( int j = 0; j < i; j++ )
        {
            const struct Editor_Loc* other = &commands[j].u.loc.after;
            int w;
            int h;
            int ow;
            int oh;
            Editor_LocFootprint(10, loc->rotation, loc->loc_id == LOC_CRATE ? 2 : 1,
                loc->loc_id == LOC_CRATE ? 3 : 1, &w, &h);
            Editor_LocFootprint(10, other->rotation, other->loc_id == LOC_CRATE ? 2 : 1,
                other->loc_id == LOC_CRATE ? 3 : 1, &ow, &oh);
            if( loc->x < other->x + ow && other->x < loc->x + w && loc->z < other->z + oh
                && other->z < loc->z + h )
                overlap++;
        }
    }
    CHECK(in_set && in_disc && overlap == 0 && turned > 0,
        "scatter: every piece an add of the set, in the disc, no two footprints crossing, some turned "
        "(set %d disc %d overlap %d turned %d)",
        in_set, in_disc, overlap, turned);
    apply_all(commands, n, "scatter");
    CHECK(sq->loc_count == count_before + n, "scatter: applied (%d locs)", sq->loc_count);
    m = Editor_ScatterCommands(&g_editor.doc, &op, commands, EDITOR_BRUSH_TILES_MAX);
    CHECK(m == 0, "scatter: the same stroke over it again adds nothing (%d)", m);
    op.erase = 1;
    m = Editor_ScatterCommands(&g_editor.doc, &op, commands, EDITOR_BRUSH_TILES_MAX);
    CHECK(m == n, "scatter: erase takes exactly what was dropped (%d of %d)", m, n);
    apply_all(commands, m, "scatter erase");
    CHECK(sq->loc_count == count_before, "scatter: erased (%d locs)", sq->loc_count);
    CHECK(Editor_Undo(&g_editor) == m && sq->loc_count == count_before + n && Editor_Undo(&g_editor) == n
              && sq->loc_count == count_before,
        "scatter: each stroke one undo step");
    op.erase = 0;
    op.rotate = 0;
    op.density = 10;
    op.brush.falloff = EDITOR_BRUSH_FALLOFF_LINEAR;
    m = Editor_ScatterCommands(&g_editor.doc, &op, commands, EDITOR_BRUSH_TILES_MAX);
    turned = 0;
    for( int i = 0; i < m; i++ )
        if( commands[i].u.loc.after.rotation )
            turned++;
    CHECK(m < n && turned == 0, "scatter: a tenth of the density is far fewer, none turned (%d of %d)", m, n);
}

static void
test_plateau(void)
{
    static struct Editor_Cmd commands[EDITOR_BRUSH_TILES_MAX * 4];
    struct Editor_Brush brush;
    int const cx = BASE_X + 10;
    int const cz = BASE_Z + 50;
    int const top = -64;
    int own[4];
    int centre;
    int n;
    int flat = 1;
    int eased = 1;

    memset(&brush, 0, sizeof(brush));
    brush.radius = 1;
    centre = derived(cx, cz);
    for( int d = 0; d < 4; d++ )
        own[d] = derived(cx + 1 + d, cz);
    n = Editor_PlateauBrushCommands(&g_editor.doc, 0, top, 2, &brush, cx, cz, commands, EDITOR_BRUSH_TILES_MAX * 4);
    CHECK(n >= 9 && n <= 49, "plateau: the disc and its skirt (%d tiles)", n);
    apply_all(commands, n, "plateau");
    for( int dz = -1; dz <= 1; dz++ )
        for( int dx = -1; dx <= 1; dx++ )
            if( derived(cx + dx, cz + dz) != top )
                flat = 0;
    CHECK(flat, "plateau: the nine tiles of the top are at the height (%d %d)", derived(cx, cz), derived(cx + 1, cz + 1));
    /* East of the top: distance 1 nearest the top, 2 further, 3 untouched. */
    {
        int const h1 = derived(cx + 2, cz);
        int const h2 = derived(cx + 3, cz);
        int const h3 = derived(cx + 4, cz);
        int const g1 = top - h1 < 0 ? h1 - top : top - h1;
        int const g2 = top - h2 < 0 ? h2 - top : top - h2;
        int const g3 = top - own[2] < 0 ? own[2] - top : top - own[2];
        if( !(g1 < g2 && g2 < g3) || h3 != own[3] )
            eased = 0;
        CHECK(eased, "plateau: the skirt eases from the top to the ground and stops at it (%d %d %d / own %d %d)",
            h1, h2, h3, own[1], own[2]);
    }
    CHECK(Editor_Undo(&g_editor) == n && derived(cx, cz) == centre && derived(cx + 2, cz) == own[1],
        "plateau: one undo puts the ground back (%d, %d)", derived(cx, cz), derived(cx + 2, cz));
    /* The rectangle: the same through the Rect op, with the skirt. */
    {
        struct Editor_TileRect rect;
        int m;
        rect.valid = 1;
        rect.x0 = cx - 2;
        rect.z0 = cz - 2;
        rect.x1 = cx + 2;
        rect.z1 = cz + 2;
        rect.level = 0;
        m = Editor_TileRectPlateau(&g_editor.doc, &rect, top, 1, commands, EDITOR_BRUSH_TILES_MAX * 4);
        CHECK(m >= 25 && m <= 49, "plateau: a 5x5 rectangle and its one-tile skirt (%d)", m);
        apply_all(commands, m, "plateau");
        CHECK(derived(cx - 2, cz + 2) == top && derived(cx + 2, cz - 2) == top && derived(cx + 4, cz) == own[3],
            "plateau: the rectangle's corners are on top, two tiles out is untouched");
        CHECK(Editor_Undo(&g_editor) == m, "plateau: one undo for the rectangle");
    }
}

static void
test_refused_runs(void)
{
    static struct Editor_PlaceRun run;
    int const x0 = BASE_X + 30;
    int const z0 = BASE_Z + 40;
    uint32_t serial;
    int count;

    /* A centrepiece run over a tile another loc's centrepiece holds, and one
     * this loc already holds at this pose. */
    seed(LOC_CRATE, 10, 0, x0 + 2, z0, 0);
    seed(LOC_TREE, 10, 0, x0 + 3, z0, 0);
    arm_place(LOC_TREE, 10, 0);
    serial = g_ui.status_serial;
    (void)edp_run_begin(&g_ui, &run, x0, z0, 0);
    (void)edp_run_to(&g_ui, &run, x0 + 4, z0, 0);
    CHECK(!run.plan.wall && run.plan.count == 5 && run.plan.pieces[2].state == EDITOR_LOC_RUN_TAKEN
              && run.plan.pieces[2].holder.loc_id == LOC_CRATE && run.plan.pieces[3].state == EDITOR_LOC_RUN_KEEP
              && run.plan.place_count == 3 && run.plan.refused_count == 1 && run.plan.first_refused == 2,
        "tile run: the crate's key is refused (red), the tree's own kept, three to place (place %d refused %d)",
        run.plan.place_count, run.plan.refused_count);
    CHECK(g_ui.status_serial == serial + 1 && strstr(Editor_UiStatusText(&g_ui), "Place refused: tile ")
              && strstr(Editor_UiStatusText(&g_ui), "holds a centrepiece of that shape (loc 500); press E to swap"),
        "tile run: the status bar says why (\"%s\")", Editor_UiStatusText(&g_ui));
    (void)edp_run_to(&g_ui, &run, x0 + 5, z0, 0);
    (void)edp_run_to(&g_ui, &run, x0 + 6, z0, 0);
    CHECK(g_ui.status_serial == serial + 1, "tile run: once a run, not once a tile (%u sentences)",
        g_ui.status_serial - serial);
    count = edp_run_end(&g_ui, &run);
    CHECK(count == 5 && loc_at(LOC_TREE, x0, z0, 0, 10) && loc_at(LOC_CRATE, x0 + 2, z0, 0, 10)
              && !loc_at(LOC_TREE, x0 + 2, z0, 0, 10) && loc_at(LOC_TREE, x0 + 6, z0, 0, 10),
        "tile run: the release skips the taken key and places the rest (%d)", count);
    CHECK(g_ui.status_serial == serial + 1, "tile run: and the release does not say it again");
    CHECK(Editor_Undo(&g_editor) == 5 && loc_at(LOC_CRATE, x0 + 2, z0, 0, 10) && loc_at(LOC_TREE, x0 + 3, z0, 0, 10)
              && !loc_at(LOC_TREE, x0, z0, 0, 10),
        "tile run: one undo, the crate and the old tree untouched");

    /* A wall run onto another loc's wall layer (a different shape): refused. */
    seed(LOC_OTHER_WALL, 2, 0, x0 + 1, z0 + 5, 0);
    arm_place(LOC_WALL, 0, 1);
    (void)edp_run_begin(&g_ui, &run, x0, z0 + 5, 0);
    (void)edp_run_to(&g_ui, &run, x0 + 2, z0 + 5, 0);
    CHECK(run.plan.pieces[1].state == EDITOR_LOC_RUN_TAKEN && run.plan.pieces[1].holder.loc_id == LOC_OTHER_WALL
              && strstr(Editor_UiStatusText(&g_ui), "holds a wall L of another loc (loc 1903)"),
        "wall run: another loc's L on the tile refuses the straight piece (\"%s\")", Editor_UiStatusText(&g_ui));
    edp_run_cancel(&run);

    /* Into a square that is not open: refused, named. */
    (void)edp_run_begin(&g_ui, &run, BASE_X + 62, z0, 0);
    (void)edp_run_to(&g_ui, &run, BASE_X + 66, z0, 0);
    CHECK(run.plan.count == 5 && run.plan.pieces[1].state == EDITOR_LOC_RUN_PLACE
              && run.plan.pieces[2].state == EDITOR_LOC_RUN_CLOSED && run.plan.refused_count == 3
              && strstr(Editor_UiStatusText(&g_ui), "is in m51_50, which is not open, and 2 more tiles of the run"),
        "wall run: the closed square's tiles are refused (\"%s\")", Editor_UiStatusText(&g_ui));
    CHECK(edp_run_end(&g_ui, &run) == 2 && loc_at(LOC_WALL, BASE_X + 63, z0, 0, 0),
        "wall run: the open square's two are placed");
    (void)Editor_Undo(&g_editor);
}

/* ---- the marquee and select same ----------------------------------------------------- */

static void
test_marquee(void)
{
    struct Editor_TileRect rect;
    int const x = BASE_X + 4;
    int const z = BASE_Z + 50;

    /* A 2x3 crate (covers x..x+1, z..z+2), the same turned (covers x+10..
     * x+12, z..z+1), a wall at (x+5, z+5) on level 0 and its twin above. */
    seed(LOC_CRATE, 10, 0, x, z, 0);
    seed(LOC_CRATE, 10, 1, x + 10, z, 0);
    seed(LOC_WALL, 0, 0, x + 5, z + 5, 0);
    seed(LOC_WALL, 0, 0, x + 5, z + 5, 1);
    g_ui.tools.tool = EDITOR_TOOL_SELECT;
    g_ui.tools.loc_count = 0;

    rect = (struct Editor_TileRect){ 1, x + 1, z + 2, x + 3, z + 4, 0 };
    CHECK(edp_select_rect(&g_ui, &rect, EDITOR_LOC_SELECT_REPLACE) == 1 && g_ui.tools.locs[0].loc_id == LOC_CRATE
              && g_ui.tools.locs[0].x == x % 64,
        "marquee: a rectangle over the crate's far corner selects it by footprint (%d)", g_ui.tools.loc_count);
    rect = (struct Editor_TileRect){ 1, x + 13, z + 1, x + 12, z + 1, 0 };
    CHECK(edp_select_rect(&g_ui, &rect, EDITOR_LOC_SELECT_ADD) == 2 && g_ui.tools.locs[1].rotation == 1,
        "marquee: Shift adds the turned crate, whose footprint reaches x+12 (%d)", g_ui.tools.loc_count);
    rect = (struct Editor_TileRect){ 1, x + 13, z + 2, x + 14, z + 2, 0 };
    CHECK(edp_select_rect(&g_ui, &rect, EDITOR_LOC_SELECT_ADD) == 2,
        "marquee: beside it, past its footprint, adds nothing (%d)", g_ui.tools.loc_count);
    rect = (struct Editor_TileRect){ 1, x, z, x + 5, z + 5, 0 };
    CHECK(edp_select_rect(&g_ui, &rect, EDITOR_LOC_SELECT_TOGGLE) == 2 && g_ui.tools.locs[0].rotation == 1
              && g_ui.tools.locs[1].loc_id == LOC_WALL && g_ui.tools.locs[1].level == 0,
        "marquee: Ctrl toggles the first crate off and the level-0 wall on, never the one above (%d)",
        g_ui.tools.loc_count);
    rect = (struct Editor_TileRect){ 1, x + 10, z, x + 10, z, 0 };
    CHECK(edp_select_rect(&g_ui, &rect, EDITOR_LOC_SELECT_SUBTRACT) == 1 && g_ui.tools.locs[0].loc_id == LOC_WALL,
        "marquee: Alt subtracts the turned crate (%d)", g_ui.tools.loc_count);
    rect = (struct Editor_TileRect){ 1, x + 5, z + 5, x + 5, z + 5, 1 };
    CHECK(edp_select_rect(&g_ui, &rect, EDITOR_LOC_SELECT_REPLACE) == 1 && g_ui.tools.locs[0].level == 1,
        "marquee: on level 1 it is the wall above");
    rect = (struct Editor_TileRect){ 1, x, z - 40, x + 70, z + 30, 0 };
    CHECK(edp_select_rect(&g_ui, &rect, EDITOR_LOC_SELECT_REPLACE) == 1
              && strstr(Editor_UiStatusText(&g_ui), "Select refused: a marquee covers 4096 tiles (64x64) at most"),
        "marquee: a rectangle past 4096 tiles is refused, said, and the selection kept (\"%s\")",
        Editor_UiStatusText(&g_ui));

    /* Select same: every LOC_WALL on level 0 of the open squares. */
    {
        int const walls = locs_of(LOC_WALL, 0);
        CHECK(walls >= 7, "select same: the run's six and the marquee's wall are on level 0 (%d)", walls);
        CHECK(edp_select_same(&g_ui, LOC_WALL, 0, EDITOR_LOC_SELECT_REPLACE) == walls,
            "select same: selects all %d (%d)", walls, g_ui.tools.loc_count);
        for( int i = 0; i < g_ui.tools.loc_count; i++ )
            CHECK(g_ui.tools.locs[i].loc_id == LOC_WALL && g_ui.tools.locs[i].level == 0,
                "select same: %d is a wall of that id on level 0", i);
        CHECK(edp_select_same(&g_ui, LOC_WALL, 1, EDITOR_LOC_SELECT_ADD) == walls + 1,
            "select same: Shift adds level 1's one");
        /* The chord's world-free half: the primary selected loc's id. */
        g_ui.tools.loc_count = 1;
        CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_EDIT_SELECT_SAME) && g_ui.tools.loc_count == walls,
            "select same: Alt+A with no world takes the primary's id (%d)", g_ui.tools.loc_count);
        g_ui.tools.loc_count = 0;
        CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_EDIT_SELECT_SAME)
                  && strstr(Editor_UiStatusText(&g_ui), "Select same refused"),
            "select same: with nothing to name it says so");
    }
}

/* ---- Fit, shared --------------------------------------------------------------------- */

static void
test_fit_shared(void)
{
    ToriRSChrome_Init(&g_peer_chrome);
    ToriRSChrome_SetSurface(&g_peer_chrome, 800, 600);
    Editor_UiInit(&g_peer_ui, &g_peer_chrome, &g_peer);
    Editor_UiSetVisible(&g_peer_ui, 1);
    Editor_SetStateCallback(&g_peer, on_state, &g_peer_ui);
    Editor_SetStateCallback(&g_editor, on_state, &g_ui);
    frame_of(&g_chrome, &g_ui);
    frame_of(&g_peer_chrome, &g_peer_ui);
    Editor_PumpFacts(&g_editor);
    Editor_PumpFacts(&g_peer);
    CHECK(edp_place_fit_enabled(&g_ui) && edp_place_fit_enabled(&g_peer_ui), "fit: on in both windows at boot");

    /* G in the main window: the other window's Locs panel and Catalog pane
     * read the same bit (edp_place_fit_enabled of its own tools). */
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_PLACE_FIT) && !edp_place_fit_enabled(&g_ui), "fit: G turns it off here");
    frame_of(&g_chrome, &g_ui);
    Editor_PumpFacts(&g_editor);
    Editor_PumpFacts(&g_peer);
    frame_of(&g_peer_chrome, &g_peer_ui);
    CHECK(!edp_place_fit_enabled(&g_peer_ui) && g_peer_ui.tools.place_fit == 0,
        "fit: the Client's other window hears it off (STATE_TOOLS)");
    CHECK(edp_place_fit_enabled(&g_ui) == 0, "fit: and its own echo leaves this window off");

    /* Back the other way: the other window turns it on. */
    edp_place_fit_set(&g_peer_ui, 1);
    frame_of(&g_peer_chrome, &g_peer_ui);
    Editor_PumpFacts(&g_peer);
    Editor_PumpFacts(&g_editor);
    frame_of(&g_chrome, &g_ui);
    CHECK(edp_place_fit_enabled(&g_ui) == 1, "fit: the other window's toggle reaches this one");

    /* With Fit off the ghost's pose is the pose; on, a wall beside fits. */
    {
        int shape;
        int rotation;
        arm_place(LOC_WALL, 0, 3);
        edp_place_fit_set(&g_ui, 0);
        edp_place_pose_at(&g_ui, BASE_X + 10, BASE_Z + 9, 0, &shape, &rotation);
        CHECK(shape == 0 && rotation == 3, "fit: off, the pose (%d rot %d)", shape, rotation);
        edp_place_fit_set(&g_ui, 1);
        edp_place_pose_at(&g_ui, BASE_X + 9, BASE_Z + 10, 0, &shape, &rotation);
        CHECK(shape == 0 && rotation == 1,
            "fit: on, west of the run's first wall it continues the north edge (%d rot %d)",
            shape, rotation);
    }
    Editor_SetStateCallback(&g_peer, NULL, NULL);
    Editor_SetStateCallback(&g_editor, NULL, NULL);
}

/* ---- refusals say why ------------------------------------------------------------------ */

static void
test_refusals(void)
{
    uint32_t serial = g_ui.status_serial;

    CHECK(edp_locs_place(&g_ui, LOC_WALL, 0, 0, BASE_X + 70, BASE_Z + 3, 0) == 0
              && strstr(Editor_UiStatusText(&g_ui), "Place refused: tile 3270,3203 L0 is in m51_50, which is not open"),
        "refusals: a place into a closed square (\"%s\")", Editor_UiStatusText(&g_ui));
    CHECK(edp_locs_place(&g_ui, LOC_WALL, 10, 0, BASE_X + 3, BASE_Z + 3, 0) == 0
              && strstr(Editor_UiStatusText(&g_ui), "Place refused: loc 1902 has no model for shape 10"),
        "refusals: a shape the loc lacks (\"%s\")", Editor_UiStatusText(&g_ui));
    g_ui.tools.loc_count = 0;
    CHECK(edp_locs_nudge(&g_ui, 1, 0) == 0 && strstr(Editor_UiStatusText(&g_ui), "Nudge refused: select a loc first"),
        "refusals: a nudge of nothing (\"%s\")", Editor_UiStatusText(&g_ui));
    CHECK(edp_locs_move_to(&g_ui, BASE_X + 1, BASE_Z + 1) == 0
              && strstr(Editor_UiStatusText(&g_ui), "Move refused: select a loc first"),
        "refusals: a move of nothing (\"%s\")", Editor_UiStatusText(&g_ui));
    g_ui.tools.pick_kind = -1;
    g_ui.tools.pick_id = -1;
    CHECK(edp_locs_swap(&g_ui) == 0 && strstr(Editor_UiStatusText(&g_ui), "Swap refused: pick a replacement loc"),
        "refusals: a swap with no pick (\"%s\")", Editor_UiStatusText(&g_ui));
    /* Reshape onto a held key: the wall's next shape is taken on its tile by
     * the other wall loc. */
    {
        struct Editor_LocRef ref;
        seed(LOC_WALL, 0, 0, BASE_X + 40, BASE_Z + 20, 0);
        seed(LOC_OTHER_WALL, 1, 0, BASE_X + 40, BASE_Z + 20, 0);
        Editor_LocRefAt(&ref, BASE_X + 40, BASE_Z + 20, 0, 0, LOC_WALL, 0);
        g_ui.tools.locs[0] = ref;
        g_ui.tools.loc_count = 1;
        CHECK(edp_locs_reshape(&g_ui) == 0
                  && strstr(Editor_UiStatusText(&g_ui), "Reshape refused: tile 3240,3220 L0 holds a wall corner post"),
            "refusals: a reshape onto a held shape (\"%s\")", Editor_UiStatusText(&g_ui));
    }
    CHECK(g_ui.status_serial == serial + 6, "refusals: each said once in the status bar (%u)",
        g_ui.status_serial - serial);
}

/* ---- Delete says it (design section 7, review fix) ------------------------------------ */

static void
test_delete_said(void)
{
    struct Editor_TileRect rect;
    int const x = BASE_X + 30;
    int const z = BASE_Z + 60;
    char want[128];

    snprintf(g_crate.name, sizeof(g_crate.name), "Crate");
    seed(LOC_CRATE, 10, 0, x, z, 0);
    seed(LOC_WALL, 0, 0, x + 6, z, 0);
    seed(LOC_WALL, 0, 0, x + 7, z, 0);
    g_ui.tools.tool = EDITOR_TOOL_SELECT;
    g_ui.tools.loc_count = 0;
    rect = (struct Editor_TileRect){ 1, x, z, x, z, 0 };
    CHECK(edp_select_rect(&g_ui, &rect, EDITOR_LOC_SELECT_REPLACE) == 1, "delete: the crate is selected");
    CHECK(edp_locs_delete(&g_ui) == 1 && !loc_at(LOC_CRATE, x, z, 0, 10), "delete: the crate is gone");
    snprintf(want, sizeof(want), "Deleted Crate at %d,%d L0; Ctrl+Z undoes it", x, z);
    CHECK(strcmp(Editor_UiStatusText(&g_ui), want) == 0, "delete: the status bar names it and the undo (\"%s\")",
        Editor_UiStatusText(&g_ui));
    rect = (struct Editor_TileRect){ 1, x + 6, z, x + 7, z, 0 };
    CHECK(edp_select_rect(&g_ui, &rect, EDITOR_LOC_SELECT_REPLACE) == 2, "delete: two walls selected");
    CHECK(edp_locs_delete(&g_ui) == 2
              && strcmp(Editor_UiStatusText(&g_ui), "Deleted 2 locs; Ctrl+Z undoes it") == 0,
        "delete: a set is counted (\"%s\")", Editor_UiStatusText(&g_ui));
}

int
main(
    int argc,
    char** argv)
{
    struct EditorHost host;
    const char* root;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <tmp-content-dir>\n", argv[0]);
        return 2;
    }
    root = argv[1];
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    records_init();
    test_path();
    test_rules();
    if( !RSCache_ProfileByName("osrs242", &g_profile) )
    {
        printf("FAIL: no osrs242 profile\n");
        return 1;
    }
    write_fixture(root);
    if( !Editor_HostOpenMapEdEmbed(&host, root, NULL) )
    {
        printf("FAIL: the embedded ToriRSMapEd did not open %s\n", root);
        return 1;
    }
    Editor_OpenHost(&g_editor, &host, root, &g_profile);
    CHECK(g_editor.writable, "the tmp tree is writable");
    CHECK(Editor_OpenDoc(&g_editor, EDITOR_DOC_SQUARE, "m50_50")
              && Editor_OpenDoc(&g_editor, EDITOR_DOC_SQUARE, "m50_51")
              && square(&g_editor, 50, 50) && square(&g_editor, 50, 51) && !square(&g_editor, 51, 50),
        "m50_50 and m50_51 open, m51_50 not");
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1280, 800);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    Editor_UiSetVisible(&g_ui, 1);
    g_ui.app_services.user_data = &g_app_stand_in;
    g_ui.app_services.loc_record = test_loc_record;
    test_wall_run();
    test_decor_pose();
    test_fit_run();
    test_diagonal_run();
    test_noise();
    test_relief();
    test_scatter();
    test_plateau();
    test_mix();
    test_refused_runs();
    test_marquee();
    test_fit_shared();
    test_refusals();
    test_delete_said();
    Editor_UiFree(&g_peer_ui);
    Editor_UiFree(&g_ui);
    Editor_Close(&g_peer);
    Editor_Close(&g_editor);
    printf("editor_placement_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}

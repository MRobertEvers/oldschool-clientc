/*
 * tools-panels (docs/MAPED_IMPLEMENTATION.md phase 3): the panel modules,
 * headless, against the real chrome model and an embedded ToriRSMapEd.
 *
 *   ./editor_panels_test <tmp-content-dir>
 *
 * The content dir is a small tree the make target copies (two maps/ squares
 * and a neighbour, a lane holding one of them, an empty lane, the world-map
 * areas, the floor configs, a generated spawn file). Everything this test
 * writes lands there.
 */

#include "editor/editor.h"
#include "editor/editor_areas.h"
#include "editor/editor_cfg.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_floorcolor.h"
#include "editor/editor_script.h"
#include "editor/builder/builder_ssc.h"
#include "editor/editor_host.h"
#include "editor/editor_panel.h"
#include "editor/editor_ui.h"
#include "editor/editor_view.h"
#include "editor/panels/edpanel_catalog_model.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_newform.h"
#include "editor/panels/edpanel_ops.h"
#include "editor/panels/edpanel_picker.h"
#include "editor/panels/edpanel_picker_find.h"
#include "editor/panels/edpanel_picker_nav.h"
#include "editor/panels/edpanel_shapes.h"
#include "editor/panels/edpanels.h"
#include "editor/editor_spawn.h"
#include "editor/map_editor_ghost.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/torirs_chrome_metrics.h"
#include "ui/uitree_debug_overlay.h"

#include "osrs/palette.h"
#include <rscache.h>

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

/* m50_50's tiles in world coordinates. */
#define SQ_X0 (50 * 64)
#define SQ_Z0 (50 * 64)

static struct Editor g_editor;
static struct RSCache g_profile;
static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static struct Editor_Cmd g_cmds[EDITOR_RECT_TILES_MAX];
static struct Editor_TileClip g_clip;

/* ---- helpers --------------------------------------------------------------------- */

/**
 * The checks here drive the panels as windows of their own -- dragged by
 * their title bars, two of one default group shown at once, found by their
 * titles -- as they were written before panel groups (workbench 6.0). So a
 * registry from Editor_UiRegisterToolsPanels takes every module out of its
 * default group first; editor_groups_test.c (test-editor-groups) tests the
 * groups themselves.
 */
static void
ungroup_all(struct EditorUi* ui)
{
    for( int i = 0; i < ui->panel_count; i++ )
        if( Editor_UiGroupOf(ui, ui->panels[i].name) >= 0 )
            Editor_UiLeaveGroup(ui, ui->panels[i].name);
}

static int
apply_all(
    const struct Editor_Cmd* commands,
    int count)
{
    int applied = 0;
    Editor_StrokeBegin(&g_editor);
    for( int i = 0; i < count; i++ )
        applied += Editor_Apply(&g_editor, &commands[i]) ? 1 : 0;
    Editor_StrokeEnd(&g_editor);
    return applied;
}

static struct Editor_Square*
square_of(
    int map_x,
    int map_z)
{
    return Editor_DocFindSquare(&g_editor.doc, map_x, map_z);
}

static struct Editor_Tile*
tile_at(
    int world_x,
    int world_z,
    int level)
{
    struct Editor_Square* square = square_of(world_x / 64, world_z / 64);
    if( !square )
        return NULL;
    return &square->tiles[Editor_TileIndex(world_x % 64, world_z % 64, level)];
}

static int
derived(
    int world_x,
    int world_z)
{
    int height = 0;
    (void)Editor_OpsDerivedHeight(&g_editor.doc, world_x, world_z, 0, &height);
    return height;
}

static int
find_panel(const char* title)
{
    for( int i = 0; i < g_chrome.panel_count; i++ )
        if( strcmp(g_chrome.panels[i].title, title) == 0 )
            return i;
    return -1;
}

/** A widget of `kind` on `panel` whose label or text is `caption` (NULL:
 *  the first of that kind). */
static int
find_widget(
    int panel,
    int kind,
    const char* caption)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel != panel || w->kind != kind )
            continue;
        if( !caption || strcmp(w->label, caption) == 0 || strcmp(w->text, caption) == 0 )
            return i;
    }
    return -1;
}

static void
frame(void)
{
    ToriRSChrome_Build(&g_chrome);
    Editor_UiTick(&g_ui);
    ToriRSChrome_Build(&g_chrome);
}

static void
activate(int widget)
{
    assert(widget >= 0);
    g_chrome.activated = widget;
    frame();
}

static void
push_graph(
    int graph,
    int kind,
    int x,
    int y,
    int value)
{
    struct ToriRSChromeEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.widget = graph;
    event.serial = g_chrome.widgets[graph].serial;
    event.x = x;
    event.y = y;
    event.value = value;
    event.key = ToriRSChrome_GraphKeyAt(&g_chrome, graph, x, y);
    (void)ToriRSChrome_PushEvent(&g_chrome, &event);
}

/* The fake window: what the hooks were asked. */
static struct
{
    int open_calls;
    int open_x;
    int open_z;
    int open_add;
    char open_storage[EDITOR_DOC_STORAGE_MAX];
    int new_view_calls;
    int selection_calls;
} g_window;

static void
fake_open_square(
    void* user,
    int map_x,
    int map_z,
    const char* storage,
    int add)
{
    (void)user;
    g_window.open_calls++;
    g_window.open_x = map_x;
    g_window.open_z = map_z;
    g_window.open_add = add;
    snprintf(g_window.open_storage, sizeof(g_window.open_storage), "%s", storage ? storage : "");
}

static void
fake_open_in_new_view(
    void* user,
    int map_x,
    int map_z)
{
    (void)user;
    (void)map_x;
    (void)map_z;
    g_window.new_view_calls++;
}

static int
fake_floor_rgb(
    void* user,
    int is_overlay,
    int id,
    uint32_t* out)
{
    (void)user;
    /* Overlays past 700 are "not loaded": Every id asks for them. */
    if( is_overlay && id > 700 )
        return 0;
    *out = is_overlay ? 0x8040c0u + (uint32_t)id : 0x40a040u + (uint32_t)id;
    return 1;
}

static int g_floor_requests;
static int g_floor_request_first;

static void
fake_floor_request(
    void* user,
    int is_overlay,
    int id)
{
    (void)user;
    if( g_floor_requests == 0 )
        g_floor_request_first = is_overlay ? id : -id;
    g_floor_requests++;
}

static int
fake_view_window(
    void* user,
    int* out_map_x,
    int* out_map_z,
    int* out_w,
    int* out_h)
{
    (void)user;
    *out_map_x = 50;
    *out_map_z = 50;
    *out_w = 2;
    *out_h = 2;
    return 1;
}

static void
fake_selection_changed(void* user)
{
    (void)user;
    g_window.selection_calls++;
}

/* ---- shapes --------------------------------------------------------------------- */

static void
test_shapes(void)
{
    struct Editor_ShapeThumb plain;
    struct Editor_ShapeThumb diag0;
    struct Editor_ShapeThumb diag1;

    Editor_ShapeThumbBuild(0, 0, &plain);
    CHECK(plain.overlay_cells == EDITOR_SHAPE_GRID * EDITOR_SHAPE_GRID,
        "shape 0 (plain) is overlay over the whole tile (%d cells)", plain.overlay_cells);
    Editor_ShapeThumbBuild(1, 0, &diag0);
    Editor_ShapeThumbBuild(1, 1, &diag1);
    CHECK(diag0.overlay_cells > 16 && diag0.overlay_cells < 48,
        "shape 1 (diagonal) covers about half the tile (%d)", diag0.overlay_cells);
    CHECK(memcmp(diag0.cells, diag1.cells, sizeof(diag0.cells)) != 0,
        "rotating shape 1 changes its picture");
    for( int s = 0; s < EDITOR_SHAPE_COUNT; s++ )
    {
        struct Editor_ShapeThumb t;
        int underlay = 0;
        Editor_ShapeThumbBuild(s, 0, &t);
        for( int i = 0; i < EDITOR_SHAPE_GRID * EDITOR_SHAPE_GRID; i++ )
            underlay += t.cells[i] == EDITOR_SHAPE_CELL_UNDERLAY;
        CHECK(t.overlay_cells > 0, "shape %d draws some overlay", s);
        CHECK(s == 0 || underlay > 0, "shape %d leaves some underlay showing", s);
    }
}

/* ---- brushes -------------------------------------------------------------------- */

static void
test_brushes(void)
{
    struct Editor_BrushOp op;
    struct Editor_Tile before[25];
    int const cx = SQ_X0 + 10;
    int const cz = SQ_Z0 + 10;
    int count;
    int expected = 0;

    memset(&op, 0, sizeof(op));
    op.tool = EDITOR_TOOL_RAISE;
    op.world_x = cx;
    op.world_z = cz;
    op.level = 0;
    op.brush.radius = 2;
    op.brush.falloff = EDITOR_BRUSH_FALLOFF_FLAT;
    op.brush.step = 3;

    for( int dz = -2; dz <= 2; dz++ )
        for( int dx = -2; dx <= 2; dx++ )
        {
            before[(dz + 2) * 5 + dx + 2] = *tile_at(cx + dx, cz + dz, 0);
            expected += Editor_BrushWeight(&op.brush, dx, dz) > 0.0;
        }
    CHECK(expected == 21, "a radius-2 disc is 21 tiles (%d)", expected);

    {
        int heights_before[25];
        int raised = 1;
        for( int i = 0; i < 25; i++ )
            heights_before[i] = derived(cx + i % 5 - 2, cz + i / 5 - 2);
        count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
        CHECK(count == 21, "raise makes one command per tile of the disc (%d)", count);
        CHECK(apply_all(g_cmds, count) == count, "the authority applies the raise");
        for( int dz = -2; dz <= 2; dz++ )
            for( int dx = -2; dx <= 2; dx++ )
            {
                int const i = (dz + 2) * 5 + dx + 2;
                if( Editor_BrushWeight(&op.brush, dx, dz) <= 0.0 )
                    continue;
                if( !tile_at(cx + dx, cz + dz, 0)->has_height
                    || derived(cx + dx, cz + dz) >= heights_before[i] )
                    raised = 0;
            }
        CHECK(raised, "every tile of the disc is authored and higher (negative is up)");
        CHECK(Editor_Undo(&g_editor) == count, "one undo reverts the whole application");
        {
            int restored = 1;
            for( int i = 0; i < 25; i++ )
                if( memcmp(tile_at(cx + i % 5 - 2, cz + i / 5 - 2, 0), &before[i], sizeof(before[i])) != 0 )
                    restored = 0;
            CHECK(restored, "undo restores the authored tiles exactly, absence included");
        }
    }

    /* Smooth falloff: the rim moves less than the centre. */
    op.brush.falloff = EDITOR_BRUSH_FALLOFF_LINEAR;
    op.brush.step = 8;
    CHECK(Editor_BrushWeight(&op.brush, 0, 0) > Editor_BrushWeight(&op.brush, 2, 0),
        "linear falloff weighs the centre over the rim");
    op.brush.falloff = EDITOR_BRUSH_FALLOFF_SMOOTH;
    CHECK(Editor_BrushWeight(&op.brush, 0, 0) == 1.0 && Editor_BrushWeight(&op.brush, 3, 0) == 0.0,
        "smooth falloff: 1 at the centre, 0 outside the disc");

    /* Flatten: every tile goes to the target. */
    {
        int const target = derived(cx, cz) - 64;
        int flat = 1;
        memset(&op, 0, sizeof(op));
        op.tool = EDITOR_TOOL_FLATTEN;
        op.world_x = cx;
        op.world_z = cz;
        op.brush.radius = 1;
        op.flatten_height = target;
        count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
        CHECK(count == 9, "flatten radius 1 touches the 3x3 (%d)", count);
        apply_all(g_cmds, count);
        for( int dz = -1; dz <= 1; dz++ )
            for( int dx = -1; dx <= 1; dx++ )
                if( derived(cx + dx, cz + dz) != derived(cx, cz) )
                    flat = 0;
        CHECK(flat, "after flatten the 3x3 is one height (%d)", derived(cx, cz));
        CHECK(abs(derived(cx, cz) - target) <= 8, "...the target's nearest step (%d vs %d)",
            derived(cx, cz), target);
    }

    /* Smooth pulls a spike toward its neighbours. */
    {
        struct Editor_Cmd spike;
        int const sx = SQ_X0 + 30;
        int const sz = SQ_Z0 + 30;
        int before_h;
        int after_h;
        Editor_CmdInitSquare(&spike, EDITOR_CMD_TILE, 50, 50);
        spike.u.tile.x = 30;
        spike.u.tile.z = 30;
        spike.u.tile.before = *tile_at(sx, sz, 0);
        spike.u.tile.after = spike.u.tile.before;
        spike.u.tile.after.has_height = 1;
        spike.u.tile.after.height = 200;
        apply_all(&spike, 1);
        before_h = derived(sx, sz);
        memset(&op, 0, sizeof(op));
        op.tool = EDITOR_TOOL_SMOOTH;
        op.world_x = sx;
        op.world_z = sz;
        op.brush.radius = 0;
        op.brush.strength = 100;
        count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
        apply_all(g_cmds, count);
        after_h = derived(sx, sz);
        CHECK(count == 1 && after_h > before_h + 400, "smooth pulls a spike down toward its neighbours (%d -> %d)",
            before_h, after_h);
    }

    /* Paint: underlay, then overlay with a shape and rotation. */
    {
        int painted = 1;
        memset(&op, 0, sizeof(op));
        op.tool = EDITOR_TOOL_PAINT_UNDERLAY;
        op.world_x = cx;
        op.world_z = cz;
        op.brush.radius = 1;
        op.underlay_id = 77;
        apply_all(g_cmds, Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX));
        op.tool = EDITOR_TOOL_PAINT_OVERLAY;
        op.overlay_id = 12;
        op.overlay_shape = 3;
        op.overlay_rotation = 2;
        apply_all(g_cmds, Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX));
        for( int dz = -1; dz <= 1; dz++ )
            for( int dx = -1; dx <= 1; dx++ )
            {
                const struct Editor_Tile* t = tile_at(cx + dx, cz + dz, 0);
                if( t->underlay_id != 77 || !t->has_overlay || t->overlay_id != 12 || t->shape != 3
                    || t->rotation != 2 )
                    painted = 0;
            }
        CHECK(painted, "underlay 77 and overlay 12 shape 3 rotation 2 painted on the 3x3");
        op.tool = EDITOR_TOOL_PAINT_FLAGS;
        op.flags_value = 0x1 | 0x4;
        apply_all(g_cmds, Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX));
        CHECK(tile_at(cx, cz, 0)->settings == 5, "the flags brush writes the f token (%d)",
            tile_at(cx, cz, 0)->settings);
        count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
        CHECK(count == 0, "painting what is already there makes no command (%d)", count);
    }

    /* A brush across the square border edits both squares' documents. */
    {
        int in_a = 0;
        int in_b = 0;
        memset(&op, 0, sizeof(op));
        op.tool = EDITOR_TOOL_RAISE;
        op.world_x = SQ_X0 + 10;
        op.world_z = SQ_Z0 + 63;
        op.brush.radius = 2;
        op.brush.step = 1;
        count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
        for( int i = 0; i < count; i++ )
        {
            in_a += strcmp(g_cmds[i].doc_key, "m50_50") == 0;
            in_b += strcmp(g_cmds[i].doc_key, "m50_51") == 0;
        }
        CHECK(count == 21 && in_a == 13 && in_b == 8,
            "a seam stroke is 21 commands, 13 in m50_50 and 8 in m50_51 (%d/%d/%d)", count, in_a, in_b);
        CHECK(apply_all(g_cmds, count) == count, "both squares accept their half");
        op.world_x = SQ_X0 + 63;
        op.world_z = SQ_Z0 + 10;
        count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
        in_a = 0;
        for( int i = 0; i < count; i++ )
            in_a += strcmp(g_cmds[i].doc_key, "m50_50") == 0;
        CHECK(in_a == count, "tiles of a square that is not open are skipped (%d of %d)", in_a, count);
    }
}

/* ---- the rectangle -------------------------------------------------------------- */

static void
test_rect(void)
{
    struct Editor_TileRect rect = { 1, SQ_X0 + 40, SQ_Z0 + 40, SQ_X0 + 43, SQ_Z0 + 42, 0 };
    struct EditorUiTools tools;
    int count;
    int same = 1;

    /* Give the source something to copy. */
    memset(&tools, 0, sizeof(tools));
    tools.underlay_id = 33;
    tools.overlay_id = 5;
    tools.overlay_shape = 1;
    count = Editor_TileRectFill(&g_editor.doc, &rect, &tools, g_cmds, EDITOR_RECT_TILES_MAX);
    CHECK(count == 12, "fill a 4x3 rectangle: 12 commands (%d)", count);
    apply_all(g_cmds, count);
    CHECK(tile_at(SQ_X0 + 41, SQ_Z0 + 41, 0)->underlay_id == 33, "the fill painted the underlay");

    CHECK(Editor_TileClipCopy(&g_editor.doc, &rect, &g_clip) == 12, "copy takes 12 tiles");
    count = Editor_TileClipPaste(&g_editor.doc, &g_clip, SQ_X0 + 50, SQ_Z0 + 50, 0, g_cmds,
        EDITOR_RECT_TILES_MAX);
    CHECK(count > 0, "paste makes commands (%d)", count);
    apply_all(g_cmds, count);
    for( int dz = 0; dz < 3; dz++ )
        for( int dx = 0; dx < 4; dx++ )
        {
            const struct Editor_Tile* a = tile_at(SQ_X0 + 40 + dx, SQ_Z0 + 40 + dz, 0);
            const struct Editor_Tile* b = tile_at(SQ_X0 + 50 + dx, SQ_Z0 + 50 + dz, 0);
            if( a->underlay_id != b->underlay_id || a->has_overlay != b->has_overlay
                || a->overlay_id != b->overlay_id || a->shape != b->shape )
                same = 0;
        }
    CHECK(same, "the pasted rectangle carries the copied floors");

    count = Editor_TileRectClear(&g_editor.doc, &rect, g_cmds, EDITOR_RECT_TILES_MAX);
    apply_all(g_cmds, count);
    {
        const struct Editor_Tile empty = { 0 };
        CHECK(memcmp(tile_at(SQ_X0 + 42, SQ_Z0 + 41, 0), &empty, sizeof(empty)) == 0,
            "clear leaves a tile with no tokens");
    }
    {
        struct Editor_TileRect big = { 1, SQ_X0, SQ_Z0, SQ_X0 + 64, SQ_Z0 + 64, 0 };
        struct Editor_TileRect out;
        CHECK(!Editor_TileRectNormalise(&big, &out), "a rectangle past 64x64 tiles is refused");
    }
}

/* ---- locs ----------------------------------------------------------------------- */

static int
loc_count_at(
    int world_x,
    int world_z,
    int shape)
{
    struct Editor_Square* square = square_of(world_x / 64, world_z / 64);
    int count = 0;
    if( !square )
        return 0;
    for( int i = 0; i < square->loc_count; i++ )
        if( square->locs[i].level == 0 && square->locs[i].x == world_x % 64
            && square->locs[i].z == world_z % 64 && square->locs[i].shape == shape )
            count++;
    return count;
}

/** A run of `n` tiles on level 0 of m50_50, starting at local x, that no loc
 *  of the square stands on (the copied .jl2 is the real one). */
static int
free_run(
    int z,
    int n,
    int from_x,
    int to_x)
{
    struct Editor_Square* square = square_of(50, 50);
    for( int x = from_x; x + n - 1 <= to_x; x++ )
    {
        int clear = 1;
        for( int i = 0; i < square->loc_count && clear; i++ )
            if( square->locs[i].level == 0 && square->locs[i].z == z && square->locs[i].x >= x
                && square->locs[i].x < x + n )
                clear = 0;
        if( clear )
            return x;
    }
    return -1;
}

static void
test_locs(void)
{
    struct Editor_LocOpResult result;
    struct Editor_LocRef refs[2];
    int lz_local = -1;
    int lx_local = -1;
    int row_local = -1;
    int lx;
    int lz;
    int count;

    for( int z = 2; z < 60 && (lx_local < 0 || row_local < 0); z++ )
    {
        int const a = free_run(z, 4, 2, 40);
        int const b = free_run(z, 5, 2, 40);
        if( a >= 0 && b >= 0 && b + 5 <= 40 )
        {
            lz_local = z;
            lx_local = a;
            row_local = b;
        }
    }
    CHECK(lz_local >= 0, "m50_50 has free tiles to place on");
    if( lz_local < 0 )
        return;
    lx = SQ_X0 + lx_local;
    lz = SQ_Z0 + lz_local;

    count = Editor_LocOpsPlace(&g_editor.doc, 1531, 10, 1, lx, lz, 0, g_cmds, 4, &result);
    CHECK(count == 1 && apply_all(g_cmds, count) == 1, "place one loc");
    CHECK(loc_count_at(lx, lz, 10) == 1, "it is in the document");
    refs[0] = result.refs[0];
    CHECK(Editor_LocRefResolve(&g_editor.doc, &refs[0]) >= 0, "its ref resolves");

    count = Editor_LocOpsNudge(&g_editor.doc, refs, 1, 1, 0, g_cmds, 4, &result);
    apply_all(g_cmds, count);
    CHECK(loc_count_at(lx, lz, 10) == 0 && loc_count_at(lx + 1, lz, 10) == 1, "nudge east moved it");
    refs[0] = result.refs[0];
    CHECK(refs[0].x == lx_local + 1 && Editor_LocRefResolve(&g_editor.doc, &refs[0]) >= 0,
        "the selection rides the move");

    count = Editor_LocOpsRotate(&g_editor.doc, refs, 1, 1, g_cmds, 4, &result);
    apply_all(g_cmds, count);
    refs[0] = result.refs[0];
    CHECK(refs[0].rotation == 2, "rotate turned it a quarter (%d)", refs[0].rotation);

    count = Editor_LocOpsReshape(&g_editor.doc, refs, 1, g_cmds, 4, &result);
    apply_all(g_cmds, count);
    refs[0] = result.refs[0];
    CHECK(refs[0].shape == 11 && loc_count_at(lx + 1, lz, 11) == 1, "reshape cycles 10 -> 11");
    CHECK(Editor_LocShapeCycle(9) == 0 && Editor_LocShapeCycle(22) == 22 && Editor_LocShapeCycle(21) == 12,
        "shape families: walls wrap, ground decor stays, roofs wrap");

    count = Editor_LocOpsDuplicate(&g_editor.doc, refs, 1, 1, 0, g_cmds, 4, &result);
    apply_all(g_cmds, count);
    CHECK(loc_count_at(lx + 1, lz, 11) == 1 && loc_count_at(lx + 2, lz, 11) == 1,
        "duplicate leaves the original and adds a copy beside it");
    refs[1] = result.refs[0];

    count = Editor_LocOpsDelete(&g_editor.doc, refs, 2, g_cmds, 4, &result);
    CHECK(count == 2 && apply_all(g_cmds, count) == 2, "delete both");
    CHECK(loc_count_at(lx + 1, lz, 11) == 0 && loc_count_at(lx + 2, lz, 11) == 0, "both gone");

    /* Across the border: a delete in one square and an add in the other,
     * one undo step. */
    CHECK(Editor_OpenDoc(&g_editor, EDITOR_DOC_SQUARE, "m51_50"), "open m51_50");
    {
        int const base_a = loc_count_at(SQ_X0 + 63, lz, 10);
        int const base_b = loc_count_at(SQ_X0 + 64, lz, 10);
        count = Editor_LocOpsPlace(&g_editor.doc, 1531, 10, 0, SQ_X0 + 63, lz, 0, g_cmds, 4, &result);
        apply_all(g_cmds, count);
        refs[0] = result.refs[0];
        count = Editor_LocOpsNudge(&g_editor.doc, refs, 1, 1, 0, g_cmds, 4, &result);
        CHECK(count == 2, "a nudge across the border is two commands (%d)", count);
        CHECK(apply_all(g_cmds, count) == 2, "both squares accept");
        CHECK(loc_count_at(SQ_X0 + 63, lz, 10) == base_a && loc_count_at(SQ_X0 + 64, lz, 10) == base_b + 1,
            "the loc is in m51_50 now");
        CHECK(result.refs[0].map_x == 51 && result.refs[0].x == 0, "the ref names m51_50 tile 0");
        CHECK(Editor_Undo(&g_editor) == 2, "one undo takes both halves back");
        CHECK(loc_count_at(SQ_X0 + 63, lz, 10) == base_a + 1 && loc_count_at(SQ_X0 + 64, lz, 10) == base_b,
            "the loc is back in m50_50");
    }

    /* A row of walls nudged along itself: the far one moves first. */
    {
        struct Editor_LocRef row[3];
        int ok = 1;
        for( int i = 0; i < 3; i++ )
        {
            count = Editor_LocOpsPlace(&g_editor.doc, 1531, 0, 0, SQ_X0 + row_local + i, lz, 0, g_cmds, 4, &result);
            apply_all(g_cmds, count);
            row[i] = result.refs[0];
        }
        count = Editor_LocOpsNudge(&g_editor.doc, row, 3, 1, 0, g_cmds, 8, &result);
        apply_all(g_cmds, count);
        for( int i = 0; i < 3; i++ )
            if( loc_count_at(SQ_X0 + row_local + 1 + i, lz, 0) != 1 )
                ok = 0;
        CHECK(ok && loc_count_at(SQ_X0 + row_local, lz, 0) == 0, "three adjacent walls nudge as a block");
    }
}

/* ---- spawns -------------------------------------------------------------------- */

static void
test_spawns(void)
{
    struct Editor_DocHandle handle;
    struct Editor_SpawnFile generated;
    struct Editor_SpawnRow row;
    struct Editor_Cmd command;
    struct Editor_SpawnFile* edited;
    char key[EDITOR_DOC_KEY_MAX];
    int first_row = -1;

    CHECK(Editor_OpenDoc(&g_editor, EDITOR_DOC_SPAWN, "m50_50"), "the edited spawn document opens (empty)");
    Editor_SpawnGeneratedKey(key, 50, 50);
    CHECK(Editor_OpenDoc(&g_editor, EDITOR_DOC_TEXT, key), "the generated spawn file opens as text");
    CHECK(Editor_DocFind(&g_editor.doc, EDITOR_DOC_TEXT, key, &handle) && handle.text->length > 0,
        "...with its rows");
    Editor_SpawnInit(&generated);
    CHECK(Editor_SpawnParse(&generated, handle.text->text, handle.text->length).ok, "it parses");
    for( int i = 0; i < generated.line_count && first_row < 0; i++ )
        if( generated.lines[i].kind == EDITOR_SPAWN_LINE_ROW )
            first_row = i;
    CHECK(first_row >= 0, "the generated file has a row");
    if( first_row < 0 )
        return;
    row = generated.lines[first_row].row;

    Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, "m50_50", &handle);
    edited = &handle.spawn->file;
    CHECK(Editor_SpawnDeleteCommand(edited, &row, &command)
              && command.kind == EDITOR_CMD_SPAWN_SUPPRESS && command.has_after,
        "deleting a generated spawn is a suppression row");
    CHECK(apply_all(&command, 1) == 1, "the authority applies it");
    Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, "m50_50", &handle);
    edited = &handle.spawn->file;
    CHECK(Editor_SpawnIsSuppressed(edited, &row), "the edited file suppresses %s", row.name);
    CHECK(!Editor_SpawnDeleteCommand(edited, &row, &command), "suppressing twice makes no command");

    {
        struct Editor_SpawnRow restore = row;
        restore.suppress = 1;
        Editor_SpawnCommand(&command, &restore, 0);
        CHECK(apply_all(&command, 1) == 1, "restore drops the suppression");
        Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, "m50_50", &handle);
        CHECK(!Editor_SpawnIsSuppressed(&handle.spawn->file, &row), "the generated spawn is back");
    }

    memset(&row, 0, sizeof(row));
    snprintf(row.name, sizeof(row.name), "hans");
    row.abs_x = SQ_X0 + 7;
    row.abs_z = SQ_Z0 + 8;
    row.count = 1;
    Editor_SpawnCommand(&command, &row, 1);
    CHECK(command.doc_kind == EDITOR_DOC_SPAWN && strcmp(command.doc_key, "m50_50") == 0,
        "a placed spawn addresses the square's spawn document");
    CHECK(apply_all(&command, 1) == 1, "place a spawn");
    Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, "m50_50", &handle);
    CHECK(Editor_SpawnFind(&handle.spawn->file, &row) >= 0, "the edited file has the row");
    CHECK(Editor_SpawnDeleteCommand(&handle.spawn->file, &row, &command)
              && command.kind == EDITOR_CMD_SPAWN && command.has_before,
        "deleting an edited spawn removes its row");
    CHECK(apply_all(&command, 1) == 1, "...and the authority applies it");
    Editor_SpawnFree(&generated);
}

/* ---- the picker ------------------------------------------------------------------ */

/* ---- the catalog over the server's index; Copy to new; spawn Duplicate ------------ */

/* Written into the tmp tree before the embedded server opens it, so the
 * catalog's first build indexes them: two locs, an npc, an obj, and the loc
 * client membership file a copy appends to. */
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

/* The npc field register: the content tree's own fields/npc.ini, so the
 * record test walks every key the tree declares. */
static int g_register_real;

static void
write_field_register(const char* root)
{
    static const char* const candidates[] = {
        "../OSRS-Content/osrs-content/fields/npc.ini",
        "OSRS-Content/osrs-content/fields/npc.ini",
    };
    char path[2048];
    FILE* in = NULL;

    snprintf(path, sizeof(path), "%s/fields", root);
    mkdir(path, 0755);
    for( size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]) && !in; i++ )
        in = fopen(candidates[i], "rb");
    if( in )
    {
        FILE* out;
        char buffer[4096];
        size_t got;
        snprintf(path, sizeof(path), "%s/fields/npc.ini", root);
        out = fopen(path, "wb");
        if( !out )
        {
            printf("FAIL: cannot write %s\n", path);
            exit(1);
        }
        while( (got = fread(buffer, 1, sizeof(buffer), in)) > 0 )
            fwrite(buffer, 1, got, out);
        fclose(out);
        fclose(in);
        g_register_real = 1;
        return;
    }
    write_text(root, "fields/npc.ini",
        "[npc.wanderrange]\nscope = server\n\n[npc.maxrange]\nscope = server\n\n[npc.huntrange]\ntext = param\n\n"
        "[npc.patrol]\ntext = indexed\n\n[npc.desc]\nscope = client\n");
}

static void
write_catalog_fixture(const char* root)
{
    char path[2048];

    snprintf(path, sizeof(path), "%s/pack", root);
    mkdir(path, 0755);
    write_text(root, "configs/all.loc",
        "// test locs\n\n[test_crate]\nname=Crate\nmodels=15402\nop1=Search\n\n[test_door]\nname=Door\nop1=Open\n");
    write_text(root, "configs/all.loc.compack", "0=test_crate\n1=test_door\n");
    write_text(root, "configs/all.npc", "[test_guard]\nname=Guard\nop1=Talk-to\n");
    write_text(root, "configs/all.npc.compack", "0=test_guard\n");
    write_text(root, "configs/all.obj", "[test_knife]\nname=Knife\n");
    write_text(root, "configs/all.obj.compack", "0=test_knife\n");
    write_text(root, "pack/loc.client", "// loc client half\n");

    /* test_guard's server half: a generated layer and an authored overlay
     * over it (the merge order the Record panel resolves). */
    snprintf(path, sizeof(path), "%s/server/scripts/_explicit", root);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/server/scripts/_explicit/configs", root);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/server/scripts/areas/test", root);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/server/scripts/areas/test/configs", root);
    mkdir(path, 0755);
    write_text(root, "server/scripts/_explicit/configs/server_fields.generated.npc",
        "// generated\n\n[test_guard]\nwanderrange=9\nmaxrange=12\nrespawnrate=30 // "
        "wiki\nparam=death_drop,bones\n\n// trailing note\n");
    write_text(root, "server/scripts/areas/test/configs/test.npc",
        "// authored\n[test_guard]\nwanderrange=2\nparam=huntrange,4\n");
    /* Two triggers the catalog indexes (the Record panel's Scripts rows, the
     * Script panel). */
    snprintf(path, sizeof(path), "%s/server/scripts/areas/test/scripts", root);
    mkdir(path, 0755);
    write_text(root, "server/scripts/areas/test/scripts/test.rs2",
        "// test triggers\n[oploc1,test_crate]\nmes(\"A crate.\");\n\n[opnpc1,test_guard]\nmes(\"Halt.\");\n");
    write_field_register(root);
}

/** The model over the embedded server: a query per change, none per frame. */
static void
test_catalog_model(void)
{
    struct Editor_CatalogModel model;
    struct EditorHost* host = &g_editor.host;
    const struct EditorHost_CatalogRecord* record;
    char text[96];
    int queries;
    int pages;

    Editor_CatalogModelInit(&model, TORIRSMAPED_CATALOG_LOC, 5);
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "");
    CHECK(Editor_CatalogModelRefresh(&model, host) == 1 && model.queries == 1, "catalog: the first frame asks");
    Editor_CatalogModelCountText(&model, text, sizeof(text));
    CHECK(strcmp(text, "2 of 2 loc") == 0, "catalog: the count is the server's (%s)", text);
    for( int i = 0; i < 4; i++ )
    {
        Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "");
        (void)Editor_CatalogModelRefresh(&model, host);
    }
    CHECK(model.queries == 1, "catalog: four unchanged frames ask nothing (%d queries)", model.queries);

    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "crat");
    CHECK(Editor_CatalogModelRefresh(&model, host) == 0, "catalog: a changed find text settles a frame first");
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "crate");
    CHECK(Editor_CatalogModelRefresh(&model, host) == 0 && model.queries == 1,
        "catalog: typing on restarts the settle; nothing asked for the prefix");
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "crate");
    CHECK(Editor_CatalogModelRefresh(&model, host) == 1 && model.queries == 2, "catalog: the settled word is asked");
    Editor_CatalogModelCountText(&model, text, sizeof(text));
    CHECK(strcmp(text, "1 of 1 loc") == 0 && Editor_CatalogModelRowCount(&model) == 1
              && strcmp(Editor_CatalogModelRow(&model, 0)->name, "test_crate") == 0,
        "catalog: 'crate' finds test_crate (%s)", text);

    record = Editor_CatalogModelRecord(&model, host, 0);
    CHECK(record && model.record_fetches == 1 && strcmp(record->name, "test_crate") == 0 && record->field_count >= 3,
        "catalog: the picked row's record comes back with its fields");
    if( record )
    {
        int found = 0;
        for( int i = 0; i < record->field_count; i++ )
            found |= strcmp(record->fields[i].key, "op1") == 0 && strcmp(record->fields[i].value, "Search") == 0;
        CHECK(found, "catalog: the record carries op1=Search from the config block");
    }
    (void)Editor_CatalogModelRecord(&model, host, 0);
    CHECK(model.record_fetches == 1, "catalog: the same row's record is held, not asked again");

    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "zzz_matches_nothing");
    (void)Editor_CatalogModelRefresh(&model, host);
    CHECK(Editor_CatalogModelRefresh(&model, host) == 1, "catalog: a miss is asked once");
    queries = model.queries;
    Editor_CatalogModelCountText(&model, text, sizeof(text));
    CHECK(strcmp(text, "0 of 0 loc") == 0, "catalog: and reads 0 of 0 (%s)", text);
    for( int i = 0; i < 3; i++ )
    {
        Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "zzz_matches_nothing");
        (void)Editor_CatalogModelRefresh(&model, host);
    }
    CHECK(model.queries == queries, "catalog: the miss is remembered (%d queries)", model.queries);

    /* A kind change asks at once; paging through the underlays. */
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_UNDERLAY, "");
    CHECK(Editor_CatalogModelRefresh(&model, host) == 1, "catalog: a kind change asks without settling");
    pages = (int)Editor_CatalogModelPageCount(&model);
    CHECK(model.result.total > 5 && Editor_CatalogModelRowCount(&model) == 5 && pages > 1,
        "catalog: a page is the page size (5 of %u underlays, %d pages)", model.result.total, pages);
    Editor_CatalogModelSetPage(&model, 1);
    CHECK(Editor_CatalogModelRefresh(&model, host) == 1 && model.result.page == 1, "catalog: Next asks page 1");
    Editor_CatalogModelSetPage(&model, 100000);
    CHECK(model.page == (uint32_t)pages - 1, "catalog: a page past the end clamps to the last");
    (void)Editor_CatalogModelRefresh(&model, host);
    CHECK(Editor_CatalogModelRowCount(&model) >= 1 && Editor_CatalogModelRowCount(&model) <= 5,
        "catalog: the last page holds the remainder");
    queries = model.queries;
    Editor_CatalogModelInvalidate(&model);
    CHECK(Editor_CatalogModelRefresh(&model, host) == 1 && model.queries == queries + 1,
        "catalog: an invalidation asks again");

    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_ANY, "test_");
    (void)Editor_CatalogModelRefresh(&model, host);
    (void)Editor_CatalogModelRefresh(&model, host);
    CHECK(model.result.total >= 4, "catalog: all kinds finds the four test_ records (%u)", model.result.total);
    CHECK(Editor_CatalogModelKindCount() == TORIRSMAPED_CATALOG_KIND_COUNT + 1
              && Editor_CatalogModelKindAt(Editor_CatalogModelKindIndex(TORIRSMAPED_CATALOG_SQUARE))
                     == TORIRSMAPED_CATALOG_SQUARE,
        "catalog: the dropdown offers every kind and all of them");
    Editor_CatalogModelFree(&model);
}

static struct ToriRSChrome g_cat_chrome;
static struct EditorUi g_cat_ui;

static void
cat_frame(void)
{
    ToriRSChrome_Build(&g_cat_chrome);
    Editor_UiTick(&g_cat_ui);
    ToriRSChrome_Build(&g_cat_chrome);
}

static void
cat_activate(int widget)
{
    assert(widget >= 0);
    g_cat_chrome.activated = widget;
    cat_frame();
}

static int
cat_widget(
    int panel,
    int kind,
    const char* caption)
{
    for( int i = 0; i < g_cat_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_cat_chrome.widgets[i];
        if( w->panel != panel || w->kind != kind )
            continue;
        if( !caption || strcmp(w->label, caption) == 0 || strcmp(w->text, caption) == 0 )
            return i;
    }
    return -1;
}

static int
cat_panel(const char* title)
{
    for( int i = 0; i < g_cat_chrome.panel_count; i++ )
        if( strcmp(g_cat_chrome.panels[i].title, title) == 0 )
            return i;
    return -1;
}

static int
cat_console_has(const char* needle)
{
    for( int i = 0; i < 8; i++ )
    {
        const char* line = Editor_UiConsoleLine(&g_cat_ui, i);
        if( line && strstr(line, needle) )
            return 1;
    }
    return 0;
}

/** The registered Catalog panel, driven through its widgets: find, pick,
 *  Copy to new; the Spawns panel's Duplicate and the catalog pick. */
static void
test_catalog_panel(void)
{
    const struct Editor_CatalogModel* model;
    struct Editor_DocHandle handle;
    char text[96];
    int panel;
    int find;
    int list;
    int new_name;
    int copy;
    int kind;
    int queries;

    ToriRSChrome_Init(&g_cat_chrome);
    ToriRSChrome_SetSurface(&g_cat_chrome, 1280, 800);
    Editor_UiInit(&g_cat_ui, &g_cat_chrome, &g_editor);
    Editor_Catalog_Register(&g_cat_ui);
    Editor_Spawns_Register(&g_cat_ui);
    Editor_UiSetVisible(&g_cat_ui, 1);
    panel = cat_panel("Catalog");
    CHECK(panel >= 0, "catalog: the panel registers without an App");
    if( panel < 0 )
        return;
    ToriRSChrome_PanelSetVisible(&g_cat_chrome, panel, 1);
    find = cat_widget(panel, TORIRS_CHROME_W_TEXTINPUT, "Find");
    new_name = cat_widget(panel, TORIRS_CHROME_W_TEXTINPUT, "New name");
    copy = cat_widget(panel, TORIRS_CHROME_W_BUTTON, "Copy to new...");
    list = cat_widget(panel, TORIRS_CHROME_W_VLIST, NULL);
    kind = cat_widget(panel, TORIRS_CHROME_W_DROPDOWN, "Kind");
    CHECK(find >= 0 && new_name >= 0 && copy >= 0 && list >= 0 && kind >= 0, "catalog: its widgets are there");
    if( find < 0 || new_name < 0 || copy < 0 || list < 0 || kind < 0 )
        return;

    cat_frame();
    model = Editor_Catalog_Model(&g_cat_ui);
    CHECK(model && model->queries == 1, "catalog: opening asks once");
    Editor_CatalogModelRangeText(model, 0, text, sizeof(text));
    CHECK(strcmp(text, "1 to 2 of 2 loc") == 0 && cat_widget(panel, TORIRS_CHROME_W_LABEL, text) >= 0,
        "catalog: the count row reads %s from the server's total, not the provider's", text);
    for( int i = 0; i < 3; i++ )
        cat_frame();
    CHECK(model->queries == 1, "catalog: idle frames ask nothing");

    ToriRSChrome_SetText(&g_cat_chrome, find, "crate");
    cat_frame();
    queries = model->queries;
    cat_frame();
    CHECK(model->queries == queries + 1 && model->result.total == 1, "catalog: the typed find asks once it settles");
    cat_frame();
    CHECK(model->queries == queries + 1, "catalog: and not again");

    /* Pick it: a loc row is the pick, and arms Place. */
    ToriRSChrome_VListSetSelected(&g_cat_chrome, list, 0);
    cat_activate(list);
    CHECK(g_cat_ui.tools.pick_kind == 0 && g_cat_ui.tools.pick_id == 0
              && strcmp(g_cat_ui.tools.pick_name, "test_crate (0)") == 0,
        "catalog: picking test_crate makes it the pick (%d %d %s)", g_cat_ui.tools.pick_kind, g_cat_ui.tools.pick_id,
        g_cat_ui.tools.pick_name);
    CHECK(g_cat_ui.tools.tool == EDITOR_TOOL_LOC_PLACE, "catalog: a loc pick arms Place");
    CHECK(model->record_fetches == 1 && cat_widget(panel, TORIRS_CHROME_W_LABEL, "loc test_crate  id 0  Crate") >= 0,
        "catalog: the record view names it");
    cat_frame();
    CHECK(model->record_fetches == 1, "catalog: the record is not asked per frame");

    /* Copy to new... */
    ToriRSChrome_SetText(&g_cat_chrome, new_name, "Bad Name");
    cat_activate(copy);
    CHECK(g_cat_ui.tools.pick_id == 0 && cat_console_has("must be [a-z0-9_]"), "catalog: a bad new name is refused here");
    ToriRSChrome_SetText(&g_cat_chrome, new_name, "test_crate_copy");
    cat_activate(copy);
    CHECK(g_cat_ui.tools.pick_kind == 0 && g_cat_ui.tools.pick_id == 70000
              && strcmp(g_cat_ui.tools.pick_name, "test_crate_copy (70000)") == 0,
        "catalog: Copy to new makes the copy the pick, id 70000 (%d)", g_cat_ui.tools.pick_id);
    CHECK(Editor_DocFind(&g_editor.doc, EDITOR_DOC_CFG, "loc:test_crate_copy", &handle) && handle.text
              && handle.text->length == strlen("[test_crate_copy]\nname=Crate\nmodels=15402\nop1=Search\n")
              && memcmp(handle.text->text, "[test_crate_copy]\nname=Crate\nmodels=15402\nop1=Search\n",
                     handle.text->length)
                     == 0,
        "catalog: the copy is open in the mirror with every field of the source");
    CHECK(Editor_DocFind(&g_editor.doc, EDITOR_DOC_TEXT, "pack:loc.client", &handle) == 0
              || (handle.text && strstr(handle.text->text, "test_crate_copy\n")),
        "catalog: a mirror holding the membership file follows it");
    ToriRSChrome_VListSetSelected(&g_cat_chrome, list, 0);
    cat_frame();
    queries = model->queries;
    cat_frame();
    CHECK(queries >= 2, "catalog: the copy invalidated the page (asked again)");
    ToriRSChrome_VListSetSelected(&g_cat_chrome, list, 0);
    cat_activate(list);
    cat_activate(copy);
    CHECK(cat_console_has("refused") && g_cat_ui.tools.pick_id == 0, "catalog: copying onto a taken name is refused");
    CHECK(Editor_Undo(&g_editor) >= 3, "catalog: one undo takes the whole copy back");
    CHECK(Editor_DocFind(&g_editor.doc, EDITOR_DOC_CFG, "loc:test_crate_copy", &handle) && handle.text
              && handle.text->length == 0,
        "catalog: after the undo the copy's record is empty again");

    /* The npc kind, and the spawns panel resolving the pick to a symbol. */
    ToriRSChrome_SetText(&g_cat_chrome, find, "");
    ToriRSChrome_DropdownSetSelected(&g_cat_chrome, kind, Editor_CatalogModelKindIndex(TORIRSMAPED_CATALOG_NPC));
    cat_activate(kind);
    cat_frame();
    cat_frame();
    Editor_CatalogModelCountText(model, text, sizeof(text));
    CHECK(strcmp(text, "1 of 1 npc") == 0, "catalog: kind npc lists the npc (%s)", text);
    ToriRSChrome_VListSetSelected(&g_cat_chrome, list, 0);
    cat_activate(list);
    CHECK(g_cat_ui.tools.pick_kind == 1 && g_cat_ui.tools.pick_id == 0, "catalog: an npc row is the pick");
    {
        int spawns = cat_panel("Spawns");
        int use = spawns >= 0 ? cat_widget(spawns, TORIRS_CHROME_W_BUTTON, "Use catalog pick") : -1;
        int name = spawns >= 0 ? cat_widget(spawns, TORIRS_CHROME_W_TEXTINPUT, "Name") : -1;
        CHECK(use >= 0 && name >= 0, "spawns: Use catalog pick is there");
        if( use >= 0 && name >= 0 )
        {
            cat_activate(use);
            CHECK(strcmp(ToriRSChrome_Text(&g_cat_chrome, name), "test_guard") == 0,
                "spawns: the npc pick resolves to its pack symbol through the catalog");
        }
    }
}

/** Duplicate [D] on the Spawns panel: the selection one tile east, then at
 *  the latched tile; one undo step each; the copies are the selection. */
static void
test_spawn_duplicate(void)
{
    struct Editor_SpawnRow rows[4];
    struct Editor_SpawnRow selected[4];
    struct Editor_SpawnRow copies[4];
    struct Editor_Cmd commands[4];
    struct Editor_DocHandle handle;
    int spawns;
    int list;
    int duplicate;
    int count;

    /* The op: plain SPAWN adds, into the square the copy lands in. */
    memset(rows, 0, sizeof(rows));
    rows[0].suppress = 1;
    snprintf(rows[0].name, sizeof(rows[0].name), "goblin");
    rows[0].abs_x = SQ_X0 + 63;
    rows[0].abs_z = SQ_Z0 + 5;
    rows[1] = rows[0];
    rows[1].abs_x = SQ_X0 + 10;
    rows[1].level = 3;
    count = Editor_SpawnOpsDuplicate(rows, 2, 1, 0, 0, commands, 4, copies);
    CHECK(count == 2 && commands[0].kind == EDITOR_CMD_SPAWN && commands[0].has_after && !commands[0].has_before
              && strcmp(commands[0].doc_key, "m51_50") == 0 && strcmp(commands[1].doc_key, "m50_50") == 0
              && copies[0].suppress == 0 && copies[0].abs_x == SQ_X0 + 64,
        "spawns: a duplicate is a SPAWN add, in the square it lands in");
    CHECK(Editor_SpawnOpsDuplicate(rows, 2, 0, 0, 1, commands, 4, copies) == 1,
        "spawns: a copy past level 3 is skipped");

    spawns = cat_panel("Spawns");
    list = spawns >= 0 ? cat_widget(spawns, TORIRS_CHROME_W_VLIST, NULL) : -1;
    duplicate = spawns >= 0 ? cat_widget(spawns, TORIRS_CHROME_W_BUTTON, "Duplicate") : -1;
    CHECK(list >= 0 && duplicate >= 0, "spawns: the panel has Duplicate");
    if( list < 0 || duplicate < 0 )
        return;
    ToriRSChrome_PanelSetVisible(&g_cat_chrome, spawns, 1);
    g_cat_ui.tools.tile_valid = 1;
    g_cat_ui.tools.tile_x = SQ_X0 + 1;
    g_cat_ui.tools.tile_z = SQ_Z0 + 1;
    g_cat_ui.tools.tile_level = 0;
    cat_frame();
    cat_frame();

    /* Select two rows: a click, then a shift-click. */
    g_cat_ui.shift_held = 0;
    ToriRSChrome_VListSetSelected(&g_cat_chrome, list, 0);
    cat_activate(list);
    g_cat_ui.shift_held = 1;
    ToriRSChrome_VListSetSelected(&g_cat_chrome, list, 1);
    cat_activate(list);
    g_cat_ui.shift_held = 0;
    CHECK(Editor_Spawns_Selection(&g_cat_ui, selected, 4) == 2, "spawns: click + shift-click select two rows");
    if( Editor_Spawns_Selection(&g_cat_ui, selected, 4) != 2 )
        return;

    /* Latched on the first one's own tile: one tile east. */
    g_cat_ui.tools.tile_x = selected[0].abs_x;
    g_cat_ui.tools.tile_z = selected[0].abs_z;
    g_cat_ui.tools.tile_level = selected[0].level;
    cat_activate(duplicate);
    CHECK(Editor_Spawns_Selection(&g_cat_ui, copies, 4) == 2 && copies[0].abs_x == selected[0].abs_x + 1
              && copies[1].abs_x == selected[1].abs_x + 1 && copies[0].abs_z == selected[0].abs_z
              && strcmp(copies[0].name, selected[0].name) == 0 && !copies[0].suppress,
        "spawns: Duplicate copies the selection one tile east, and the copies are selected");
    {
        char key[EDITOR_DOC_KEY_MAX];
        Editor_SpawnDocKeyAt(key, copies[0].abs_x, copies[0].abs_z);
        CHECK(Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, key, &handle) && handle.spawn
                  && Editor_SpawnFind(&handle.spawn->file, &copies[0]) >= 0,
            "spawns: the copy is a row of the edited file");
    }
    CHECK(Editor_Undo(&g_editor) == 2, "spawns: one undo takes both copies back");
    cat_frame();
    CHECK(Editor_Spawns_Selection(&g_cat_ui, copies, 4) == 0, "spawns: and the vanished copies leave the selection");

    /* At the latched tile, offsets kept. */
    g_cat_ui.shift_held = 0;
    ToriRSChrome_VListSetSelected(&g_cat_chrome, list, 0);
    cat_activate(list);
    g_cat_ui.shift_held = 1;
    ToriRSChrome_VListSetSelected(&g_cat_chrome, list, 1);
    cat_activate(list);
    g_cat_ui.shift_held = 0;
    (void)Editor_Spawns_Selection(&g_cat_ui, selected, 4);
    g_cat_ui.tools.tile_x = SQ_X0 + 20;
    g_cat_ui.tools.tile_z = SQ_Z0 + 30;
    g_cat_ui.tools.tile_level = selected[0].level;
    cat_activate(duplicate);
    CHECK(Editor_Spawns_Selection(&g_cat_ui, copies, 4) == 2 && copies[0].abs_x == SQ_X0 + 20
              && copies[0].abs_z == SQ_Z0 + 30
              && copies[1].abs_x - copies[0].abs_x == selected[1].abs_x - selected[0].abs_x
              && copies[1].abs_z - copies[0].abs_z == selected[1].abs_z - selected[0].abs_z,
        "spawns: with another tile latched the copies land there, offsets kept");
    CHECK(Editor_Undo(&g_editor) == 2, "spawns: and undo as one step");

    Editor_UiFree(&g_cat_ui);
}

static struct Editor_Picker g_picker;

static void
test_picker(void)
{
    struct EditorHost_CatalogSquares atlas;
    struct Editor_PickerSearch search;
    int index;
    int lane_index;
    int map_x;
    int map_z;
    int x;
    int y;

    memset(&atlas, 0, sizeof(atlas));
    CHECK(g_editor.capabilities & TORIRSMAPED_CAP_CATALOG, "the embedded server has the catalog");
    CHECK(Editor_HostMapEdCatalogSquares(&g_editor.host, &atlas) == EDITOR_HOST_OK, "CATALOG_SQUARES answers");
    Editor_PickerInit(&g_picker);
    Editor_PickerSetAtlas(&g_picker, &atlas);
    Editor_HostCatalogSquaresFree(&atlas);

    CHECK(g_picker.storage_count >= 3, "storages: maps plus both lanes, the empty one too (%d)",
        g_picker.storage_count);
    CHECK(Editor_PickerStorageIndex(&g_picker, "maps") == 0 && Editor_PickerStorageIndex(&g_picker, "") == 0,
        "maps/ is storage 0, by either spelling");
    CHECK(Editor_PickerStorageIndex(&g_picker, "ported/emptylane/maps") > 0, "the empty lane is listed");
    index = Editor_PickerFind(&g_picker, 50, 50, 0);
    lane_index = Editor_PickerFind(&g_picker, 50, 50, Editor_PickerStorageIndex(&g_picker, "ported/testlane/maps"));
    CHECK(index >= 0 && lane_index >= 0 && index != lane_index, "m50_50 is in maps/ and in the lane");
    CHECK(Editor_PickerFind(&g_picker, 50, 50, -1) == index, "an any-storage lookup prefers maps/");
    CHECK(g_picker.tab_count >= 1, "there is at least one tab (%d)", g_picker.tab_count);
    if( index < 0 )
        return;
    {
        int const tab = Editor_PickerTabOfSquare(&g_picker, index);
        int const area = g_picker.squares[index].primary_area;
        CHECK(area >= 0 && g_picker.tabs[tab].area == area, "m50_50's tab is its area's (%s)",
            g_picker.tabs[tab].title);
        CHECK(area >= 0 && g_picker.areas[area].is_main && tab == 0, "the surface tab comes first");
        printf("picker: %d squares, %d areas, %d tabs, %d storages; m50_50 on '%s' (#%06x)\n",
            g_picker.square_count, g_picker.area_count, g_picker.tab_count, g_picker.storage_count,
            g_picker.tabs[tab].title, (unsigned)g_picker.squares[index].rgb);
    }
    CHECK(g_picker.squares[index].rgb != 0, "m50_50 has a thumbnail colour");
    {
        char key[EDITOR_DOC_KEY_MAX];
        Editor_PickerSquareKey(&g_picker, lane_index, key);
        CHECK(strcmp(key, "ported/testlane/maps/m50_50") == 0, "a lane square's key carries its storage (%s)", key);
    }

    /* Tabs: the surface first, the dungeon its own, Unmapped last. */
    {
        int const dungeon = Editor_PickerFind(&g_picker, 27, 83, -1);
        int unmapped = 0;
        for( int i = 0; i < g_picker.square_count; i++ )
            unmapped += g_picker.squares[i].primary_area < 0;
        CHECK(dungeon >= 0 && Editor_PickerTabOfSquare(&g_picker, dungeon) != Editor_PickerTabOfSquare(&g_picker, index),
            "m27_83 is on its own area's tab, not the surface's");
        CHECK(g_picker.tab_count >= 2, "a tab per claiming area (%d)", g_picker.tab_count);
        CHECK(!unmapped || strcmp(g_picker.tabs[g_picker.tab_count - 1].title, "Unmapped") == 0,
            "an unmapped square puts the Unmapped tab last (%d unmapped)", unmapped);
        for( int t = 0; t < g_picker.tab_count; t++ )
            printf("picker: tab %d '%s' %d squares, x %d..%d z %d..%d\n", t, g_picker.tabs[t].title,
                g_picker.tabs[t].count, g_picker.tabs[t].min_x, g_picker.tabs[t].max_x, g_picker.tabs[t].min_z,
                g_picker.tabs[t].max_z);
        if( dungeon >= 0 )
        {
            int const tab = Editor_PickerTabOfSquare(&g_picker, dungeon);
            CHECK(Editor_PickerSearch(&g_picker, g_picker.tabs[tab].title, &search)
                      && search.kind == EDITOR_PICKER_SEARCH_AREA && search.tab == tab,
                "the dungeon's tab found by its name ('%s')", g_picker.tabs[tab].title);
            CHECK(Editor_PickerSearch(&g_picker, "m27_83", &search) && search.tab == tab,
                "searching m27_83 from the surface switches to its tab");
            Editor_PickerSetTab(&g_picker, tab);
            CHECK(!Editor_PickerSquareOnTab(&g_picker, index, tab), "the surface square is not on the dungeon tab");
        }
    }

    /* Search. */
    Editor_PickerSetTab(&g_picker, 0);
    CHECK(Editor_PickerSearch(&g_picker, "m50_50", &search) && search.kind == EDITOR_PICKER_SEARCH_SQUARE
              && search.square == index,
        "search m50_50");
    CHECK(Editor_PickerSearch(&g_picker, " 50 50 ", &search) && search.kind == EDITOR_PICKER_SEARCH_SQUARE
              && search.map_x == 50 && search.map_z == 50,
        "search '50 50'");
    CHECK(Editor_PickerSearch(&g_picker, "50,51", &search) && search.map_z == 51 && search.square >= 0,
        "search '50,51'");
    CHECK(Editor_PickerSearch(&g_picker, "3222 3218", &search) && search.kind == EDITOR_PICKER_SEARCH_TILE
              && search.map_x == 50 && search.map_z == 50 && search.square == index,
        "search an absolute tile");
    CHECK(Editor_PickerSearch(&g_picker, "3222 3218 1", &search) && search.level == 1, "a tile with a level");
    CHECK(Editor_PickerSearch(&g_picker, "m99_99", &search) && search.square == -1,
        "an empty square is found as coordinates");
    CHECK(!Editor_PickerSearch(&g_picker, "m50_50junk", &search), "junk after a key is refused");
    CHECK(!Editor_PickerSearch(&g_picker, "zzqqxx-no-such-area", &search), "an unknown name finds nothing");
    {
        const char* area = g_picker.tabs[0].title;
        char part[8];
        snprintf(part, sizeof(part), "%.5s", area);
        CHECK(Editor_PickerSearch(&g_picker, part, &search) && search.kind == EDITOR_PICKER_SEARCH_AREA,
            "an area by part of its name ('%s' of '%s')", part, area);
    }

    /* The grid: north up, hit test, zoom about the pointer, pan. */
    Editor_PickerSetTab(&g_picker, Editor_PickerTabOfSquare(&g_picker, index));
    Editor_PickerFit(&g_picker, 360, 300);
    g_picker.zoom = 16.0;
    Editor_PickerCentre(&g_picker, 50, 50);
    x = EDITOR_PICKER_LEFT + (360 - EDITOR_PICKER_LEFT - 2) / 2;
    y = EDITOR_PICKER_TOP + (300 - EDITOR_PICKER_TOP - 2) / 2;
    CHECK(Editor_PickerCellAt(&g_picker, x, y, &map_x, &map_z) && map_x == 50 && map_z == 50,
        "the centre of the well is m50_50 (%d,%d)", map_x, map_z);
    CHECK(Editor_PickerCellAt(&g_picker, x, y - 16, &map_x, &map_z) && map_z == 51, "one cell up is north (z %d)",
        map_z);
    CHECK(Editor_PickerSquareAt(&g_picker, x, y) == index, "the square at the centre is m50_50");
    /* The wheel: continuous, a notch EDITOR_PICKER_ZOOM_STEP, about the
     * pointer -- the map point under it (not just its square) stays put. */
    {
        double const before_x = g_picker.origin_x + (double)(x + 5 - EDITOR_PICKER_LEFT) / g_picker.zoom;
        double const before_z = g_picker.origin_z - (double)(y + 5 - EDITOR_PICKER_TOP) / g_picker.zoom;
        double after_x;
        double after_z;
        Editor_PickerZoom(&g_picker, -1, x + 5, y + 5);
        after_x = g_picker.origin_x + (double)(x + 5 - EDITOR_PICKER_LEFT) / g_picker.zoom;
        after_z = g_picker.origin_z - (double)(y + 5 - EDITOR_PICKER_TOP) / g_picker.zoom;
        CHECK(fabs(g_picker.zoom - 16.0 * EDITOR_PICKER_ZOOM_STEP) < 1e-9, "a notch in is x%.2f (%.3f)",
            EDITOR_PICKER_ZOOM_STEP, g_picker.zoom);
        CHECK(fabs(after_x - before_x) < 1e-9 && fabs(after_z - before_z) < 1e-9,
            "the map point under the pointer stays under it (%.4f,%.4f -> %.4f,%.4f)", before_x, before_z, after_x,
            after_z);
        CHECK(Editor_PickerCellAt(&g_picker, x + 5, y + 5, &map_x, &map_z) && map_x == 50 && map_z == 50,
            "...so its square does");
        for( int i = 0; i < 40; i++ )
            Editor_PickerZoom(&g_picker, -1, x, y);
        CHECK(fabs(g_picker.zoom - Editor_PickerZoomMax(&g_picker)) < 1e-9
                  && g_picker.zoom >= (double)(300 - EDITOR_PICKER_TOP - 2),
            "zooming in stops with one square filling the well or closer (%.1f)", g_picker.zoom);
        for( int i = 0; i < 80; i++ )
            Editor_PickerZoom(&g_picker, 1, x, y);
        CHECK(fabs(g_picker.zoom - Editor_PickerZoomMin(&g_picker)) < 1e-9, "...and out at the page fitting (%.2f)",
            g_picker.zoom);
        g_picker.zoom = 24.0;
        Editor_PickerCentre(&g_picker, 50, 50);
    }
    Editor_PickerPan(&g_picker, -24, 0);
    CHECK(Editor_PickerCellAt(&g_picker, x + 5, y + 5, &map_x, &map_z) && map_x == 51,
        "dragging left one cell shows the square east (%d)", map_x);
    /* Fit: the page's squares, edge to edge in the narrower direction. */
    {
        const struct Editor_PickerTab* tab = &g_picker.tabs[g_picker.active_tab];
        int cx;
        int cy;
        int cw;
        int ch;
        int ex;
        int ey;
        int ew;
        int eh;
        Editor_PickerFit(&g_picker, 360, 300);
        Editor_PickerCellBox(&g_picker, tab->min_x, tab->max_z, &cx, &cy, &cw, &ch);
        Editor_PickerCellBox(&g_picker, tab->max_x, tab->min_z, &ex, &ey, &ew, &eh);
        CHECK(cx >= EDITOR_PICKER_LEFT - 1 && cy >= EDITOR_PICKER_TOP - 1 && ex + ew <= 360 + 1 && ey + eh <= 300 + 1,
            "Fit frames every square of the page (%d,%d .. %d,%d)", cx, cy, ex + ew, ey + eh);
        CHECK(ex + ew - cx >= 360 - EDITOR_PICKER_LEFT - 4 || ey + eh - cy >= 300 - EDITOR_PICKER_TOP - 4,
            "...filling the well one way (%d x %d)", ex + ew - cx, ey + eh - cy);
    }
    /* Labels thin out so they never touch. */
    CHECK(Editor_PickerLabelEvery(3.0, EDITOR_PICKER_LABEL_W) * 3.0 >= EDITOR_PICKER_LABEL_W
              && Editor_PickerLabelEvery(300.0, EDITOR_PICKER_LABEL_W) == 1
              && Editor_PickerLabelEvery(12.0, EDITOR_PICKER_LABEL_W) == 5,
        "axis labels every 10 at 3 px, every square at 300, every 5 at 12 (%d, %d, %d)",
        Editor_PickerLabelEvery(3.0, EDITOR_PICKER_LABEL_W), Editor_PickerLabelEvery(300.0, EDITOR_PICKER_LABEL_W),
        Editor_PickerLabelEvery(12.0, EDITOR_PICKER_LABEL_W));
    g_picker.zoom = 16.0;
    Editor_PickerCentre(&g_picker, 50, 50);
}

static void
test_newform(void)
{
    struct Editor_NewSquareForm form;
    struct Editor_NewSquareCheck check;
    struct EditorHost_DocCreate request;
    int const empty_lane = Editor_PickerStorageIndex(&g_picker, "ported/emptylane/maps");
    int const test_lane = Editor_PickerStorageIndex(&g_picker, "ported/testlane/maps");

    memset(&form, 0, sizeof(form));
    form.mode = EDITOR_NEW_SQUARE_NEW;
    form.map_x = 50;
    form.map_z = 50;
    form.storage = 0;
    CHECK(!Editor_NewSquareValidate(&g_picker, &form, &check, NULL) && strstr(check.message, "already exists")
              && strstr(check.message, "maps/"),
        "m50_50 in maps/ is refused, naming the owner (%s)", check.message);
    form.storage = test_lane;
    CHECK(!Editor_NewSquareValidate(&g_picker, &form, &check, NULL), "...and in the lane that holds it");
    form.storage = empty_lane;
    CHECK(Editor_NewSquareValidate(&g_picker, &form, &check, &request), "...but not in the empty lane (%s)",
        check.message);
    CHECK(strcmp(request.storage, "ported/emptylane/maps") == 0 && strcmp(request.key, "m50_50") == 0
              && request.template_kind == EDITOR_DOC_TEMPLATE_EMPTY,
        "the request names the key, the lane and the empty template");
    CHECK(strstr(check.message, "also in") != NULL, "it says where else the square is (%s)", check.message);

    form.storage = -1;
    snprintf(form.new_lane, sizeof(form.new_lane), "Bad Name");
    CHECK(!Editor_NewSquareValidate(&g_picker, &form, &check, NULL), "a lane name with spaces is refused");
    snprintf(form.new_lane, sizeof(form.new_lane), "fresh_lane");
    form.map_x = 60;
    form.map_z = 60;
    CHECK(Editor_NewSquareValidate(&g_picker, &form, &check, &request) && check.creates_lane
              && strcmp(request.storage, "ported/fresh_lane/maps") == 0,
        "a new lane is created by naming it (%s)", check.message);

    form.storage = 0;
    form.template_kind = EDITOR_DOC_TEMPLATE_COPY;
    CHECK(!Editor_NewSquareValidate(&g_picker, &form, &check, NULL), "copy with nothing selected is refused");
    snprintf(form.source_key, sizeof(form.source_key), "m50_50");
    CHECK(Editor_NewSquareValidate(&g_picker, &form, &check, &request) && strcmp(request.source_key, "m50_50") == 0,
        "copy of the selection carries its key");
    form.template_kind = EDITOR_DOC_TEMPLATE_FLAT;
    form.fill_height = 0;
    CHECK(!Editor_NewSquareValidate(&g_picker, &form, &check, NULL), "a flat fill at h 0 is refused");
    form.fill_height = 12;
    form.fill_underlay = 40;
    CHECK(Editor_NewSquareValidate(&g_picker, &form, &check, &request) && request.fill_height == 12
              && request.fill_underlay == 40,
        "flat fill carries its height and underlay");

    memset(&form, 0, sizeof(form));
    form.mode = EDITOR_NEW_SQUARE_SAVE_AS;
    form.map_x = 50;
    form.map_z = 50;
    form.storage = 0;
    snprintf(form.source_key, sizeof(form.source_key), "m50_50");
    CHECK(!Editor_NewSquareValidate(&g_picker, &form, &check, NULL), "save as new into its own storage is refused");
    form.storage = empty_lane;
    CHECK(Editor_NewSquareValidate(&g_picker, &form, &check, &request)
              && request.template_kind == EDITOR_DOC_TEMPLATE_COPY,
        "save as new into another storage is a copy");
    form.map_x = 300;
    CHECK(!Editor_NewSquareValidate(&g_picker, &form, &check, NULL), "coordinates past 255 are refused");
}

/* ---- the chrome ------------------------------------------------------------------ */

/** The well-local point at the centre of square (map_x, map_z) as the picker
 *  module last drew it. */
static int
picker_point(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z,
    int* out_x,
    int* out_y)
{
    int w;
    int h;
    Editor_PickerCellBox(picker, map_x, map_z, out_x, out_y, &w, &h);
    *out_x += w / 2;
    *out_y += h / 2;
    return *out_x > EDITOR_PICKER_LEFT && *out_y > EDITOR_PICKER_TOP && *out_x < picker->well_w
           && *out_y < picker->well_h;
}

static int g_squares_panel = -1;

/** Type into the picker's Find field and press Enter. */
static void
search_field(const char* text)
{
    int const input = find_widget(g_squares_panel, TORIRS_CHROME_W_TEXTINPUT, "Find");
    assert(input >= 0);
    ToriRSChrome_SetText(&g_chrome, input, text);
    activate(input);
    frame();
}

/* A surface point inside the GRAPH well and the well-local point it is:
 * the chrome's own Pinch reports the conversion, and the probe is taken back
 * off the queue. */
static void
well_point(
    int graph,
    int* out_sx,
    int* out_sy,
    int* out_lx,
    int* out_ly)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[graph];
    struct ToriRSChromeEvent probe;

    *out_sx = w->x + w->w / 2;
    *out_sy = w->y + w->h / 2;
    CHECK(ToriRSChrome_Pinch(&g_chrome, *out_sx, *out_sy, 1.0) == 1, "the well's middle takes a pinch");
    CHECK(ToriRSChrome_TakeEvent(&g_chrome, &probe) && probe.kind == TORIRS_CHROME_EVENT_GRAPH_PINCH,
        "as a GRAPH_PINCH");
    *out_lx = probe.x;
    *out_ly = probe.y;
}

/*
 * Trackpad gestures over the world map, through the chrome's real routing
 * (ToriRSChrome_WheelFrame, what app_chrome_route_input hands a frame to):
 * a pinch zooms continuously about its centre, a two-finger scroll pans and
 * never zooms, a mouse wheel still steps x1.25, ctrl+wheel zooms by the exact
 * fraction, and a pinch that came with a wheel echo spends the echo.
 */
static void
test_picker_gestures(
    int graph,
    struct Editor_Picker* picker)
{
    struct ToriRSChromeWheelFrame f;
    int sx;
    int sy;
    int lx;
    int ly;
    double map_x;
    double map_z;
    double zoom;

    frame();
    well_point(graph, &sx, &sy, &lx, &ly);
    Editor_PickerZoomTo(picker, 32.0, lx, ly);
    frame();

    /* A 2x pinch about the well's middle: the zoom doubles, exactly, and the
     * map point under the centre stays under it. (The chrome carries a scale
     * in 1/65536ths, so other ratios are exact to about 1e-5.) */
    map_x = picker->origin_x + (double)(lx - EDITOR_PICKER_LEFT) / picker->zoom;
    map_z = picker->origin_z - (double)(ly - EDITOR_PICKER_TOP) / picker->zoom;
    zoom = picker->zoom;
    memset(&f, 0, sizeof(f));
    f.x = sx;
    f.y = sy;
    f.pinch_scale = 2.0f;
    f.pinch_x = sx;
    f.pinch_y = sy;
    CHECK(ToriRSChrome_WheelFrame(&g_chrome, &f) == 1, "a pinch over the well is taken");
    frame();
    CHECK(fabs(picker->zoom - 2.0 * zoom) < 1e-6, "a 2x pinch is a 2x zoom, no 1.25 steps (%.3f -> %.3f)", zoom,
        picker->zoom);
    CHECK(fabs(picker->origin_x + (double)(lx - EDITOR_PICKER_LEFT) / picker->zoom - map_x) < 1e-9
              && fabs(picker->origin_z - (double)(ly - EDITOR_PICKER_TOP) / picker->zoom - map_z) < 1e-9,
        "the map point under the pinch centre stays under it (%.4f,%.4f)", map_x, map_z);

    /* A pinch out of 0.8 then: continuous the other way. */
    zoom = picker->zoom;
    f.pinch_scale = 0.8f;
    (void)ToriRSChrome_WheelFrame(&g_chrome, &f);
    frame();
    CHECK(fabs(picker->zoom / (0.8 * zoom) - 1.0) < 1e-4, "a 0.8 pinch is a 0.8 zoom (%.3f)", picker->zoom);

    /* A two-finger scroll pans by its deltas and never zooms, even with the
     * integer notch SDL rounded out of it in the same frame. */
    {
        double const origin_x = picker->origin_x;
        double const origin_z = picker->origin_z;
        int const scale = g_chrome.scale > 0 ? g_chrome.scale : 1;
        zoom = picker->zoom;
        memset(&f, 0, sizeof(f));
        f.x = sx;
        f.y = sy;
        f.wheel = 1;
        f.scroll_dx = 30.0f * (float)scale;
        f.scroll_dy = -20.0f * (float)scale;
        f.px_per_notch = 48;
        f.trackpad = 1;
        CHECK(ToriRSChrome_WheelFrame(&g_chrome, &f) == 1, "a trackpad scroll over the well is taken");
        frame();
        CHECK(picker->zoom == zoom, "a two-finger scroll never zooms (%.3f -> %.3f)", zoom, picker->zoom);
        /* Scroll right (+dx) moves the map left: the origin goes east by dx;
         * scroll down (-dy) moves the map up: the origin goes south. */
        CHECK(fabs(picker->origin_x - (origin_x + 30.0 / zoom)) < 1e-6,
            "scroll right pans the map left by exactly dx (%.4f -> %.4f)", origin_x, picker->origin_x);
        CHECK(fabs(picker->origin_z - (origin_z - 20.0 / zoom)) < 1e-6,
            "scroll down pans the map up by exactly dy (%.4f -> %.4f)", origin_z, picker->origin_z);
    }

    /* A mouse wheel (whole deltas, no trackpad flag) still steps x1.25 about
     * the cursor. */
    zoom = picker->zoom;
    memset(&f, 0, sizeof(f));
    f.x = sx;
    f.y = sy;
    f.wheel = -1;
    f.scroll_dy = -48.0f;
    f.px_per_notch = 48;
    CHECK(ToriRSChrome_WheelFrame(&g_chrome, &f) == 1, "a mouse wheel over the well is taken");
    frame();
    CHECK(fabs(picker->zoom / (zoom * 1.25) - 1.0) < 1e-9, "a mouse notch is still one x1.25 step (%.3f -> %.3f)", zoom,
        picker->zoom);

    /* ctrl+wheel zooms continuously: a quarter notch is a quarter of the
     * ratio, in the plain wheel's direction (down is in). */
    zoom = picker->zoom;
    memset(&f, 0, sizeof(f));
    f.x = sx;
    f.y = sy;
    f.ctrl = 1;
    f.scroll_dy = -12.0f;
    f.px_per_notch = 48;
    f.trackpad = 1;
    CHECK(ToriRSChrome_WheelFrame(&g_chrome, &f) == 1, "ctrl+wheel over the well is taken");
    frame();
    CHECK(fabs(picker->zoom / (zoom * pow(1.25, 0.25)) - 1.0) < 1e-4, "ctrl + a quarter notch zooms by 1.25^0.25 (%.4f -> %.4f)",
        zoom, picker->zoom);
    /* ...and with only an integer wheel (no exact deltas), a whole step. */
    zoom = picker->zoom;
    f.scroll_dy = 0.0f;
    f.px_per_notch = 0;
    f.trackpad = 0;
    f.wheel = 1;
    (void)ToriRSChrome_WheelFrame(&g_chrome, &f);
    frame();
    CHECK(fabs(picker->zoom / (zoom / 1.25) - 1.0) < 1e-4, "ctrl + a whole notch up is one step out (%.4f)", picker->zoom);

    /* A browser's pinch arrives as a ctrl+wheel too: the pinch takes it and
     * its wheel echo is spent, not stepped on top. */
    zoom = picker->zoom;
    memset(&f, 0, sizeof(f));
    f.x = sx;
    f.y = sy;
    f.wheel = -1;
    f.scroll_dy = -30.0f;
    f.px_per_notch = 100;
    f.trackpad = 1;
    f.pinch_scale = 1.5f;
    f.pinch_x = sx;
    f.pinch_y = sy;
    f.pinch_from_wheel = 1;
    (void)ToriRSChrome_WheelFrame(&g_chrome, &f);
    frame();
    CHECK(fabs(picker->zoom / (zoom * 1.5) - 1.0) < 1e-4, "a browser pinch zooms by its scale only (%.3f -> %.3f)", zoom,
        picker->zoom);

    /* The bus's gesture frames (workbench 6.1) reach the same picker through
     * ToriRSChrome_Gesture: a PINCH frame zooms by its exact ratio about its
     * centre, the map point there within a pixel of where it was; a
     * two-finger PAN frame pans the map with the fingers and never zooms. */
    {
        struct ToriRSChromeGesture g;
        double px;
        double pz;

        frame();
        zoom = picker->zoom;
        map_x = picker->origin_x + (double)(lx - EDITOR_PICKER_LEFT) / picker->zoom;
        map_z = picker->origin_z - (double)(ly - EDITOR_PICKER_TOP) / picker->zoom;
        memset(&g, 0, sizeof(g));
        g.kind = TORIRS_CHROME_GESTURE_PINCH;
        g.x = sx;
        g.y = sy;
        g.fingers = 2;
        g.scale_q16 = 3 * 65536 / 2;
        g.phase = TORIRS_CHROME_GESTURE_UPDATE;
        CHECK(ToriRSChrome_Gesture(&g_chrome, &g) == 1, "a PINCH frame over the well is taken");
        frame();
        CHECK(fabs(picker->zoom / (zoom * 1.5) - 1.0) < 1e-4, "a 1.5 PINCH frame is a 1.5 zoom (%.3f -> %.3f)", zoom,
            picker->zoom);
        /* Where the map point that was under the pinch is drawn now, in well px. */
        px = (map_x - picker->origin_x) * picker->zoom + EDITOR_PICKER_LEFT;
        pz = (picker->origin_z - map_z) * picker->zoom + EDITOR_PICKER_TOP;
        CHECK(fabs(px - (double)lx) <= 1.0 && fabs(pz - (double)ly) <= 1.0,
            "the picker zooms about the pinch within one pixel (%.2f,%.2f vs %d,%d)", px, pz, lx, ly);

        zoom = picker->zoom;
        {
            double const origin_x = picker->origin_x;
            double const origin_z = picker->origin_z;
            int const scale = g_chrome.scale > 0 ? g_chrome.scale : 1;
            memset(&g, 0, sizeof(g));
            g.kind = TORIRS_CHROME_GESTURE_PAN;
            g.x = sx;
            g.y = sy;
            g.dx = 30 * scale;
            g.dy = 20 * scale;
            g.fingers = 2;
            g.phase = TORIRS_CHROME_GESTURE_UPDATE;
            CHECK(ToriRSChrome_Gesture(&g_chrome, &g) == 1, "a PAN frame over the well is taken");
            frame();
            CHECK(picker->zoom == zoom, "a PAN frame never zooms");
            /* Fingers right and down carry the map with them: the origin goes
             * west by dx and north by dy, as a drag would. */
            CHECK(fabs(picker->origin_x - (origin_x - 30.0 / zoom)) < 1e-6
                      && fabs(picker->origin_z - (origin_z + 20.0 / zoom)) < 1e-6,
                "a PAN frame pans the map with the fingers (%.4f,%.4f -> %.4f,%.4f)", origin_x, origin_z,
                picker->origin_x, picker->origin_z);
        }
    }

    /* Off every panel nothing is taken: the game keeps its wheel, and an
     * unclaimed pinch is the caller's to drop. */
    memset(&f, 0, sizeof(f));
    f.x = f.pinch_x = 2;
    f.y = f.pinch_y = 795;
    f.pinch_scale = 2.0f;
    f.scroll_dy = 10.0f;
    f.px_per_notch = 48;
    f.trackpad = 1;
    if( ToriRSChrome_HitTest(&g_chrome, 2, 795) < 0 && !ToriRSChrome_WantsPointer(&g_chrome, 2, 795) )
        CHECK(ToriRSChrome_WheelFrame(&g_chrome, &f) == 0, "a pinch and a scroll off the chrome are not taken");

    /* The keyboard keeps working after all of it. */
    zoom = picker->zoom;
    {
        struct ToriRSChromeEvent key;
        memset(&key, 0, sizeof(key));
        key.kind = TORIRS_CHROME_EVENT_GRAPH_KEY;
        key.widget = graph;
        key.serial = g_chrome.widgets[graph].serial;
        key.value = '+';
        (void)ToriRSChrome_PushEvent(&g_chrome, &key);
        frame();
    }
    CHECK(fabs(picker->zoom - zoom * 1.25) < 1e-6 || picker->zoom == Editor_PickerZoomMax(picker),
        "+ still zooms in a step (%.3f -> %.3f)", zoom, picker->zoom);
}

static void
test_chrome(void)
{
    int squares_panel;
    int graph;
    const struct Editor_Picker* picker;
    int x;
    int y;

    /* The ops tests left unsaved edits; a click-to-open would (rightly) ask
     * the save guard first. Saved into the tmp tree. */
    CHECK(Editor_SaveAll(&g_editor) > 0, "save the ops tests' edits into the tmp tree");
    CHECK(!Editor_DocHasUnsaved(&g_editor.doc), "nothing unsaved");

    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1280, 800);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.hooks.open_square = fake_open_square;
    g_ui.hooks.open_in_new_view = fake_open_in_new_view;
    g_ui.hooks.floor_rgb = fake_floor_rgb;
    g_ui.hooks.floor_request = fake_floor_request;
    g_ui.hooks.view_window = fake_view_window;
    g_ui.hooks.selection_changed = fake_selection_changed;
    Editor_UiRegisterToolsPanels(&g_ui);
    ungroup_all(&g_ui);

    CHECK(Editor_UiFindPanel(&g_ui, "squares") && Editor_UiFindPanel(&g_ui, "new_square")
              && Editor_UiFindPanel(&g_ui, "save_guard") && Editor_UiFindPanel(&g_ui, "toolrail")
              && Editor_UiFindPanel(&g_ui, "terrain") && Editor_UiFindPanel(&g_ui, "palette")
              && Editor_UiFindPanel(&g_ui, "console") && Editor_UiFindPanel(&g_ui, "locs")
              && Editor_UiFindPanel(&g_ui, "spawns") && Editor_UiFindPanel(&g_ui, "rect")
              && Editor_UiFindPanel(&g_ui, "menubar"),
        "every App-free module registered");
    CHECK(!Editor_UiFindPanel(&g_ui, "catalog") && !Editor_UiFindPanel(&g_ui, "inspector"),
        "the App-bound modules are skipped without an App");

    Editor_UiSetVisible(&g_ui, 1);
    squares_panel = find_panel("Squares");
    g_squares_panel = squares_panel;
    CHECK(squares_panel >= 0 && g_chrome.panels[squares_panel].visible, "the square picker opens with the editor");
    for( int i = 0; i < 3; i++ )
        frame();
    picker = edp_squares_picker(&g_ui);
    CHECK(picker && picker->square_count >= 6, "the picker fetched the atlas (%d squares)",
        picker ? picker->square_count : -1);
    /* The well is the Square map window's (a Find below opens it). */
    graph = find_widget(find_panel("Square map"), TORIRS_CHROME_W_GRAPH, NULL);
    CHECK(graph >= 0, "the picker has its GRAPH well");
    if( !picker || graph < 0 )
        return;

    /* The well shows the surface around m50_50. */
    {
        struct Editor_PickerSearch search;
        int const area = find_widget(squares_panel, TORIRS_CHROME_W_DROPDOWN, "Area");
        CHECK(find_widget(squares_panel, TORIRS_CHROME_W_TABSTRIP, NULL) < 0,
            "no tab strip: ~50 area labels in one row cannot be read");
        /* Every area plus Unmapped, then a "lane: <name>" row per lane
         * storage (workbench 6.4), then Instance space when no tab is it. */
        int lanes = 0;
        char lane[EDITOR_PICKER_NAME_MAX];
        for( int s = 0; s < picker->storage_count; s++ )
            lanes += Editor_PickerLaneName(picker->storages[s], lane) ? 1 : 0;
        int const rows = picker->tab_count + lanes + (Editor_PickerInstanceTab(picker) < 0 ? 1 : 0);
        CHECK(lanes >= 1 && area >= 0 && g_chrome.widgets[area].option_count == rows,
            "the Area dropdown lists every area plus Unmapped, then the lanes (%d of %d, %d lanes)",
            area >= 0 ? g_chrome.widgets[area].option_count : -1, rows, lanes);
        (void)search;
        search_field("m50_50");
        CHECK(picker->items_drawn > 4, "the well was drawn (%d items)", picker->items_drawn);
        CHECK(picker_point(picker, 50, 50, &x, &y), "m50_50 is in view");
        CHECK(Editor_PickerSquareOfKey(picker, ToriRSChrome_GraphKeyAt(&g_chrome, graph, x, y))
                  == Editor_PickerFind(picker, 50, 50, 0),
            "the GRAPH key under m50_50's cell names m50_50");
    }

    /* Hover names the square; click opens; shift-click adds. */
    push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_HOVER, x, y, 0);
    frame();
    {
        int const map_panel = find_panel("Square map");
        int const readout = find_widget(map_panel, TORIRS_CHROME_W_LABEL, NULL);
        int found = 0;
        for( int i = readout; i >= 0 && i < g_chrome.widget_count; i++ )
            if( g_chrome.widgets[i].panel == map_panel && strstr(g_chrome.widgets[i].text, "m50_50") )
                found = 1;
        CHECK(found, "hover names m50_50 in the readout");
    }
    memset(&g_window, 0, sizeof(g_window));
    push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
    push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x, y, 0);
    frame();
    CHECK(g_window.open_calls == 1 && g_window.open_x == 50 && g_window.open_z == 50 && !g_window.open_add,
        "a click opens m50_50 in place (%d calls)", g_window.open_calls);
    /* m50_51 is the cell above; in the 1280x800 column's short well it can
     * sit under the top ruler, so Find brings it into view first (Find
     * centres, it opens nothing). */
    search_field("m50_51");
    CHECK(picker_point(picker, 50, 51, &x, &y), "m50_51 is in view");
    g_ui.shift_held = 1;
    push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
    push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x, y, 0);
    frame();
    g_ui.shift_held = 0;
    CHECK(g_window.open_calls == 2 && g_window.open_z == 51 && g_window.open_add,
        "shift-click adds m50_51 to the open set (%d calls, m%d_%d, add %d; m50_50 at %d,%d, well %d,%d %dx%d)",
        g_window.open_calls, g_window.open_x, g_window.open_z, g_window.open_add, x, y, g_chrome.widgets[graph].x,
        g_chrome.widgets[graph].y, g_chrome.widgets[graph].w, g_chrome.widgets[graph].h);

    /* A drag pans, and does not open. */
    {
        double const origin = picker->origin_x;
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_DRAG, x + 40, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x + 40, y, 0);
        frame();
        CHECK(picker->origin_x < origin && g_window.open_calls == 2,
            "a drag pans the map and opens nothing (origin %.3f -> %.3f, %d calls)", origin, picker->origin_x,
            g_window.open_calls);
    }
    {
        double const cell = picker->zoom;
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_WHEEL, x, y, 1);
        frame();
        CHECK(picker->zoom < cell, "the wheel zooms out (%.2f -> %.2f)", cell, picker->zoom);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_WHEEL, x, y, -1);
        frame();
        CHECK(fabs(picker->zoom - cell) < 1e-9, "...and back in (%.2f)", picker->zoom);
    }

    test_picker_gestures(graph, (struct Editor_Picker*)picker);

    /* Right-click: the menu; Copy to opens the storage question. */
    {
        int menu_panel;
        int copy_item;
        int new_panel;
        search_field("m50_50");
        picker_point(picker, 50, 50, &x, &y);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_MENU, x, y, 0);
        frame();
        menu_panel = find_panel("m50_50");
        CHECK(menu_panel >= 0 && g_chrome.panels[menu_panel].visible, "right-click opens the square's menu");
        copy_item = find_widget(menu_panel, TORIRS_CHROME_W_MENUITEM, "Copy to...");
        CHECK(copy_item >= 0 && find_widget(menu_panel, TORIRS_CHROME_W_MENUITEM, "Open in new view") >= 0
                  && find_widget(menu_panel, TORIRS_CHROME_W_MENUITEM, "Delete (clear)...") >= 0,
            "the menu offers Open in new view, Copy to, Delete");
        activate(find_widget(menu_panel, TORIRS_CHROME_W_MENUITEM, "Open in new view"));
        CHECK(g_window.new_view_calls == 1, "Open in new view asks the window");
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_MENU, x, y, 0);
        frame();
        activate(copy_item);
        new_panel = find_panel("Copy square to");
        CHECK(new_panel >= 0 && g_chrome.panels[new_panel].visible && ToriRSChrome_ModalPanel(&g_chrome) == new_panel,
            "Copy to opens the New modal in its copy mode");
        activate(find_widget(new_panel, TORIRS_CHROME_W_BUTTON, "Cancel"));
        CHECK(!g_chrome.panels[new_panel].visible, "Cancel closes it");
    }

    /* The Area dropdown switches the area the well draws, fitted. */
    {
        int const area = find_widget(squares_panel, TORIRS_CHROME_W_DROPDOWN, "Area");
        int const other = picker->tab_count - 3; /* the last area before Instance space and Unmapped */
        CHECK(picker->tabs[1].area == EDITOR_PICKER_TAB_FULL && strcmp(picker->tabs[1].title, "Full map") == 0,
            "the Full map page sits beside the surface");
        ToriRSChrome_DropdownSetSelected(&g_chrome, area, other);
        activate(area);
        CHECK(picker->active_tab == other && picker->tabs[other].area >= 0, "the Area dropdown picks the area (%s)",
            picker->tabs[picker->active_tab].title);
        search_field("m50_50");
        CHECK(picker->active_tab == 0, "a search switches back to the square's tab");
    }

    /* Delete (clear): right-click, confirm, one undo step. */
    {
        struct Editor_Square* square;
        int menu_panel;
        int confirm_panel;
        int locs_before;
        search_field("m51_50");
        picker_point(picker, 51, 50, &x, &y);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_MENU, x, y, 0);
        frame();
        menu_panel = find_panel("m51_50");
        CHECK(menu_panel >= 0 && g_chrome.panels[menu_panel].visible, "right-click on m51_50 opens its menu");
        activate(find_widget(menu_panel, TORIRS_CHROME_W_MENUITEM, "Delete (clear)..."));
        confirm_panel = find_panel("Delete square");
        CHECK(confirm_panel >= 0 && g_chrome.panels[confirm_panel].visible, "Delete asks first");
        square = square_of(51, 50);
        locs_before = square ? square->loc_count : -1;
        activate(find_widget(confirm_panel, TORIRS_CHROME_W_BUTTON, "Clear it"));
        square = square_of(51, 50);
        CHECK(square && square->loc_count == 0 && locs_before > 0, "m51_50 is cleared (%d locs before)", locs_before);
        CHECK(Editor_Undo(&g_editor) > 0 && square_of(51, 50)->loc_count == locs_before,
            "one undo brings the whole square back");
    }

    /* The New button: coordinates, storage, template; then DOC_CREATE. */
    {
        int new_panel;
        int create;
        int input_x;
        int input_z;
        int storage;
        int check_row = -1;
        int lane_row = -1;

        activate(find_widget(squares_panel, TORIRS_CHROME_W_BUTTON, "New square..."));
        new_panel = find_panel("New square");
        CHECK(new_panel >= 0 && g_chrome.panels[new_panel].visible, "New square... opens the modal");
        create = find_widget(new_panel, TORIRS_CHROME_W_BUTTON, "Create");
        input_x = find_widget(new_panel, TORIRS_CHROME_W_TEXTINPUT, "Map x");
        input_z = find_widget(new_panel, TORIRS_CHROME_W_TEXTINPUT, "Map z");
        storage = find_widget(new_panel, TORIRS_CHROME_W_DROPDOWN, "Stored in");
        CHECK(create >= 0 && input_x >= 0 && input_z >= 0 && storage >= 0, "the modal has its fields");
        for( int i = 0; i < g_chrome.widgets[storage].option_count; i++ )
            if( strstr(g_chrome.widgets[storage].options[i], "emptylane") )
                lane_row = i;
        CHECK(lane_row >= 0, "the storage list names the empty lane");
        CHECK(strstr(g_chrome.widgets[storage].options[g_chrome.widgets[storage].option_count - 1], "New lane"),
            "...and offers a new lane last");

        /* Taken: Create stays disabled and the owner is named. */
        ToriRSChrome_SetText(&g_chrome, input_x, "50");
        ToriRSChrome_SetText(&g_chrome, input_z, "50");
        ToriRSChrome_DropdownSetSelected(&g_chrome, storage, 0);
        frame();
        for( int i = 0; i < g_chrome.widget_count; i++ )
            if( g_chrome.widgets[i].panel == new_panel && strstr(g_chrome.widgets[i].text, "already exists") )
                check_row = i;
        CHECK(check_row >= 0 && ToriRSChrome_Disabled(&g_chrome, create),
            "m50_50 in maps/: refused before anything is sent");

        ToriRSChrome_SetText(&g_chrome, input_x, "61");
        ToriRSChrome_SetText(&g_chrome, input_z, "62");
        ToriRSChrome_DropdownSetSelected(&g_chrome, storage, lane_row);
        frame();
        CHECK(!ToriRSChrome_Disabled(&g_chrome, create), "m61_62 in the empty lane can be created");
        memset(&g_window, 0, sizeof(g_window));
        activate(create);
        CHECK(!g_chrome.panels[new_panel].visible, "Create closes the modal");
        {
            struct Editor_Square* created = square_of(61, 62);
            CHECK(created && created->loaded, "the created square is open in the mirror");
            CHECK(created && strcmp(Editor_DocSquareStorage(&g_editor.doc, created), "ported/emptylane/maps") == 0,
                "...stored in the lane");
        }
        CHECK(g_window.open_calls == 1 && g_window.open_x == 61 && g_window.open_add
                  && strcmp(g_window.open_storage, "ported/emptylane/maps") == 0,
            "...and the window is asked to show it");
        for( int i = 0; i < 4; i++ )
            frame();
        picker = edp_squares_picker(&g_ui);
        {
            int const at = Editor_PickerFind(picker, 61, 62, -1);
            CHECK(at >= 0 && (picker->squares[at].flags & TORIRSMAPED_SQUARE_CREATED),
                "the atlas row appears flagged CREATED (nothing on disk yet)");
        }

        /* The same coordinates again: the server's refusal is shown. */
        Editor_NewSquare_Open(&g_ui, EDITOR_NEW_SQUARE_NEW, NULL, 61, 62);
        frame();
        CHECK(!ToriRSChrome_Disabled(&g_chrome, create), "m61_62 in maps/ (the default) is free");
        ToriRSChrome_DropdownSetSelected(&g_chrome, storage, lane_row);
        frame();
        CHECK(ToriRSChrome_Disabled(&g_chrome, create), "m61_62 in that lane is now refused by the form");
        activate(find_widget(new_panel, TORIRS_CHROME_W_BUTTON, "Cancel"));
    }

    /* The save guard: unsaved edits hold an open-in-place behind the modal. */
    {
        struct EditorUiGuard guard;
        int guard_panel;
        struct Editor_Cmd command;

        Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, 50, 50);
        command.u.tile.x = 1;
        command.u.tile.z = 1;
        command.u.tile.before = *tile_at(SQ_X0 + 1, SQ_Z0 + 1, 0);
        command.u.tile.after = command.u.tile.before;
        command.u.tile.after.underlay_id = (uint8_t)(command.u.tile.before.underlay_id + 1);
        Editor_Apply(&g_editor, &command);
        CHECK(Editor_DocHasUnsaved(&g_editor.doc), "the mirror has unsaved edits");
        memset(&guard, 0, sizeof(guard));
        guard.action = EDITOR_UI_GUARD_OPEN_SQUARE;
        guard.map_x = 51;
        guard.map_z = 50;
        memset(&g_window, 0, sizeof(g_window));
        CHECK(!Editor_UiGuarded(&g_ui, &guard), "the guarded open waits");
        frame();
        guard_panel = find_panel("Unsaved changes");
        CHECK(guard_panel >= 0 && g_chrome.panels[guard_panel].visible, "the save guard asks");
        CHECK(g_window.open_calls == 0, "nothing opened yet");
        activate(find_widget(guard_panel, TORIRS_CHROME_W_BUTTON, "Cancel"));
        CHECK(!g_chrome.panels[guard_panel].visible && g_window.open_calls == 0, "Cancel drops the open");
        Editor_UiGuarded(&g_ui, &guard);
        frame();
        activate(find_widget(guard_panel, TORIRS_CHROME_W_BUTTON, "Save"));
        CHECK(g_window.open_calls == 1 && g_window.open_x == 51, "Save saves, then opens");
        CHECK(!Editor_DocHasUnsaved(&g_editor.doc), "nothing is unsaved after Save");
    }

    /* The tool rail arms tools; the console lists what happened. */
    {
        int const rail = find_panel("Tools");
        int const raise = find_widget(rail, TORIRS_CHROME_W_ICONBUTTON, "Raise");
        int const console_panel = find_panel("Output");
        int const list = find_widget(console_panel, TORIRS_CHROME_W_VLIST, NULL);
        CHECK(raise >= 0, "the rail has a Raise button");
        activate(raise);
        CHECK(g_ui.tools.tool == EDITOR_TOOL_RAISE && ToriRSChrome_Checked(&g_chrome, raise),
            "Raise is armed and pressed");
        CHECK(list >= 0 && g_ui.console_count > 0 && g_chrome.widgets[list].imax == g_ui.console_count,
            "the console lists the session's lines (%d of %d)", list >= 0 ? g_chrome.widgets[list].imax : -1,
            g_ui.console_count);
    }

    /* The view frame: this window's own, from the hook. */
    {
        int self = 0;
        for( int i = 0; i < EDITOR_UI_VIEW_FRAMES_MAX; i++ )
            if( g_ui.frames[i].used && g_ui.frames[i].slot == g_ui.view_slot && g_ui.frames[i].map_x == 50
                && g_ui.frames[i].w == 2 )
                self = 1;
        CHECK(self, "this window's frame is kept for the picker");
        {
            int32_t values[5] = { 40, 41, 3, 3, 12345 };
            Editor_UiApplyState(&g_ui, TORIRSMAPED_STATE_VIEW_SQUARE, values, 5);
        }
        self = 0;
        for( int i = 0; i < EDITOR_UI_VIEW_FRAMES_MAX; i++ )
            if( g_ui.frames[i].used && g_ui.frames[i].slot == 12345 && g_ui.frames[i].map_x == 40 )
                self = 1;
        CHECK(self, "another view's STATE_VIEW_SQUARE is framed");
    }

    Editor_UiFree(&g_ui);
}

/* ---- the world map under the picker (editor_worldmap.h) ----------------------- */

#include "editor/editor_worldmap.h"

/* The fake App's map: what the picker asked of it. The map has tiles on
 * squares 40..60 x 40..60 only. */
static struct
{
    int area_calls;
    int area_pending;
    int polls;
    int renders;
    int publishes;
    int bad_publishes;
    int max_slot;
    int serial;
    int missing;
    int redraws;
    /* The last poll of the VIEW (a background slice of the rest of the page,
     * EDITOR_WORLDMAP_SLICE square and aligned, is counted apart). */
    int x0;
    int z0;
    int w;
    int h;
    int slice_polls;
    double zoom;
    int drawn_pixels;
    /* Map pixels over every picture since the test last zeroed it: the last
     * picture alone can be one of the well's edges, past the land. */
    int drawn_total;
    int dungeon_pixels;
    int icon_calls;
    int area;
} g_map;

static int g_view_x = 50;
static int g_view_z = 50;

static int
fake_moving_view_window(
    void* user,
    int* out_map_x,
    int* out_map_z,
    int* out_w,
    int* out_h)
{
    (void)user;
    *out_map_x = g_view_x;
    *out_map_z = g_view_z;
    *out_w = 2;
    *out_h = 2;
    return 1;
}

static int
fake_map_area(
    void* user,
    const char* name)
{
    (void)user;
    g_map.area_calls++;
    if( g_map.area_pending > 0 )
    {
        g_map.area_pending--;
        return -2;
    }
    if( strcmp(name, "*") == 0 )
        return EDITOR_UI_MAP_AREA_ALL;
    return strcmp(name, "main") == 0 ? 0 : -1;
}

static int
fake_map_poll(
    void* user,
    int area_id,
    int x0,
    int z0,
    int w,
    int h,
    double zoom,
    int* out_missing)
{
    (void)user;
    (void)zoom;
    if( area_id != 0 && area_id != EDITOR_UI_MAP_AREA_ALL )
        return -1;
    g_map.polls++;
    g_map.area = area_id;
    if( w == EDITOR_WORLDMAP_SLICE && h == EDITOR_WORLDMAP_SLICE && x0 % EDITOR_WORLDMAP_SLICE == 0
        && z0 % EDITOR_WORLDMAP_SLICE == 0 )
        g_map.slice_polls++;
    else
    {
        g_map.x0 = x0;
        g_map.z0 = z0;
        g_map.w = w;
        g_map.h = h;
    }
    *out_missing = g_map.missing;
    return g_map.serial;
}

static int
fake_map_render_tile(
    void* user,
    int area_id,
    double zoom,
    int gx0,
    int gy0,
    int size,
    uint32_t* out)
{
    (void)user;
    if( area_id != 0 && area_id != EDITOR_UI_MAP_AREA_ALL )
        return 0;
    g_map.renders++;
    g_map.zoom = zoom;
    g_map.drawn_pixels = 0;
    for( int row = 0; row < size; row++ )
    {
        int const map_z = EDITOR_PICKER_WORLD_SQUARES - 1 - Editor_PickerSquareOfPixel(zoom, gy0 + row);
        for( int col = 0; col < size; col++ )
        {
            int const map_x = Editor_PickerSquareOfPixel(zoom, gx0 + col);
            /* The surface's squares; with every area, the Ancient Cavern's
             * m27_83 too, at its true coordinate. */
            int const dungeon = area_id == EDITOR_UI_MAP_AREA_ALL && map_x == 27 && map_z == 83;
            int const on = (map_x >= 40 && map_x <= 60 && map_z >= 40 && map_z <= 60) || dungeon;
            out[row * size + col] = on ? 0xFF204080u : 0;
            g_map.drawn_pixels += on;
            g_map.drawn_total += on;
            g_map.dungeon_pixels += dungeon;
        }
    }
    return 1;
}

static int
fake_map_publish(
    void* user,
    int slot,
    const uint32_t* argb,
    int w,
    int h)
{
    (void)user;
    (void)argb;
    g_map.publishes++;
    if( w != EDITOR_WORLDMAP_TILE || h != EDITOR_WORLDMAP_TILE || slot < 0 || slot >= EDITOR_UI_MAP_IMAGE_SLOTS )
        g_map.bad_publishes++;
    if( slot > g_map.max_slot )
        g_map.max_slot = slot;
    return 0x4C000000 + slot;
}

static int
fake_map_icons(
    void* user,
    int area_id,
    int x0,
    int z0,
    int w,
    int h,
    struct EditorUiMapIcon* out,
    int capacity,
    int* out_pending)
{
    (void)user;
    (void)area_id;
    g_map.icon_calls++;
    *out_pending = 0;
    if( capacity < 2 || x0 > 50 || x0 + w <= 50 || z0 > 50 || z0 + h <= 50 )
        return 0;
    memset(out, 0, 2 * sizeof(*out));
    /* A bank booth's icon at Lumbridge castle and the town's label. */
    out[0].tile_x = 3222;
    out[0].tile_z = 3218;
    out[0].scene_id = 777;
    out[0].w = 15;
    out[0].h = 15;
    out[1].tile_x = 3222;
    out[1].tile_z = 3240;
    out[1].text_size = 2;
    snprintf(out[1].label, sizeof(out[1].label), "Lumbridge");
    return 2;
}

static void
fake_map_redraw(void* user)
{
    (void)user;
    g_map.redraws++;
}

/** GRAPH items of `kind` in `graph`'s picture (keyed only, for rects). */
static int
graph_items_of(
    int graph,
    int kind,
    int keyed)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[graph];
    int n = 0;
    for( int i = 0; i < w->graph_count; i++ )
    {
        const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[w->graph_first + i];
        if( it->kind == kind && (!keyed || it->key != 0) )
            n++;
    }
    return n;
}

/** Outline items of colour `color`; the last one's box in `out_*`. */
static int
graph_outlines_of(
    int graph,
    uint32_t color,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[graph];
    int n = 0;
    for( int i = 0; i < w->graph_count; i++ )
    {
        const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[w->graph_first + i];
        if( it->kind != TORIRS_CHROME_GRAPH_RECT_OUTLINE || it->color != color )
            continue;
        n++;
        *out_x = it->x;
        *out_y = it->y;
        *out_w = it->w;
        *out_h = it->h;
    }
    return n;
}

/** Ticks until the map has every tile of the view (or `limit`). */
static int
settle_map(int limit)
{
    int ticks = 0;
    int renders = -1;
    while( ticks < limit && renders != g_map.renders )
    {
        renders = g_map.renders;
        frame();
        ticks++;
    }
    return ticks;
}

/** Whether the underlay's pictures cover well point (x, y). */
static int
underlay_covers(
    const struct Editor_Picker* picker,
    int x,
    int y)
{
    for( int i = 0; i < picker->underlay.count; i++ )
    {
        const struct Editor_PickerImage* image = &picker->underlay.items[i];
        if( x >= image->x && x < image->x + image->w && y >= image->y && y < image->y + image->h )
            return 1;
    }
    return 0;
}

static void
test_worldmap(void)
{
    struct Editor_Picker* picker;
    struct Editor_PickerColors colors;
    int squares_panel;
    int graph;
    int x;
    int y;

    Editor_PickerDefaultColors(&colors);
    /* The Squares panel outlines the focused square white (workbench 6.4). */
    colors.selected = EDITOR_SQUARES_FOCUS_RGB;
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1280, 800);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.hooks.open_square = fake_open_square;
    g_ui.hooks.open_in_new_view = fake_open_in_new_view;
    g_ui.hooks.floor_rgb = fake_floor_rgb;
    g_ui.hooks.view_window = fake_moving_view_window;
    memset(&g_map, 0, sizeof(g_map));
    g_map.area_pending = 1;
    g_map.serial = 1;
    g_map.missing = 2;
    g_ui.app_services.worldmap_area = fake_map_area;
    g_ui.app_services.worldmap_poll = fake_map_poll;
    g_ui.app_services.worldmap_render_tile = fake_map_render_tile;
    g_ui.app_services.image_publish = fake_map_publish;
    g_ui.app_services.worldmap_icons = fake_map_icons;
    g_ui.app_services.request_redraw = fake_map_redraw;
    Editor_UiRegisterToolsPanels(&g_ui);
    ungroup_all(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    squares_panel = find_panel("Squares");
    g_squares_panel = squares_panel;
    graph = find_widget(find_panel("Square map"), TORIRS_CHROME_W_GRAPH, NULL);
    for( int i = 0; i < 3; i++ )
        frame();
    picker = (struct Editor_Picker*)edp_squares_picker(&g_ui);
    CHECK(picker && graph >= 0, "the picker is up");
    if( !picker || graph < 0 )
        return;
    search_field("m50_50");
    CHECK(!picker->underlay.active && g_map.area_calls == 1 && g_map.renders == 0,
        "the cache's areas are still loading: no map yet, asked once (%d)", g_map.area_calls);
    CHECK(g_map.redraws > 0, "...and the window is kept drawing to ask again");
    for( int i = 0; i < EDITOR_WORLDMAP_AREA_RETRY_TICKS + 1; i++ )
        frame();
    CHECK(g_map.area_calls == 2, "asked again once after the retry, then remembered (%d)", g_map.area_calls);
    settle_map(40);
    CHECK(picker->underlay.active && picker->underlay.count > 0, "the surface's map is under the grid (%d pictures)",
        picker->underlay.count);
    CHECK(graph_items_of(graph, TORIRS_CHROME_GRAPH_IMAGE, 0) >= picker->underlay.count,
        "GRAPH image items draw them");
    CHECK(graph_items_of(graph, TORIRS_CHROME_GRAPH_RECT, 1) == 0, "no square is filled over the map");
    CHECK(g_map.bad_publishes == 0 && g_map.max_slot < EDITOR_UI_MAP_IMAGE_SLOTS,
        "every picture is %dx%d in a slot below %d (an atlas renderer reuses its place)", EDITOR_WORLDMAP_TILE,
        EDITOR_WORLDMAP_TILE, EDITOR_UI_MAP_IMAGE_SLOTS);

    /* Only the visible squares are asked for. */
    {
        int vx0;
        int vz0;
        int vw;
        int vh;
        CHECK(Editor_PickerVisibleSquares(picker, &vx0, &vz0, &vw, &vh), "the well shows squares");
        CHECK(g_map.x0 == vx0 && g_map.z0 == vz0 && g_map.w == vw && g_map.h == vh,
            "the poll is the visible squares ([%d,%d %dx%d] vs [%d,%d %dx%d])", g_map.x0, g_map.z0, g_map.w, g_map.h,
            vx0, vz0, vw, vh);
        CHECK(g_map.zoom == picker->zoom, "tiles are rendered at the zoom shown (%.2f)", g_map.zoom);
    }

    /* Regions still arriving: polled every tick; tiles drawn meanwhile are
     * drawn again once the serial moves; then nothing while nothing changes. */
    {
        int const polls = g_map.polls;
        int const renders = g_map.renders;
        for( int i = 0; i < 4; i++ )
            frame();
        CHECK(g_map.polls >= polls + 3 && g_map.renders == renders,
            "while regions load: polled (%d), not redrawn with nothing new (%d)", g_map.polls - polls,
            g_map.renders - renders);
        g_map.serial = 2;
        g_map.missing = 0;
        for( int i = 0; i < EDITOR_WORLDMAP_LOADING_TICKS + 2; i++ )
            frame();
        CHECK(g_map.renders > renders, "the last regions landed: the stale tiles drawn again (%d)",
            g_map.renders - renders);
    }
    settle_map(40);
    {
        int const renders = g_map.renders;
        int const polls = g_map.polls;
        int const publishes = g_map.publishes;
        for( int i = 0; i < 20; i++ )
            frame();
        CHECK(g_map.renders == renders && g_map.polls == polls && g_map.publishes == publishes,
            "a view that does not change reuses its pictures (renders +%d, polls +%d)", g_map.renders - renders,
            g_map.polls - polls);
    }

    /* Drag pans, never selects; a still click selects and opens. */
    {
        double const origin = picker->origin_x;
        int const renders = g_map.renders;
        int const tiles = picker->underlay.count;
        memset(&g_window, 0, sizeof(g_window));
        picker_point(picker, 50, 50, &x, &y);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_DRAG, x + 2, y, 0);
        frame();
        CHECK(picker->origin_x == origin, "a move inside the slop does not pan");
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_DRAG, x + 40, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x + 40, y, 0);
        frame();
        CHECK(picker->origin_x < origin && g_window.open_calls == 0 && Editor_PickerSelectedCount(picker) <= 1,
            "a drag pans the map and opens nothing");
        CHECK(g_map.renders - renders <= tiles / 2 + 2,
            "the pan renders only the tiles it brought in (%d of %d)", g_map.renders - renders, tiles);
        settle_map(40);
    }

    /* Plain clicks: A then B leaves exactly one selection mark, at B; the
     * squares in this window's world keep their own (inset) mark, and lose
     * it when the world moves on. */
    {
        int sx = 0;
        int sy = 0;
        int sw = 0;
        int sh = 0;
        int bx;
        int by;
        int bw;
        int bh;
        int marks;
        search_field("m50_50");
        /* The open-here inset is the 40 px band's (Editor_PickerLodAt). */
        Editor_PickerZoomTo(picker, 48.0, 0, 0);
        Editor_PickerCentre(picker, 50, 50);
        settle_map(40);
        picker_point(picker, 50, 50, &x, &y);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x, y, 0);
        frame();
        for( int i = 0; i < 40; i++ )
            frame(); /* not a double-click */
        picker_point(picker, 51, 50, &x, &y);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x, y, 0);
        frame();
        marks = graph_outlines_of(graph, colors.selected, &sx, &sy, &sw, &sh);
        Editor_PickerCellBox(picker, 51, 50, &bx, &by, &bw, &bh);
        CHECK(marks == 1 && sx == bx && sy == by, "click A then B: one selection mark, at B (%d marks, at %d,%d vs %d,%d)",
            marks, sx, sy, bx, by);
        CHECK(graph_outlines_of(graph, colors.open, &sx, &sy, &sw, &sh) >= 1,
            "the squares in this window's world have their own mark");
        {
            const struct ToriRSChromeWidget* wg = &g_chrome.widgets[graph];
            int frame_at = -1;
            int mark_at = -1;
            for( int i = 0; i < wg->graph_count; i++ )
            {
                const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[wg->graph_first + i];
                if( it->kind == TORIRS_CHROME_GRAPH_RECT_OUTLINE && it->color == colors.self_frame )
                    frame_at = i;
                if( it->kind == TORIRS_CHROME_GRAPH_RECT_OUTLINE && it->color == colors.selected )
                    mark_at = i;
            }
            CHECK(frame_at >= 0 && mark_at > frame_at,
                "the selection mark is drawn over this window's frame, never hidden by it (%d after %d)", mark_at,
                frame_at);
        }
        g_view_x = 30;
        g_view_z = 30;
        frame();
        frame();
        CHECK(graph_outlines_of(graph, colors.open, &sx, &sy, &sw, &sh) == 0,
            "...gone once the world moved on (the mirror still holds them)");
        g_view_x = 50;
        g_view_z = 50;
    }

    /* A double-click opens in place: shift-click (adds), then click again. */
    {
        memset(&g_window, 0, sizeof(g_window));
        for( int i = 0; i < 40; i++ )
            frame();
        picker_point(picker, 50, 50, &x, &y);
        g_ui.shift_held = 1;
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x, y, 0);
        frame();
        g_ui.shift_held = 0;
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x, y, 0);
        frame();
        CHECK(g_window.open_calls == 2 && g_window.open_x == 50 && !g_window.open_add,
            "shift-click adds, the double-click's second click opens m50_50 in place (%d calls, add %d)",
            g_window.open_calls, g_window.open_add);
    }

    /* Keys while the well has focus: + zooms in about the middle, Left pans. */
    {
        double const zoom = picker->zoom;
        double origin;
        ToriRSChrome_FocusWidget(&g_chrome, graph, 0);
        CHECK(ToriRSChrome_KeyChar(&g_chrome, '+'), "the focused well takes '+'");
        frame();
        CHECK(fabs(picker->zoom - zoom * EDITOR_PICKER_ZOOM_STEP) < 1e-9, "'+' zooms in a notch (%.2f -> %.2f)", zoom,
            picker->zoom);
        CHECK(ToriRSChrome_KeyChar(&g_chrome, '-'), "...and '-'");
        frame();
        CHECK(fabs(picker->zoom - zoom) < 1e-9, "'-' zooms back out (%.2f)", picker->zoom);
        origin = picker->origin_x;
        {
            int fx = -1;
            int fz = -1;
            int gx = -1;
            int gz = -1;
            (void)origin;
            CHECK(Editor_PickerFocusCell(picker, &fx, &fz), "...a square is focused");
            CHECK(ToriRSChrome_KeyEdit(&g_chrome, TORIRS_CHROME_KEY_LEFT), "...and Left");
            frame();
            CHECK(Editor_PickerFocusCell(picker, &gx, &gz) && gx == fx - 1 && gz == fz,
                "Left moves the focus a square west (m%d_%d -> m%d_%d)", fx, fz, gx, gz);
        }
        CHECK(!ToriRSChrome_KeyChar(&g_chrome, 'q'), "a letter is not the well's");
        ToriRSChrome_FocusWidget(&g_chrome, -1, 0);
    }

    /* A zoom: the old zoom's pictures stand in, scaled, while the new ones
     * arrive within the budget; then the new ones alone, sharp. */
    {
        int pictures;
        search_field("m50_50");
        settle_map(40);
        picker_point(picker, 50, 50, &x, &y);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_WHEEL, x, y, -1);
        frame();
        pictures = picker->underlay.count;
        CHECK(pictures > 0 && underlay_covers(picker, x, y), "the frame after a zoom still shows the map there (%d pictures)",
            pictures);
        settle_map(60);
        CHECK(g_map.zoom == picker->zoom, "then this zoom's tiles are drawn (%.2f)", g_map.zoom);
        {
            int stand_ins = 0;
            for( int i = 0; i < picker->underlay.count; i++ )
                stand_ins += picker->underlay.items[i].w != EDITOR_WORLDMAP_TILE;
            CHECK(stand_ins == 0, "...and no scaled stand-in is left (%d)", stand_ins);
        }
    }

    /* 48 px a square, the zoom the owner saw blank: pictures over the well,
     * non-black where regions exist. */
    {
        int vx0;
        int vz0;
        int vw;
        int vh;
        Editor_PickerZoomTo(picker, 48.0, x, y);
        Editor_PickerCentre(picker, 49, 43);
        g_map.drawn_total = 0;
        frame();
        settle_map(80);
        picker_point(picker, 49, 43, &x, &y);
        CHECK(fabs(picker->zoom - 48.0) < 1e-9 && underlay_covers(picker, x, y) && g_map.zoom == 48.0,
            "48 px a square: m49_43 is under a picture");
        CHECK(g_map.drawn_total > 0, "...and the pictures drawn have map in them (%d px)", g_map.drawn_total);
        CHECK(Editor_PickerVisibleSquares(picker, &vx0, &vz0, &vw, &vh) && g_map.w == vw, "...polled for the view");
        CHECK(g_map.bad_publishes == 0, "every picture still the one size after %d publishes", g_map.publishes);
    }

    /* Icons and names: the closest band's (from 96 px a square), placed at
     * their tiles. */
    {
        int gox;
        int goy;
        int cx;
        int cy;
        Editor_PickerZoomTo(picker, 128.0, x, y);
        Editor_PickerCentre(picker, 50, 50);
        settle_map(60);
        Editor_PickerGlobalOrigin(picker, &gox, &goy);
        cx = (int)floor((3222 + 0.5) * picker->zoom / 64.0) - gox;
        cy = (int)floor((EDITOR_PICKER_WORLD_SQUARES * 64 - 1 - 3218 + 0.5) * picker->zoom / 64.0) - goy;
        CHECK(g_map.icon_calls > 0 && picker->underlay.icon_count == 1, "128 px a square: the icon is drawn (%d)",
            picker->underlay.icon_count);
        CHECK(picker->underlay.icon_count == 1 && picker->underlay.icons[0].x == cx - 7
                  && picker->underlay.icons[0].y == cy - 7 && picker->underlay.icons[0].trans == 0,
            "...centred on tile 3222,3218, opaque (%d,%d vs %d,%d)", picker->underlay.icons[0].x,
            picker->underlay.icons[0].y, cx - 7, cy - 7);
        CHECK(picker->underlay.label_count == 1 && strcmp(picker->underlay.labels[0].text, "Lumbridge") == 0,
            "...and the town's label");
        CHECK(graph_items_of(graph, TORIRS_CHROME_GRAPH_IMAGE, 0) == picker->underlay.count + 1,
            "the icon is a GRAPH image over the map's");
        Editor_PickerZoomTo(picker, 60.0, x, y);
        Editor_PickerCentre(picker, 50, 50);
        settle_map(60);
        CHECK(picker->underlay.icon_count == 0 && picker->underlay.label_count == 0
                  && graph_items_of(graph, TORIRS_CHROME_GRAPH_IMAGE, 0) == picker->underlay.count,
            "60 px a square: no icon, no name (the 6.4 photograph check)");
        Editor_PickerZoomTo(picker, 20.0, x, y);
        Editor_PickerCentre(picker, 50, 50);
        settle_map(60);
        CHECK(picker->underlay.icon_count == 0 && picker->underlay.label_count == 0,
            "20 px a square: no icons, no labels");
    }

    /* A click on an empty square opens New square on it. */
    {
        struct Editor_PickerSearch search;
        int new_panel;
        CHECK(Editor_PickerFind(picker, 55, 55, -1) < 0, "m55_55 is empty in the test tree");
        CHECK(Editor_PickerSearch(picker, "m55_55", &search), "find m55_55");
        Editor_PickerShow(picker, &search);
        frame();
        for( int i = 0; i < 40; i++ )
            frame();
        CHECK(picker_point(picker, 55, 55, &x, &y), "m55_55 is in view");
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x, y, 0);
        frame();
        new_panel = find_panel("New square");
        CHECK(new_panel >= 0 && g_chrome.panels[new_panel].visible, "a click on the empty square opens New square");
        if( new_panel >= 0 )
        {
            int const input_x = find_widget(new_panel, TORIRS_CHROME_W_TEXTINPUT, "Map x");
            int const input_z = find_widget(new_panel, TORIRS_CHROME_W_TEXTINPUT, "Map z");
            CHECK(input_x >= 0 && strcmp(ToriRSChrome_Text(&g_chrome, input_x), "55") == 0 && input_z >= 0
                      && strcmp(ToriRSChrome_Text(&g_chrome, input_z), "55") == 0,
                "...prefilled with m55_55");
            activate(find_widget(new_panel, TORIRS_CHROME_W_BUTTON, "Cancel"));
        }
        CHECK(picker->underlay.active, "the map stays under the grid");
    }

    /* The Full map: every square of the grid where it is, every area's
     * imagery composed onto it -- the dungeon square at its true place. */
    {
        int const area = find_widget(squares_panel, TORIRS_CHROME_W_DROPDOWN, "Area");
        int min_x = 1 << 30;
        int max_x = -1;
        int min_z = 1 << 30;
        int max_z = -1;
        int dungeon;
        int vx0;
        int vz0;
        int vw;
        int vh;
        for( int i = 0; i < picker->square_count; i++ )
        {
            min_x = picker->squares[i].map_x < min_x ? picker->squares[i].map_x : min_x;
            max_x = picker->squares[i].map_x > max_x ? picker->squares[i].map_x : max_x;
            min_z = picker->squares[i].map_z < min_z ? picker->squares[i].map_z : min_z;
            max_z = picker->squares[i].map_z > max_z ? picker->squares[i].map_z : max_z;
        }
        ToriRSChrome_DropdownSetSelected(&g_chrome, area, 1);
        activate(area);
        CHECK(picker->tabs[picker->active_tab].area == EDITOR_PICKER_TAB_FULL, "the Full map page is shown");
        CHECK(picker->tabs[1].count == picker->square_count && picker->tabs[1].min_x == min_x
                  && picker->tabs[1].max_x == max_x && picker->tabs[1].min_z == min_z && picker->tabs[1].max_z == max_z,
            "its extent is every square of the atlas (%d squares, x %d..%d, z %d..%d)", picker->tabs[1].count,
            picker->tabs[1].min_x, picker->tabs[1].max_x, picker->tabs[1].min_z, picker->tabs[1].max_z);
        settle_map(80);
        CHECK(Editor_PickerVisibleSquares(picker, &vx0, &vz0, &vw, &vh) && vx0 <= min_x && vz0 <= min_z
                  && vx0 + vw > max_x && vz0 + vh > max_z,
            "fitted, the whole grid is in the well ([%d,%d %dx%d])", vx0, vz0, vw, vh);
        dungeon = Editor_PickerFind(picker, 27, 83, -1);
        g_map.dungeon_pixels = 0;
        /* Fitted the page is under 12 px a square, colours only: the map
         * is asked for from the picture band. */
        Editor_PickerZoomTo(picker, 48.0, x, y);
        Editor_PickerCentre(picker, 27, 83);
        settle_map(80);
        CHECK(g_map.area == EDITOR_UI_MAP_AREA_ALL, "every area is asked for at once");
        picker_point(picker, 27, 83, &x, &y);
        CHECK(dungeon >= 0 && Editor_PickerSquareAt(picker, x, y) == dungeon,
            "the Ancient Cavern's m27_83 is on the grid at 27,83");
        CHECK(g_map.dungeon_pixels > 0 && underlay_covers(picker, x, y),
            "...with its imagery drawn there (%d px)", g_map.dungeon_pixels);
    }

    /* The Unmapped page has no area: no map, the cells are filled. */
    {
        int const area = find_widget(squares_panel, TORIRS_CHROME_W_DROPDOWN, "Area");
        int const last = picker->tab_count - 1;
        CHECK(picker->tabs[last].area < 0, "the last page is Unmapped");
        ToriRSChrome_DropdownSetSelected(&g_chrome, area, last);
        activate(area);
        frame();
        CHECK(!picker->underlay.active && graph_items_of(graph, TORIRS_CHROME_GRAPH_IMAGE, 0) == 0, "Unmapped: no map");
        CHECK(graph_items_of(graph, TORIRS_CHROME_GRAPH_RECT, 1) > 0, "...its squares filled as ever");
    }

    /* No map services (the headless App, the web): the picker as before. */
    {
        int const area = find_widget(squares_panel, TORIRS_CHROME_W_DROPDOWN, "Area");
        ToriRSChrome_DropdownSetSelected(&g_chrome, area, 0);
        activate(area);
        /* The page fits its squares; in the 1280x800 column's short well that
         * can be under 12 px a square, colours only. The map is the picture
         * band's: zoom into it, on m50_50 so squares are in view. */
        Editor_PickerZoomTo(picker, 32.0, picker->well_w / 2, picker->well_h / 2);
        Editor_PickerCentre(picker, 50, 50);
        settle_map(60);
        CHECK(picker->underlay.active, "back on the surface: the map again (page %d, %.2f px a square, area %d, %d renders, well %dx%d, widget %dx%d)",
            picker->active_tab, picker->zoom, g_map.area, g_map.renders, picker->well_w, picker->well_h,
            g_chrome.widgets[graph].w, g_chrome.widgets[graph].h);
        g_ui.app_services.worldmap_render_tile = NULL;
        frame();
        CHECK(!picker->underlay.active && graph_items_of(graph, TORIRS_CHROME_GRAPH_RECT, 1) > 0,
            "without the services the cells are filled, no picture");
    }
    printf("worldmap: %d area asks, %d polls, %d tile renders, %d publishes (slots to %d), %d icon asks\n",
        g_map.area_calls, g_map.polls, g_map.renders, g_map.publishes, g_map.max_slot, g_map.icon_calls);

    Editor_UiFree(&g_ui);
}

/* ---- the default layout and the dock grid ---------------------------------------- */

static int
rects_overlap(
    struct ToriRSChromeRect a,
    struct ToriRSChromeRect b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

/** A panel that is part of the layout: visible, built, not a popup menu, and
 *  not the World panel (it IS the world; layout_check checks it apart). */
static int
layout_panel(int i)
{
    const struct ToriRSChromePanel* p = &g_chrome.panels[i];
    return p->visible && p->last_rect.w > 0 && p->last_rect.h > 0 && p->style != TORIRS_CHROME_PANEL_MENU
           && !p->modal && i != g_ui.world_panel;
}

static void
layout_boot(
    int w,
    int h,
    int scale)
{
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetScale(&g_chrome, scale);
    ToriRSChrome_SetSurface(&g_chrome, w, h);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.hooks.open_square = fake_open_square;
    g_ui.hooks.open_in_new_view = fake_open_in_new_view;
    g_ui.hooks.floor_rgb = fake_floor_rgb;
    g_ui.hooks.floor_request = fake_floor_request;
    g_ui.hooks.view_window = fake_view_window;
    g_ui.hooks.selection_changed = fake_selection_changed;
    Editor_UiRegisterToolsPanels(&g_ui);
    ungroup_all(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    for( int i = 0; i < 4; i++ )
        frame();
}

/** No two layout panels overlap, none covers the world, all are on the
 *  surface, and the world is the largest rectangle. */
static void
layout_check(const char* what)
{
    struct EditorUiRect const world = Editor_UiWorldRect(&g_ui);
    struct ToriRSChromeRect world_r;
    long const world_area = (long)world.w * world.h;
    long const column_area = (long)g_ui.dock_w * (g_chrome.surface_h - g_ui.dock_top);
    int shown = 0;

    world_r.x = world.x;
    world_r.y = world.y;
    world_r.w = world.w;
    world_r.h = world.h;
    CHECK(g_ui.laid_out && world.w > 0 && world.h > 0, "%s: laid out, world %dx%d", what, world.w, world.h);
    CHECK(world_area > column_area, "%s: the world (%ld) is larger than the dock column (%ld)", what,
        world_area, column_area);
    for( int i = 0; i < g_chrome.panel_count; i++ )
    {
        struct ToriRSChromeRect const r = g_chrome.panels[i].last_rect;
        if( !layout_panel(i) )
            continue;
        shown++;
        CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= g_chrome.surface_w && r.y + r.h <= g_chrome.surface_h,
            "%s: '%s' %d,%d %dx%d is on the surface", what, g_chrome.panels[i].title, r.x, r.y, r.w, r.h);
        CHECK(!rects_overlap(r, world_r), "%s: '%s' does not cover the world", what, g_chrome.panels[i].title);
        CHECK((long)r.w * r.h < world_area, "%s: '%s' is smaller than the world", what,
            g_chrome.panels[i].title);
        for( int j = i + 1; j < g_chrome.panel_count; j++ )
            if( layout_panel(j) )
                CHECK(!rects_overlap(r, g_chrome.panels[j].last_rect), "%s: '%s' and '%s' do not overlap",
                    what, g_chrome.panels[i].title, g_chrome.panels[j].title);
    }
    CHECK(shown >= 4, "%s: the menu bar, the rail and at least two panels show (%d)", what, shown);
    /* The world keeps at least EDP_WORLD_MIN_PERCENT of the surface at every
     * size from 960x600 logical up (the console strip is capped, then
     * dropped, to hold it); below that the dock's 200 px floor and the rail
     * alone take more than the rest. */
    if( g_chrome.surface_w / g_chrome.scale >= 960 && g_chrome.surface_h / g_chrome.scale >= 600 )
        CHECK(world_area * 100 >= (long)g_chrome.surface_w * g_chrome.surface_h * EDP_WORLD_MIN_PERCENT,
            "%s: the world (%ld) keeps %d%% of the surface (%ld%%)", what, world_area, EDP_WORLD_MIN_PERCENT,
            world_area * 100 / ((long)g_chrome.surface_w * g_chrome.surface_h));
    /* The status bar: a strip the width of the surface along its bottom, and
     * everything else ends above it. */
    {
        struct EditorUiPanel const* sb = Editor_UiFindPanel(&g_ui, "statusbar");
        CHECK(sb && sb->chrome_panel >= 0 && g_chrome.panels[sb->chrome_panel].visible, "%s: the status bar shows", what);
        if( sb && sb->chrome_panel >= 0 )
        {
            struct ToriRSChromeRect const r = g_chrome.panels[sb->chrome_panel].last_rect;
            CHECK(r.x == 0 && r.w == g_chrome.surface_w && r.h > 0 && r.y + r.h == g_chrome.surface_h,
                "%s: the status bar %d,%d %dx%d spans the bottom", what, r.x, r.y, r.w, r.h);
            CHECK(world.y + world.h <= r.y && g_ui.dock_bottom <= r.y, "%s: the world and the dock end above it", what);
            for( int i = 0; i < g_chrome.panel_count; i++ )
                if( layout_panel(i) && i != sb->chrome_panel )
                    CHECK(g_chrome.panels[i].last_rect.y + g_chrome.panels[i].last_rect.h <= r.y,
                        "%s: '%s' ends above the status bar", what, g_chrome.panels[i].title);
        }
    }
    /* The tool rail never runs off the bottom: it wraps into columns. */
    {
        int const rail = find_panel("Tools");
        struct EditorUiPanel const* sb = Editor_UiFindPanel(&g_ui, "statusbar");
        int const strip_top = sb && sb->chrome_panel >= 0 && g_chrome.panels[sb->chrome_panel].visible
                                  ? g_chrome.panels[sb->chrome_panel].last_rect.y
                                  : g_chrome.surface_h;
        CHECK(rail >= 0 && g_chrome.panels[rail].last_rect.y + g_chrome.panels[rail].last_rect.h <= strip_top,
            "%s: the tool rail ends above the status bar", what);
    }
    /* Compact: every default dock member is in the column -- expanded or as
     * its title bar -- and a collapsed one is exactly its collapsed height.
     * A member of a panel group is its group's frame's (never collapsed: its
     * strip is the tabs), so only the ungrouped ones are asked about. */
    if( Editor_UiCompact(&g_ui) )
    {
        static const char* const k_members[] = { "terrain", "palette", "squares", "areas", "history" };
        int collapsed = 0;
        for( int m = 0; m < 5; m++ )
        {
            struct EditorUiPanel const* p = Editor_UiFindPanel(&g_ui, k_members[m]);
            int const cp = p ? p->chrome_panel : -1;
            if( cp < 0 || !g_ui.member[cp - g_ui.panel_first] || g_ui.group_plus1[p - g_ui.panels] )
                continue;
            CHECK(g_chrome.panels[cp].visible && g_ui.docked[cp - g_ui.panel_first],
                "%s: compact: %s is in the column", what, k_members[m]);
            if( ToriRSChrome_PanelIsCollapsed(&g_chrome, cp) )
            {
                collapsed++;
                CHECK(g_chrome.panels[cp].last_rect.h == ToriRSChrome_PanelCollapsedHeight(&g_chrome, cp),
                    "%s: compact: %s collapsed is its title bar (%d px)", what, k_members[m],
                    g_chrome.panels[cp].last_rect.h);
            }
        }
        printf("layout %s: compact, %d collapsed\n", what, collapsed);
    }
    /* The World panel docks over exactly the region the others leave, and
     * the world rect is its body: inside it, below its title bar. */
    {
        int const wp = find_panel("World");
        CHECK(wp >= 0 && wp == g_ui.world_panel && g_chrome.panels[wp].visible, "%s: the World panel shows", what);
        if( wp >= 0 )
        {
            struct ToriRSChromeRect const r = g_chrome.panels[wp].last_rect;
            struct ToriRSChromeRect const body = ToriRSChrome_PanelBodyRect(&g_chrome, wp);
            CHECK(r.x == g_ui.world.x && r.y == g_ui.world.y && r.w == g_ui.world.w && r.h == g_ui.world.h,
                "%s: World %d,%d %dx%d docks over the region %d,%d %dx%d", what, r.x, r.y, r.w, r.h, g_ui.world.x,
                g_ui.world.y, g_ui.world.w, g_ui.world.h);
            CHECK(world.x == body.x && world.y == body.y && world.w == body.w && world.h == body.h,
                "%s: the world rect is the World panel's body", what);
            CHECK(world.y > r.y && world.x >= r.x && world.x + world.w <= r.x + r.w && world.y + world.h <= r.y + r.h,
                "%s: the body sits under the title bar, inside the panel", what);
            CHECK(g_chrome.panels[wp].hollow, "%s: World is hollow", what);
        }
    }
    printf("layout %s: world %d,%d %dx%d, column x %d w %d:", what, world.x, world.y, world.w, world.h,
        g_ui.dock_x, g_ui.dock_w);
    for( int i = 0; i < g_chrome.panel_count; i++ )
        if( layout_panel(i) && g_chrome.panels[i].style == TORIRS_CHROME_PANEL_WINDOW )
            printf(" %s@%d,%d %dx%d", g_chrome.panels[i].title, g_chrome.panels[i].last_rect.x,
                g_chrome.panels[i].last_rect.y, g_chrome.panels[i].last_rect.w, g_chrome.panels[i].last_rect.h);
    printf("\n");
    CHECK(find_panel("Terrain") >= 0 && g_chrome.panels[find_panel("Terrain")].visible,
        "%s: Terrain heads the column", what);
}

static void
drag(
    int from_x,
    int from_y,
    int to_x,
    int to_y)
{
    ToriRSChrome_MouseMove(&g_chrome, from_x, from_y);
    (void)ToriRSChrome_MouseDown(&g_chrome, from_x, from_y);
    ToriRSChrome_MouseMove(&g_chrome, (from_x + to_x) / 2, (from_y + to_y) / 2);
    frame();
    ToriRSChrome_MouseMove(&g_chrome, to_x, to_y);
    (void)ToriRSChrome_MouseUp(&g_chrome, to_x, to_y);
    frame();
}

static void
test_layout(void)
{
    /* Desktops and laptops at 1x (1366x768 and below are the compact ones),
     * and the HighDPI surfaces at 2x: a 1440x900 MacBook is 2880x1800. */
    static const int sizes_1x[][2] = { { 1280, 800 }, { 1530, 1006 }, { 1920, 1080 }, { 2560, 1440 },
                                       { 1366, 768 }, { 1280, 720 },  { 1024, 600 },  { 960, 600 } };
    static const int sizes_2x[][2] = { { 1280, 800 }, { 1530, 1006 }, { 1920, 1080 }, { 2560, 1440 }, { 2880, 1800 } };
    char what[64];

    for( int s = 0; s < (int)(sizeof(sizes_1x) / sizeof(sizes_1x[0])); s++ )
    {
        layout_boot(sizes_1x[s][0], sizes_1x[s][1], 1);
        snprintf(what, sizeof(what), "%dx%d at 1x", sizes_1x[s][0], sizes_1x[s][1]);
        CHECK(Editor_UiCompact(&g_ui) == (sizes_1x[s][1] < EDP_COMPACT_H || sizes_1x[s][0] < EDP_COMPACT_W),
            "%s: compact is automatic below %dx%d logical (%d)", what, EDP_COMPACT_W, EDP_COMPACT_H,
            Editor_UiCompact(&g_ui));
        layout_check(what);
        Editor_UiFree(&g_ui);
    }
    for( int s = 0; s < (int)(sizeof(sizes_2x) / sizeof(sizes_2x[0])); s++ )
    {
        layout_boot(sizes_2x[s][0], sizes_2x[s][1], 2);
        snprintf(what, sizeof(what), "%dx%d at 2x", sizes_2x[s][0], sizes_2x[s][1]);
        layout_check(what);
        Editor_UiFree(&g_ui);
    }
    CHECK(!Editor_UiCompact(&g_ui) || 1, "a 2880x1800 surface at 2x is a 1440x900 window: not compact");

    /* The compact dock at 1024x600: every member in the column, the ones the
     * column cannot hold at their minimum collapsed to their title bars; a
     * title click expands a collapsed panel (collapsing the least recently
     * expanded when there is no room) and collapses an expanded one. */
    {
        int first_collapsed = -1;
        int first_expanded = -1;
        int expanded_before = 0;
        int expanded_after = 0;

        layout_boot(1024, 600, 1);
        CHECK(Editor_UiCompact(&g_ui), "1024x600 is compact");
        for( int i = g_ui.panel_first; i < g_ui.panel_end; i++ )
        {
            if( !g_ui.docked[i - g_ui.panel_first] || g_chrome.panels[i].style != TORIRS_CHROME_PANEL_WINDOW
                || i == g_ui.world_panel || i == find_panel("Output") )
                continue;
            if( ToriRSChrome_PanelIsCollapsed(&g_chrome, i) )
            {
                if( first_collapsed < 0 )
                    first_collapsed = i;
            }
            else
            {
                expanded_before++;
                if( first_expanded < 0 )
                    first_expanded = i;
            }
        }
        CHECK(first_collapsed >= 0 && first_expanded >= 0, "1024x600: the column has expanded (%d) and collapsed panels",
            expanded_before);
        if( first_collapsed >= 0 )
        {
            struct ToriRSChromeRect const r = g_chrome.panels[first_collapsed].last_rect;
            ToriRSChrome_MouseMove(&g_chrome, r.x + 30, r.y + 6);
            (void)ToriRSChrome_MouseDown(&g_chrome, r.x + 30, r.y + 6);
            (void)ToriRSChrome_MouseUp(&g_chrome, r.x + 30, r.y + 6);
            frame();
            frame();
            CHECK(!ToriRSChrome_PanelIsCollapsed(&g_chrome, first_collapsed), "a title click expands '%s'",
                g_chrome.panels[first_collapsed].title);
            CHECK(!g_chrome.panels[first_collapsed].user_placed, "and the click did not place it");
            for( int i = g_ui.panel_first; i < g_ui.panel_end; i++ )
                if( g_ui.docked[i - g_ui.panel_first] && g_chrome.panels[i].style == TORIRS_CHROME_PANEL_WINDOW
                    && i != g_ui.world_panel && i != find_panel("Output") && !ToriRSChrome_PanelIsCollapsed(&g_chrome, i) )
                    expanded_after++;
            CHECK(expanded_after <= expanded_before + 1, "expanding one collapsed another when there was no room (%d -> %d)",
                expanded_before, expanded_after);
            layout_check("1024x600 after expanding a collapsed panel");
        }
        if( first_expanded >= 0 && !ToriRSChrome_PanelIsCollapsed(&g_chrome, first_expanded) )
        {
            struct ToriRSChromeRect const r = g_chrome.panels[first_expanded].last_rect;
            ToriRSChrome_MouseMove(&g_chrome, r.x + 30, r.y + 6);
            (void)ToriRSChrome_MouseDown(&g_chrome, r.x + 30, r.y + 6);
            (void)ToriRSChrome_MouseUp(&g_chrome, r.x + 30, r.y + 6);
            frame();
            frame();
            CHECK(ToriRSChrome_PanelIsCollapsed(&g_chrome, first_expanded), "a title click collapses '%s'",
                g_chrome.panels[first_expanded].title);
            layout_check("1024x600 after collapsing a panel");
        }
        /* The saved layout carries the collapsed state and the setting. */
        {
            struct EditorLayoutFile file;
            const struct EditorLayoutPanel* sp;
            Editor_UiLayoutCapture(&g_ui, &file);
            sp = first_expanded >= 0 ? EditorLayout_Find(&file, "terrain") : NULL;
            CHECK(file.has_compact && file.compact == -1, "the layout file says compact = -1 (automatic)");
            CHECK(sp && sp->has_collapsed, "the layout file states each panel's collapsed state");
        }
        Editor_UiFree(&g_ui);
    }

    /* View > Compact panels on a large surface forces the compact dock; again
     * turns it off; Reset layout returns to automatic. */
    {
        int view = -1;
        layout_boot(1920, 1080, 1);
        CHECK(!Editor_UiCompact(&g_ui), "1920x1080 is not compact");
        for( int i = 0; i < g_chrome.widget_count; i++ )
            if( g_chrome.widgets[i].kind == TORIRS_CHROME_W_DROPDOWN && g_chrome.widgets[i].menu_mode
                && strcmp(g_chrome.widgets[i].text, "View") == 0 )
                view = i;
        CHECK(view >= 0, "the View menu exists");
        if( view >= 0 )
        {
            int compact_row = 0;
            for( int r = 0; r < g_chrome.widgets[view].option_count; r++ )
                if( strstr(g_chrome.widgets[view].options[r], "Compact panels") )
                    compact_row = r;
            CHECK(strncmp(g_chrome.widgets[view].options[compact_row], "Compact panels: off (auto)", 26) == 0
                      || strstr(g_chrome.widgets[view].options[compact_row], "Compact panels") != NULL,
                "the View menu has a Compact panels row (%s)", g_chrome.widgets[view].options[compact_row]);
            ToriRSChrome_DropdownSetSelected(&g_chrome, view, compact_row);
            activate(view);
            frame();
            frame();
            CHECK(Editor_UiCompact(&g_ui) && g_ui.compact_mode == 1, "View > Compact panels turns it on");
            layout_check("1920x1080 compact by the View menu");
            ToriRSChrome_DropdownSetSelected(&g_chrome, view, compact_row);
            activate(view);
            frame();
            frame();
            CHECK(!Editor_UiCompact(&g_ui) && g_ui.compact_mode == 0, "and off again");
            Editor_UiResetLayout(&g_ui);
            frame();
            CHECK(g_ui.compact_mode == -1, "Reset layout returns compact to automatic");
        }
        /* Ctrl+Shift+C is the chord. */
        CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_COMPACT) && g_ui.compact_mode == 1,
            "the Compact panels action toggles it");
        Editor_UiResetLayout(&g_ui);
        Editor_UiFree(&g_ui);
    }

    /* A frame resize lays out again; so does every size in between. */
    layout_boot(1280, 800, 1);
    ToriRSChrome_SetSurface(&g_chrome, 1920, 1080);
    frame();
    frame();
    CHECK(g_ui.layout_surface_w == 1920, "a surface resize lays out again");
    layout_check("resized to 1920x1080");

    /* A dropped panel snaps its origin to the grid, and it stays put. */
    {
        int const terrain = find_panel("Terrain");
        int const palette = find_panel("Palette");
        int const grid = EDP_GRID;
        struct EditorUiRect const world = Editor_UiWorldRect(&g_ui);
        struct ToriRSChromeRect r = g_chrome.panels[terrain].last_rect;
        int const to_x = world.x + 333;
        int const to_y = world.y + 211;

        CHECK(g_ui.snap && g_chrome.snap_grid == grid, "snapping is on by default (grid %d)", g_chrome.snap_grid);
        drag(r.x + 30, r.y + 6, to_x + 30, to_y + 6);
        CHECK(g_chrome.panels[terrain].user_placed, "the drag marked Terrain as the user's");
        CHECK(g_chrome.panels[terrain].x % grid == 0 && g_chrome.panels[terrain].y % grid == 0
                  && abs(g_chrome.panels[terrain].x - to_x) <= grid / 2
                  && abs(g_chrome.panels[terrain].y - to_y) <= grid / 2,
            "dropped at %d,%d, Terrain landed on the grid at %d,%d", to_x, to_y, g_chrome.panels[terrain].x,
            g_chrome.panels[terrain].y);

        /* Dropped a few pixels off Terrain's right edge, level with it: it
         * abuts and aligns instead of landing on a grid line. */
        r = g_chrome.panels[terrain].last_rect;
        {
            struct ToriRSChromeRect const p = g_chrome.panels[palette].last_rect;
            int const want_x = r.x + r.w + 5;
            int const want_y = r.y + 3;
            drag(p.x + 30, p.y + 6, want_x + 30, want_y + 6);
            /* Aligned with Terrain's top, or with its bottom when that edge
             * is the nearer one (the two heights come from the layout's
             * share of the column, which every docked panel moves). */
            CHECK(g_chrome.panels[palette].x == r.x + r.w
                      && (g_chrome.panels[palette].y == r.y
                          || g_chrome.panels[palette].y + p.h == r.y + r.h),
                "Palette dropped at %d,%d snapped to Terrain's edge (%d,%d), at %d,%d", want_x, want_y,
                r.x + r.w, r.y, g_chrome.panels[palette].x, g_chrome.panels[palette].y);
        }

        /* A resize rounds the size to the grid. */
        r = g_chrome.panels[terrain].last_rect;
        drag(r.x + r.w - 4, r.y + r.h - 4, r.x + r.w - 4 + 37, r.y + r.h - 4 - 45);
        CHECK(g_chrome.panels[terrain].fixed_w % grid == 0 && g_chrome.panels[terrain].fixed_h % grid == 0
                  && g_chrome.panels[terrain].fixed_w != r.w,
            "Terrain resized from %dx%d to %dx%d, on the grid", r.w, r.h, g_chrome.panels[terrain].fixed_w,
            g_chrome.panels[terrain].fixed_h);

        /* A resize of the frame leaves the dragged panel where it was. */
        r = g_chrome.panels[terrain].last_rect;
        ToriRSChrome_SetSurface(&g_chrome, 2560, 1440);
        frame();
        frame();
        CHECK(g_chrome.panels[terrain].x == r.x && g_chrome.panels[terrain].y == r.y,
            "a dragged panel keeps its place through a frame resize");

        /* Snap off: a drop lands where it was let go. */
        Editor_UiSetSnap(&g_ui, 0);
        frame();
        r = g_chrome.panels[terrain].last_rect;
        drag(r.x + 30, r.y + 6, r.x + 30 + 13, r.y + 6 + 7);
        CHECK(g_chrome.panels[terrain].x == r.x + 13 && g_chrome.panels[terrain].y == r.y + 7,
            "with Snap to grid off a drop is exact");
        Editor_UiSetSnap(&g_ui, 1);

        /* View > Snap to grid toggles it; View > Reset layout takes it all
         * back. Both are rows after the panel rows of the View menu. */
        {
            int view = -1;
            int snap_row = -1;
            int reset_row = -1;
            for( int i = 0; i < g_chrome.widget_count; i++ )
                if( g_chrome.widgets[i].kind == TORIRS_CHROME_W_DROPDOWN && g_chrome.widgets[i].menu_mode
                    && strcmp(g_chrome.widgets[i].text, "View") == 0 )
                    view = i;
            CHECK(view >= 0, "the menu bar has a View menu");
            if( view >= 0 )
            {
                int const rows = g_chrome.widgets[view].option_count;
                reset_row = rows - 2;
                snap_row = rows - 1;
                ToriRSChrome_DropdownSetSelected(&g_chrome, view, snap_row);
                activate(view);
                CHECK(!g_ui.snap && g_chrome.snap_grid == 0, "View > Snap to grid turns snapping off");
                ToriRSChrome_DropdownSetSelected(&g_chrome, view, snap_row);
                activate(view);
                CHECK(g_ui.snap && g_chrome.snap_grid == EDP_GRID, "and on again");
                ToriRSChrome_DropdownSetSelected(&g_chrome, view, reset_row);
                activate(view);
            }
            else
                Editor_UiResetLayout(&g_ui);
        }
        frame();
        frame();
        CHECK(!g_chrome.panels[terrain].user_placed && g_chrome.panels[terrain].x == g_ui.dock_x
                  && g_chrome.panels[terrain].y == g_ui.dock_top,
            "Reset layout puts Terrain back at the head of the column (%d,%d)", g_chrome.panels[terrain].x,
            g_chrome.panels[terrain].y);
        layout_check("after Reset layout at 2560x1440");
    }

    /* A popup opened later goes beside the column, over the world only. */
    {
        int const locs = find_panel("Locs");
        struct Editor_LocRef ref;
        memset(&ref, 0, sizeof(ref));
        ref.map_x = 50;
        ref.map_z = 50;
        Editor_UiSelectLoc(&g_ui, &ref);
        frame();
        frame();
        CHECK(locs >= 0 && g_chrome.panels[locs].visible, "selecting a loc opens Locs");
        if( locs >= 0 )
            for( int i = 0; i < g_chrome.panel_count; i++ )
                if( i != locs && layout_panel(i) )
                    CHECK(!rects_overlap(g_chrome.panels[locs].last_rect, g_chrome.panels[i].last_rect),
                        "Locs opens beside the column, clear of '%s'", g_chrome.panels[i].title);
        Editor_UiSelectLoc(&g_ui, NULL);
        frame();
    }
    Editor_UiFree(&g_ui);
}

/* ---- the World panel and the saved layout ------------------------------------------- */

#include "editor/editor_layout.h"
#include "editor/task_editor_layout_io.h"
#include "platform/platform_x_io.h"

static const char* g_tmp_root;

/** A menu row "label\tchord" names `label` (the chord is the hotkey's). */
static int
menu_row_is(
    const char* row,
    const char* label)
{
    size_t const n = strlen(label);
    return strncmp(row, label, n) == 0 && (row[n] == '\0' || row[n] == '\t');
}

/** The View menu's dropdown, and the row whose label is `label` (-1). */
static int
view_menu(void)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
        if( g_chrome.widgets[i].kind == TORIRS_CHROME_W_DROPDOWN && g_chrome.widgets[i].menu_mode
            && strcmp(g_chrome.widgets[i].text, "View") == 0 )
            return i;
    return -1;
}

static void
view_toggle(const char* label)
{
    int const view = view_menu();
    int row = -1;
    CHECK(view >= 0, "the View menu exists");
    if( view < 0 )
        return;
    for( int r = 0; r < g_chrome.widgets[view].option_count; r++ )
        if( g_chrome.widgets[view].options && menu_row_is(g_chrome.widgets[view].options[r], label) )
            row = r;
    CHECK(row >= 0, "View lists '%s'", label);
    if( row < 0 )
        return;
    ToriRSChrome_DropdownSetSelected(&g_chrome, view, row);
    activate(view);
    frame();
}

static int
rect_eq(
    struct EditorUiRect a,
    struct EditorUiRect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static void
test_world_panel(void)
{
    int wp;
    struct ToriRSChromeRect r;
    struct EditorUiRect world;
    struct EditorUiRect moved;

    layout_boot(1530, 1006, 1);
    wp = find_panel("World");
    CHECK(wp >= 0 && Editor_UiWorldShown(&g_ui), "the World panel is a panel and shows");
    if( wp < 0 )
    {
        Editor_UiFree(&g_ui);
        return;
    }
    world = Editor_UiWorldRect(&g_ui);
    r = g_chrome.panels[wp].last_rect;

    /* The body passes the pointer through to the world; the title bar and the
     * grip are the panel's. */
    CHECK(!ToriRSChrome_WantsPointer(&g_chrome, world.x + world.w / 2, world.y + world.h / 2),
        "the World body does not take the pointer (the world gets it)");
    CHECK(ToriRSChrome_WantsPointer(&g_chrome, r.x + 40, r.y + 4), "the World title bar takes the pointer");
    CHECK(ToriRSChrome_WantsPointer(&g_chrome, r.x + r.w - 4, r.y + r.h - 4), "the World grip takes the pointer");

    /* A drag that starts in the body is the world's: the panel stays. */
    drag(world.x + world.w / 2, world.y + world.h / 2, world.x + world.w / 2 + 90, world.y + world.h / 2 + 50);
    CHECK(!g_chrome.panels[wp].user_placed && rect_eq(Editor_UiWorldRect(&g_ui), world),
        "a drag in the World body (camera orbit) does not move the panel");

    /* View > World hides it: no world rect, nothing to draw or pick. */
    view_toggle("World");
    CHECK(!g_chrome.panels[wp].visible && !Editor_UiWorldShown(&g_ui), "View > World hides the World panel");
    moved = Editor_UiWorldRect(&g_ui);
    CHECK(moved.w == 0 && moved.h == 0, "with World hidden the world rect is empty (%dx%d)", moved.w, moved.h);
    /* A relayout (a resize) leaves it shut. */
    ToriRSChrome_SetSurface(&g_chrome, 1600, 1000);
    frame();
    frame();
    CHECK(!g_chrome.panels[wp].visible, "a hidden World stays hidden through a relayout");
    ToriRSChrome_SetSurface(&g_chrome, 1530, 1006);
    frame();
    frame();
    view_toggle("World");
    CHECK(g_chrome.panels[wp].visible && rect_eq(Editor_UiWorldRect(&g_ui), world),
        "View > World shows it again where it was");

    /* Moving it by its title bar moves the world with it, on the grid. */
    drag(r.x + 60, r.y + 6, r.x + 60 + 120, r.y + 6 + 80);
    moved = Editor_UiWorldRect(&g_ui);
    CHECK(g_chrome.panels[wp].user_placed, "the World drag marked it as placed");
    CHECK(moved.x - world.x == g_chrome.panels[wp].x - r.x && moved.y - world.y == g_chrome.panels[wp].y - r.y
              && moved.w == world.w && moved.h == world.h && moved.x != world.x,
        "moving World moves the world rect with it (%d,%d -> %d,%d)", world.x, world.y, moved.x, moved.y);
    CHECK(g_chrome.panels[wp].x % EDP_GRID == 0 && g_chrome.panels[wp].y % EDP_GRID == 0,
        "the dropped World snapped to the grid (%d,%d)", g_chrome.panels[wp].x, g_chrome.panels[wp].y);

    /* Resizing it resizes the world. */
    r = g_chrome.panels[wp].last_rect;
    drag(r.x + r.w - 4, r.y + r.h - 4, r.x + r.w - 4 - 200, r.y + r.h - 4 - 120);
    moved = Editor_UiWorldRect(&g_ui);
    CHECK(moved.w < world.w && moved.h < world.h
              && moved.w == ToriRSChrome_PanelBodyRect(&g_chrome, wp).w
              && g_chrome.panels[wp].fixed_w % EDP_GRID == 0,
        "resizing World resizes the world rect (%dx%d -> %dx%d)", world.w, world.h, moved.w, moved.h);

    /* Reset layout docks it back over the region. */
    Editor_UiResetLayout(&g_ui);
    frame();
    frame();
    CHECK(!g_chrome.panels[wp].user_placed && rect_eq(Editor_UiWorldRect(&g_ui), world),
        "Reset layout docks World back over the region");
    layout_check("World after Reset layout");
    Editor_UiFree(&g_ui);
}

/* ---- pop out, the title-bar Close and Pop out, edge resize ---------------------- */

/* The fake App: the camera it answers and what it was asked to spawn. */
static struct
{
    int calls;
    char module[32];
    int had_camera;
    struct EditorViewCamera camera;
    int camera_level;
    int refuse;
} g_pop;

static int
fake_world_camera(
    void* user,
    int level,
    struct EditorViewCamera* out)
{
    (void)user;
    memset(out, 0, sizeof(*out));
    out->x = 3222 * 128 + 64;
    out->z = 3218 * 128 + 64;
    out->level = level;
    out->yaw = 512;
    out->pitch = 300;
    out->zoom = 1500;
    g_pop.camera_level = level;
    return 1;
}

static int
fake_spawn_panel(
    void* user,
    const char* module,
    const struct EditorViewCamera* camera,
    char* why,
    int why_size)
{
    (void)user;
    g_pop.calls++;
    snprintf(g_pop.module, sizeof(g_pop.module), "%s", module);
    g_pop.had_camera = camera != NULL;
    if( camera )
        g_pop.camera = *camera;
    if( g_pop.refuse )
    {
        snprintf(why, (size_t)why_size, "no socket for a second process");
        return 0;
    }
    return 1;
}

static int
popout_console_has(const char* needle)
{
    for( int i = 0; i < 8; i++ )
    {
        const char* line = Editor_UiConsoleLine(&g_ui, i);
        if( line && strstr(line, needle) )
            return 1;
    }
    return 0;
}

/** Press and release at the middle of `r`, as the pointer does. */
static void
click_rect(struct ToriRSChromeRect r)
{
    int const x = r.x + r.w / 2;
    int const y = r.y + r.h / 2;
    ToriRSChrome_MouseMove(&g_chrome, x, y);
    (void)ToriRSChrome_MouseDown(&g_chrome, x, y);
    (void)ToriRSChrome_MouseUp(&g_chrome, x, y);
    frame();
    frame();
}

/** The View menu's row for `label`, or NULL. */
static const char*
view_row(const char* label)
{
    int const view = view_menu();
    if( view < 0 || !g_chrome.widgets[view].options )
        return NULL;
    for( int r = 0; r < g_chrome.widgets[view].option_count; r++ )
        if( menu_row_is(g_chrome.widgets[view].options[r], label) )
            return g_chrome.widgets[view].options[r];
    return NULL;
}

static void
test_popout(void)
{
    int wp;
    int terrain;
    int palette;
    const char* row;
    struct ToriRSChromeRect r;

    memset(&g_pop, 0, sizeof(g_pop));
    layout_boot(1530, 1006, 1);
    g_ui.app_services.world_camera = fake_world_camera;
    g_ui.app_services.spawn_panel = fake_spawn_panel;
    wp = find_panel("World");
    terrain = find_panel("Terrain");
    palette = find_panel("Palette");
    CHECK(wp >= 0 && terrain >= 0 && palette >= 0, "World, Terrain and Palette are panels");
    if( wp < 0 || terrain < 0 || palette < 0 )
    {
        Editor_UiFree(&g_ui);
        return;
    }

    /* Every View-menu window wears a Close box, a Pop out beside it and a
     * grip; the menu bar (not a window) wears none. */
    CHECK(g_chrome.panels[wp].closable && g_chrome.panels[wp].poppable && g_chrome.panels[wp].resizable,
        "World has a Close box, a Pop out and a grip");
    CHECK(g_chrome.panels[terrain].closable && g_chrome.panels[terrain].poppable
              && g_chrome.panels[terrain].resizable,
        "Terrain has a Close box, a Pop out and a grip");
    {
        struct ToriRSChromeRect const close = ToriRSChrome_PanelCloseRect(&g_chrome, terrain);
        struct ToriRSChromeRect const pop = ToriRSChrome_PanelPopOutRect(&g_chrome, terrain);
        struct ToriRSChromeRect const box = g_chrome.panels[terrain].last_rect;
        CHECK(close.w > 0 && pop.w > 0 && pop.x + pop.w <= close.x && close.x + close.w <= box.x + box.w
                  && pop.y == close.y && pop.y >= box.y,
            "Pop out sits left of Close, both in the title bar (pop %d,%d close %d,%d)", pop.x, pop.y, close.x,
            close.y);
    }

    /* Pop out World with level 1 pinned: its title-bar button asks the App
     * for a window at the World panel's camera on level 1's ground, and the
     * panel leaves this window. */
    g_ui.tools.edit_level = 1;
    frame();
    click_rect(ToriRSChrome_PanelPopOutRect(&g_chrome, wp));
    CHECK(g_pop.calls == 1 && strcmp(g_pop.module, "world") == 0, "World's Pop out asks for a world window");
    CHECK(g_pop.had_camera && g_pop.camera_level == 1 && g_pop.camera.level == 1
              && g_pop.camera.x == 3222 * 128 + 64 && g_pop.camera.z == 3218 * 128 + 64 && g_pop.camera.yaw == 512
              && g_pop.camera.pitch == 300 && g_pop.camera.zoom == 1500,
        "with the World panel's pose and the pinned level (level %d)", g_pop.camera.level);
    CHECK(!g_chrome.panels[wp].visible && !Editor_UiWorldShown(&g_ui) && Editor_UiPanelPoppedOut(&g_ui, "world"),
        "and World is hidden here, popped out");
    CHECK(popout_console_has("World popped out into its own window"), "one console line says so");
    /* (Auto again: the World title names a pinned level, and the layout
     * checks below find the panel by its title.) */
    g_ui.tools.edit_level = -1;
    row = view_row("World");
    CHECK(row && strstr(row, "popped out"), "the View menu row says World is popped out (%s)", row ? row : "-");
    /* The space is the panels': with no World the dock runs in columns
     * across the surface, Terrain still at the head of the rightmost. */
    frame();
    CHECK(g_ui.dock_x < 1530 - g_ui.dock_w, "with World out the dock widens to columns (x %d, w %d)", g_ui.dock_x,
        g_ui.dock_w);
    CHECK(g_chrome.panels[terrain].visible && g_chrome.panels[terrain].x == 1530 - g_ui.dock_w
              && g_chrome.panels[terrain].y == g_ui.dock_top,
        "Terrain heads the rightmost column (%d,%d)", g_chrome.panels[terrain].x, g_chrome.panels[terrain].y);

    /* Pop in: View > World shows it here again (one line), and the layout
     * gives it its region back. */
    view_toggle("World");
    frame();
    CHECK(g_chrome.panels[wp].visible && Editor_UiWorldShown(&g_ui) && !Editor_UiPanelPoppedOut(&g_ui, "world"),
        "View > World pops it back in");
    CHECK(popout_console_has("World popped back in"), "one console line says so");
    layout_check("World popped back in");
    row = view_row("World");
    CHECK(row && !strstr(row, "popped out") && !strstr(row, "hidden"), "and its View row is plain again");

    /* A refusal leaves the panel where it was and says why. */
    g_pop.refuse = 1;
    CHECK(!Editor_UiPanelPopOut(&g_ui, "terrain") && g_chrome.panels[terrain].visible
              && !Editor_UiPanelPoppedOut(&g_ui, "terrain"),
        "a refused pop out keeps Terrain here");
    CHECK(popout_console_has("could not pop out Terrain: no socket for a second process"),
        "and the console says why");
    g_pop.refuse = 0;

    /* Any panel pops out by name (the hotkey's entry point); the column
     * closes up behind it. */
    CHECK(Editor_UiPanelPopOut(&g_ui, "terrain") && strcmp(g_pop.module, "terrain") == 0 && g_pop.had_camera,
        "Terrain pops out by name, with the camera");
    frame();
    frame();
    CHECK(!g_chrome.panels[terrain].visible && g_chrome.panels[palette].visible
              && g_chrome.panels[palette].y == g_ui.dock_top,
        "Terrain is gone and Palette moves up to the head of the column (y %d)", g_chrome.panels[palette].y);
    view_toggle("Terrain");
    frame();
    CHECK(g_chrome.panels[terrain].visible && !Editor_UiPanelPoppedOut(&g_ui, "terrain")
              && g_chrome.panels[terrain].y == g_ui.dock_top,
        "View > Terrain pops it back in at the head of the column");

    /* The Close box: exactly View > Terrain off -- hidden, the View row
     * says so, and the View menu brings it back. */
    click_rect(ToriRSChrome_PanelCloseRect(&g_chrome, terrain));
    CHECK(!g_chrome.panels[terrain].visible && !Editor_UiPanelPoppedOut(&g_ui, "terrain"),
        "Terrain's Close box hides it");
    row = view_row("Terrain");
    CHECK(row && strstr(row, "hidden"), "the View menu row says Terrain is hidden (%s)", row ? row : "-");
    view_toggle("Terrain");
    frame();
    row = view_row("Terrain");
    CHECK(g_chrome.panels[terrain].visible && row && !strstr(row, "hidden"), "View > Terrain shows it again");

    /* Edge resize: Terrain widened from its LEFT edge -- the right edge
     * stays, the box snaps to the grid, and it counts as placed (the saved
     * layout keeps it). */
    frame();
    r = g_chrome.panels[terrain].last_rect;
    {
        int edge_panel = -1;
        int const edges = ToriRSChrome_PanelEdgeAt(&g_chrome, r.x + 1, r.y + r.h / 2, &edge_panel);
        CHECK(edges == TORIRS_CHROME_EDGE_LEFT && edge_panel == terrain, "Terrain's left border is a resize edge");
        CHECK(ToriRSChrome_PanelEdgeAt(&g_chrome, r.x + r.w / 2, r.y + r.h / 2, &edge_panel) == 0,
            "its middle is not");
    }
    drag(r.x + 1, r.y + r.h / 2, r.x + 1 - 80, r.y + r.h / 2);
    CHECK(abs(g_chrome.panels[terrain].x - (r.x - 80)) <= EDP_GRID / 2
              && g_chrome.panels[terrain].x + g_chrome.panels[terrain].fixed_w == r.x + r.w
              && g_chrome.panels[terrain].user_placed,
        "dragging Terrain's left edge 80 px widens it to the left, the right edge kept (x %d -> %d, w %d -> %d)",
        r.x, g_chrome.panels[terrain].x, r.w, g_chrome.panels[terrain].fixed_w);
    CHECK(g_chrome.panels[terrain].x % EDP_GRID == 0, "and the moved edge lands on the grid");
    /* The bottom-left corner moves two edges; a drag past the minimum
     * stops at a usable panel instead of inverting. */
    r = g_chrome.panels[terrain].last_rect;
    drag(r.x + 1, r.y + r.h - 2, r.x + r.w + 400, r.y + r.h - 2 + 16);
    CHECK(g_chrome.panels[terrain].fixed_w >= 120 && g_chrome.panels[terrain].x + g_chrome.panels[terrain].fixed_w
              == r.x + r.w,
        "a left edge dragged past the right one stops at the minimum (w %d)", g_chrome.panels[terrain].fixed_w);
    CHECK(g_chrome.panels[terrain].fixed_h > r.h, "and the bottom edge of that corner moved down (h %d -> %d)", r.h,
        g_chrome.panels[terrain].fixed_h);
    /* The top edge of the World panel resizes it too (and the world rect
     * follows, as with the grip). */
    {
        struct EditorUiRect const before = Editor_UiWorldRect(&g_ui);
        r = g_chrome.panels[wp].last_rect;
        drag(r.x + r.w / 2, r.y + 1, r.x + r.w / 2, r.y + 1 + 48);
        CHECK(abs(g_chrome.panels[wp].y - (r.y + 48)) <= EDP_GRID / 2
                  && Editor_UiWorldRect(&g_ui).h == before.h - (g_chrome.panels[wp].y - r.y)
                  && g_chrome.panels[wp].y + g_chrome.panels[wp].fixed_h == r.y + r.h,
            "World's top edge moves it down, its bottom kept, and the world rect shrinks with it (h %d -> %d)",
            before.h, Editor_UiWorldRect(&g_ui).h);
    }
    Editor_UiFree(&g_ui);

    /* A popped-out window: one panel over the whole surface, nothing else,
     * no Close / Pop out, and it pops nothing further. */
    layout_boot(900, 700, 1);
    g_ui.app_services.world_camera = fake_world_camera;
    g_ui.app_services.spawn_panel = fake_spawn_panel;
    terrain = find_panel("Terrain");
    wp = find_panel("World");
    CHECK(Editor_UiSetSolo(&g_ui, "terrain") && !Editor_UiSetSolo(&g_ui, "menubar"),
        "a titled module can be a window's solo panel; the menu bar cannot");
    Editor_UiSetSolo(&g_ui, "terrain");
    frame();
    frame();
    CHECK(terrain >= 0 && g_chrome.panels[terrain].visible && g_chrome.panels[terrain].x == 0
              && g_chrome.panels[terrain].y == 0 && g_chrome.panels[terrain].fixed_w == 900
              && g_chrome.panels[terrain].fixed_h == 700,
        "the solo Terrain fills the surface");
    {
        int others = 0;
        for( int i = g_ui.panel_first; i < g_ui.panel_end; i++ )
            others += i != terrain && g_chrome.panels[i].visible;
        CHECK(others == 0 && !Editor_UiWorldShown(&g_ui), "no other panel shows, World neither (%d)", others);
    }
    CHECK(terrain >= 0 && !g_chrome.panels[terrain].closable && !g_chrome.panels[terrain].poppable,
        "the solo panel wears no Close / Pop out");
    g_pop.calls = 0;
    CHECK(!Editor_UiPanelPopOut(&g_ui, "palette") && g_pop.calls == 0, "a popped-out window pops nothing further");
    ToriRSChrome_SetSurface(&g_chrome, 1100, 760);
    frame();
    frame();
    CHECK(terrain >= 0 && g_chrome.panels[terrain].fixed_w == 1100 && g_chrome.panels[terrain].fixed_h == 760,
        "and follows the window's size");
    (void)wp;
    Editor_UiFree(&g_ui);
}

/* ---- the tools every window of a Client shares ----------------------------------- */


/* An Open in a window that shows no world (a popped-out Squares panel) is
 * relayed to the windows that show one; a window that shows a world opens
 * locally and acts on another window's relayed open. */
static void
main_on_state(
    void* user,
    uint32_t key,
    const int32_t* values,
    int count);
static struct
{
    int hits;
    int map_x;
    int map_z;
    int add;
    char storage[EDITOR_DOC_STORAGE_MAX];
} g_open_heard;
static void
open_square_recorder(
    void* user,
    int map_x,
    int map_z,
    const char* storage,
    int add)
{
    (void)user;
    g_open_heard.hits++;
    g_open_heard.map_x = map_x;
    g_open_heard.map_z = map_z;
    g_open_heard.add = add;
    snprintf(g_open_heard.storage, sizeof(g_open_heard.storage), "%s", storage);
}
static struct
{
    int hits;
    int32_t values[5];
} g_open_relayed;
static void
open_peer_on_state(
    void* user,
    uint32_t key,
    const int32_t* values,
    int count)
{
    (void)user;
    if( key == TORIRSMAPED_STATE_OPEN_SQUARE && count >= 5 )
    {
        memcpy(g_open_relayed.values, values, sizeof(g_open_relayed.values));
        g_open_relayed.hits++;
    }
}
static void
test_open_square_relay(void)
{
    static struct Editor peer;
    struct EditorHost peer_host;
    int32_t request[5];

    layout_boot(1530, 1006, 1);
    if( !Editor_HostOpenMapEdEmbedPeer(&peer_host, &g_editor.host, TORIRSMAPED_ROLE_GENERIC, 1) )
    {
        CHECK(0, "open relay: a second window joins the Client");
        Editor_UiFree(&g_ui);
        return;
    }
    Editor_OpenHost(&peer, &peer_host, "popped-out squares", &g_profile);
    memset(&g_open_heard, 0, sizeof(g_open_heard));
    memset(&g_open_relayed, 0, sizeof(g_open_relayed));
    Editor_SetStateCallback(&peer, open_peer_on_state, NULL);
    Editor_SetStateCallback(&g_editor, main_on_state, &g_ui);
    g_ui.hooks.open_square = open_square_recorder;
    frame();
    Editor_PumpFacts(&peer);

    /* The main window shows a world: its own Open is local, nothing relayed. */
    CHECK(Editor_UiWorldShown(&g_ui), "open relay: the main window shows a world");
    Editor_UiOpenSquare(&g_ui, 40, 52, "", 0, 0);
    frame();
    Editor_PumpFacts(&g_editor);
    Editor_PumpFacts(&peer);
    CHECK(g_open_heard.hits == 1 && g_open_heard.map_x == 40 && g_open_heard.map_z == 52 && !g_open_heard.add,
        "open relay: a window with a world opens locally (%d hits)", g_open_heard.hits);
    CHECK(g_open_relayed.hits == 0, "open relay: and relays nothing (%d heard)", g_open_relayed.hits);

    /* Another window's request lands here: the window that shows a world
     * opens it, add and all. */
    request[0] = 41;
    request[1] = 53;
    request[2] = 1;
    request[3] = -1;
    request[4] = 7;
    Editor_StateSet(&peer, TORIRSMAPED_STATE_OPEN_SQUARE, request, 5);
    Editor_PumpFacts(&peer);
    Editor_PumpFacts(&g_editor);
    frame();
    CHECK(g_open_heard.hits == 2 && g_open_heard.map_x == 41 && g_open_heard.map_z == 53 && g_open_heard.add
              && g_open_heard.storage[0] == '\0',
        "open relay: a relayed open lands in the window that shows a world (%d hits, m%d_%d add %d)",
        g_open_heard.hits, g_open_heard.map_x, g_open_heard.map_z, g_open_heard.add);

    /* With its World hidden (popped out, or a panel-only window) an Open is
     * relayed instead of opened here, with a serial that moves per request. */
    ToriRSChrome_PanelSetVisible(g_ui.chrome, g_ui.world_panel, 0);
    CHECK(!Editor_UiWorldShown(&g_ui), "open relay: the main window's world is hidden");
    /* The peer heard the echo of its own request above; count from here. */
    memset(&g_open_relayed, 0, sizeof(g_open_relayed));
    Editor_UiOpenSquare(&g_ui, 42, 54, "", 0, 0);
    Editor_UiOpenSquare(&g_ui, 42, 54, "", 0, 0);
    frame();
    Editor_PumpFacts(&g_editor);
    Editor_PumpFacts(&peer);
    CHECK(g_open_heard.hits == 2, "open relay: nothing opened in the window without a world (%d hits)",
        g_open_heard.hits);
    CHECK(g_open_relayed.hits == 2 && g_open_relayed.values[0] == 42 && g_open_relayed.values[1] == 54
              && g_open_relayed.values[2] == 0 && g_open_relayed.values[4] == 2,
        "open relay: the other window heard both requests, the second with serial 2 (%d heard, serial %d)",
        g_open_relayed.hits, g_open_relayed.values[4]);
    CHECK(popout_console_has("in the window that shows the world"),
        "open relay: the console says where the open went");

    Editor_SetStateCallback(&g_editor, NULL, NULL);
    Editor_Close(&peer);
    Editor_UiFree(&g_ui);
}

/* The main window here is g_ui over g_editor; the other window is a second
 * connection to the same embedded server joined to the same Client (what a
 * popped-out window is), with no registry: it records what it hears. */
static struct
{
    int tool;
    int tool_hits;
    int tools_hits;
    int32_t tools[TORIRSMAPED_TOOLS_VALUES];
    int selection_hits;
    int32_t selection[7];
} g_peer_heard;

static void
peer_on_state(
    void* user,
    uint32_t key,
    const int32_t* values,
    int count)
{
    (void)user;
    if( key == TORIRSMAPED_STATE_TOOL && count >= 1 )
    {
        g_peer_heard.tool = values[0];
        g_peer_heard.tool_hits++;
    }
    else if( key == TORIRSMAPED_STATE_TOOLS && count >= TORIRSMAPED_TOOLS_VALUES )
    {
        memcpy(g_peer_heard.tools, values, sizeof(g_peer_heard.tools));
        g_peer_heard.tools_hits++;
    }
    else if( key == TORIRSMAPED_STATE_SELECTION && count >= 7 )
    {
        memcpy(g_peer_heard.selection, values, sizeof(g_peer_heard.selection));
        g_peer_heard.selection_hits++;
    }
}

static void
main_on_state(
    void* user,
    uint32_t key,
    const int32_t* values,
    int count)
{
    Editor_UiApplyState((struct EditorUi*)user, key, values, count);
}

static void
test_shared_tools(void)
{
    static struct Editor peer;
    struct EditorHost peer_host;
    int32_t tools[TORIRSMAPED_TOOLS_VALUES];
    int32_t value;
    int terrain;

    layout_boot(1530, 1006, 1);
    terrain = find_panel("Terrain");
    if( !Editor_HostOpenMapEdEmbedPeer(&peer_host, &g_editor.host, TORIRSMAPED_ROLE_GENERIC, 1) )
    {
        CHECK(0, "shared tools: a second window joins the Client");
        Editor_UiFree(&g_ui);
        return;
    }
    Editor_OpenHost(&peer, &peer_host, "popped-out window", &g_profile);
    memset(&g_peer_heard, 0, sizeof(g_peer_heard));
    Editor_SetStateCallback(&peer, peer_on_state, NULL);
    Editor_SetStateCallback(&g_editor, main_on_state, &g_ui);
    frame();
    Editor_PumpFacts(&peer);
    CHECK(g_peer_heard.tools_hits == 0, "shared tools: a window's boot publishes no tools (its defaults are no change)");

    /* Main -> other window: the tool (the 3 key arms Raise), the brush, the
     * palette, all heard. */
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_RAISE);
    g_ui.tools.brush.radius = 5;
    g_ui.tools.brush.strength = 70;
    g_ui.tools.underlay_id = 12;
    g_ui.tools.overlay_none = 0;
    g_ui.tools.overlay_id = 7;
    g_ui.tools.place_shape = 22;
    frame();
    Editor_PumpFacts(&g_editor);
    Editor_PumpFacts(&peer);
    CHECK(g_peer_heard.tool_hits >= 1 && g_peer_heard.tool == EDITOR_TOOL_RAISE,
        "shared tools: the other window hears the main window arm Raise");
    CHECK(g_peer_heard.tools_hits >= 1 && g_peer_heard.tools[0] == 5 && g_peer_heard.tools[3] == 70
              && g_peer_heard.tools[4] == 12 && g_peer_heard.tools[5] == 7 && g_peer_heard.tools[6] == 0
              && g_peer_heard.tools[10] == 22,
        "shared tools: and its brush, palette and placement shape (radius %d underlay %d overlay %d)",
        g_peer_heard.tools[0], g_peer_heard.tools[4], g_peer_heard.tools[5]);
    CHECK(g_ui.tools.brush.radius == 5 && g_ui.tools.underlay_id == 12,
        "shared tools: the main window's own echo leaves its tools as they are (radius %d underlay %d, %d echoes "
        "pending)",
        g_ui.tools.brush.radius, g_ui.tools.underlay_id, g_ui.tools_echo_count);

    /* Other window -> main: its tool, brush, palette and selection land in
     * the main window's tools, and the Terrain panel follows. */
    value = EDITOR_TOOL_SELECT;
    Editor_StateSet(&peer, TORIRSMAPED_STATE_TOOL, &value, 1);
    memcpy(tools, g_peer_heard.tools, sizeof(tools));
    tools[0] = 9;
    tools[2] = 4;
    tools[4] = 33;
    tools[12] = 1;
    Editor_StateSet(&peer, TORIRSMAPED_STATE_TOOLS, tools, TORIRSMAPED_TOOLS_VALUES);
    {
        int32_t const selection[7] = { EDITOR_SELECTION_TERRAIN, 3210, 3220, 0, 0, 0, 0 };
        Editor_StateSet(&peer, TORIRSMAPED_STATE_SELECTION, selection, 7);
    }
    Editor_PumpFacts(&peer);
    Editor_PumpFacts(&g_editor);
    frame();
    CHECK(g_ui.tools.tool == EDITOR_TOOL_SELECT, "shared tools: the main window takes the other window's tool");
    CHECK(g_ui.tools.brush.radius == 9 && g_ui.tools.brush.step == 4 && g_ui.tools.underlay_id == 33
              && g_ui.tools.vis_level == 1,
        "shared tools: and its brush, palette and view level (radius %d step %d underlay %d vis %d)",
        g_ui.tools.brush.radius, g_ui.tools.brush.step, g_ui.tools.underlay_id, g_ui.tools.vis_level);
    if( terrain >= 0 )
    {
        int slider = -1;
        for( int i = 0; i < g_chrome.widget_count && slider < 0; i++ )
            if( g_chrome.widgets[i].panel == terrain && g_chrome.widgets[i].kind == TORIRS_CHROME_W_SLIDER )
                slider = i;
        CHECK(slider >= 0 && ToriRSChrome_SliderValue(&g_chrome, slider) == 9,
            "shared tools: the Terrain panel's Radius slider follows (%d)",
            slider >= 0 ? ToriRSChrome_SliderValue(&g_chrome, slider) : -1);
    }
    g_peer_heard.tools_hits = 0;
    frame();
    Editor_PumpFacts(&g_editor);
    Editor_PumpFacts(&peer);
    CHECK(g_peer_heard.tools_hits == 0, "shared tools: tools taken from another window are not published back");

    /* The echo rule: a change made before this window's own echo returns
     * is not undone by it. */
    g_ui.tools.brush.radius = 2;
    frame();
    g_ui.tools.brush.radius = 3;
    Editor_PumpFacts(&g_editor);
    CHECK(g_ui.tools.brush.radius == 3, "shared tools: a stale echo of radius 2 does not undo radius 3");
    frame();
    Editor_PumpFacts(&g_editor);
    Editor_PumpFacts(&peer);
    CHECK(g_peer_heard.tools[0] == 3 && g_ui.tools.brush.radius == 3, "shared tools: both windows end at radius 3");

    /* A popped-out World: the World panel alone, its title naming what a
     * click in it does. */
    CHECK(Editor_UiSetSolo(&g_ui, "world"), "shared tools: World can be a window's solo panel");
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_RAISE);
    frame();
    frame();
    CHECK(g_ui.world_panel >= 0 && Editor_UiWorldShown(&g_ui)
              && strcmp(g_chrome.panels[g_ui.world_panel].title, "World - Raise") == 0,
        "shared tools: the popped-out World shows and names the armed tool (%s)",
        g_ui.world_panel >= 0 ? g_chrome.panels[g_ui.world_panel].title : "-");
    CHECK(Editor_UiWorldRect(&g_ui).w > 1400 && Editor_UiWorldRect(&g_ui).h > 900,
        "shared tools: its world fills the window (%dx%d)", Editor_UiWorldRect(&g_ui).w, Editor_UiWorldRect(&g_ui).h);
    Editor_UiClearSolo(&g_ui);
    frame();
    frame();
    CHECK(g_ui.solo_panel < 0 && strcmp(g_chrome.panels[g_ui.world_panel].title, "World") == 0
              && g_chrome.panels[g_ui.world_panel].poppable,
        "shared tools: Open panels takes it back to the whole set");

    Editor_SetStateCallback(&g_editor, NULL, NULL);
    Editor_Close(&peer);
    Editor_PumpFacts(&g_editor);
    Editor_UiFree(&g_ui);
}

/* ---- the Preview panel ---------------------------------------------------------- */

static int g_preview_resets;
static int g_preview_app_stand_in;

static void
fake_preview_invalidate(void* user)
{
    (void)user;
    g_preview_resets++;
}

/* What the Preview's gestures asked of the App (workbench 6.1). */
static int g_preview_orbit_pitch;
static int g_preview_orbit_yaw;
static int g_preview_orbits;
static int g_preview_zoom_q16;

static void
fake_preview_orbit(
    void* user,
    int pitch_step,
    int yaw_step)
{
    (void)user;
    g_preview_orbit_pitch += pitch_step;
    g_preview_orbit_yaw += yaw_step;
    g_preview_orbits++;
}

static void
fake_preview_zoom(
    void* user,
    int ratio_q16)
{
    (void)user;
    g_preview_zoom_q16 = ratio_q16;
}

/** layout_boot with services standing in for an App: the Catalog, and with
 *  it the Preview and Record panels, register. */
static void
layout_boot_catalog(
    int w,
    int h,
    const struct EditorLayoutFile* file)
{
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetScale(&g_chrome, 1);
    ToriRSChrome_SetSurface(&g_chrome, w, h);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.hooks.open_square = fake_open_square;
    g_ui.hooks.open_in_new_view = fake_open_in_new_view;
    g_ui.hooks.floor_rgb = fake_floor_rgb;
    g_ui.hooks.view_window = fake_view_window;
    g_ui.hooks.selection_changed = fake_selection_changed;
    g_ui.app_services.user_data = &g_preview_app_stand_in;
    g_ui.app_services.preview_invalidate = fake_preview_invalidate;
    g_ui.app_services.preview_orbit = fake_preview_orbit;
    g_ui.app_services.preview_zoom = fake_preview_zoom;
    g_ui.app_services.world_camera = fake_world_camera;
    g_ui.app_services.spawn_panel = fake_spawn_panel;
    Editor_UiRegisterToolsPanels(&g_ui);
    ungroup_all(&g_ui);
    if( file )
        Editor_UiLayoutHold(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    frame();
    frame();
    if( file )
        Editor_UiLayoutLoaded(&g_ui, file);
    for( int i = 0; i < 4; i++ )
        frame();
}

/** The Catalog's result row `row`, picked as a click does. */
static void
preview_pick_row(
    int catalog,
    const char* find_text,
    int row)
{
    int const find = find_widget(catalog, TORIRS_CHROME_W_TEXTINPUT, "Find");
    int const list = find_widget(catalog, TORIRS_CHROME_W_VLIST, NULL);

    assert(find >= 0);
    assert(list >= 0);
    ToriRSChrome_SetText(&g_chrome, find, find_text);
    for( int i = 0; i < 3; i++ )
        frame();
    ToriRSChrome_VListSetSelected(&g_chrome, list, row);
    activate(list);
}

/* The Publish panel (edpanel_publish.c): File > Publish... opens it, it sits
 * on the surface beside or in the column without covering the world, Close
 * hides it, and the hotkey toggles it back. */
static void
test_publish(void)
{
    int publish;
    struct ToriRSChromeRect r;

    layout_boot(1280, 800, 1);
    /* Titled Lanes since workbench 6.5 (the lanes' switch, publish and
     * discard in one panel); the module is still "publish". */
    publish = find_panel("Lanes");
    CHECK(publish >= 0, "publish: the panel registers (module \"publish\", titled Lanes)");
    if( publish < 0 )
    {
        Editor_UiFree(&g_ui);
        return;
    }
    CHECK(!g_chrome.panels[publish].visible, "publish: starts closed");
    CHECK(Editor_UiFindPanel(&g_ui, "publish") != NULL, "publish: found by module name");
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_FILE_PUBLISH);
    for( int i = 0; i < 6; i++ )
        frame();
    r = g_chrome.panels[publish].last_rect;
    CHECK(g_chrome.panels[publish].visible, "publish: File > Publish... opens it");
    CHECK(r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 && r.x + r.w <= g_chrome.surface_w
              && r.y + r.h <= g_chrome.surface_h,
        "publish: it is on the surface (%d,%d %dx%d)", r.x, r.y, r.w, r.h);
    /* An on-demand panel flies out beside the column (never into it, never
     * under it), as the Live panel does. */
    CHECK(r.x + r.w <= g_ui.dock_x && r.y >= g_ui.dock_top, "publish: it flies out beside the column (%d,%d %dx%d; column at %d)",
        r.x, r.y, r.w, r.h, g_ui.dock_x);
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_FILE_PUBLISH);
    for( int i = 0; i < 3; i++ )
        frame();
    CHECK(!g_chrome.panels[publish].visible, "publish: the hotkey again closes it");
    Editor_UiFree(&g_ui);
}

static void
test_preview(void)
{
    static struct Editor peer;
    static struct ToriRSChrome pv_chrome;
    static struct EditorUi pv_ui;
    struct EditorHost peer_host;
    struct EditorLayoutFile file;
    struct EditorLayoutFile without;
    struct EditorUiPanel* module;
    int preview;
    int catalog;
    int view;
    int reset;
    int subject;
    int kind;
    int id;
    int box_w;
    int box_h;
    int well_w;
    int well_h;
    const char* row;

    g_preview_resets = 0;
    memset(&g_pop, 0, sizeof(g_pop));
    layout_boot_catalog(1280, 800, NULL);
    preview = find_panel("Preview");
    catalog = find_panel("Catalog");
    CHECK(preview >= 0 && catalog >= 0, "preview: the Catalog registers a Preview panel beside it");
    if( preview < 0 || catalog < 0 )
    {
        Editor_UiFree(&g_ui);
        return;
    }
    module = Editor_UiFindPanel(&g_ui, "preview");
    CHECK(module && module - g_ui.panels == Editor_UiFindPanel(&g_ui, "catalog") - g_ui.panels + 1,
        "preview: module \"preview\", right after the Catalog in the registry (and the View menu)");
    CHECK(g_chrome.panels[preview].closable && g_chrome.panels[preview].poppable
              && g_chrome.panels[preview].resizable,
        "preview: it wears the Close box, the Pop out and the grip");
    row = view_row("Preview");
    CHECK(row && strstr(row, "Shift+P"), "preview: View > Preview shows its chord (%s)", row ? row : "-");
    CHECK(Editor_HotkeyForPanel("preview") == EDITOR_HK_VIEW_PREVIEW, "preview: view.preview toggles it");

    /* The Catalog no longer carries the model view; the Preview does. */
    CHECK(find_widget(catalog, TORIRS_CHROME_W_MODELVIEW, NULL) < 0
              && find_widget(catalog, TORIRS_CHROME_W_MENUITEM, "Reset view") < 0,
        "preview: the Catalog has no model view and no Reset view");
    view = find_widget(preview, TORIRS_CHROME_W_MODELVIEW, NULL);
    reset = find_widget(preview, TORIRS_CHROME_W_MENUITEM, "Reset view");
    subject = find_widget(preview, TORIRS_CHROME_W_LABEL, NULL);
    CHECK(view >= 0 && reset >= 0 && subject >= 0, "preview: the model view, Reset view and the subject row");
    if( view < 0 || reset < 0 || subject < 0 )
    {
        Editor_UiFree(&g_ui);
        return;
    }

    /* 1280x800 has no room for either in the column: both hidden by the
     * layout -- not a close of the user's. Opening the Catalog opens the
     * Preview against its left side, top-aligned. */
    CHECK(!g_chrome.panels[catalog].visible && !g_chrome.panels[preview].visible,
        "preview: at 1280x800 the layout leaves the Catalog and the Preview closed");
    CHECK(Editor_Preview_ModelView(&g_ui) == -1, "preview: closed, it has no well to render into");
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_CATALOG);
    frame();
    frame();
    CHECK(g_chrome.panels[catalog].visible && g_chrome.panels[preview].visible && Editor_Preview_AutoOpens(&g_ui) == 1,
        "preview: opening the Catalog opened the Preview once (%d)", Editor_Preview_AutoOpens(&g_ui));
    {
        struct ToriRSChromeRect const c = g_chrome.panels[catalog].last_rect;
        struct ToriRSChromeRect const p = g_chrome.panels[preview].last_rect;
        CHECK((p.x + p.w == c.x || p.x == c.x + c.w) && p.y == c.y,
            "preview: beside the Catalog (preview %d,%d %dx%d, catalog %d,%d %dx%d)", p.x, p.y, p.w, p.h, c.x, c.y,
            c.w, c.h);
    }
    CHECK(Editor_Preview_ModelView(&g_ui) == view, "preview: open, its well is the one the App renders into");
    CHECK(!Editor_Preview_Subject(&g_ui, &kind, &id) && strstr(g_chrome.widgets[subject].text, "pick a loc"),
        "preview: with nothing picked or selected it asks for a pick (%s)", g_chrome.widgets[subject].text);

    /* With nothing picked, the primary selected loc. */
    g_ui.tools.loc_count = 1;
    g_ui.tools.locs[0].loc_id = 1;
    frame();
    CHECK(Editor_Preview_Subject(&g_ui, &kind, &id) && kind == 0 && id == 1
              && strstr(g_chrome.widgets[subject].text, "selected loc 1"),
        "preview: with nothing picked it shows the selected loc (%d %d, %s)", kind, id,
        g_chrome.widgets[subject].text);
    g_ui.tools.loc_count = 0;

    /* It follows the Catalog's pick. */
    Editor_SetStateCallback(&g_editor, main_on_state, &g_ui);
    preview_pick_row(catalog, "crate", 0);
    frame();
    CHECK(Editor_Preview_Subject(&g_ui, &kind, &id) && kind == 0 && id == 0
              && strcmp(g_chrome.widgets[subject].text, "loc test_crate (0)") == 0,
        "preview: a picked row is its subject (%d %d, %s)", kind, id, g_chrome.widgets[subject].text);
    CHECK(Editor_Preview_AutoOpens(&g_ui) == 1, "preview: already open, a pick does not open it again");

    /* Reset view: the App's camera back to the default pose. */
    activate(reset);
    CHECK(g_preview_resets == 1, "preview: Reset view asks the App for the default pose (%d)", g_preview_resets);

    /* Gestures over the well (workbench 6.1), no focus needed: a pan orbits
     * at the middle-drag's gearing (4 yaw, 2 pitch a logical px), a slow one
     * still turns it, and a pinch zooms by its ratio. */
    {
        struct ToriRSChromeWidget const* w = &g_chrome.widgets[view];
        struct ToriRSChromeGesture g;
        int const scale = g_chrome.scale > 0 ? g_chrome.scale : 1;

        g_preview_orbit_pitch = g_preview_orbit_yaw = g_preview_orbits = g_preview_zoom_q16 = 0;
        memset(&g, 0, sizeof(g));
        g.kind = TORIRS_CHROME_GESTURE_PAN;
        g.x = w->x + w->w / 2;
        g.y = w->y + w->h / 2;
        g.dx = 10 * scale;
        g.dy = -3 * scale;
        g.fingers = 1;
        g.phase = TORIRS_CHROME_GESTURE_UPDATE;
        CHECK(ToriRSChrome_Gesture(&g_chrome, &g) == 1, "preview: a pan over the well is the chrome's");
        frame();
        CHECK(g_preview_orbit_yaw == 40 && g_preview_orbit_pitch == -6,
            "preview: a pan orbits, 4 yaw and 2 pitch a px (%d, %d)", g_preview_orbit_yaw, g_preview_orbit_pitch);
        /* A quarter px of yaw a frame: four frames make one step. */
        g.dx = 0;
        g.dy = 0;
        g_preview_orbit_yaw = 0;
        for( int i = 0; i < 4; i++ )
        {
            struct ToriRSChromeEvent ev;
            memset(&ev, 0, sizeof(ev));
            ev.kind = TORIRS_CHROME_EVENT_VIEW_ORBIT;
            ev.widget = view;
            ev.serial = g_chrome.widgets[view].serial;
            ev.dx = 64;
            (void)ToriRSChrome_PushEvent(&g_chrome, &ev);
            frame();
        }
        CHECK(g_preview_orbit_yaw == 4, "preview: a slow finger still turns it (%d)", g_preview_orbit_yaw);
        g.kind = TORIRS_CHROME_GESTURE_PINCH;
        g.scale_q16 = 3 * 65536 / 2;
        g.fingers = 2;
        CHECK(ToriRSChrome_Gesture(&g_chrome, &g) == 1, "preview: a pinch over the well is the chrome's");
        frame();
        CHECK(g_preview_zoom_q16 == 3 * 65536 / 2, "preview: a pinch zooms by its ratio (%d)", g_preview_zoom_q16);
    }

    /* The well fills the body, and grows with the panel. */
    (void)ToriRSChrome_ModelViewBox(&g_chrome, view, &box_w, &box_h, &well_w, &well_h);
    CHECK(box_w == g_chrome.widgets[view].w && box_w > 200,
        "preview: the well is the panel's width (%d of %d)", box_w, g_chrome.widgets[view].w);
    {
        int const was_h = box_h;
        ToriRSChrome_PanelSetFixedHeight(&g_chrome, preview, g_chrome.panels[preview].last_rect.h + 200);
        ToriRSChrome_PanelSetScrollable(&g_chrome, preview, 1);
        frame();
        frame();
        (void)ToriRSChrome_ModelViewBox(&g_chrome, view, &box_w, &box_h, &well_w, &well_h);
        CHECK(box_h == was_h + 200 && g_chrome.panels[preview].content_h <= g_chrome.panels[preview].view_h,
            "preview: 200 px taller, the well is 200 px taller and nothing scrolls (%d -> %d)", was_h, box_h);
    }

    /* The pick relay: a popped-out Preview -- another window of the Client,
     * its own registry, solo -- follows this window's picks. */
    if( Editor_HostOpenMapEdEmbedPeer(&peer_host, &g_editor.host, TORIRSMAPED_ROLE_GENERIC, 1) )
    {
        int pv_preview;
        int pv_subject = -1;

        Editor_OpenHost(&peer, &peer_host, "popped-out Preview", &g_profile);
        ToriRSChrome_Init(&pv_chrome);
        ToriRSChrome_SetSurface(&pv_chrome, 800, 600);
        Editor_UiInit(&pv_ui, &pv_chrome, &peer);
        pv_ui.app_services.user_data = &g_preview_app_stand_in;
        Editor_Catalog_Register(&pv_ui);
        CHECK(Editor_UiSetSolo(&pv_ui, "preview"), "preview: a window can be the Preview alone");
        Editor_UiSetVisible(&pv_ui, 1);
        Editor_SetStateCallback(&peer, main_on_state, &pv_ui);
        pv_preview = -1;
        for( int i = 0; i < pv_chrome.panel_count; i++ )
            if( strcmp(pv_chrome.panels[i].title, "Preview") == 0 )
                pv_preview = i;
        for( int i = 0; i < pv_chrome.widget_count && pv_subject < 0; i++ )
            if( pv_chrome.widgets[i].panel == pv_preview && pv_chrome.widgets[i].kind == TORIRS_CHROME_W_LABEL )
                pv_subject = i;

        /* The main window picks test_guard (npc 0). */
        ToriRSChrome_DropdownSetSelected(&g_chrome, find_widget(catalog, TORIRS_CHROME_W_DROPDOWN, "Kind"),
            Editor_CatalogModelKindIndex(TORIRSMAPED_CATALOG_NPC));
        activate(find_widget(catalog, TORIRS_CHROME_W_DROPDOWN, "Kind"));
        preview_pick_row(catalog, "", 0);
        CHECK(g_ui.tools.pick_kind == 1 && g_ui.tools.pick_id == 0, "preview: the main window picked test_guard");
        Editor_PumpFacts(&g_editor);
        Editor_PumpFacts(&peer);
        for( int i = 0; i < 3; i++ )
        {
            ToriRSChrome_Build(&pv_chrome);
            Editor_UiTick(&pv_ui);
        }
        ToriRSChrome_Build(&pv_chrome);
        CHECK(pv_ui.tools.pick_kind == 1 && pv_ui.tools.pick_id == 0
                  && Editor_Preview_Subject(&pv_ui, &kind, &id) && kind == 1 && id == 0,
            "preview: the popped-out Preview heard the pick (%d %d)", pv_ui.tools.pick_kind, pv_ui.tools.pick_id);
        CHECK(pv_subject >= 0 && strcmp(pv_chrome.widgets[pv_subject].text, "npc test_guard (0)") == 0,
            "preview: and names it from its catalog (%s)", pv_subject >= 0 ? pv_chrome.widgets[pv_subject].text : "-");
        CHECK(g_ui.tools.pick_kind == 1 && g_ui.tools.pick_id == 0,
            "preview: the main window's own echo left its pick alone");

        /* The echo rule: a pick made before this window's echo returns is not
         * undone by it. */
        ToriRSChrome_DropdownSetSelected(&g_chrome, find_widget(catalog, TORIRS_CHROME_W_DROPDOWN, "Kind"),
            Editor_CatalogModelKindIndex(TORIRSMAPED_CATALOG_LOC));
        activate(find_widget(catalog, TORIRS_CHROME_W_DROPDOWN, "Kind"));
        preview_pick_row(catalog, "door", 0);
        preview_pick_row(catalog, "crate", 0);
        Editor_PumpFacts(&g_editor);
        Editor_PumpFacts(&peer);
        frame();
        ToriRSChrome_Build(&pv_chrome);
        Editor_UiTick(&pv_ui);
        CHECK(g_ui.tools.pick_kind == 0 && g_ui.tools.pick_id == 0 && pv_ui.tools.pick_kind == 0
                  && pv_ui.tools.pick_id == 0,
            "preview: door then crate: both windows end on test_crate (main %d, popped-out %d)", g_ui.tools.pick_id,
            pv_ui.tools.pick_id);

        Editor_SetStateCallback(&peer, NULL, NULL);
        Editor_UiFree(&pv_ui);
        Editor_Close(&peer);
        Editor_PumpFacts(&g_editor);
    }
    else
        CHECK(0, "preview: a second window joins the Client");

    /* Popped out here, it is not opened again by the next pick. */
    frame();
    click_rect(ToriRSChrome_PanelPopOutRect(&g_chrome, preview));
    CHECK(g_pop.calls == 1 && strcmp(g_pop.module, "preview") == 0 && !g_chrome.panels[preview].visible,
        "preview: its Pop out asks for a \"preview\" window and hides it here");
    preview_pick_row(catalog, "door", 0);
    frame();
    CHECK(!g_chrome.panels[preview].visible && Editor_Preview_AutoOpens(&g_ui) == 1,
        "preview: popped out, a pick does not open it here");
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_PREVIEW);
    frame();
    frame();
    CHECK(g_chrome.panels[preview].visible && !Editor_UiPanelPoppedOut(&g_ui, "preview"),
        "preview: View > Preview (Ctrl+Shift+P) pops it back in");

    /* The Close box hides it, and the user's close holds for the session:
     * neither a pick nor the Catalog opening again opens it. */
    click_rect(ToriRSChrome_PanelCloseRect(&g_chrome, preview));
    CHECK(!g_chrome.panels[preview].visible, "preview: the Close box hides it");
    row = view_row("Preview");
    CHECK(row && strstr(row, "hidden"), "preview: the View row says hidden (%s)", row ? row : "-");
    preview_pick_row(catalog, "crate", 0);
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_CATALOG);
    frame();
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_CATALOG);
    frame();
    frame();
    CHECK(g_chrome.panels[catalog].visible && !g_chrome.panels[preview].visible
              && Editor_Preview_AutoOpens(&g_ui) == 1,
        "preview: closed by the user, it stays closed through a pick and a Catalog reopen");

    /* A layout saved before the Preview existed: everything else applies,
     * the Preview keeps its default (closed, its registration box). */
    Editor_UiLayoutCapture(&g_ui, &file);
    EditorLayout_Clear(&without);
    without.version = file.version;
    without.surface_w = file.surface_w;
    without.surface_h = file.surface_h;
    without.scale = file.scale;
    without.has_snap = file.has_snap;
    without.snap = file.snap;
    for( int i = 0; i < file.panel_count; i++ )
        if( strcmp(file.panels[i].name, "preview") != 0 )
            *EditorLayout_Add(&without, file.panels[i].name) = file.panels[i];
    CHECK(EditorLayout_Find(&file, "preview") && !EditorLayout_Find(&without, "preview"),
        "preview: the saved layout names it; the old file does not");
    Editor_SetStateCallback(&g_editor, NULL, NULL);
    Editor_UiFree(&g_ui);
    {
        /* The control: the same file WITH the Preview, booted the same way. */
        unsigned char shown[TORIRS_CHROME_MAX_PANELS];
        int same = 1;

        layout_boot_catalog(1280, 800, &file);
        for( int i = 0; i < g_chrome.panel_count; i++ )
            shown[i] = (unsigned char)g_chrome.panels[i].visible;
        Editor_UiFree(&g_ui);
        layout_boot_catalog(1280, 800, &without);
        preview = find_panel("Preview");
        catalog = find_panel("Catalog");
        CHECK(preview >= 0 && catalog >= 0, "preview: a layout without \"preview\" loads");
        for( int i = 0; i < g_chrome.panel_count; i++ )
            if( i != preview && g_chrome.panels[i].visible != shown[i] )
            {
                printf("  %s: open %d without the preview line, %d with it\n", g_chrome.panels[i].title,
                    g_chrome.panels[i].visible, shown[i]);
                same = 0;
            }
        CHECK(same, "preview: every other panel comes back as it does from the file that names the Preview");
        CHECK(preview >= 0 && !g_chrome.panels[preview].user_placed
                  && g_chrome.panels[preview].visible == g_chrome.panels[catalog].visible,
            "preview: the Preview takes its default -- beside the Catalog when that opens (catalog %d preview %d)",
            g_chrome.panels[catalog].visible, preview >= 0 ? g_chrome.panels[preview].visible : -1);
    }
    Editor_UiFree(&g_ui);
}

/** Every saved field of two files agrees. */
static int
layout_files_equal(
    const struct EditorLayoutFile* a,
    const struct EditorLayoutFile* b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void
layout_boot_saved(
    int w,
    int h,
    int scale,
    const struct EditorLayoutFile* file);

/* One click on a panel's title at chrome time `ms`. */
static void
title_click(
    int x,
    int y,
    int64_t ms)
{
    ToriRSChrome_Tick(&g_chrome, ms);
    ToriRSChrome_MouseMove(&g_chrome, x, y);
    (void)ToriRSChrome_MouseDown(&g_chrome, x, y);
    (void)ToriRSChrome_MouseUp(&g_chrome, x, y);
    frame();
}

static int
rect_is(
    struct ToriRSChromeRect r,
    int x,
    int y,
    int w,
    int h)
{
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

/*
 * A double-click on a panel's title maximises it over the whole dock surface
 * (below the menu bar) and over every other panel; another restores the box
 * it had; a title drag while maximised restores it first and then moves it;
 * a slow second click is two clicks; the saved layout records the state and
 * the box it restores to, and a boot with that file comes back maximised.
 */
static void
test_maximise(void)
{
    struct EditorLayoutFile saved;
    struct EditorLayoutFile reread;
    struct ToriRSChromeRect before;
    struct ToriRSChromeRect bar;
    const struct EditorLayoutPanel* sp;
    int terrain;
    int menubar = -1;
    int top;
    int bottom = 1006;
    int tx;
    int ty;
    char* text = NULL;
    int size = 0;

    layout_boot(1530, 1006, 1);
    terrain = find_panel("Terrain");
    CHECK(terrain >= 0 && g_chrome.panels[terrain].visible, "Terrain is open");
    if( terrain < 0 )
        return;
    /* By module: the status bar is a MENUBAR-style strip too. */
    {
        struct EditorUiPanel const* menu_module = Editor_UiFindPanel(&g_ui, "menubar");
        struct EditorUiPanel const* status_module = Editor_UiFindPanel(&g_ui, "statusbar");
        if( menu_module && g_chrome.panels[menu_module->chrome_panel].visible )
            menubar = menu_module->chrome_panel;
        /* The area stops above the status bar at the foot. */
        if( status_module )
            bottom = g_chrome.panels[status_module->chrome_panel].last_rect.y;
    }
    bar = menubar >= 0 ? g_chrome.panels[menubar].last_rect : (struct ToriRSChromeRect){ 0, 0, 0, 0 };
    top = bar.y + bar.h;
    before = g_chrome.panels[terrain].last_rect;
    tx = before.x + 24;
    ty = before.y + 6;

    /* Too slow: two clicks, nothing maximised. */
    title_click(tx, ty, 10000);
    title_click(tx, ty, 10000 + TORIRS_CHROME_DOUBLE_CLICK_MS + 50);
    CHECK(!g_chrome.panels[terrain].maximised, "two title clicks further apart than the double-click time do nothing");

    /* Double-click: the whole surface between the menu bar and the status
     * bar, over the others. */
    title_click(tx, ty, 20000);
    title_click(tx + 1, ty, 20120);
    CHECK(g_chrome.panels[terrain].maximised, "a title double-click maximises the panel");
    CHECK(bottom < 1006 && rect_is(g_chrome.panels[terrain].last_rect, 0, top, 1530, bottom - top),
        "to the dock surface between the bars (%d,%d %dx%d, menu bar ends at %d, status bar starts at %d)",
        g_chrome.panels[terrain].last_rect.x, g_chrome.panels[terrain].last_rect.y,
        g_chrome.panels[terrain].last_rect.w, g_chrome.panels[terrain].last_rect.h, top, bottom);
    /* Over the others: a point inside another panel now belongs to it. */
    {
        int const palette = find_panel("Palette");
        if( palette >= 0 && g_chrome.panels[palette].visible )
        {
            struct ToriRSChromeRect const pr = g_chrome.panels[palette].last_rect;
            int other = 0;
            for( int w = g_chrome.panels[palette].first_widget; w >= 0; w = g_chrome.widgets[w].next )
                if( ToriRSChrome_HitTest(&g_chrome, g_chrome.widgets[w].x + 1, g_chrome.widgets[w].y + 1) == w )
                    other++;
            CHECK(other == 0, "no widget of a panel under it can be hit (%d) (Palette at %d,%d)", other, pr.x, pr.y);
        }
    }
    {
        int x = 0, y = 0, fw = 0, fh = 0, placed = 0;
        CHECK(ToriRSChrome_PanelRestoreBox(&g_chrome, terrain, &x, &y, &fw, &fh, &placed) && x == before.x
                  && y == before.y,
            "it keeps the box it had (%d,%d)", x, y);
    }

    /* A second double-click restores that box. */
    title_click(tx, ty, 30000);
    title_click(tx, ty, 30100);
    frame();
    CHECK(!g_chrome.panels[terrain].maximised, "a second double-click restores it");
    CHECK(rect_is(g_chrome.panels[terrain].last_rect, before.x, before.y, before.w, before.h),
        "to exactly the box it had (%d,%d %dx%d, was %d,%d %dx%d)", g_chrome.panels[terrain].last_rect.x,
        g_chrome.panels[terrain].last_rect.y, g_chrome.panels[terrain].last_rect.w,
        g_chrome.panels[terrain].last_rect.h, before.x, before.y, before.w, before.h);

    /* Maximised, then a title drag: restored first (its own width again),
     * then carried by the pointer. */
    title_click(tx, ty, 40000);
    title_click(tx, ty, 40100);
    CHECK(g_chrome.panels[terrain].maximised, "maximised again");
    ToriRSChrome_Tick(&g_chrome, 41000);
    {
        struct ToriRSChromeRect after;
        int const from_x = 400;
        int const from_y = top + 6;
        drag(from_x, from_y, from_x + 120, from_y + 200);
        frame();
        after = g_chrome.panels[terrain].last_rect;
        CHECK(!g_chrome.panels[terrain].maximised, "a title drag restores a maximised panel");
        CHECK(after.w == before.w && after.h == before.h, "at its own size (%dx%d, was %dx%d)", after.w, after.h,
            before.w, before.h);
        CHECK(after.x <= from_x + 120 && after.x + after.w > from_x + 120 && after.y <= from_y + 200
                  && after.y + 30 > from_y + 200,
            "and under the pointer where it was let go (%d,%d)", after.x, after.y);
    }

    /* Maximise, save, and boot with the file: maximised again, and restoring
     * puts back the box the file recorded. */
    before = g_chrome.panels[terrain].last_rect;
    tx = before.x + 24;
    ty = before.y + 6;
    title_click(tx, ty, 50000);
    title_click(tx, ty, 50100);
    CHECK(g_chrome.panels[terrain].maximised, "maximised before the save");
    Editor_UiLayoutCapture(&g_ui, &saved);
    sp = EditorLayout_Find(&saved, "terrain");
    CHECK(sp && sp->has_maximised && sp->maximised, "the layout records the maximised state");
    CHECK(sp && sp->x == before.x && sp->y == before.y && sp->user_placed,
        "and the box it restores to, not the maximised one (%d,%d)", sp ? sp->x : -1, sp ? sp->y : -1);
    EditorLayout_Encode(&saved, &text, &size);
    CHECK(text && strstr(text, "maximised = 1") != NULL, "the file says maximised = 1");
    CHECK(EditorLayout_Decode(&reread, text, size) && layout_files_equal(&saved, &reread),
        "and survives encode -> decode");
    ToriMem_Free(ToriMem_Process(), text);
    Editor_UiFree(&g_ui);

    layout_boot_saved(1530, 1006, 1, &reread);
    terrain = find_panel("Terrain");
    frame();
    CHECK(terrain >= 0 && g_chrome.panels[terrain].maximised, "a boot with that file comes back maximised");
    CHECK(terrain >= 0 && rect_is(g_chrome.panels[terrain].last_rect, 0, top, 1530, bottom - top),
        "over the dock surface");
    if( terrain >= 0 )
    {
        title_click(30, top + 6, 60000);
        title_click(30, top + 6, 60100);
        frame();
        CHECK(!g_chrome.panels[terrain].maximised
                  && rect_is(g_chrome.panels[terrain].last_rect, before.x, before.y, before.w, before.h),
            "and restores to the saved box (%d,%d %dx%d)", g_chrome.panels[terrain].last_rect.x,
            g_chrome.panels[terrain].last_rect.y, g_chrome.panels[terrain].last_rect.w,
            g_chrome.panels[terrain].last_rect.h);
    }

    /* Reset layout returns it unmaximised. */
    if( terrain >= 0 )
    {
        title_click(before.x + 24, before.y + 6, 70000);
        title_click(before.x + 24, before.y + 6, 70100);
        CHECK(g_chrome.panels[terrain].maximised, "maximised before Reset layout");
        Editor_UiResetLayout(&g_ui);
        frame();
        CHECK(!g_chrome.panels[terrain].maximised, "Reset layout restores a maximised panel");
    }
    Editor_UiFree(&g_ui);
}

static void
test_layout_codec(void)
{
    struct EditorLayoutFile a;
    struct EditorLayoutFile b;
    struct EditorLayoutPanel* p;
    const struct EditorLayoutPanel* q;
    char* text = NULL;
    int size = 0;

    /* encode -> decode is the identity. */
    EditorLayout_Clear(&a);
    a.version = EDITOR_LAYOUT_VERSION;
    a.surface_w = 1530;
    a.surface_h = 1006;
    a.scale = 2;
    a.has_snap = 1;
    a.snap = 0;
    p = EditorLayout_Add(&a, "world");
    p->has_visible = p->has_user_placed = p->has_x = p->has_y = p->has_w = p->has_h = 1;
    p->visible = 1;
    p->user_placed = 1;
    p->x = 40;
    p->y = 24;
    p->w = 600;
    p->h = 400;
    p = EditorLayout_Add(&a, "terrain");
    p->has_visible = 1;
    p->visible = 0;
    p = EditorLayout_Add(&a, "history");
    p->has_x = p->has_y = 1;
    p->x = -3;
    p->y = 0;
    p->has_collapsed = 1;
    p->collapsed = 1;
    a.has_compact = 1;
    a.compact = -1;
    a.has_window = 1;
    a.window_x = 120;
    a.window_y = 80;
    a.window_w = 1366;
    a.window_h = 768;
    /* A panel group's section (workbench 6.0): the panel keys plus its
     * front tab, by module name. */
    {
        struct EditorLayoutGroup* g = EditorLayout_AddGroup(&a, "right-top");
        g->has_visible = g->has_user_placed = g->has_x = g->has_y = g->has_w = g->has_h = g->has_maximised = 1;
        g->visible = 1;
        g->user_placed = 1;
        g->x = 1194;
        g->y = 237;
        g->w = 336;
        g->h = 217;
        g->has_shown = 1;
        snprintf(g->shown, sizeof(g->shown), "%s", "catalog");
        CHECK(EditorLayout_AddGroup(&a, "right-top") == g && a.group_count == 1, "AddGroup finds a group by name");
    }
    EditorLayout_Encode(&a, &text, &size);
    CHECK(strstr(text, "compact = -1\n") && strstr(text, "window_w = 1366\n") && strstr(text, "collapsed = 1\n"),
        "the file states compact, the window box and a collapsed panel");
    CHECK(text && strstr(text, "\n[group right-top]\n") && strstr(text, "shown = catalog\n"),
        "the group encodes as a [group] section");
    CHECK(text && size > 0 && (int)strlen(text) == size, "the layout encodes (%d bytes)", size);
    CHECK(EditorLayout_Decode(&b, text, size), "the encoded layout decodes as a layout");
    CHECK(layout_files_equal(&a, &b), "encode -> decode is the identity");
    ToriMem_Free(ToriMem_Process(), text);

    /* A partial file: what it states, nothing else. */
    {
        const char* partial = "[layout]\nsnap = 0\n\n[panel terrain]\nvisible = 0\n";
        CHECK(EditorLayout_Decode(&b, partial, (int)strlen(partial)), "a partial file is a layout");
        q = EditorLayout_Find(&b, "terrain");
        CHECK(b.has_snap && !b.snap && b.surface_w == 0 && b.panel_count == 1, "partial: snap only in [layout]");
        CHECK(q && q->has_visible && !q->visible && !q->has_x && !q->has_user_placed && !q->has_w,
            "partial: terrain states only visible");
    }
    /* Unknown sections and keys are ignored; malformed values skipped. */
    {
        const char* odd = "; a comment\n[layout]\nfoo = 1\nsurface_w = 800\nscale = 0\n[colours]\nx = 5\n"
                          "[panel world]\nbogus = 3\nx = 12 ; moved by hand\nw = abc\nh = -4\n"
                          "[panelists]\nx = 9\n";
        CHECK(EditorLayout_Decode(&b, odd, (int)strlen(odd)), "a file with unknown parts still decodes");
        q = EditorLayout_Find(&b, "world");
        CHECK(b.surface_w == 800 && b.scale == 0 && b.panel_count == 1, "unknown keys and sections are ignored");
        CHECK(q && q->has_x && q->x == 12 && !q->has_w && !q->has_h && !q->has_y,
            "a trailing comment is allowed; non-numeric and negative sizes are skipped");
    }
    /* Not a layout file at all, and an empty one. */
    {
        const char* prefs = "[preferences]\nversion=1\n";
        CHECK(!EditorLayout_Decode(&b, prefs, (int)strlen(prefs)) && b.panel_count == 0,
            "a file with no [layout] is not a layout");
        CHECK(!EditorLayout_Decode(&b, "", 0), "an empty file is not a layout");
    }
    /* A file from before groups has none; a member's [panel] stays a panel. */
    {
        const char* old = "[layout]\n[panel catalog]\nvisible = 1\nx = 100\n";
        CHECK(EditorLayout_Decode(&b, old, (int)strlen(old)) && b.group_count == 0 && b.panel_count == 1
                  && EditorLayout_Find(&b, "catalog")->x == 100,
            "an old file decodes with no groups");
    }
    /* TORIRS_EDITOR_LAYOUT: unset = default, empty = off, else the path. */
    {
        const char* was = getenv("TORIRS_EDITOR_LAYOUT");
        char saved[1024];
        snprintf(saved, sizeof(saved), "%s", was ? was : "");
        unsetenv("TORIRS_EDITOR_LAYOUT");
        CHECK(strcmp(EditorLayout_Path(), EDITOR_LAYOUT_DEFAULT_PATH) == 0, "unset: the default name");
#ifndef _WIN32
        /* The Windows CRT holds no empty variable (setting one to "" deletes
         * it), so there "empty" cannot be said; the unset case is above. */
        setenv("TORIRS_EDITOR_LAYOUT", "", 1);
        CHECK(EditorLayout_Path() == NULL, "empty: persistence off");
#endif
        setenv("TORIRS_EDITOR_LAYOUT", "/x/y.ini", 1);
        CHECK(strcmp(EditorLayout_Path(), "/x/y.ini") == 0, "set: that path");
        if( was )
            setenv("TORIRS_EDITOR_LAYOUT", saved, 1);
        else
            unsetenv("TORIRS_EDITOR_LAYOUT");
    }
}

/** A boot whose saved layout is read while the panels come up: the default
 *  layout is held until the file lands, then the file is applied first. */
static void
layout_boot_saved(
    int w,
    int h,
    int scale,
    const struct EditorLayoutFile* file)
{
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetScale(&g_chrome, scale);
    ToriRSChrome_SetSurface(&g_chrome, w, h);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.hooks.open_square = fake_open_square;
    g_ui.hooks.open_in_new_view = fake_open_in_new_view;
    g_ui.hooks.floor_rgb = fake_floor_rgb;
    g_ui.hooks.floor_request = fake_floor_request;
    g_ui.hooks.view_window = fake_view_window;
    g_ui.hooks.selection_changed = fake_selection_changed;
    Editor_UiRegisterToolsPanels(&g_ui);
    ungroup_all(&g_ui);
    Editor_UiLayoutHold(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    frame();
    frame();
    CHECK(!g_ui.laid_out && Editor_UiLayoutBusy(&g_ui), "the default layout waits while the saved one is read");
    Editor_UiLayoutLoaded(&g_ui, file);
    for( int i = 0; i < 4; i++ )
        frame();
}

static void
test_saved_layout(void)
{
    struct EditorLayoutFile saved;
    struct EditorLayoutFile reread;
    struct ToriRSChromeRect before[TORIRS_CHROME_MAX_PANELS];
    int visible_before[TORIRS_CHROME_MAX_PANELS];
    int panel_count;
    int wp;
    int squares;
    int terrain;
    char* text = NULL;
    int size = 0;
    uint32_t fingerprint;

    /* A session: World dragged and resized, Squares closed, snap off. */
    layout_boot(1530, 1006, 1);
    wp = find_panel("World");
    squares = find_panel("Squares");
    terrain = find_panel("Terrain");
    {
        struct ToriRSChromeRect r = g_chrome.panels[wp].last_rect;
        drag(r.x + 60, r.y + 6, r.x + 60 + 64, r.y + 6 + 48);
        r = g_chrome.panels[wp].last_rect;
        drag(r.x + r.w - 4, r.y + r.h - 4, r.x + r.w - 4 - 300, r.y + r.h - 4 - 200);
    }
    if( squares >= 0 && g_chrome.panels[squares].visible )
        view_toggle("Squares");
    Editor_UiSetSnap(&g_ui, 0);
    frame();
    frame();
    panel_count = g_chrome.panel_count;
    for( int i = 0; i < panel_count; i++ )
    {
        before[i] = g_chrome.panels[i].last_rect;
        visible_before[i] = g_chrome.panels[i].visible;
    }
    Editor_UiLayoutCapture(&g_ui, &saved);
    CHECK(saved.surface_w == 1530 && saved.surface_h == 1006 && saved.scale == 1 && saved.has_snap && !saved.snap,
        "the capture states the surface, the scale and snap");
    CHECK(EditorLayout_Find(&saved, "world") && EditorLayout_Find(&saved, "world")->user_placed,
        "the capture holds the placed World panel");
    CHECK(EditorLayout_Find(&saved, "menubar") == NULL, "the titleless menu bar is not part of the file");
    EditorLayout_Encode(&saved, &text, &size);
    CHECK(EditorLayout_Decode(&reread, text, size) && layout_files_equal(&saved, &reread),
        "the captured layout survives encode -> decode");
    printf("saved layout (%d bytes):\n%.*s", size, size > 420 ? 420 : size, text);
    printf("...\n");
    ToriMem_Free(ToriMem_Process(), text);
    Editor_UiFree(&g_ui);

    /* Boot with it on the same surface: every panel where it was. */
    layout_boot_saved(1530, 1006, 1, &reread);
    {
        int same = 1;
        for( int i = 0; i < panel_count && i < g_chrome.panel_count; i++ )
        {
            struct ToriRSChromeRect const r = g_chrome.panels[i].last_rect;
            if( g_chrome.panels[i].visible != visible_before[i]
                || (visible_before[i]
                    && (r.x != before[i].x || r.y != before[i].y || r.w != before[i].w || r.h != before[i].h)) )
            {
                same = 0;
                printf("  differs: '%s' %d,%d %dx%d v%d (was %d,%d %dx%d v%d)\n", g_chrome.panels[i].title, r.x, r.y,
                    r.w, r.h, g_chrome.panels[i].visible, before[i].x, before[i].y, before[i].w, before[i].h,
                    visible_before[i]);
            }
        }
        CHECK(same, "a boot with the saved layout puts every panel back where it was");
    }
    CHECK(!g_ui.snap && g_chrome.snap_grid == 0, "the saved Snap to grid comes back");
    CHECK(g_chrome.panels[wp].user_placed, "the World panel is still the user's");
    CHECK(squares < 0 || !g_chrome.panels[squares].visible, "the closed Squares panel stays closed");
    fingerprint = Editor_UiLayoutFingerprint(&g_ui);

    /* Reset layout still means the DEFAULT, not the file -- and it changes
     * the fingerprint, so the host writes the default back. */
    Editor_UiResetLayout(&g_ui);
    frame();
    frame();
    CHECK(!g_chrome.panels[wp].user_placed && (squares < 0 || g_chrome.panels[squares].visible),
        "Reset layout after a saved layout returns to the default set");
    layout_check("reset after a saved layout");
    CHECK(Editor_UiLayoutFingerprint(&g_ui) != fingerprint, "the reset is a change the host saves");
    {
        struct EditorLayoutFile after;
        int placed = 0;
        Editor_UiLayoutCapture(&g_ui, &after);
        for( int i = 0; i < after.panel_count; i++ )
            placed += after.panels[i].user_placed;
        CHECK(placed == 0, "the saved reset has no placed panels (%d)", placed);
    }
    Editor_UiFree(&g_ui);

    /* A stale surface (another monitor): the placed World keeps its saved
     * box, the column is laid out for the new surface. */
    layout_boot_saved(1920, 1080, 1, &reread);
    {
        struct EditorLayoutPanel const* sw = EditorLayout_Find(&reread, "world");
        CHECK(g_chrome.panels[wp].user_placed && g_chrome.panels[wp].x == sw->x && g_chrome.panels[wp].y == sw->y
                  && g_chrome.panels[wp].fixed_w == sw->w,
            "stale surface: the placed World keeps its saved box");
        CHECK(terrain >= 0 && g_chrome.panels[terrain].x == g_ui.dock_x && g_ui.dock_x + g_ui.dock_w == 1920,
            "stale surface: the column is laid out for 1920 (Terrain at %d)", g_chrome.panels[terrain].x);
    }
    Editor_UiFree(&g_ui);

    /* Saved at 1x, opened at 2x: the placed geometry scales. */
    layout_boot_saved(2560, 1440, 2, &reread);
    {
        struct EditorLayoutPanel const* sw = EditorLayout_Find(&reread, "world");
        CHECK(g_chrome.panels[wp].x == sw->x * 2 && g_chrome.panels[wp].fixed_w == sw->w * 2,
            "a 1x layout opened at 2x scales the placed World (%d -> %d)", sw->w, g_chrome.panels[wp].fixed_w);
    }
    Editor_UiFree(&g_ui);

    /* A placed panel off a smaller surface is pulled back onto it. */
    {
        struct EditorLayoutFile off = reread;
        struct EditorLayoutPanel* sw = EditorLayout_Add(&off, "world");
        sw->x = 3000;
        sw->y = 2000;
        layout_boot_saved(1280, 800, 1, &off);
        CHECK(g_chrome.panels[wp].x + g_chrome.panels[wp].fixed_w <= 1280
                  && g_chrome.panels[wp].y + g_chrome.panels[wp].fixed_h <= 800,
            "a placed panel saved off a smaller surface is pulled back onto it (%d,%d)", g_chrome.panels[wp].x,
            g_chrome.panels[wp].y);
        Editor_UiFree(&g_ui);
    }

    /* No file: the default layout, released by the empty answer. */
    layout_boot_saved(1530, 1006, 1, NULL);
    CHECK(g_ui.laid_out && !g_chrome.panels[wp].user_placed, "no saved layout: the default");
    layout_check("boot with no saved layout");
    Editor_UiFree(&g_ui);

    /* Through the real IO queue and the desktop executor, the way the App
     * writes and reads it: the file the next launch reads is the one the
     * platform wrote. */
    {
        char path[1024];
        struct PlatformX_IO* px = PlatformX_IO_New();
        struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
        struct ToriRS_Task* task;
        static struct EditorLayoutLoad load;
        FILE* f;
        char head[64] = { 0 };

        snprintf(path, sizeof(path), "%s.layout.ini", g_tmp_root);
        remove(path);
        task = CreateTask_EditorLayoutLoad(&load, path);
        io->task = task;
        while( task_run(task, io) == PT_YIELDED )
            PlatformX_IO_Process(px, io);
        task_free(task);
        CHECK(load.done && !load.found, "a missing layout file is a first launch (done, not found)");

        task = CreateTask_EditorLayoutSave(&saved, path);
        io->task = task;
        while( task_run(task, io) == PT_YIELDED )
            PlatformX_IO_Process(px, io);
        task_free(task);
        f = fopen(path, "rb");
        CHECK(f != NULL, "the IO queue wrote %s", path);
        if( f )
        {
            (void)!fread(head, 1, sizeof(head) - 1, f);
            fclose(f);
        }
        CHECK(strstr(head, "ToriRSEd dock layout") != NULL, "the file is the layout text");

        task = CreateTask_EditorLayoutLoad(&load, path);
        io->task = task;
        while( task_run(task, io) == PT_YIELDED )
            PlatformX_IO_Process(px, io);
        task_free(task);
        CHECK(load.done && load.found && layout_files_equal(&load.file, &saved),
            "the IO queue reads back exactly the layout it wrote");
        ToriRS_IOBatch_Free(io);
        PlatformX_IO_Free(px);

        /* And a boot from what the queue read. */
        layout_boot_saved(1530, 1006, 1, &load.file);
        CHECK(g_chrome.panels[wp].user_placed && g_chrome.panels[wp].last_rect.x == before[wp].x
                  && g_chrome.panels[wp].last_rect.w == before[wp].w,
            "a boot from the file the queue read applies it");
        Editor_UiFree(&g_ui);
    }
}

/* ---- editing feedback: brush footprint, spawn ghosts, the History panel ------------ */

#include "editor/map_editor_ghost.h"

static struct ToriRSChrome g_fb_chrome;
static struct EditorUi g_fb_ui;

static void
fb_frame(void)
{
    ToriRSChrome_Build(&g_fb_chrome);
    Editor_UiTick(&g_fb_ui);
    ToriRSChrome_Build(&g_fb_chrome);
}

/** The footprint the preview draws is the set of tiles the stroke edits, for
 *  every falloff and a spread of radii: one weight function, two callers. */
static void
fb_test_footprint(void)
{
    static struct Editor_BrushTile tiles[EDITOR_BRUSH_TILES_MAX];
    static unsigned char marked[2 * EDITOR_BRUSH_RADIUS_MAX + 1][2 * EDITOR_BRUSH_RADIUS_MAX + 1];
    static const int k_radii[] = { 0, 1, 2, 3, 5, 9 };
    int const cx = SQ_X0 + 32;
    int const cz = SQ_Z0 + 32;
    int mismatches = 0;
    int cases = 0;

    for( int f = 0; f < EDITOR_BRUSH_FALLOFF_COUNT; f++ )
        for( int r = 0; r < (int)(sizeof(k_radii) / sizeof(k_radii[0])); r++ )
        {
            struct Editor_BrushOp op;
            int n;
            int count;
            int inside = 1;

            memset(&op, 0, sizeof(op));
            op.tool = EDITOR_TOOL_RAISE;
            op.world_x = cx;
            op.world_z = cz;
            op.brush.radius = k_radii[r];
            op.brush.falloff = (enum Editor_BrushFalloff)f;
            op.brush.step = 4;
            n = Editor_Terrain_BrushFootprint(&op.brush, tiles, EDITOR_BRUSH_TILES_MAX);
            memset(marked, 0, sizeof(marked));
            for( int i = 0; i < n; i++ )
            {
                marked[tiles[i].dz + op.brush.radius][tiles[i].dx + op.brush.radius] = 1;
                if( tiles[i].weight != Editor_BrushWeight(&op.brush, tiles[i].dx, tiles[i].dz) )
                    inside = 0;
            }
            count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
            for( int i = 0; i < count; i++ )
            {
                int const dx = 50 * 64 + g_cmds[i].u.tile.x - cx;
                int const dz = 50 * 64 + g_cmds[i].u.tile.z - cz;
                if( dx < -op.brush.radius || dx > op.brush.radius || dz < -op.brush.radius || dz > op.brush.radius
                    || !marked[dz + op.brush.radius][dx + op.brush.radius] )
                    inside = 0;
            }
            if( count != n || !inside )
                mismatches++;
            cases++;
        }
    CHECK(mismatches == 0, "footprint: the preview's tiles and weights are the stroke's, %d cases (%d differ)",
        cases, mismatches);
    {
        struct Editor_Brush brush = { 2, EDITOR_BRUSH_FALLOFF_LINEAR, 4, 50 };
        int const n = Editor_Terrain_BrushFootprint(&brush, tiles, EDITOR_BRUSH_TILES_MAX);
        CHECK(n == 21 && tiles[0].dx == 0 && tiles[0].dz == 0 && tiles[0].weight == 1.0,
            "footprint: a radius-2 disc is 21 tiles, centre first at full weight (%d)", n);
        CHECK(Editor_Terrain_BrushFootprint(&brush, tiles, 4) == 4, "footprint: a short array takes what fits");
        CHECK(Editor_Terrain_ToolUsesFalloff(EDITOR_TOOL_RAISE) && !Editor_Terrain_ToolUsesFalloff(EDITOR_TOOL_PAINT_UNDERLAY)
                  && !Editor_Terrain_ToolUsesFalloff(EDITOR_TOOL_FLATTEN),
            "footprint: height brushes fade, flatten and paint do not");
    }
}

/** The spawn ghost's bookkeeping: pending until landed, a remove hands back
 *  exactly what landed (or nothing), and the loc ghost leaves it alone. */
static void
fb_test_spawn_ghost(void)
{
    struct MapEditorGhost ghost;
    struct MapEditorSpawnSpec npc = { MAP_EDITOR_SPAWN_NPC, 0, 40, 41, 0 };
    struct MapEditorSpawnSpec obj = { MAP_EDITOR_SPAWN_OBJ, 0, 40, 41, 0 };
    struct MapEditorSpawnSpec moved = npc;
    struct MapEditorSpawnSpec out;
    struct MapEditorGhostSpec loc = { 1530, 10, 10, 0, 10, 0 };
    struct MapEditorGhostSpec removed;
    struct MapEditorGhostSpec restore;
    bool has_restore;
    int entity = 99;

    MapEditorGhost_Reset(&ghost);
    CHECK(!MapEditorSpawnGhost_Matches(&ghost.spawn, &npc), "spawn ghost: none up matches nothing");
    MapEditorSpawnGhost_Place(&ghost.spawn, &npc);
    CHECK(MapEditorSpawnGhost_Pending(&ghost.spawn) && MapEditorSpawnGhost_Matches(&ghost.spawn, &npc),
        "spawn ghost: an npc ghost is pending until its body lands");
    moved.scene_x++;
    CHECK(!MapEditorSpawnGhost_Matches(&ghost.spawn, &moved) && !MapEditorSpawnGhost_Matches(&ghost.spawn, &obj),
        "spawn ghost: another tile or kind is another ghost");
    MapEditorSpawnGhost_Land(&ghost.spawn, 7, 3, 2);
    CHECK(!MapEditorSpawnGhost_Pending(&ghost.spawn) && ghost.spawn.size == 2,
        "spawn ghost: landed as pool 7, 2x2 footprint");

    /* The loc ghost's place / commit / remove clear the loc ghost alone. */
    MapEditorGhost_Place(&ghost, &loc, NULL);
    MapEditorGhost_Commit(&ghost);
    MapEditorGhost_Place(&ghost, &loc, NULL);
    (void)MapEditorGhost_Remove(&ghost, &removed, &has_restore, &restore);
    CHECK(ghost.spawn.active && ghost.spawn.entity == 7, "spawn ghost: the loc ghost's place/commit/remove leave it up");

    CHECK(MapEditorSpawnGhost_Remove(&ghost.spawn, 3, &out, &entity) && entity == 7 && out.kind == MAP_EDITOR_SPAWN_NPC,
        "spawn ghost: removal hands back the npc it became");
    CHECK(!MapEditorSpawnGhost_Remove(&ghost.spawn, 3, &out, &entity), "spawn ghost: a second removal has nothing");

    MapEditorSpawnGhost_Place(&ghost.spawn, &obj);
    entity = 99;
    CHECK(MapEditorSpawnGhost_Remove(&ghost.spawn, 3, &out, &entity) && entity == -1 && out.kind == MAP_EDITOR_SPAWN_OBJ,
        "spawn ghost: an obj ghost that never landed despawns nothing");
    MapEditorSpawnGhost_Place(&ghost.spawn, &obj);
    MapEditorSpawnGhost_Land(&ghost.spawn, 4, 3, 1);
    CHECK(MapEditorSpawnGhost_Remove(&ghost.spawn, 4, &out, &entity) && entity == -1,
        "spawn ghost: after a rebuild (another load_seq) the entity is already gone");
}

/** Synthetic facts through the derived history: names, a cut, a merge, a
 *  remote redo. */
static void
fb_test_history_model(void)
{
    struct Editor_History history;
    struct Editor_Cmd command;
    char name[160];

    Editor_HistoryInit(&history);

    /* A 12x8 paste: every tile gains an underlay and an overlay. */
    Editor_HistoryBreak(&history, NULL);
    for( int z = 0; z < 8; z++ )
        for( int x = 0; x < 12; x++ )
        {
            Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, 50, 50);
            command.u.tile.x = 4 + x;
            command.u.tile.z = 6 + z;
            command.u.tile.after.underlay_id = 3;
            command.u.tile.after.has_overlay = 1;
            command.u.tile.after.overlay_id = 2;
            Editor_HistoryOnFact(&history, EDITOR_CMD_FORWARD, &command);
        }
    CHECK(history.count == 1 && history.groups[0].cmd_count == 96, "history: a pump of 96 facts is one group");
    Editor_HistoryGroupName(&history.groups[0], name, sizeof(name));
    CHECK(strcmp(name, "Paste 12x8 m50_50 L0") == 0, "history: names a paste (%s)", name);

    /* A loc placement, a spawn, a config field: one group each. */
    Editor_HistoryBreak(&history, NULL);
    Editor_CmdInitSquare(&command, EDITOR_CMD_LOC, 50, 50);
    command.has_after = 1;
    command.u.loc.after.loc_id = 1530;
    command.u.loc.after.shape = 10;
    Editor_HistoryOnFact(&history, EDITOR_CMD_FORWARD, &command);
    Editor_HistoryGroupName(&history.groups[1], name, sizeof(name));
    CHECK(strcmp(name, "Place loc 1530 m50_50") == 0, "history: names a loc placement (%s)", name);

    Editor_HistoryBreak(&history, NULL);
    Editor_CmdInit(&command, EDITOR_DOC_SPAWN, "m50_50", EDITOR_CMD_SPAWN);
    command.has_after = 1;
    snprintf(command.u.spawn.name, sizeof(command.u.spawn.name), "goblin");
    command.u.spawn.abs_x = 3200;
    command.u.spawn.abs_z = 3238;
    Editor_HistoryOnFact(&history, EDITOR_CMD_FORWARD, &command);
    Editor_HistoryGroupName(&history.groups[2], name, sizeof(name));
    CHECK(strcmp(name, "Spawn npc goblin 3200,3238") == 0, "history: names a spawn (%s)", name);

    Editor_HistoryBreak(&history, NULL);
    Editor_CmdInit(&command, EDITOR_DOC_CFG, "loc:door", EDITOR_CMD_CFG_FIELD);
    snprintf(command.u.cfg_field.key, sizeof(command.u.cfg_field.key), "name");
    Editor_HistoryOnFact(&history, EDITOR_CMD_FORWARD, &command);
    Editor_HistoryGroupName(&history.groups[3], name, sizeof(name));
    CHECK(strcmp(name, "Cfg loc:door.name") == 0, "history: names a config field (%s)", name);

    Editor_HistoryBreak(&history, "New square m1_1 (copy of m50_50)");
    Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, 1, 1);
    Editor_HistoryOnFact(&history, EDITOR_CMD_FORWARD, &command);
    Editor_HistoryGroupName(&history.groups[4], name, sizeof(name));
    CHECK(strcmp(name, "New square m1_1 (copy of m50_50)") == 0, "history: an intent's label is the row (%s)", name);
    CHECK(history.count == 5 && history.applied == 5, "history: five groups, all applied");

    /* The authority's group was smaller than the paste row: an undo of 40
     * cuts it, 56 applied below the position and 40 undone above. */
    Editor_HistoryReplayBegin(&history, -1);
    for( int i = 0; i < 4; i++ )
        Editor_HistoryOnFact(&history, EDITOR_CMD_INVERSE, &command);
    Editor_HistoryReplayEnd(&history, 4);
    /* One undo that answered 4 commands: the four one-command rows were one
     * group on the authority, so they fold into one. */
    CHECK(history.applied == 1 && history.count == 2 && history.groups[1].cmd_count == 4,
        "history: an undo of 4 commands folds the 4 rows it took (%d/%d)", history.applied, history.count);
    Editor_HistoryReplayBegin(&history, -1);
    for( int i = 0; i < 40; i++ )
        Editor_HistoryOnFact(&history, EDITOR_CMD_INVERSE, &command);
    Editor_HistoryReplayEnd(&history, 40);
    CHECK(history.count == 3 && history.applied == 1 && history.groups[0].cmd_count == 56
              && history.groups[1].cmd_count == 40 && history.groups[1].approximate,
        "history: an undo shorter than the row cuts it where the authority stopped (%d/%d, %d+%d)", history.applied,
        history.count, history.groups[0].cmd_count, history.groups[1].cmd_count);

    /* A redo that spans two rows: the authority's group was both. */
    Editor_HistoryReplayBegin(&history, 1);
    for( int i = 0; i < 41; i++ )
        Editor_HistoryOnFact(&history, EDITOR_CMD_FORWARD, &command);
    Editor_HistoryReplayEnd(&history, 41);
    CHECK(history.applied == 2 && history.count == 3 && history.groups[1].cmd_count == 41,
        "history: a redo across two rows merges them (%d/%d, %d)", history.applied, history.count,
        history.groups[1].cmd_count);
    CHECK(Editor_HistoryAppliedCommands(&history) == 97, "history: 97 commands applied (%d)",
        Editor_HistoryAppliedCommands(&history));

    /* Another window's undo and redo: inverse facts then the same command
     * forward, in pumps of their own. */
    {
        struct Editor_History remote;
        struct Editor_Cmd a;
        struct Editor_Cmd b;

        Editor_HistoryInit(&remote);
        Editor_CmdInitSquare(&a, EDITOR_CMD_LOC, 50, 50);
        a.has_after = 1;
        a.u.loc.after.loc_id = 1;
        b = a;
        b.u.loc.after.loc_id = 2;
        Editor_HistoryBreak(&remote, NULL);
        Editor_HistoryOnFact(&remote, EDITOR_CMD_FORWARD, &a);
        Editor_HistoryBreak(&remote, NULL);
        Editor_HistoryOnFact(&remote, EDITOR_CMD_FORWARD, &b);
        Editor_HistoryBatchEnd(&remote);
        Editor_HistoryBreak(&remote, NULL);
        Editor_HistoryOnFact(&remote, EDITOR_CMD_INVERSE, &b);
        Editor_HistoryBatchEnd(&remote);
        CHECK(remote.count == 2 && remote.applied == 1, "history: a remote undo moves the position");
        Editor_HistoryBreak(&remote, NULL);
        Editor_HistoryOnFact(&remote, EDITOR_CMD_FORWARD, &b);
        Editor_HistoryBatchEnd(&remote);
        CHECK(remote.count == 2 && remote.applied == 2, "history: the same command forward is that window's redo");
        Editor_HistoryBreak(&remote, NULL);
        Editor_HistoryOnFact(&remote, EDITOR_CMD_INVERSE, &b);
        Editor_HistoryBatchEnd(&remote);
        Editor_HistoryBreak(&remote, NULL);
        Editor_HistoryOnFact(&remote, EDITOR_CMD_FORWARD, &a);
        Editor_HistoryBatchEnd(&remote);
        CHECK(remote.count == 2 && remote.applied == 2 && remote.groups[1].first_hash == Editor_CmdHash(&a),
            "history: a different command is a new edit and drops the redo tail");
        Editor_HistoryFree(&remote);
    }
    Editor_HistoryFree(&history);
}

/** The session's own history through the embedded authority, and the
 *  History panel over it: names, the marker, jump to a row and back. */
static void
fb_test_history_panel(void)
{
    const struct Editor_History* history = Editor_GetHistory(&g_editor);
    struct Editor_BrushOp op;
    struct Editor_LocOpResult result;
    struct Editor_Tile after_first[25];
    char name[160];
    int const cx = SQ_X0 + 40;
    int const cz = SQ_Z0 + 44;
    int first;
    int count;
    int rebuilds;
    int list;
    int row_a = -1;
    int panel = -1;

    ToriRSChrome_Init(&g_fb_chrome);
    ToriRSChrome_SetSurface(&g_fb_chrome, 1280, 800);
    Editor_UiInit(&g_fb_ui, &g_fb_chrome, &g_editor);
    Editor_History_Register(&g_fb_ui);
    Editor_UiSetVisible(&g_fb_ui, 1);
    for( int i = 0; i < g_fb_chrome.panel_count; i++ )
        if( strcmp(g_fb_chrome.panels[i].title, "History") == 0 )
            panel = i;
    CHECK(panel >= 0, "history panel: registers");
    if( panel < 0 )
        return;
    ToriRSChrome_PanelSetVisible(&g_fb_chrome, panel, 1);

    /* A: a named raise stroke on untouched ground. */
    memset(&op, 0, sizeof(op));
    op.tool = EDITOR_TOOL_RAISE;
    op.world_x = cx;
    op.world_z = cz;
    op.brush.radius = 2;
    op.brush.falloff = EDITOR_BRUSH_FALLOFF_FLAT;
    op.brush.step = 2;
    count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
    Editor_StrokeBegin(&g_editor);
    Editor_HistoryName(&g_editor, "Raise");
    for( int i = 0; i < count; i++ )
        (void)Editor_Apply(&g_editor, &g_cmds[i]);
    Editor_StrokeEnd(&g_editor);
    first = history->count - 1;
    for( int i = 0; i < 25; i++ )
        after_first[i] = *tile_at(cx + i % 5 - 2, cz + i / 5 - 2, 0);
    Editor_HistoryGroupName(&history->groups[first], name, sizeof(name));
    CHECK(strcmp(name, "Raise 21 tiles m50_50 L0") == 0 && history->groups[first].cmd_count == 21,
        "history: the stroke is one row named by its brush (%s, %d)", name, history->groups[first].cmd_count);

    /* B: the same raise again, unnamed: the tallies say Raise. */
    count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
    CHECK(apply_all(g_cmds, count) == count, "history: the second raise applies");
    Editor_HistoryGroupName(&history->groups[first + 1], name, sizeof(name));
    CHECK(strcmp(name, "Raise 21 tiles m50_50 L0") == 0, "history: an unnamed raise is named from its commands (%s)", name);

    /* C: a loc, one command outside any stroke. */
    count = Editor_LocOpsPlace(&g_editor.doc, 1530, 10, 0, cx + 6, cz, 0, g_cmds, 1, &result);
    CHECK(count == 1 && Editor_Apply(&g_editor, &g_cmds[0]), "history: the loc applies");
    Editor_HistoryGroupName(&history->groups[first + 2], name, sizeof(name));
    CHECK(strcmp(name, "Place loc 1530 m50_50") == 0, "history: a single apply is its own row (%s)", name);

    /* D: the Place tool with an npc pick: a spawn row through the catalog. */
    g_fb_ui.tools.pick_kind = 1;
    g_fb_ui.tools.pick_id = 0;
    CHECK(Editor_Locs_PlaceSpawn(&g_fb_ui, cx, cz + 6, 0) == 1, "spawn place: the npc pick places a spawn row");
    {
        struct Editor_DocHandle handle;
        char key[EDITOR_DOC_KEY_MAX];
        int found = 0;
        Editor_SpawnDocKeyAt(key, cx, cz + 6);
        if( Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, key, &handle) && handle.spawn->loaded )
            for( int i = 0; i < handle.spawn->file.line_count; i++ )
                if( handle.spawn->file.lines[i].kind == EDITOR_SPAWN_LINE_ROW
                    && strcmp(handle.spawn->file.lines[i].row.name, "test_guard") == 0
                    && handle.spawn->file.lines[i].row.abs_x == cx && handle.spawn->file.lines[i].row.abs_z == cz + 6 )
                    found = 1;
        CHECK(found, "spawn place: the row is test_guard at the clicked tile (the pick's symbol)");
    }
    Editor_HistoryGroupName(&history->groups[first + 3], name, sizeof(name));
    snprintf(g_fb_ui.tools.pick_name, sizeof(g_fb_ui.tools.pick_name), "%d,%d", cx, cz + 6);
    CHECK(strstr(name, "npc test_guard") && strstr(name, g_fb_ui.tools.pick_name),
        "history: the spawn row is named with its symbol and tile (%s)", name);
    CHECK(history->count == first + 4 && history->applied == first + 4, "history: four new rows, all applied");

    /* The panel: newest first, the marker on the newest applied. */
    fb_frame();
    CHECK(Editor_History_RowText(&g_fb_ui, 0) && strncmp(Editor_History_RowText(&g_fb_ui, 0), "> ", 2) == 0
              && strstr(Editor_History_RowText(&g_fb_ui, 0), "test_guard"),
        "history panel: row 0 is the spawn, marked current (%s)", Editor_History_RowText(&g_fb_ui, 0));
    rebuilds = Editor_History_Rebuilds(&g_fb_ui);
    fb_frame();
    fb_frame();
    CHECK(Editor_History_Rebuilds(&g_fb_ui) == rebuilds, "history panel: unchanged frames rebuild nothing");

    /* Undo: the spawn row greys to "undone" above the marker. */
    CHECK(Editor_Undo(&g_editor) == 1, "history: undo takes the spawn back");
    fb_frame();
    CHECK(Editor_History_Rebuilds(&g_fb_ui) == rebuilds + 1, "history panel: the undo rebuilt the rows once");
    CHECK(strncmp(Editor_History_RowText(&g_fb_ui, 0), "  undone ", 9) == 0
              && strncmp(Editor_History_RowText(&g_fb_ui, 1), "> ", 2) == 0,
        "history panel: the spawn is undone, the loc is current (%s | %s)", Editor_History_RowText(&g_fb_ui, 0),
        Editor_History_RowText(&g_fb_ui, 1));

    /* Click row A: back to just after the first raise. */
    for( int row = 0; Editor_History_RowText(&g_fb_ui, row); row++ )
        if( Editor_History_RowTarget(&g_fb_ui, row) == first + 1 )
            row_a = row;
    CHECK(row_a == 3, "history panel: the first raise is row 3 (%d)", row_a);
    list = -1;
    for( int i = 0; i < g_fb_chrome.widget_count; i++ )
        if( g_fb_chrome.widgets[i].panel == panel && g_fb_chrome.widgets[i].kind == TORIRS_CHROME_W_VLIST )
            list = i;
    CHECK(list >= 0, "history panel: has its list");
    if( list >= 0 && row_a >= 0 )
    {
        int restored = 1;
        ToriRSChrome_VListSetSelected(&g_fb_chrome, list, row_a);
        g_fb_chrome.activated = list;
        fb_frame();
        for( int i = 0; i < 25; i++ )
            if( memcmp(tile_at(cx + i % 5 - 2, cz + i / 5 - 2, 0), &after_first[i], sizeof(after_first[i])) != 0 )
                restored = 0;
        CHECK(history->applied == first + 1 && restored,
            "history panel: a click on the first raise undoes back to it (%d applied, tiles %s)", history->applied,
            restored ? "as after it" : "differ");
        CHECK(Editor_LocRefResolve(&g_editor.doc, &result.refs[0]) < 0, "history panel: the loc is gone with it");
    }
    CHECK(Editor_History_Jump(&g_fb_ui, first + 4) == 3 && history->applied == first + 4,
        "history: a jump to the newest redoes the three steps (%d)", history->applied);
    CHECK(Editor_LocRefResolve(&g_editor.doc, &result.refs[0]) >= 0, "history: the loc is back");

    /* Filter by document key. */
    {
        int input = -1;
        int rows = 0;
        for( int i = 0; i < g_fb_chrome.widget_count; i++ )
            if( g_fb_chrome.widgets[i].panel == panel && g_fb_chrome.widgets[i].kind == TORIRS_CHROME_W_TEXTINPUT )
                input = i;
        CHECK(input >= 0, "history panel: has its filter");
        if( input >= 0 )
        {
            ToriRSChrome_SetText(&g_fb_chrome, input, "no_such_doc");
            fb_frame();
            while( Editor_History_RowText(&g_fb_ui, rows) )
                rows++;
            CHECK(rows == 1 && strstr(Editor_History_RowText(&g_fb_ui, 0), "session start"),
                "history panel: a filter nothing matches leaves the start row (%d rows)", rows);
            ToriRSChrome_SetText(&g_fb_chrome, input, "");
            fb_frame();
        }
    }
    Editor_UiFree(&g_fb_ui);
}


/* ---- spawn edit form, move, wander preview ------------------------------------ */

static const struct Editor_HistoryGroup*
newest_group(void)
{
    const struct Editor_History* history = Editor_GetHistory(&g_editor);
    return history->groups && history->applied > 0 ? &history->groups[history->applied - 1] : NULL;
}

static int
newest_group_named(const char* needle)
{
    char name[EDITOR_HISTORY_LABEL_MAX + 64];
    const struct Editor_HistoryGroup* group = newest_group();
    if( !group )
        return 0;
    Editor_HistoryGroupName(group, name, sizeof(name));
    return strstr(name, needle) != NULL;
}

static struct Editor_SpawnFile*
edited_file(int world_x, int world_z)
{
    struct Editor_DocHandle handle;
    char key[EDITOR_DOC_KEY_MAX];
    static struct Editor_SpawnFile empty;
    Editor_SpawnDocKeyAt(key, world_x, world_z);
    /* Opened once; the mirror follows it from then on. */
    if( (!Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, key, &handle) || !handle.spawn->loaded)
        && !Editor_OpenDoc(&g_editor, EDITOR_DOC_SPAWN, key) )
    {
        printf("FAIL: cannot open the spawn document %s\n", key);
        g_failed++;
        return &empty;
    }
    if( !Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, key, &handle) || !handle.spawn->loaded )
    {
        printf("FAIL: the spawn document %s is not loaded\n", key);
        g_failed++;
        return &empty;
    }
    return &handle.spawn->file;
}

/** Editor_SpawnFind in the edited file of (x, z); -2 when it is not open. */
static int
find_in(
    int world_x,
    int world_z,
    const struct Editor_SpawnRow* row)
{
    return Editor_SpawnFind(edited_file(world_x, world_z), row);
}

static char*
emit_file(const struct Editor_SpawnFile* file)
{
    size_t size = Editor_SpawnEmit(file, NULL, 0);
    char* text = malloc(size + 1);
    assert(text);
    Editor_SpawnEmit(file, text, size + 1);
    return text;
}

static struct Editor_SpawnRow
spawn_row(const char* name, int is_obj, int x, int z, int level, int count)
{
    struct Editor_SpawnRow row;
    memset(&row, 0, sizeof(row));
    row.is_obj = is_obj;
    snprintf(row.name, sizeof(row.name), "%s", name);
    row.abs_x = x;
    row.abs_z = z;
    row.level = level;
    row.count = count;
    return row;
}

/** A fresh registry for the edit tests (test_spawn_duplicate freed the last):
 *  the Catalog (which brings the Record panel) and Spawns, headless. */
static void
cat_ui_open(void)
{
    ToriRSChrome_Init(&g_cat_chrome);
    ToriRSChrome_SetSurface(&g_cat_chrome, 1280, 800);
    Editor_UiInit(&g_cat_ui, &g_cat_chrome, &g_editor);
    Editor_Catalog_Register(&g_cat_ui);
    Editor_Spawns_Register(&g_cat_ui);
    Editor_UiSetVisible(&g_cat_ui, 1);
    cat_frame();
}

static void
test_spawn_edit(void)
{
    struct Editor_Cmd command;
    struct Editor_SpawnFile* file;
    struct Editor_SpawnRow a = spawn_row("test_guard", 0, SQ_X0 + 5, SQ_Z0 + 5, 0, 1);
    struct Editor_SpawnRow b = spawn_row("test_guard", 0, SQ_X0 + 8, SQ_Z0 + 9, 0, 1);
    struct Editor_SpawnRow sel[4];
    struct MapEditorWanderRect rects[MAP_EDITOR_WANDER_RECTS_MAX];
    char* before;
    char* after;
    int spawns;
    int line_before;
    int n;

    cat_ui_open();
    spawns = cat_panel("Spawns");
    CHECK(spawns >= 0, "edit: the Spawns panel is there");
    if( spawns < 0 )
        return;
    ToriRSChrome_PanelSetVisible(&g_cat_chrome, spawns, 1);
    g_cat_ui.tools.tile_valid = 1;
    g_cat_ui.tools.tile_x = SQ_X0 + 5;
    g_cat_ui.tools.tile_z = SQ_Z0 + 5;
    g_cat_ui.tools.tile_level = 0;
    /* Two edited rows, a comment line between them that must survive. */
    Editor_SpawnCommand(&command, &a, 1);
    CHECK(apply_all(&command, 1) == 1, "edit: place guard a");
    Editor_SpawnCommand(&command, &b, 1);
    CHECK(apply_all(&command, 1) == 1, "edit: place guard b");
    cat_frame();
    file = edited_file(a.abs_x, a.abs_z);
    CHECK(file != NULL, "edit: the edited file is open");
    if( !file )
        return;
    line_before = Editor_SpawnFind(file, &a);
    before = emit_file(file);

    /* The form: a move is one SPAWN_EDIT in place, named "move spawn". */
    sel[0] = a;
    Editor_Spawns_Select(&g_cat_ui, sel, 1);
    cat_frame();
    {
        int name = cat_widget(spawns, TORIRS_CHROME_W_TEXTINPUT, "Spawn name");
        int x = cat_widget(spawns, TORIRS_CHROME_W_TEXTINPUT, "X");
        CHECK(name >= 0 && x >= 0 && strcmp(ToriRSChrome_Text(&g_cat_chrome, name), "test_guard") == 0
                  && atoi(ToriRSChrome_Text(&g_cat_chrome, x)) == a.abs_x,
            "edit: the form shows the selected row");
    }
    n = Editor_Spawns_SetEditForm(&g_cat_ui, "test_guard", 0, a.abs_x + 2, a.abs_z + 1, 0, 1, 0);
    {
        struct Editor_SpawnRow moved = a;
        moved.abs_x += 2;
        moved.abs_z += 1;
        file = edited_file(a.abs_x, a.abs_z);
        CHECK(n == 1 && Editor_SpawnFind(file, &moved) == line_before && Editor_SpawnFind(file, &a) < 0,
            "edit: the row is rewritten on its own line (%d -> %d)", line_before, Editor_SpawnFind(file, &moved));
        CHECK(newest_group_named("move spawn") && newest_group() && newest_group()->kinds[EDITOR_CMD_SPAWN_EDIT] == 1,
            "edit: one undo step named move spawn, one SPAWN_EDIT");
        after = emit_file(file);
        {
            /* Every other line byte-identical. */
            int diff_lines = 0;
            const char* p = before;
            const char* q = after;
            while( *p || *q )
            {
                const char* pe = strchr(p, '\n');
                const char* qe = strchr(q, '\n');
                size_t pl = pe ? (size_t)(pe - p) : strlen(p);
                size_t ql = qe ? (size_t)(qe - q) : strlen(q);
                diff_lines += pl != ql || memcmp(p, q, pl) != 0;
                p = pe ? pe + 1 : p + pl;
                q = qe ? qe + 1 : q + ql;
            }
            CHECK(diff_lines == 1, "edit: exactly one line of the file changed (%d)", diff_lines);
        }
        free(after);
        CHECK(Editor_Spawns_Selection(&g_cat_ui, sel, 4) == 1 && Editor_SpawnRowsEqual(&sel[0], &moved),
            "edit: the selection follows the edited row");
    }
    CHECK(Editor_Undo(&g_editor) == 1, "edit: one undo");
    file = edited_file(a.abs_x, a.abs_z);
    after = emit_file(file);
    CHECK(strcmp(before, after) == 0, "edit: the undo restores the file byte for byte");
    free(after);

    /* The `#<id>` escape, then a display name resolved to its symbol. */
    sel[0] = a;
    Editor_Spawns_Select(&g_cat_ui, sel, 1);
    cat_frame();
    n = Editor_Spawns_SetEditForm(&g_cat_ui, "#0", 0, a.abs_x, a.abs_z, 0, 1, 0);
    {
        struct Editor_SpawnRow escaped = a;
        snprintf(escaped.name, sizeof(escaped.name), "#0");
        CHECK(n == 1 && find_in(a.abs_x, a.abs_z, &escaped) == line_before
                  && newest_group_named("edit spawn"),
            "edit: #0 is accepted (the catalog has npc 0) and named edit spawn");
        Editor_Spawns_Select(&g_cat_ui, &escaped, 1);
        cat_frame();
    }
    n = Editor_Spawns_SetEditForm(&g_cat_ui, "Guard", 0, a.abs_x, a.abs_z, 0, 1, 0);
    CHECK(n == 1 && find_in(a.abs_x, a.abs_z, &a) == line_before,
        "edit: the display name 'Guard' resolves to test_guard");
    CHECK(Editor_Undo(&g_editor) == 1 && Editor_Undo(&g_editor) == 1, "edit: two undos");
    Editor_Spawns_Select(&g_cat_ui, &a, 1);
    cat_frame();
    CHECK(Editor_Spawns_SetEditForm(&g_cat_ui, "no_such_npc", 0, a.abs_x, a.abs_z, 0, 1, 0) == 0
              && find_in(a.abs_x, a.abs_z, &a) == line_before,
        "edit: a name the catalog does not know is refused, nothing written");
    CHECK(Editor_Spawns_SetEditForm(&g_cat_ui, "test_guard", 0, a.abs_x, a.abs_z, 4, 1, 0) == 0,
        "edit: level 4 is refused");

    /* Kind npc -> obj: a delete and an add (another section), one step. */
    n = Editor_Spawns_SetEditForm(&g_cat_ui, "#0", 1, a.abs_x, a.abs_z, 0, 3, 0);
    {
        struct Editor_SpawnRow as_obj = spawn_row("#0", 1, a.abs_x, a.abs_z, 0, 3);
        file = edited_file(a.abs_x, a.abs_z);
        CHECK(n == 2 && Editor_SpawnFind(file, &as_obj) >= 0 && Editor_SpawnFind(file, &a) < 0,
            "edit: npc -> obj moves the row to the OBJ section with its count");
        CHECK(Editor_Undo(&g_editor) == 2, "edit: and one undo takes both commands back");
    }

    /* Suppressed on an edited row: it becomes a `-name` row, in place. (The
     * kind change's undo re-added the row at its section's end: a SPAWN
     * delete's inverse is an insert, so find it again.) */
    line_before = find_in(a.abs_x, a.abs_z, &a);
    CHECK(line_before >= 0, "edit: guard a is back after the undo");
    Editor_Spawns_Select(&g_cat_ui, &a, 1);
    cat_frame();
    n = Editor_Spawns_SetEditForm(&g_cat_ui, "test_guard", 0, a.abs_x, a.abs_z, 0, 1, 1);
    {
        struct Editor_SpawnRow mark = a;
        mark.suppress = 1;
        file = edited_file(a.abs_x, a.abs_z);
        CHECK(n == 1 && Editor_SpawnFind(file, &mark) == line_before && file->lines[line_before].text[0] == '-',
            "edit: Suppressed writes the row as a suppression row on its line");
        CHECK(Editor_Undo(&g_editor) == 1, "edit: undo");
    }

    /* Multi-select: a shared offset moves both, one undo step. */
    sel[0] = a;
    sel[1] = b;
    Editor_Spawns_Select(&g_cat_ui, sel, 2);
    cat_frame();
    n = Editor_Spawns_SetEditForm(&g_cat_ui, "test_guard", 0, a.abs_x, a.abs_z + 3, 1, 1, 0);
    {
        struct Editor_SpawnRow a2 = a;
        struct Editor_SpawnRow b2 = b;
        a2.abs_z += 3;
        a2.level = 1;
        b2.abs_z += 3;
        b2.level = 1;
        file = edited_file(a.abs_x, a.abs_z);
        CHECK(n == 2 && Editor_SpawnFind(file, &a2) >= 0 && Editor_SpawnFind(file, &b2) >= 0,
            "edit: a multi-selection moves by the anchor's offset (z+3, level+1)");
        CHECK(Editor_Undo(&g_editor) == 2, "edit: one undo for the group");
    }

    /* The world-click move: arm, then the click lands. */
    Editor_Spawns_Select(&g_cat_ui, &a, 1);
    cat_frame();
    {
        int move = cat_widget(spawns, TORIRS_CHROME_W_BUTTON, "Move: click a tile");
        CHECK(move >= 0, "move: the button is there");
        if( move >= 0 )
            cat_activate(move);
        CHECK(Editor_Spawns_MoveArmed(&g_cat_ui), "move: armed");
        n = Editor_Spawns_WanderRects(&g_cat_ui, 1, SQ_X0 + 20, SQ_Z0 + 20, 0, -1, rects, MAP_EDITOR_WANDER_RECTS_MAX);
        {
            int footprint = 0;
            for( int i = 0; i < n; i++ )
                footprint += rects[i].kind == MAP_EDITOR_WANDER_FOOTPRINT && rects[i].x0 == SQ_X0 + 20
                             && rects[i].z0 == SQ_Z0 + 20;
            CHECK(footprint == 1, "move: the pending destination is previewed under the pointer");
        }
        n = Editor_Spawns_MoveTo(&g_cat_ui, SQ_X0 + 20, SQ_Z0 + 20, 0);
        {
            struct Editor_SpawnRow there = spawn_row("test_guard", 0, SQ_X0 + 20, SQ_Z0 + 20, 0, 1);
            CHECK(n == 1 && find_in(a.abs_x, a.abs_z, &there) == line_before
                      && newest_group_named("move spawn") && !Editor_Spawns_MoveArmed(&g_cat_ui),
                "move: the click re-tiles the spawn in place, one step, disarmed");
        }
        CHECK(Editor_Undo(&g_editor) == 1, "move: undo");
    }

    /* A GENERATED row: suppressed, and an edited row with the new fields. */
    {
        struct Editor_SpawnFile generated;
        struct Editor_DocHandle handle;
        char key[EDITOR_DOC_KEY_MAX];
        struct Editor_SpawnRow g;
        int found = 0;

        Editor_SpawnGeneratedKey(key, 50, 50);
        Editor_SpawnInit(&generated);
        if( Editor_DocFind(&g_editor.doc, EDITOR_DOC_TEXT, key, &handle) && handle.text )
            (void)Editor_SpawnParse(&generated, handle.text->text, handle.text->length);
        for( int i = 0; i < generated.line_count && !found; i++ )
            if( generated.lines[i].kind == EDITOR_SPAWN_LINE_ROW && !generated.lines[i].row.is_obj
                && !Editor_SpawnIsSuppressed(edited_file(SQ_X0, SQ_Z0), &generated.lines[i].row) )
            {
                g = generated.lines[i].row;
                found = 1;
            }
        CHECK(found, "edit: m50_50's generated file has an npc row");
        if( found )
        {
            struct Editor_SpawnRow moved = g;
            struct Editor_SpawnRow mark = g;
            moved.abs_x = g.abs_x + 1 < SQ_X0 + 64 ? g.abs_x + 1 : g.abs_x - 1;
            mark.suppress = 1;
            Editor_Spawns_Select(&g_cat_ui, &g, 1);
            cat_frame();
            n = Editor_Spawns_SetEditForm(&g_cat_ui, g.name, 0, moved.abs_x, moved.abs_z, moved.level, 1, 0);
            file = edited_file(SQ_X0, SQ_Z0);
            CHECK(n == 2 && Editor_SpawnFind(file, &mark) >= 0 && Editor_SpawnFind(file, &moved) >= 0,
                "edit: a generated row is suppressed and the edited row added (%d)", n);
            CHECK(Editor_Undo(&g_editor) == 2 && find_in(SQ_X0, SQ_Z0, &mark) < 0,
                "edit: one undo restores the generated spawn");
        }
        Editor_SpawnFree(&generated);
    }

    /* The wander preview: test_guard's record (generated 9/12, authored
     * wanderrange 2) through the catalog, then without a pending move. */
    Editor_Spawns_Select(&g_cat_ui, &a, 1);
    cat_frame();
    n = Editor_Spawns_WanderRects(&g_cat_ui, 0, 0, 0, 0, -1, rects, MAP_EDITOR_WANDER_RECTS_MAX);
    CHECK(n == 2 && rects[0].kind == MAP_EDITOR_WANDER_AREA && MapEditorWander_RectTiles(&rects[0]) == 5 * 5
              && rects[0].x0 == a.abs_x - 2 && rects[1].kind == MAP_EDITOR_WANDER_LEASH
              && MapEditorWander_RectTiles(&rects[1]) == 25 * 25,
        "wander: the selected guard's square is wanderrange 2 (5x5) and its leash maxrange 12 (25x25): %d rects", n);
    n = Editor_Spawns_WanderRects(&g_cat_ui, 1, SQ_X0 + 30, SQ_Z0 + 30, 0, 0, rects, MAP_EDITOR_WANDER_RECTS_MAX);
    CHECK(n == 4 && rects[0].x0 == SQ_X0 + 28 && rects[0].x1 == SQ_X0 + 32,
        "wander: the spawn ghost of npc 0 under the pointer gets the same squares there");
    {
        int toggle = cat_widget(spawns, TORIRS_CHROME_W_CHECKBOX, "Show wander range");
        CHECK(toggle >= 0, "wander: the toggle is there");
        if( toggle >= 0 )
        {
            ToriRSChrome_SetChecked(&g_cat_chrome, toggle, 0);
            CHECK(Editor_Spawns_WanderRects(&g_cat_ui, 1, SQ_X0, SQ_Z0, 0, 0, rects, MAP_EDITOR_WANDER_RECTS_MAX) == 0,
                "wander: off draws nothing");
            ToriRSChrome_SetChecked(&g_cat_chrome, toggle, 1);
        }
    }
    free(before);
}

/* ---- the Record panel: every field of every layer ------------------------------- */

static void
test_record_editor(void)
{
    const struct Editor_CfgRecord* record;
    int row;
    int n;

    CHECK(Editor_DocKeyValid(EDITOR_DOC_CFG, "npc:test_guard@areas/test/configs/test.npc"), "keys: an overlay key");
    CHECK(Editor_DocKeyValid(EDITOR_DOC_CFG, "npc:test_guard"), "keys: a plain key keeps its meaning");
    CHECK(!Editor_DocKeyValid(EDITOR_DOC_CFG, "npc:test_guard@../configs/all.npc"), "keys: no .. in an overlay");
    CHECK(!Editor_DocKeyValid(EDITOR_DOC_CFG, "npc:test_guard@areas/test/configs/test.loc"),
        "keys: the overlay's extension is the type");
    CHECK(!Editor_DocKeyValid(EDITOR_DOC_CFG, "npc:test_guard@"), "keys: an empty overlay");
    CHECK(!Editor_DocKeyValid(EDITOR_DOC_CFG, "npc:@areas/x.npc"), "keys: an empty name");

    CHECK(Editor_Record_Open(&g_cat_ui, TORIRSMAPED_CATALOG_NPC, "test_guard") == 1, "record: opens test_guard");
    cat_frame();
    record = Editor_Record_Current(&g_cat_ui);
    CHECK(record && record->layer_count == 3 && record->layers[0].rank == 0 && record->layers[1].rank == 1
              && record->layers[2].rank == 2 && record->layers[1].present && record->layers[2].present,
        "record: three layers in merge order: all.npc, the generated overlay, the authored one");
    if( !record || record->layer_count != 3 )
        return;
    row = Editor_CfgRecordFindRow(record, "wanderrange", "", 0);
    CHECK(row >= 0 && record->rows[row].layer == 2 && strcmp(record->rows[row].value, "2") == 0
              && record->rows[row].shadowed == 1,
        "record: wanderrange is the authored 2, over the generated 9");
    row = Editor_CfgRecordFindRow(record, "respawnrate", "", 0);
    CHECK(row >= 0 && record->rows[row].layer == 1 && strcmp(record->rows[row].value, "30") == 0,
        "record: respawnrate is the generated 30 (its // comment is not the value)");
    row = Editor_CfgRecordFindRow(record, "param", "huntrange", 0);
    CHECK(row >= 0 && record->rows[row].layer == 2 && strcmp(record->rows[row].value, "4") == 0,
        "record: param huntrange is its own field");
    row = Editor_CfgRecordFindRow(record, "op1", "", 0);
    CHECK(row >= 0 && record->rows[row].layer == 0 && strcmp(record->rows[row].value, "Talk-to") == 0,
        "record: op1 comes from all.npc");
    {
        int wander = -1;
        int leash = -1;
        CHECK(Editor_Record_WanderValues(&g_cat_ui, "npc", "test_guard", &wander, &leash) && wander == 2 && leash == 12,
            "record: the wander preview reads 2 / 12 from it");
    }

    /* Edit where stated: the authored overlay's line, one undo step. */
    n = Editor_Record_SetField(&g_cat_ui, "wanderrange", "", "6", -1);
    record = Editor_Record_Current(&g_cat_ui);
    row = Editor_CfgRecordFindRow(record, "wanderrange", "", 0);
    CHECK(n == 1 && record->rows[row].layer == 2 && strcmp(record->rows[row].value, "6") == 0
              && newest_group_named("edit record"),
        "record: wanderrange set to 6 in the authored overlay, named edit record");
    CHECK(Editor_Undo(&g_editor) == 1, "record: undo");
    record = Editor_Record_Current(&g_cat_ui);
    row = Editor_CfgRecordFindRow(record, "wanderrange", "", 0);
    CHECK(strcmp(record->rows[row].value, "2") == 0, "record: the undo brings back 2");

    /* A generated value keeps its trailing comment. */
    n = Editor_Record_SetField(&g_cat_ui, "respawnrate", "", "50", -1);
    {
        struct Editor_DocHandle handle;
        CHECK(n == 1
                  && Editor_DocFind(&g_editor.doc, EDITOR_DOC_CFG,
                      "npc:test_guard@_explicit/configs/server_fields.generated.npc", &handle)
                  && handle.text && strstr(handle.text->text, "respawnrate=50 // wiki"),
            "record: respawnrate 50 written into the generated block, comment kept");
    }
    CHECK(Editor_Undo(&g_editor) == 1, "record: undo");

    /* Override: respawnrate into the authored overlay, then removed again. */
    n = Editor_Record_SetField(&g_cat_ui, "respawnrate", "", "45", 2);
    record = Editor_Record_Current(&g_cat_ui);
    row = Editor_CfgRecordFindRow(record, "respawnrate", "", 0);
    CHECK(n == 1 && record->rows[row].layer == 2 && strcmp(record->rows[row].value, "45") == 0
              && record->rows[row].shadowed == 1,
        "record: override in the authored overlay wins over the generated 30");
    n = Editor_Record_SetField(&g_cat_ui, "respawnrate", "", "", -1);
    record = Editor_Record_Current(&g_cat_ui);
    row = Editor_CfgRecordFindRow(record, "respawnrate", "", 0);
    CHECK(n == 1 && record->rows[row].layer == 1 && strcmp(record->rows[row].value, "30") == 0,
        "record: removing the override shows the generated 30 again");
    CHECK(Editor_Undo(&g_editor) == 1 && Editor_Undo(&g_editor) == 1, "record: two undos");

    /* Every key of the field register can be set through the commands. */
    {
        struct Editor_DocHandle handle;
        struct Editor_CfgRegisterField fields[EDITOR_CFG_REGISTER_MAX];
        int field_count = 0;
        int set_ok = 0;
        int undo_ok = 0;
        if( Editor_DocFind(&g_editor.doc, EDITOR_DOC_TEXT, "fields:npc.ini", &handle) && handle.text )
            field_count = Editor_CfgRegisterParse("npc", handle.text->text, handle.text->length, fields,
                EDITOR_CFG_REGISTER_MAX);
        CHECK(field_count >= 5 && (!g_register_real || field_count >= 60),
            "register: fields/npc.ini read as a document (%d keys%s)", field_count,
            g_register_real ? ", the tree's own" : ", a stand-in");
        for( int f = 0; f < field_count; f++ )
        {
            char key[EDITOR_CMD_NAME_MAX];
            const char* sub = fields[f].text_param ? fields[f].key : "";
            char value[32];
            const char* k = fields[f].text_param ? "param" : fields[f].key;
            int r;
            if( fields[f].text_indexed )
            {
                snprintf(key, sizeof(key), "%s1", fields[f].key);
                k = key;
            }
            snprintf(value, sizeof(value), "%d", 7000 + f);
            n = Editor_Record_SetField(&g_cat_ui, k, sub, value, -1);
            record = Editor_Record_Current(&g_cat_ui);
            r = Editor_CfgRecordFindRow(record, k, sub, 0);
            if( n >= 1 && r >= 0 && strcmp(record->rows[r].value, value) == 0 && record->rows[r].layer >= 0 )
                set_ok++;
            else
                printf("  register: %s%s%s did not set (n=%d)\n", k, sub[0] ? ":" : "", sub, n);
            if( n >= 1 && Editor_Undo(&g_editor) == n )
                undo_ok++;
        }
        CHECK(field_count > 0 && set_ok == field_count && undo_ok == field_count,
            "register: all %d keys set and undone through CFG commands (%d set, %d undone)", field_count, set_ok,
            undo_ok);
    }

    /* A layer with no block yet: CFG_BLOCK add, then the field. */
    {
        struct Editor_CfgRecord fresh;
        struct Editor_Cmd commands[2];
        struct Editor_DocHandle handle;
        const char* key = "npc:test_guard@areas/test/configs/more.npc";
        Editor_CfgRecordInit(&fresh, "npc", "test_guard");
        CHECK(Editor_OpenDoc(&g_editor, EDITOR_DOC_CFG, key), "record: a new overlay's block opens empty");
        Editor_CfgRecordAddLayer(&fresh, "areas/test/configs/more.npc", "", 0);
        Editor_CfgRecordResolve(&fresh, NULL, 0);
        n = Editor_CfgRecordFieldCommands(&fresh, "wanderrange", "", 0, "3", 0, commands, 2);
        CHECK(n == 2 && commands[0].kind == EDITOR_CMD_CFG_BLOCK && commands[1].kind == EDITOR_CMD_CFG_FIELD,
            "record: overriding into a file without the block opens it first");
        CHECK(apply_all(commands, n) == 2 && Editor_DocFind(&g_editor.doc, EDITOR_DOC_CFG, key, &handle)
                  && handle.text && handle.text->length == strlen("[test_guard]\nwanderrange=3\n")
                  && memcmp(handle.text->text, "[test_guard]\nwanderrange=3\n", handle.text->length) == 0,
            "record: the new block reads [test_guard] wanderrange=3");
        Editor_CfgRecordFree(&fresh);
    }

    /* Save writes the overlay block back over itself, nothing else. */
    n = Editor_Record_SetField(&g_cat_ui, "wanderrange", "", "4", -1);
    CHECK(n == 1, "record: wanderrange 4");
    CHECK(Editor_SaveDoc(&g_editor, EDITOR_DOC_CFG, "npc:test_guard@areas/test/configs/test.npc"),
        "record: save the authored overlay");
    CHECK(Editor_SaveDoc(&g_editor, EDITOR_DOC_CFG, "npc:test_guard@_explicit/configs/server_fields.generated.npc")
              || 1,
        "record: saving an unchanged block is harmless");
    {
        char path[2048];
        FILE* f;
        char text[512] = "";
        size_t got;
        snprintf(path, sizeof(path), "%s/server/scripts/areas/test/configs/test.npc", g_tmp_root);
        f = fopen(path, "rb");
        got = f ? fread(text, 1, sizeof(text) - 1, f) : 0;
        if( f )
            fclose(f);
        text[got] = '\0';
        CHECK(strcmp(text, "// authored\n[test_guard]\nwanderrange=4\nparam=huntrange,4\n") == 0,
            "record: the overlay file holds exactly the edit (%s)", text);
        snprintf(path, sizeof(path), "%s/server/scripts/_explicit/configs/server_fields.generated.npc", g_tmp_root);
        f = fopen(path, "rb");
        got = f ? fread(text, 1, sizeof(text) - 1, f) : 0;
        if( f )
            fclose(f);
        text[got] = '\0';
        CHECK(strcmp(text, "// generated\n\n[test_guard]\nwanderrange=9\nmaxrange=12\nrespawnrate=30 // wiki\n"
                           "param=death_drop,bones\n\n// trailing note\n")
                  == 0,
            "record: the generated file is byte for byte as it was (%s)", text);
    }

    /* The catalog's record view follows the merge order too. */
    {
        struct EditorHost_CatalogRecord cat;
        memset(&cat, 0, sizeof(cat));
        if( Editor_HostMapEdCatalogRecord(&g_editor.host, TORIRSMAPED_CATALOG_NPC, -1, "test_guard", &cat)
            == EDITOR_HOST_OK )
        {
            int found = 0;
            for( int i = 0; i < cat.field_count; i++ )
                found |= strcmp(cat.fields[i].key, "wanderrange") == 0 && strcmp(cat.fields[i].value, "4") == 0;
            CHECK(found, "record: the catalog's merged record reads the authored wanderrange (saved: 4)");
        }
        else
            CHECK(0, "record: the catalog answers test_guard");
        Editor_HostCatalogRecordFree(&cat);
    }
    Editor_UiFree(&g_cat_ui);
}

/* ---- named areas and square collections (edpanel_areas.c) ---------------------- */

static int
areas_count(void)
{
    const struct Editor_AreasFile* file = Editor_Areas_File(&g_ui, NULL);
    return file ? Editor_AreasAreaCount(file) : -1;
}

static const struct Editor_Area*
areas_find(const char* name)
{
    const struct Editor_AreasFile* file = Editor_Areas_File(&g_ui, NULL);
    int const line = file ? Editor_AreasFindArea(file, name) : -1;
    return line >= 0 ? file->lines[line].area : NULL;
}

static const struct Editor_Collection*
areas_find_coll(const char* name)
{
    const struct Editor_AreasFile* file = Editor_Areas_File(&g_ui, NULL);
    int const line = file ? Editor_AreasFindCollection(file, name) : -1;
    return line >= 0 ? file->lines[line].collection : NULL;
}

static int
areas_console_has(const char* needle)
{
    for( int i = 0; i < 8; i++ )
    {
        const char* line = Editor_UiConsoleLine(&g_ui, i);
        if( line && strstr(line, needle) )
            return 1;
    }
    return 0;
}

/** The `nth` (0-based) widget of `kind` captioned `caption` on `panel`. */
static int
areas_widget(
    int panel,
    int kind,
    const char* caption,
    int nth)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel != panel || w->kind != kind )
            continue;
        if( strcmp(w->label, caption) != 0 && strcmp(w->text, caption) != 0 )
            continue;
        if( nth-- == 0 )
            return i;
    }
    return -1;
}

static void
test_areas(void)
{
    struct Editor_DocHandle handle;
    struct Editor_AreaOverlay overlay[8];
    struct Editor_Picker* picker;
    int areas_panel;
    int squares_panel;
    int w_new;
    int w_name;
    int w_rename;
    int w_level;
    int w_set_level;
    int w_add_rect;
    int w_status = -1;
    int w_outlines;
    int w_hide;
    int w_coll_name;
    int w_from_picker;
    int w_add_current;
    int w_remove_current;
    int w_delete_coll;
    int w_coll_dropdown;
    int n;

    CHECK(Editor_DocFind(&g_editor.doc, EDITOR_DOC_AREAS, "areas", &handle) && handle.text && handle.text->loaded,
        "areas: the session opened the AREAS document with the editor");
    CHECK(handle.text && handle.text->length == 0, "areas: absent on disk = empty");

    layout_boot(1920, 1080, 1);
    CHECK(Editor_UiFindPanel(&g_ui, "areas") != NULL, "areas: the panel registered");
    areas_panel = find_panel("Areas");
    squares_panel = find_panel("Squares");
    CHECK(areas_panel >= 0 && squares_panel >= 0, "areas: its chrome panel exists");
    if( areas_panel < 0 || squares_panel < 0 )
    {
        Editor_UiFree(&g_ui);
        return;
    }
    Editor_Areas_Show(&g_ui);
    frame();
    CHECK(g_chrome.panels[areas_panel].visible, "areas: shown");
    w_new = areas_widget(areas_panel, TORIRS_CHROME_W_BUTTON, "New from rect", 0);
    w_name = areas_widget(areas_panel, TORIRS_CHROME_W_TEXTINPUT, "Name", 0);
    w_coll_name = areas_widget(areas_panel, TORIRS_CHROME_W_TEXTINPUT, "Name", 1);
    w_rename = areas_widget(areas_panel, TORIRS_CHROME_W_BUTTON, "Rename", 0);
    w_level = areas_widget(areas_panel, TORIRS_CHROME_W_DROPDOWN, "Level", 0);
    w_set_level = areas_widget(areas_panel, TORIRS_CHROME_W_BUTTON, "Set level", 0);
    w_add_rect = areas_widget(areas_panel, TORIRS_CHROME_W_BUTTON, "Add rect", 0);
    w_outlines = areas_widget(areas_panel, TORIRS_CHROME_W_CHECKBOX, "Show outlines", 0);
    w_hide = areas_widget(areas_panel, TORIRS_CHROME_W_CHECKBOX, "Hide others", 0);
    w_from_picker = areas_widget(areas_panel, TORIRS_CHROME_W_BUTTON, "From picker", 0);
    w_add_current = areas_widget(areas_panel, TORIRS_CHROME_W_BUTTON, "Add current", 0);
    w_remove_current = areas_widget(areas_panel, TORIRS_CHROME_W_BUTTON, "Remove current", 0);
    w_delete_coll = areas_widget(areas_panel, TORIRS_CHROME_W_BUTTON, "Delete", 1);
    w_coll_dropdown = areas_widget(squares_panel, TORIRS_CHROME_W_DROPDOWN, "Collection", 0);
    CHECK(w_new >= 0 && w_name >= 0 && w_coll_name >= 0 && w_rename >= 0 && w_level >= 0 && w_set_level >= 0
              && w_add_rect >= 0 && w_outlines >= 0 && w_hide >= 0 && w_from_picker >= 0 && w_add_current >= 0
              && w_remove_current >= 0 && w_delete_coll >= 0 && w_coll_dropdown >= 0,
        "areas: every control is there (and the picker's Collection dropdown)");
    if( w_new < 0 || w_name < 0 || w_coll_name < 0 || w_rename < 0 || w_level < 0 || w_set_level < 0
        || w_add_rect < 0 || w_outlines < 0 || w_hide < 0 || w_from_picker < 0 || w_add_current < 0
        || w_remove_current < 0 || w_delete_coll < 0 || w_coll_dropdown < 0 )
    {
        Editor_UiFree(&g_ui);
        return;
    }

    /* New from rect with no rectangle: the panel says so, nothing is sent. */
    memset(&g_ui.tools.rect, 0, sizeof(g_ui.tools.rect));
    activate(w_new);
    CHECK(areas_count() == 0, "areas: no rectangle, no area");
    for( int i = 0; i < g_chrome.widget_count; i++ )
        if( g_chrome.widgets[i].panel == areas_panel && strstr(g_chrome.widgets[i].text, "no rectangle") )
            w_status = i;
    CHECK(w_status >= 0, "areas: the panel says there is no rectangle");

    /* New from rect -> AREA_SET -> named history row -> undo removes it. */
    g_ui.tools.rect.valid = 1;
    g_ui.tools.rect.x0 = SQ_X0 + 25;
    g_ui.tools.rect.z0 = SQ_Z0 + 26;
    g_ui.tools.rect.x1 = SQ_X0 + 5;
    g_ui.tools.rect.z1 = SQ_Z0 + 8;
    g_ui.tools.rect.level = 0;
    ToriRSChrome_SetText(&g_chrome, w_name, "Lumbridge castle");
    activate(w_new);
    CHECK(areas_count() == 1, "areas: New from rect marked one area");
    {
        const struct Editor_Area* a = areas_find("Lumbridge castle");
        CHECK(a && a->level == 0 && a->rect_count == 1 && a->rects[0].x0 == SQ_X0 + 5 && a->rects[0].z1 == SQ_Z0 + 26,
            "areas: named, level 0, the rectangle normalised");
    }
    CHECK(strcmp(Editor_Areas_Selected(&g_ui), "Lumbridge castle") == 0, "areas: the new area is the selection");
    CHECK(newest_group_named("Mark area Lumbridge castle"), "areas: history row 'Mark area Lumbridge castle'");
    CHECK(Editor_DocFind(&g_editor.doc, EDITOR_DOC_AREAS, "areas", &handle) && handle.text->dirty,
        "areas: the document is unsaved");
    CHECK(Editor_Undo(&g_editor) == 1, "areas: undo is one step");
    frame();
    CHECK(areas_count() == 0, "areas: undo removed the area");
    CHECK(Editor_DocFind(&g_editor.doc, EDITOR_DOC_AREAS, "areas", &handle) && handle.text->length == 0,
        "areas: and the header it brought (the document is empty again)");
    CHECK(Editor_Redo(&g_editor) == 1 && areas_count() == 1, "areas: redo brings it back");
    frame();
    CHECK(Editor_Areas_Select(&g_ui, "Lumbridge castle"), "areas: select it again");

    /* Rename, Set level, Add rect: each one named step. */
    ToriRSChrome_SetText(&g_chrome, w_name, "Castle");
    activate(w_rename);
    CHECK(areas_find("Castle") && !areas_find("Lumbridge castle"), "areas: renamed");
    CHECK(newest_group_named("Rename area Lumbridge castle to Castle"), "areas: history row for the rename");
    CHECK(strcmp(Editor_Areas_Selected(&g_ui), "Castle") == 0, "areas: the selection follows the rename");
    ToriRSChrome_DropdownSetSelected(&g_chrome, w_level, 2);
    activate(w_set_level);
    CHECK(areas_find("Castle") && areas_find("Castle")->level == 1, "areas: Set level 1");
    CHECK(newest_group_named("Set level of area Castle to 1"), "areas: history row for the level");
    g_ui.tools.rect.x0 = SQ_X0 + 30;
    g_ui.tools.rect.z0 = SQ_Z0 + 30;
    g_ui.tools.rect.x1 = SQ_X0 + 33;
    g_ui.tools.rect.z1 = SQ_Z0 + 31;
    activate(w_add_rect);
    CHECK(areas_find("Castle") && areas_find("Castle")->rect_count == 2, "areas: a second rectangle");
    CHECK(newest_group_named("Add rect to area Castle"), "areas: history row for the rect");
    ToriRSChrome_SetText(&g_chrome, w_name, "Castle");
    activate(w_new);
    CHECK(areas_count() == 1 && areas_console_has("already exists"), "areas: a taken name is refused");

    /* The overlay: every rect, the name on the first, the selection marked;
     * Hide others and Show outlines. */
    n = Editor_Areas_Overlay(&g_ui, overlay, 8);
    CHECK(n == 2 && overlay[0].first && strcmp(overlay[0].label, "Castle") == 0 && !overlay[1].first
              && overlay[0].selected && overlay[0].level == 1 && overlay[1].x0 == SQ_X0 + 30,
        "areas: the overlay has both rects, labelled once, selected (%d)", n);
    ToriRSChrome_SetText(&g_chrome, w_name, "Second");
    g_ui.tools.rect.x0 = SQ_X0 + 40;
    g_ui.tools.rect.x1 = SQ_X0 + 41;
    activate(w_new);
    CHECK(areas_count() == 2 && strcmp(Editor_Areas_Selected(&g_ui), "Second") == 0, "areas: a second area");
    CHECK(Editor_Areas_Overlay(&g_ui, overlay, 8) == 3, "areas: three rects in the overlay");
    ToriRSChrome_SetChecked(&g_chrome, w_hide, 1);
    n = Editor_Areas_Overlay(&g_ui, overlay, 8);
    CHECK(n == 1 && strcmp(overlay[0].label, "Second") == 0, "areas: Hide others leaves the selection (%d)", n);
    ToriRSChrome_SetChecked(&g_chrome, w_hide, 0);
    ToriRSChrome_SetChecked(&g_chrome, w_outlines, 0);
    CHECK(Editor_Areas_Overlay(&g_ui, overlay, 8) == 0, "areas: Show outlines off draws nothing");
    ToriRSChrome_SetChecked(&g_chrome, w_outlines, 1);
    CHECK(!Editor_Areas_GoTo(&g_ui) && areas_console_has("needs a world"), "areas: Go to without a world says so");

    /* Collections: New from picker selection, the picker filter, add /
     * remove the current square, Open all, delete. */
    picker = (struct Editor_Picker*)edp_squares_picker(&g_ui);
    CHECK(picker != NULL, "areas: the picker has its atlas");
    if( picker )
    {
        int const a = Editor_PickerFind(picker, 50, 50, -1);
        int const b = Editor_PickerFind(picker, 50, 51, -1);
        int const c = Editor_PickerFind(picker, 51, 50, -1);
        CHECK(a >= 0 && b >= 0 && c >= 0, "areas: m50_50, m50_51, m51_50 are in the atlas");
        if( a >= 0 && b >= 0 && c >= 0 )
        {
            Editor_PickerSelect(picker, a, 0);
            Editor_PickerSelect(picker, b, 1);
            ToriRSChrome_SetText(&g_chrome, w_coll_name, "Lumbridge");
            activate(w_from_picker);
            CHECK(areas_find_coll("Lumbridge") && areas_find_coll("Lumbridge")->square_count == 2
                      && Editor_CollectionFind(areas_find_coll("Lumbridge"), "m50_50") >= 0
                      && Editor_CollectionFind(areas_find_coll("Lumbridge"), "m50_51") >= 0,
                "areas: a collection of the two picker squares, m50_50 and m50_51");
            CHECK(newest_group_named("New collection Lumbridge"), "areas: history row for the collection");
            CHECK(strcmp(Editor_Areas_SelectedCollection(&g_ui), "Lumbridge") == 0, "areas: it is the selection");

            /* The picker filter: only the collection's squares, on any page. */
            frame();
            CHECK(g_chrome.widgets[w_coll_dropdown].option_count == 2,
                "areas: the Collection dropdown lists all + one (%d)", g_chrome.widgets[w_coll_dropdown].option_count);
            ToriRSChrome_DropdownSetSelected(&g_chrome, w_coll_dropdown, 1);
            activate(w_coll_dropdown);
            CHECK(picker->filter_on && picker->filter_shown == 2, "areas: the picker is filtered to 2 squares");
            CHECK(strcmp(Editor_SquarePicker_Collection(&g_ui), "Lumbridge") == 0, "areas: filtered to Lumbridge");
            CHECK(picker->filter_mask && picker->filter_mask[a] && picker->filter_mask[b] && !picker->filter_mask[c],
                "areas: m51_50 is filtered out");

            /* Add the current square (the picker's focus) -- the filter follows. */
            Editor_PickerSelect(picker, c, 0);
            activate(w_add_current);
            CHECK(areas_find_coll("Lumbridge") && areas_find_coll("Lumbridge")->square_count == 3,
                "areas: Add current square");
            CHECK(newest_group_named("Add 1 square to collection Lumbridge"), "areas: history row for the add");
            frame();
            CHECK(picker->filter_shown == 3, "areas: the filter took the new square (%d)", picker->filter_shown);
            activate(w_remove_current);
            CHECK(areas_find_coll("Lumbridge") && areas_find_coll("Lumbridge")->square_count == 2,
                "areas: Remove current square");

            /* Open all: each as a document, and to the window. */
            g_window.open_calls = 0;
            CHECK(Editor_Areas_OpenAll(&g_ui, 0) == 2, "areas: Open all opened both as documents");
            frame();
            CHECK(g_window.open_calls == 2 && g_window.open_add == 1, "areas: and asked the window for both (%d)",
                g_window.open_calls);

            /* Deleting the collection turns the filter off. */
            activate(w_delete_coll);
            frame();
            CHECK(!areas_find_coll("Lumbridge"), "areas: collection deleted");
            CHECK(!picker->filter_on && Editor_SquarePicker_Collection(&g_ui)[0] == '\0',
                "areas: the picker shows every square again");
            CHECK(Editor_Undo(&g_editor) == 1 && areas_find_coll("Lumbridge"), "areas: undo the delete");
            Editor_PickerSelect(picker, -1, 0);
        }
    }

    /* Save: the document lands at editor/areas.txt, header first. */
    CHECK(Editor_SaveDoc(&g_editor, EDITOR_DOC_AREAS, "areas") == 1, "areas: saved");
    {
        char path[1200];
        FILE* f;
        char text[4096];
        size_t got = 0;
        snprintf(path, sizeof(path), "%s/editor/areas.txt", g_tmp_root);
        f = fopen(path, "rb");
        CHECK(f != NULL, "areas: %s written", path);
        if( f )
        {
            got = fread(text, 1, sizeof(text) - 1, f);
            fclose(f);
        }
        text[got] = '\0';
        CHECK(strncmp(text, "# ToriRSMapEd named areas", 25) == 0, "areas: the header first");
        CHECK(strstr(text, "\narea Castle | level 1 | rects ") != NULL, "areas: the Castle row");
        CHECK(strstr(text, "\ncollection Lumbridge | squares ") != NULL && strstr(text, " m50_50") != NULL
                  && strstr(text, " m50_51") != NULL,
            "areas: the collection row in '%s'", text);
    }
    CHECK(Editor_DocFind(&g_editor.doc, EDITOR_DOC_AREAS, "areas", &handle) && !handle.text->dirty,
        "areas: the document is saved");
    Editor_UiFree(&g_ui);
}

static void
test_editing_feedback(void)
{
    fb_test_footprint();
    fb_test_spawn_ghost();
    fb_test_history_model();
    fb_test_history_panel();
}

/* ---- the active level ------------------------------------------------------ */

static int
menubar_item(const char* text)
{
    struct EditorUiPanel* bar = Editor_UiFindPanel(&g_ui, "menubar");
    if( !bar )
        return -1;
    return find_widget(bar->chrome_panel, TORIRS_CHROME_W_MENUITEM, text);
}

/** A real click: pointer down and up on the widget's box, then a frame. */
static void
click_widget(int widget)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[widget];
    int const x = w->x + w->w / 2;
    int const y = w->y + w->h / 2;
    ToriRSChrome_MouseMove(&g_chrome, x, y);
    (void)ToriRSChrome_MouseDown(&g_chrome, x, y);
    (void)ToriRSChrome_MouseUp(&g_chrome, x, y);
    frame();
}

/** The terrain panel's radio `caption` of the group whose first row is
 *  `first_caption` ("auto" for the edit level, "all" for the view cap). */
static int
terrain_radio_checked(
    const char* group_first,
    const char* caption)
{
    int const panel = find_panel("Terrain");
    int in_group = 0;
    if( panel < 0 )
        return -1;
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel != panel || w->kind != TORIRS_CHROME_W_RADIO )
            continue;
        if( strcmp(w->label, group_first) == 0 || strcmp(w->text, group_first) == 0 )
            in_group = 1;
        else if( strcmp(w->label, "auto") == 0 || strcmp(w->label, "all") == 0 || strcmp(w->text, "auto") == 0
                 || strcmp(w->text, "all") == 0 )
            in_group = 0;
        if( in_group && (strcmp(w->label, caption) == 0 || strcmp(w->text, caption) == 0) )
            return w->checked;
    }
    return -1;
}

static const char*
world_title(void)
{
    struct EditorUiPanel* world = Editor_UiFindPanel(&g_ui, "world");
    return world && world->chrome_panel >= 0 ? g_chrome.panels[world->chrome_panel].title : "";
}

/** The first tile of m50_50 at `level` the file states nothing for. */
static int
empty_tile_at(
    int level,
    int* out_x,
    int* out_z)
{
    for( int z = 8; z < 56; z++ )
        for( int x = 8; x < 56; x++ )
        {
            const struct Editor_Tile* t = tile_at(SQ_X0 + x, SQ_Z0 + z, level);
            if( t && !t->has_height && !t->has_overlay && !t->underlay_id && !t->settings )
            {
                *out_x = SQ_X0 + x;
                *out_z = SQ_Z0 + z;
                return 1;
            }
        }
    return 0;
}

static void
test_active_level(void)
{
    static const char* const k_levels[5] = { "Auto", "0", "1", "2", "3" };
    struct EditorUiPanel* bar;
    struct EditorUiRect world;
    int buttons[5];
    int above;
    int view;

    layout_boot(1530, 1006, 1);
    bar = Editor_UiFindPanel(&g_ui, "menubar");
    CHECK(bar != NULL, "level: the menu bar is registered");
    if( !bar )
    {
        Editor_UiFree(&g_ui);
        return;
    }
    world = Editor_UiWorldRect(&g_ui);
    view = view_menu();
    for( int i = 0; i < 5; i++ )
    {
        const struct ToriRSChromeWidget* w;
        buttons[i] = menubar_item(k_levels[i]);
        CHECK(buttons[i] >= 0, "level: the menu bar's Level row has '%s'", k_levels[i]);
        if( buttons[i] < 0 )
        {
            Editor_UiFree(&g_ui);
            return;
        }
        w = &g_chrome.widgets[buttons[i]];
        CHECK(w->w > 0 && w->h > 0 && w->y + w->h <= world.y && view >= 0 && w->x > g_chrome.widgets[view].x,
            "level: '%s' is on screen in the bar, right of View, above the world (%d,%d %dx%d, world y %d)",
            k_levels[i], w->x, w->y, w->w, w->h, world.y);
    }
    above = menubar_item("Above");
    CHECK(above >= 0, "level: the row has Above");
    if( above < 0 )
    {
        Editor_UiFree(&g_ui);
        return;
    }

    /* Auto by default: nothing pinned, every level drawn, Above unavailable. */
    CHECK(g_ui.tools.edit_level == -1 && g_ui.tools.vis_level == -1, "level: Auto by default");
    CHECK(g_chrome.widgets[buttons[0]].checked && !g_chrome.widgets[buttons[2]].checked,
        "level: Auto is the pressed button at boot");
    CHECK(g_chrome.widgets[above].disabled, "level: Above is unavailable under Auto");
    CHECK(strcmp(world_title(), "World") == 0, "level: the World title is plain under Auto (%s)", world_title());

    /* A real click on "1" pins level 1 and caps the view there. */
    click_widget(buttons[2]);
    CHECK(g_ui.tools.edit_level == 1, "level: clicking 1 pins level 1 (%d)", g_ui.tools.edit_level);
    CHECK(g_ui.tools.vis_level == 1 && !g_ui.tools.vis_solo, "level: the view cap follows to 1 (levels above hidden)");
    CHECK(g_chrome.widgets[buttons[2]].checked && !g_chrome.widgets[buttons[0]].checked,
        "level: 1 is pressed, Auto is not");
    CHECK(!g_chrome.widgets[above].disabled && !g_chrome.widgets[above].checked, "level: Above offered, off");
    CHECK(strcmp(world_title(), "World - level 1") == 0, "level: the World title names it (%s)", world_title());
    CHECK(terrain_radio_checked("auto", "1") == 1 && terrain_radio_checked("all", "1") == 1,
        "level: the terrain panel's level and view radios follow the row");

    /* Above: every level drawn, the pin kept; a step keeps showing them. */
    click_widget(above);
    CHECK(g_ui.tools.edit_level == 1 && g_ui.tools.vis_level == -1 && g_chrome.widgets[above].checked,
        "level: Above draws every level and keeps the pin");
    CHECK(strstr(world_title(), "level 1") && strstr(world_title(), "above"), "level: the title says so (%s)",
        world_title());
    click_widget(buttons[3]);
    CHECK(g_ui.tools.edit_level == 2 && g_ui.tools.vis_level == -1, "level: a new pin keeps Above");
    click_widget(above);
    CHECK(g_ui.tools.vis_level == 2 && !g_chrome.widgets[above].checked, "level: Above off caps at the pin again");

    /* Auto restores the unpinned view exactly. */
    click_widget(buttons[0]);
    CHECK(g_ui.tools.edit_level == -1 && g_ui.tools.vis_level == -1 && g_chrome.widgets[above].disabled,
        "level: Auto unpins and draws every level");
    CHECK(strcmp(world_title(), "World") == 0, "level: the title is plain again");

    /* The keys' step (PageUp / PageDown): from Auto at the hovered plane,
     * clamped at 0 and 3. */
    CHECK(edp_level_step(&g_ui, 1, 0) == 1 && g_ui.tools.vis_level == 1, "level: PageUp from Auto on 0 pins 1");
    CHECK(edp_level_step(&g_ui, 1, 0) == 2, "level: PageUp steps to 2");
    CHECK(edp_level_step(&g_ui, 1, 0) == 3, "level: PageUp steps to 3");
    CHECK(edp_level_step(&g_ui, 1, 0) == 3 && g_ui.tools.edit_level == 3, "level: PageUp clamps at 3");
    for( int i = 0; i < 5; i++ )
        (void)edp_level_step(&g_ui, -1, 0);
    CHECK(g_ui.tools.edit_level == 0 && g_ui.tools.vis_level == 0, "level: PageDown clamps at 0");
    frame();
    CHECK(g_chrome.widgets[buttons[1]].checked && strcmp(world_title(), "World - level 0") == 0,
        "level: a key's pin presses the row's button on the next frame");
    edp_level_set(&g_ui, EDP_LEVEL_AUTO);
    CHECK(edp_level_step(&g_ui, -1, 2) == 1, "level: PageDown from Auto on level 2 pins 1");
    edp_level_set(&g_ui, EDP_LEVEL_AUTO);
    frame();

    /* Another window's pin (STATE_EDIT_LEVEL lands in edit_level alone): the
     * cap, the row and the terrain radios follow on the next frame. */
    {
        int32_t value = 3;
        Editor_UiApplyState(&g_ui, TORIRSMAPED_STATE_EDIT_LEVEL, &value, 1);
        frame();
        CHECK(g_ui.tools.edit_level == 3 && g_ui.tools.vis_level == 3, "level: a relayed pin caps the view too");
        CHECK(g_chrome.widgets[buttons[4]].checked && terrain_radio_checked("auto", "3") == 1,
            "level: a relayed pin presses the row and the terrain radio");
        value = -1;
        Editor_UiApplyState(&g_ui, TORIRSMAPED_STATE_EDIT_LEVEL, &value, 1);
        frame();
        CHECK(g_ui.tools.edit_level == -1 && g_ui.tools.vis_level == -1 && g_chrome.widgets[buttons[0]].checked,
            "level: a relayed Auto draws every level again");
    }

    /* Every tool edits the pinned plane, whatever the pointer's pick says. */
    {
        int bad = 0;
        edp_level_set(&g_ui, 2);
        for( int t = 0; t < EDITOR_TOOL_COUNT; t++ )
        {
            Editor_UiSetTool(&g_ui, (enum Editor_Tool)t);
            for( int hover = 0; hover < 4; hover++ )
                bad += edp_level_click(&g_ui, hover) != 2;
        }
        CHECK(bad == 0, "level: the click level is the pinned 2 at every tool and every picked plane (%d off)", bad);
        edp_level_set(&g_ui, EDP_LEVEL_AUTO);
        bad = 0;
        for( int hover = 0; hover < 4; hover++ )
            bad += edp_level_click(&g_ui, hover) != hover;
        CHECK(bad == 0 && edp_level_click(&g_ui, -1) == 0 && edp_level_click(&g_ui, 7) == 3,
            "level: Auto clicks the picked plane, clamped");
        Editor_UiSetTool(&g_ui, EDITOR_TOOL_SELECT);
    }

    /* An empty upper floor takes edits: paint and height create its tiles, a
     * loc and a spawn land on that level. */
    {
        struct Editor_BrushOp op;
        int x2 = 0;
        int z2 = 0;
        int x3 = 0;
        int z3 = 0;
        int count;
        int undo = 0;
        const struct Editor_Tile* t;
        struct Editor_Square* sq = square_of(50, 50);

        CHECK(empty_tile_at(2, &x2, &z2) && empty_tile_at(3, &x3, &z3), "level: m50_50 has empty level-2/3 tiles");
        edp_level_set(&g_ui, 2);
        memset(&op, 0, sizeof(op));
        op.tool = EDITOR_TOOL_PAINT_UNDERLAY;
        op.world_x = x2;
        op.world_z = z2;
        op.level = edp_level_click(&g_ui, 0);
        op.underlay_id = 7;
        op.brush.radius = 0;
        op.brush.step = 1;
        count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
        CHECK(count == 1 && edp_apply(&g_ui, g_cmds, count, "paint") == 1, "level: paint on empty level 2 applies");
        undo++;
        t = tile_at(x2, z2, 2);
        CHECK(op.level == 2 && t && t->underlay_id == 7, "level: the empty level-2 tile now has the underlay");

        op.tool = EDITOR_TOOL_RAISE;
        op.world_x = x3;
        op.world_z = z3;
        op.level = 3;
        op.brush.step = 4;
        count = Editor_BrushCommands(&g_editor.doc, &op, g_cmds, EDITOR_RECT_TILES_MAX);
        CHECK(count == 1 && edp_apply(&g_ui, g_cmds, count, "raise") == 1, "level: raise on empty level 3 applies");
        undo++;
        t = tile_at(x3, z3, 3);
        CHECK(t && t->has_height, "level: the empty level-3 tile now states a height");

        {
            int locs_before = sq ? sq->loc_count : 0;
            int found = 0;
            CHECK(edp_locs_place(&g_ui, 1530, 10, 0, x2, z2, edp_level_click(&g_ui, 0)) == 1,
                "level: a loc places on pinned level 2");
            undo++;
            for( int i = 0; sq && i < sq->loc_count; i++ )
                if( sq->locs[i].loc_id == 1530 && sq->locs[i].x == x2 % 64 && sq->locs[i].z == z2 % 64
                    && sq->locs[i].level == 2 )
                    found = 1;
            CHECK(found && sq->loc_count == locs_before + 1, "level: the loc document holds it at level 2");
        }
        {
            struct Editor_DocHandle handle;
            char key[EDITOR_DOC_KEY_MAX];
            int found = 0;
            g_ui.tools.pick_kind = 1;
            g_ui.tools.pick_id = 0;
            CHECK(Editor_Locs_PlaceSpawn(&g_ui, x2, z2, edp_level_click(&g_ui, 0)) == 1,
                "level: a spawn places on pinned level 2");
            undo++;
            Editor_SpawnDocKeyAt(key, x2, z2);
            if( Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, key, &handle) && handle.spawn->loaded )
                for( int i = 0; i < handle.spawn->file.line_count; i++ )
                    if( handle.spawn->file.lines[i].kind == EDITOR_SPAWN_LINE_ROW
                        && handle.spawn->file.lines[i].row.abs_x == x2 && handle.spawn->file.lines[i].row.abs_z == z2
                        && handle.spawn->file.lines[i].row.level == 2 )
                        found = 1;
            CHECK(found, "level: the spawn row's level is 2");
            g_ui.tools.pick_kind = -1;
            g_ui.tools.pick_id = -1;
        }
        for( int i = 0; i < undo; i++ )
            (void)Editor_Undo(&g_editor);
        t = tile_at(x2, z2, 2);
        CHECK(t && t->underlay_id == 0, "level: undo empties the level-2 tile again");
        edp_level_set(&g_ui, EDP_LEVEL_AUTO);
    }
    frame();
    Editor_UiFree(&g_ui);
}


/* ---- hotkeys (docs/MAPED_HOTKEYS.md) ------------------------------------------- */

/** The menu bar's drop titled `title` ("File", "Edit", "View"). */
static int
menu_drop(const char* title)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
        if( g_chrome.widgets[i].kind == TORIRS_CHROME_W_DROPDOWN && g_chrome.widgets[i].menu_mode
            && strcmp(g_chrome.widgets[i].text, title) == 0 )
            return i;
    return -1;
}

/** The row of drop `drop` whose label part is `label`, or NULL. */
static const char*
menu_row(
    int drop,
    const char* label)
{
    if( drop < 0 || !g_chrome.widgets[drop].options )
        return NULL;
    for( int r = 0; r < g_chrome.widgets[drop].option_count; r++ )
        if( menu_row_is(g_chrome.widgets[drop].options[r], label) )
            return g_chrome.widgets[drop].options[r];
    return NULL;
}

/** The Hotkeys panel's list row of `action` (workbench 6.8: one wide cells
 *  VLIST, editor_workspaces_test.c drives its sort and filters), -1 when the
 *  search or a filter hides it. */
static int
hotkeys_row(
    int panel,
    int action)
{
    static int rows[EDITOR_HK_COUNT];
    int const n = Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT);
    (void)panel;
    for( int i = 0; i < n; i++ )
        if( rows[i] == action )
            return i;
    return -1;
}

/** Field `field` of the list's row of `action` as the VLIST holds it (the
 *  row brought on screen first), "" when not listed. */
static const char*
hotkeys_cell(
    int action,
    int field)
{
    static char out[96];
    int const list = Editor_Hotkeys_ListWidget(&g_ui);
    int const row = hotkeys_row(-1, action);
    const char* text = NULL;

    out[0] = '\0';
    if( row < 0 || list < 0 )
        return out;
    ToriRSChrome_VListSetScroll(&g_chrome, list, row);
    frame();
    for( int i = 0; i < TORIRS_CHROME_VROW_POOL; i++ )
        if( g_chrome.vrows[i].owner == list && g_chrome.vrows[i].index == row )
            text = g_chrome.vrows[i].text;
    for( int f = 0; f < field && text; f++ )
    {
        text = strchr(text, TORIRS_CHROME_CELL_SEP);
        if( text )
            text++;
    }
    if( text )
    {
        const char* end = strchr(text, TORIRS_CHROME_CELL_SEP);
        snprintf(out, sizeof(out), "%.*s", end ? (int)(end - text) : (int)strlen(text), text);
    }
    return out;
}

/** A click on chord cell `cell` (1 or 2) of `action`'s row. */
static void
hotkeys_click_cell(
    int action,
    int cell)
{
    int const list = Editor_Hotkeys_ListWidget(&g_ui);
    ToriRSChrome_VListSetSelected(&g_chrome, list, hotkeys_row(-1, action));
    g_chrome.widgets[list].vlist_cell = cell;
    activate(list);
}

/** Feed the Hotkeys panel one frame with `chord_text` pressed (NULL: only
 *  the keys already set in `in`). */
static int
hotkeys_feed(const char* chord_text)
{
    struct Editor_HotkeyInput in;
    struct Editor_Chord chord;

    memset(&in, 0, sizeof(in));
    if( chord_text )
    {
        int const ok = Editor_ChordParse(chord_text, &chord);
        assert(ok);
        (void)ok;
        if( chord.mods & EDITOR_HK_MOD_CTRL )
            in.held[TORIRSK_CTRL] = 1;
        if( chord.mods & EDITOR_HK_MOD_SHIFT )
            in.held[TORIRSK_SHIFT] = 1;
        in.down[chord.key] = 1;
        in.held[chord.key] = 1;
    }
    return Editor_Hotkeys_Feed(&g_ui, &in);
}

static void
test_hotkeys(void)
{
    struct EditorUiPanel* module;
    int panel;
    int rows = 0;
    int search;
    char expect[64];
    char chord[32];
    struct Editor_Chord parsed;

    layout_boot(1530, 1006, 1);
    module = Editor_UiFindPanel(&g_ui, "hotkeys");
    CHECK(module && module->chrome_panel >= 0, "hotkeys: the panel is registered");
    if( !module )
    {
        Editor_UiFree(&g_ui);
        return;
    }
    panel = module->chrome_panel;
    CHECK(!g_chrome.panels[panel].visible, "hotkeys: hidden until asked for");
    {
        const char* row = menu_row(view_menu(), "Hotkeys");
        /* The row's right column: the state ("hidden") before the chord. */
        CHECK(row && strchr(row, '\t') && strlen(row) >= 2 && strcmp(row + strlen(row) - 2, "F1") == 0
                  && strstr(row, "hidden"),
            "hotkeys: View lists it, hidden, with F1 (%s)", row ? row : "-");
    }
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_HOTKEYS) == 1, "hotkeys: F1's action runs");
    frame();
    frame();
    CHECK(g_chrome.panels[panel].visible, "hotkeys: F1 opens the panel");

    /* One row per action, every action. */
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        int const r = hotkeys_row(panel, a);
        CHECK(r >= 0, "hotkeys: a row for %s", Editor_HotkeyInfo(a)->id);
        if( r >= 0 )
            rows++;
    }
    CHECK(rows == EDITOR_HK_COUNT, "hotkeys: %d rows for %d actions", rows, EDITOR_HK_COUNT);
    Editor_HotkeyDisplay(&g_ui.hotkeys, EDITOR_HK_EDIT_UNDO, chord, (int)sizeof(chord));
    snprintf(expect, sizeof(expect), "%s -", chord);
    CHECK(strcmp(hotkeys_cell(EDITOR_HK_EDIT_UNDO, 1), "Undo") == 0
              && strcmp(hotkeys_cell(EDITOR_HK_EDIT_UNDO, 3), expect) == 0,
        "hotkeys: Undo's row has its chord and an empty second cell (%s)", hotkeys_cell(EDITOR_HK_EDIT_UNDO, 3));

    /* Search filters by any column. */
    search = find_widget(panel, TORIRS_CHROME_W_TEXTINPUT, "Search");
    CHECK(search >= 0, "hotkeys: a search box");
    if( search >= 0 )
    {
        ToriRSChrome_SetText(&g_chrome, search, "redo");
        frame();
        CHECK(hotkeys_row(panel, EDITOR_HK_EDIT_REDO) >= 0 && hotkeys_row(panel, EDITOR_HK_EDIT_UNDO) < 0
                  && hotkeys_row(panel, EDITOR_HK_TOOL_RAISE) < 0,
            "hotkeys: 'redo' leaves Redo and hides Undo and Raise");
        ToriRSChrome_SetText(&g_chrome, search, "F12");
        frame();
        CHECK(hotkeys_row(panel, EDITOR_HK_VIEW_WORLD) >= 0 && hotkeys_row(panel, EDITOR_HK_EDIT_REDO) < 0,
            "hotkeys: a chord finds its action (F12 -> World)");
        ToriRSChrome_SetText(&g_chrome, search, "");
        frame();
        CHECK(hotkeys_row(panel, EDITOR_HK_EDIT_UNDO) >= 0, "hotkeys: an empty search shows all");
    }

    /* Click Bake's first chord cell, press Ctrl+Z: Bake is bound, the File
     * row says so, and Bake and Undo clash, marked on both rows. */
    hotkeys_click_cell(EDITOR_HK_FILE_BAKE, 1);
    CHECK(Editor_Hotkeys_Capturing(&g_ui) == EDITOR_HK_FILE_BAKE && Editor_Hotkeys_CaptureSlot(&g_ui) == 0,
        "hotkeys: a click on a chord cell captures for it");
    CHECK(hotkeys_feed("shift") == 1 && Editor_Hotkeys_Capturing(&g_ui) == EDITOR_HK_FILE_BAKE,
        "hotkeys: a modifier alone never binds");
    CHECK(hotkeys_feed("ctrl+z") == 1 && Editor_Hotkeys_Capturing(&g_ui) < 0, "hotkeys: Ctrl+Z ends the capture");
    frame();
    (void)Editor_ChordParse("ctrl+z", &parsed);
    Editor_ChordDisplay(parsed, chord, (int)sizeof(chord));
    {
        const char* row = menu_row(menu_drop("File"), "Bake cache (slow)");
        snprintf(expect, sizeof(expect), "Bake cache (slow)\t%s", chord);
        CHECK(row && strcmp(row, expect) == 0, "hotkeys: the File row re-labels at once (%s)", row ? row : "-");
    }
    CHECK(strcmp(hotkeys_cell(EDITOR_HK_FILE_BAKE, 5), "!") == 0 && strcmp(hotkeys_cell(EDITOR_HK_EDIT_UNDO, 5), "!") == 0,
        "hotkeys: the clash is marked on both rows");
    CHECK(strcmp(hotkeys_cell(EDITOR_HK_EDIT_REDO, 5), "") == 0, "hotkeys: Redo is not");

    /* Rebind Undo away: the clash clears; the Edit row follows. */
    hotkeys_click_cell(EDITOR_HK_EDIT_UNDO, 1);
    (void)hotkeys_feed("ctrl+u");
    frame();
    (void)Editor_ChordParse("ctrl+u", &parsed);
    Editor_ChordDisplay(parsed, chord, (int)sizeof(chord));
    {
        const char* row = menu_row(menu_drop("Edit"), "Undo");
        snprintf(expect, sizeof(expect), "Undo\t%s", chord);
        CHECK(row && strcmp(row, expect) == 0, "hotkeys: Edit > Undo shows the new chord (%s)", row ? row : "-");
    }
    CHECK(strcmp(hotkeys_cell(EDITOR_HK_FILE_BAKE, 5), "") == 0, "hotkeys: no clash left");

    /* Esc cancels; Backspace unbinds the slot. */
    hotkeys_click_cell(EDITOR_HK_TOOL_RAISE, 1);
    (void)hotkeys_feed("escape");
    CHECK(Editor_Hotkeys_Capturing(&g_ui) < 0 && Editor_HotkeysIsDefault(&g_ui.hotkeys, EDITOR_HK_TOOL_RAISE),
        "hotkeys: Esc cancels and keeps the chord");
    hotkeys_click_cell(EDITOR_HK_TOOL_RAISE, 1);
    (void)hotkeys_feed("backspace");
    frame();
    CHECK(g_ui.hotkeys.bind[EDITOR_HK_TOOL_RAISE].chord[0].key == TORIRSK_UNKNOWN, "hotkeys: Backspace unbinds");
    CHECK(strcmp(hotkeys_cell(EDITOR_HK_TOOL_RAISE, 3), "- -") == 0, "hotkeys: the row says unbound in both cells");
    {
        struct EditorUiPanel* rail = Editor_UiFindPanel(&g_ui, "toolrail");
        int const raise = rail ? find_widget(rail->chrome_panel, TORIRS_CHROME_W_ICONBUTTON, "Raise") : -1;
        CHECK(raise >= 0 && strcmp(g_chrome.widgets[raise].tooltip, "Raise terrain (brush)") == 0
                  && g_chrome.widgets[raise].chord[0] == '\0' && g_chrome.widgets[raise].tooltip_chord[0] == '\0',
            "hotkeys: the rail drops the badge and the tip's chord (%s / '%s')",
            raise >= 0 ? g_chrome.widgets[raise].tooltip : "-", raise >= 0 ? g_chrome.widgets[raise].chord : "-");
    }

    /* Reset all. */
    activate(find_widget(panel, TORIRS_CHROME_W_BUTTON, "Reset all"));
    frame();
    {
        int all = 1;
        for( int a = 0; a < EDITOR_HK_COUNT; a++ )
            all &= Editor_HotkeysIsDefault(&g_ui.hotkeys, a);
        CHECK(all, "hotkeys: Reset all restores the table");
    }
    {
        struct EditorUiPanel* rail = Editor_UiFindPanel(&g_ui, "toolrail");
        int const raise = rail ? find_widget(rail->chrome_panel, TORIRS_CHROME_W_ICONBUTTON, "Raise") : -1;
        /* The badge wears the chord; the tooltip falls back to it. */
        CHECK(raise >= 0 && strcmp(g_chrome.widgets[raise].tooltip, "Raise terrain (brush)") == 0
                  && strcmp(g_chrome.widgets[raise].chord, "3") == 0,
            "hotkeys: the rail badge wears its chord again (%s / '%s')",
            raise >= 0 ? g_chrome.widgets[raise].tooltip : "-", raise >= 0 ? g_chrome.widgets[raise].chord : "-");
    }
    {
        int const level1 = menubar_item("1");
        Editor_HotkeyDisplay(&g_ui.hotkeys, EDITOR_HK_LEVEL_PIN1, chord, (int)sizeof(chord));
        CHECK(level1 >= 0 && strstr(g_chrome.widgets[level1].tooltip, chord) != NULL,
            "hotkeys: the Level button names its chord (%s)", level1 >= 0 ? g_chrome.widgets[level1].tooltip : "-");
    }

    /* The actions the keys share with the panels. */
    {
        int const radius = g_ui.tools.brush.radius;
        int const terrain = find_panel("Terrain");
        int const slider = terrain >= 0 ? find_widget(terrain, TORIRS_CHROME_W_SLIDER, "Radius  [ ]") : -1;
        (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_BRUSH_RADIUS_UP);
        frame();
        CHECK(g_ui.tools.brush.radius == radius + 1, "hotkeys: ] grows the radius (%d)", g_ui.tools.brush.radius);
        CHECK(slider >= 0 && ToriRSChrome_SliderValue(&g_chrome, slider) == radius + 1,
            "hotkeys: the Terrain slider follows");
        (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_BRUSH_RADIUS_DOWN);
        frame();
    }
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_RAISE);
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_LOWER);
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_TOOL_PREVIOUS);
    CHECK(g_ui.tools.tool == EDITOR_TOOL_RAISE, "hotkeys: ` swaps back to the previous tool");
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_TOOL_PREVIOUS);
    CHECK(g_ui.tools.tool == EDITOR_TOOL_LOWER, "hotkeys: ...and again");
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_SELECT);
    {
        int const terrain = find_panel("Terrain");
        CHECK(terrain >= 0 && g_chrome.panels[terrain].visible, "hotkeys: Terrain is open before Tab");
        (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_HIDE_PANELS);
        frame();
        CHECK(!g_chrome.panels[terrain].visible && !g_chrome.panels[panel].visible
                  && g_chrome.panels[g_ui.world_panel].visible,
            "hotkeys: Tab hides every panel but World");
        (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_HIDE_PANELS);
        frame();
        CHECK(g_chrome.panels[terrain].visible && g_chrome.panels[panel].visible,
            "hotkeys: Tab again brings back what was open");
    }
    frame();
    Editor_UiFree(&g_ui);
}

/* ---- the Triggers panel (edpanel_triggers.c) ------------------------------------------- */

/** Row `row` of a VLIST as the chrome holds it, NULL when off screen. */
static const char*
cat_vrow(
    int widget,
    int row)
{
    for( int i = 0; i < TORIRS_CHROME_VROW_POOL; i++ )
        if( g_cat_chrome.vrows[i].owner == widget && g_cat_chrome.vrows[i].index == row )
            return g_cat_chrome.vrows[i].text;
    return NULL;
}

static const char*
text_of(const char* key)
{
    struct Editor_DocHandle handle;
    if( !Editor_DocFind(&g_editor.doc, EDITOR_DOC_TEXT, key, &handle) || !handle.text || !handle.text->loaded )
        return NULL;
    return handle.text->text ? handle.text->text : "";
}

static int
read_file_text(
    const char* path,
    char* out,
    size_t out_size)
{
    FILE* file = fopen(path, "rb");
    size_t n;
    if( !file )
        return 0;
    n = fread(out, 1, out_size - 1, file);
    out[n] = '\0';
    fclose(file);
    return 1;
}

/* What the fake editor (TORIRS_EDITOR_OPEN_CMD, test_triggers) logs for
 * `file` at `line`: printf's `<path>:<line>` and a newline; on Windows cmd's
 * echo, the path as the template quoted it, `"<path>":<line>`. */
#ifdef _WIN32
#define OPENED_AT(file, line) file "\":" line
#else
#define OPENED_AT(file, line) file ":" line "\n"
#endif

/** The editor command runs in the background: wait (up to 3 s) until the
 *  file holds `needle`. */
static int
wait_file_has(
    const char* path,
    const char* needle,
    char* out,
    size_t out_size)
{
    for( int i = 0; i < 300; i++ )
    {
        if( read_file_text(path, out, out_size) && strstr(out, needle) )
            return 1;
        usleep(10000);
    }
    return 0;
}

static void
dump_console(int lines)
{
    for( int i = lines - 1; i >= 0; i-- )
    {
        const char* line = Editor_UiConsoleLine(&g_cat_ui, i);
        if( line )
            printf("    console: %s\n", line);
    }
}

/* ---- the status bar (edpanel_statusbar.c) ------------------------------------------ */

static struct
{
    int on;
    int x;
    int z;
    int level;
} g_statusbar_hover;

static int
statusbar_fake_hover(
    void* user,
    int* out_world_x,
    int* out_world_z,
    int* out_level)
{
    (void)user;
    if( !g_statusbar_hover.on )
        return 0;
    *out_world_x = g_statusbar_hover.x;
    *out_world_z = g_statusbar_hover.z;
    *out_level = g_statusbar_hover.level;
    return 1;
}

#define STATUSBAR_RUNS_MAX 16

/** The bar's shown runs, left to right: their widgets into `out`. */
static int
statusbar_runs(
    int panel,
    int* out)
{
    int count = 0;
    for( int w = g_chrome.panels[panel].first_widget; w >= 0 && count < STATUSBAR_RUNS_MAX;
         w = g_chrome.widgets[w].next )
        if( g_chrome.widgets[w].kind == TORIRS_CHROME_W_LABEL && !g_chrome.widgets[w].hidden )
            out[count++] = w;
    return count;
}

/** The shown run whose text is `text`, or -1. */
static int
statusbar_run(
    int panel,
    const char* text)
{
    int runs[STATUSBAR_RUNS_MAX];
    int const count = statusbar_runs(panel, runs);
    for( int i = 0; i < count; i++ )
        if( strcmp(g_chrome.widgets[runs[i]].text, text) == 0 )
            return runs[i];
    return -1;
}

/** Every shown run lies inside the bar, none cut (rule R2), and none carries a
 *  cut-off hint row. */
static void
statusbar_check_fits(
    int panel,
    const char* what)
{
    int runs[STATUSBAR_RUNS_MAX];
    int const count = statusbar_runs(panel, runs);
    struct ToriRSChromeRect const bar = g_chrome.panels[panel].last_rect;
    int const pad = TORIRS_CHROME_M_PAD * ToriRSChrome_Scale(&g_chrome);

    for( int i = 0; i < count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[runs[i]];
        int const text_w = ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_MENU, ToriRSChrome_Scale(&g_chrome), w->text);
        /* The bar draws the text at the run's box + half a pad. */
        int const text_x = w->x + pad / 2;
        CHECK(text_x >= bar.x && text_x + text_w <= bar.x + bar.w, "%s: run '%s' (%d..%d) lies inside the bar (%d..%d)",
            what, w->text, text_x, text_x + text_w, bar.x, bar.x + bar.w);
        if( w->text[0] == '[' )
            CHECK(strstr(w->text, "...") == NULL, "%s: hint run '%s' is whole", what, w->text);
    }
}

/** The first label of `panel` whose text holds `needle`, or -1. */
static int
label_with(
    int panel,
    const char* needle)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel == panel && w->kind == TORIRS_CHROME_W_LABEL && strstr(w->text, needle) )
            return i;
    }
    return -1;
}

/** The well's cell index of the swatch Editor_Palette_SwatchAt places at
 *  (x, y): 13 cells of 14 px a row. */
static int
swatch_index(
    int x,
    int y)
{
    return ((y - 1) / 14) * 13 + (x - 1) / 14;
}

/* The Palette's blending aids (the owner's request, 2026-11-06): the floor
 * colour maths against the client's own, the blend strip across a seam,
 * the last two picks as the strip's ends, the hover readout, a swatch
 * press, Every id and the sorts, the colour picker's matches by hex and by
 * slider, and the Terrain panel's Mix slider for Paint underlay. */
static void
test_palette_colour(void)
{
    static const uint32_t k_colours[] = { 0x282820u, 0xB7D49Au, 0xFC49AAu, 0x604040u, 0x7296B7u,
                                          0x191919u, 0xffffffu, 0x000000u, 0x40a04cu, 0x29380Fu };
    uint32_t strip[23];
    int ids[9];
    int palette;
    int terrain;
    int graph_under;
    int w_all;
    int w_sort;
    int w_hue;
    int w_sat;
    int w_light;
    int w_hex;
    int w_mix;
    int x = 0;
    int y = 0;
    int n;
    int same = 1;

    /* The maths: HSL24 and HSL16 as 3rd/toridraw/osrs/palette.c has them. */
    for( size_t i = 0; i < sizeof(k_colours) / sizeof(k_colours[0]); i++ )
    {
        struct HSL const h = palette_rgb_to_hsl24((int)k_colours[i]);
        struct Editor_FloorHsl e;
        Editor_FloorHslOfRgb(k_colours[i], &e);
        if( e.hue != h.hue || e.sat != h.sat || e.light != h.light || e.luminance != h.luminance
            || e.chroma != h.chroma )
            same = 0;
        if( Editor_FloorHsl16(h.hue, h.sat, h.light) != palette_hsl24_to_hsl16(h.hue, h.sat, h.light) )
            same = 0;
    }
    CHECK(same, "floorcolor: HSL24 and HSL16 as palette.c packs them");
    {
        uint32_t const white = Editor_FloorRgbOfHsl16(127);
        uint32_t const green = Editor_FloorRgbOfHsl16((21 << 10) | (7 << 7) | 64);
        CHECK(Editor_FloorRgbOfHsl16(0) == 0 && (white >> 16) >= 0xf0 && ((white >> 8) & 0xff) >= 0xf0
                  && (white & 0xff) >= 0xf0 && ((green >> 8) & 0xff) > 0xc0 && ((green >> 8) & 0xff) > (green >> 16)
                  && ((green >> 8) & 0xff) > (green & 0xff),
            "floorcolor: the palette's black, near-white and the green of hue 21 (#%06x #%06x #%06x)",
            (unsigned)Editor_FloorRgbOfHsl16(0), (unsigned)white, (unsigned)green);
    }
    {
        struct Editor_FloorHsl e;
        uint32_t const back = Editor_FloorRgbOfHsl(85, 128, 96);
        Editor_FloorHslOfRgb(back, &e);
        CHECK(abs(e.hue - 85) <= 2 && abs(e.sat - 128) <= 3 && abs(e.light - 96) <= 2,
            "floorcolor: the sliders' HSL comes back from its colour (#%06x: %d %d %d)", (unsigned)back, e.hue,
            e.sat, e.light);
    }
    Editor_FloorBlendStrip(0x40a040u, 0x40a040u, strip, 23);
    same = 1;
    for( int i = 0; i < 23; i++ )
        if( strip[i] != Editor_FloorShown(0x40a040u) )
            same = 0;
    CHECK(same, "blend strip: one underlay each side is that underlay's ground everywhere");
    Editor_FloorBlendStrip(0x282820u, 0xB7D49Au, strip, 23);
    CHECK(strip[0] == Editor_FloorShown(0x282820u) && strip[22] == Editor_FloorShown(0xB7D49Au)
              && strip[11] != strip[0] && strip[11] != strip[22] && strip[5] == strip[0] && strip[17] == strip[22],
        "blend strip: the planes' own ground beyond five tiles, the average between (#%06x #%06x #%06x)",
        (unsigned)strip[0], (unsigned)strip[11], (unsigned)strip[22]);
    {
        struct Editor_FloorHsl before;
        struct Editor_FloorHsl after;
        int rising = 1;
        Editor_FloorHslOfRgb(strip[5], &before);
        for( int i = 6; i <= 17; i++ )
        {
            Editor_FloorHslOfRgb(strip[i], &after);
            if( after.light < before.light - 2 )
                rising = 0;
            before = after;
        }
        CHECK(rising, "blend strip: the lightness climbs from the dark side to the light one");
    }
    CHECK(Editor_FloorDistance(0x282820u, 0x282820u) == 0
              && Editor_FloorDistance(0x282820u, 0x2a2a22u) < Editor_FloorDistance(0x282820u, 0xB7D49Au),
        "floorcolor: a near colour is nearer than a far one (%d < %d)", Editor_FloorDistance(0x282820u, 0x2a2a22u),
        Editor_FloorDistance(0x282820u, 0xB7D49Au));

    layout_boot(1920, 1080, 1);
    palette = find_panel("Palette");
    terrain = find_panel("Terrain");
    CHECK(palette >= 0 && terrain >= 0, "palette: the Palette and Terrain panels exist");
    if( palette < 0 || terrain < 0 )
    {
        Editor_UiFree(&g_ui);
        return;
    }
    ToriRSChrome_PanelSetVisible(&g_chrome, palette, 1);
    frame();
    graph_under = find_widget(palette, TORIRS_CHROME_W_GRAPH, NULL);
    w_all = find_widget(palette, TORIRS_CHROME_W_CHECKBOX, "Every id");
    w_sort = find_widget(palette, TORIRS_CHROME_W_DROPDOWN, "Sort");
    w_hue = find_widget(palette, TORIRS_CHROME_W_SLIDER, "Hue");
    w_sat = find_widget(palette, TORIRS_CHROME_W_SLIDER, "Sat (%)");
    w_light = find_widget(palette, TORIRS_CHROME_W_SLIDER, "Light (%)");
    w_hex = find_widget(palette, TORIRS_CHROME_W_TEXTINPUT, "#");
    CHECK(graph_under >= 0 && w_all >= 0 && w_sort >= 0 && w_hue >= 0 && w_sat >= 0 && w_light >= 0 && w_hex >= 0,
        "palette: the wells, Every id, Sort, the three sliders and the hex field (%d %d %d %d %d %d %d)",
        graph_under, w_all, w_sort, w_hue, w_sat, w_light, w_hex);
    if( graph_under < 0 || w_all < 0 || w_sort < 0 || w_hue < 0 || w_hex < 0 )
    {
        Editor_UiFree(&g_ui);
        return;
    }

    /* Two picks: the strip runs from the first to the second, the picker
     *  takes the second's colour, and that record is its own nearest. */
    g_ui.tools.underlay_id = 12;
    frame();
    g_ui.tools.underlay_id = 40;
    frame();
    CHECK(Editor_Palette_BlendFrom(&g_ui) == 12, "palette: the strip runs from the pick before (%d)",
        Editor_Palette_BlendFrom(&g_ui));
    CHECK(label_with(palette, "Blend 12 -> 40") >= 0, "palette: the blend row names both picks and their distance");
    CHECK(strcmp(ToriRSChrome_Text(&g_chrome, w_hex), "#40a068") == 0, "palette: the hex field shows the pick's colour (%s)",
        ToriRSChrome_Text(&g_chrome, w_hex));
    n = Editor_Palette_Matches(&g_ui, 0, ids, 9);
    CHECK(n == 9 && ids[0] == 40, "palette: the pick's record is the nearest underlay to its colour (%d: %d)", n,
        n > 0 ? ids[0] : -1);
    CHECK(label_with(palette, "Underlays: 40") >= 0, "palette: the match row lists the nearest first");

    /* A hover names the swatch; a press picks it and arms the brush. */
    CHECK(Editor_Palette_SwatchAt(&g_ui, 0, 40, &x, &y), "palette: the pick has a swatch");
    push_graph(graph_under, TORIRS_CHROME_EVENT_GRAPH_HOVER, x, y, 0);
    frame();
    CHECK(strstr(g_chrome.widgets[graph_under].tooltip, "underlay 40") && strstr(g_chrome.widgets[graph_under].tooltip, "#40a068"),
        "palette: the hover names the swatch's id and colour (%s)", g_chrome.widgets[graph_under].tooltip);
    CHECK(Editor_Palette_SwatchAt(&g_ui, 0, 12, &x, &y), "palette: the earlier pick is still listed");
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_RAISE);
    push_graph(graph_under, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
    frame();
    CHECK(g_ui.tools.underlay_id == 12 && g_ui.tools.tool == EDITOR_TOOL_PAINT_UNDERLAY
              && Editor_Palette_BlendFrom(&g_ui) == 40,
        "palette: the press picks 12, arms Paint underlay and the strip now runs from 40 (%d %d %d)",
        g_ui.tools.underlay_id, (int)g_ui.tools.tool, Editor_Palette_BlendFrom(&g_ui));

    /* Every id lists the window's whole set (the fake answers every id). */
    CHECK(!Editor_Palette_SwatchAt(&g_ui, 0, 200, &x, &y), "palette: an unused id is not listed by default");
    CHECK(g_floor_requests == 0, "palette: nothing asked of the window before Every id (%d)", g_floor_requests);
    ToriRSChrome_SetChecked(&g_chrome, w_all, 1);
    activate(w_all);
    CHECK(Editor_Palette_SwatchAt(&g_ui, 0, 200, &x, &y) && Editor_Palette_SwatchAt(&g_ui, 1, 700, &x, &y)
              && !Editor_Palette_SwatchAt(&g_ui, 0, 257, &x, &y),
        "palette: Every id lists underlay 200 and overlay 700, not an underlay past 256");
    for( int i = 0; i < 20; i++ )
        frame();
    CHECK(g_floor_requests == 1024 - 700 && g_floor_request_first == 701,
        "palette: Every id asks the window for the overlays it has not loaded, 701 first (%d asked, first %d)",
        g_floor_requests, g_floor_request_first);

    /* By lightness: along the well the light never falls; by hue, the
     *  hue-then-light key never falls. */
    for( int sort = 1; sort <= 2; sort++ )
    {
        struct Editor_FloorHsl hsl;
        int order[257];
        int ordered = 1;
        int seen = 0;
        ToriRSChrome_DropdownSetSelected(&g_chrome, w_sort, sort);
        activate(w_sort);
        memset(order, 0, sizeof(order));
        for( int id = 1; id <= 256; id++ )
            if( Editor_Palette_SwatchAt(&g_ui, 0, id, &x, &y) && swatch_index(x, y) < 257 )
            {
                order[swatch_index(x, y)] = id;
                seen++;
            }
        for( int i = 2, last = -1; i < 257; i++ )
        {
            uint32_t rgb;
            int key;
            if( !order[i] )
                continue;
            fake_floor_rgb(NULL, 0, order[i], &rgb);
            Editor_FloorHslOfRgb(rgb, &hsl);
            key = sort == 2 ? hsl.light * 256 + hsl.hue : hsl.sat < 24 ? (1 << 16) + hsl.light : hsl.hue * 256 + hsl.light;
            if( key < last )
                ordered = 0;
            last = key;
        }
        CHECK(seen == 256 && ordered, "palette: sort %d orders the 256 swatches by its key (%d seen)", sort, seen);
    }
    ToriRSChrome_DropdownSetSelected(&g_chrome, w_sort, 0);
    activate(w_sort);
    ToriRSChrome_SetChecked(&g_chrome, w_all, 0);
    activate(w_all);

    /* The hex finds the record of that exact colour; the sliders follow. */
    ToriRSChrome_SetText(&g_chrome, w_hex, "#40a04c");
    activate(w_hex);
    n = Editor_Palette_Matches(&g_ui, 0, ids, 9);
    CHECK(n == 9 && ids[0] == 12, "palette: #40a04c's nearest underlay is 12 (%d)", n > 0 ? ids[0] : -1);
    CHECK(ToriRSChrome_SliderValue(&g_chrome, w_hue) > 100 && ToriRSChrome_SliderValue(&g_chrome, w_hue) < 150
              && ToriRSChrome_SliderValue(&g_chrome, w_light) > 30 && ToriRSChrome_SliderValue(&g_chrome, w_light) < 60,
        "palette: the sliders show the hex's hue and lightness (%d, %d)", ToriRSChrome_SliderValue(&g_chrome, w_hue),
        ToriRSChrome_SliderValue(&g_chrome, w_light));
    n = Editor_Palette_Matches(&g_ui, 1, ids, 9);
    CHECK(n == 9, "palette: nine overlays matched too (%d)", n);
    /* A slider: the colour and the hex follow, the matches move. */
    ToriRSChrome_SliderSet(&g_chrome, w_light, 90);
    activate(w_light);
    n = Editor_Palette_Matches(&g_ui, 0, ids, 9);
    CHECK(ToriRSChrome_Text(&g_chrome, w_hex)[0] == '#' && strcmp(ToriRSChrome_Text(&g_chrome, w_hex), "#40a04c") != 0
              && label_with(palette, ToriRSChrome_Text(&g_chrome, w_hex)) >= 0 && n == 9 && ids[0] != 12,
        "palette: a lighter colour by slider shows in the hex and the row, and matches elsewhere (%s, %d)",
        ToriRSChrome_Text(&g_chrome, w_hex), n > 0 ? ids[0] : -1);

    /* The eyedroppers: a button arms one, the next ground pick takes that
     *  layer only (the overlay with its shape and rotation) and disarms. */
    {
        int const w_drop_under = find_widget(palette, TORIRS_CHROME_W_BUTTON, "Eyedropper: pick a tile's underlay");
        int const w_drop_over = find_widget(palette, TORIRS_CHROME_W_BUTTON, "Eyedropper: pick a tile's overlay");
        struct Editor_Tile* tile;

        if( !square_of(50, 50) || !square_of(50, 50)->loaded )
            CHECK(Editor_OpenDoc(&g_editor, EDITOR_DOC_SQUARE, "m50_50"), "palette: open m50_50 for the eyedropper");
        tile = tile_at(SQ_X0 + 5, SQ_Z0 + 6, 0);
        CHECK(w_drop_under >= 0 && w_drop_over >= 0 && tile, "palette: two eyedropper buttons and a tile (%d %d)",
            w_drop_under, w_drop_over);
        if( w_drop_under >= 0 && w_drop_over >= 0 && tile )
        {
            tile->underlay_id = 33;
            tile->has_overlay = 1;
            tile->overlay_id = 7;
            tile->shape = 4;
            tile->rotation = 3;
            g_ui.tools.underlay_id = 12;
            g_ui.tools.overlay_none = 1;
            g_ui.tools.overlay_id = 0;
            g_ui.tools.overlay_shape = 0;
            g_ui.tools.overlay_rotation = 0;
            CHECK(Editor_Palette_DropperArmed(&g_ui) == 0, "palette: no eyedropper armed at first");
            activate(w_drop_under);
            CHECK(Editor_Palette_DropperArmed(&g_ui) == 1, "palette: the underlay button arms the underlay dropper");
            activate(w_drop_under);
            CHECK(Editor_Palette_DropperArmed(&g_ui) == 0, "palette: pressing it again disarms");
            activate(w_drop_under);
            Editor_UiSetTool(&g_ui, EDITOR_TOOL_RAISE);
            CHECK(Editor_Palette_DropperAt(&g_ui, SQ_X0 + 5, SQ_Z0 + 6, 0) && g_ui.tools.underlay_id == 33
                      && g_ui.tools.overlay_none && g_ui.tools.tool == EDITOR_TOOL_PAINT_UNDERLAY
                      && Editor_Palette_DropperArmed(&g_ui) == 0,
                "palette: the underlay dropper takes 33 alone, arms Paint underlay and disarms (%d %d %d)",
                g_ui.tools.underlay_id, g_ui.tools.overlay_none, (int)g_ui.tools.tool);
            activate(w_drop_over);
            CHECK(Editor_Palette_DropperArmed(&g_ui) == 2, "palette: the overlay button arms the overlay dropper");
            g_ui.tools.underlay_id = 12;
            CHECK(Editor_Palette_DropperAt(&g_ui, SQ_X0 + 5, SQ_Z0 + 6, 0) && !g_ui.tools.overlay_none
                      && g_ui.tools.overlay_id == 7 && g_ui.tools.overlay_shape == 4 && g_ui.tools.overlay_rotation == 3
                      && g_ui.tools.underlay_id == 12 && g_ui.tools.tool == EDITOR_TOOL_PAINT_OVERLAY,
                "palette: the overlay dropper takes 7 shape 4 rotation 3, leaves the underlay (%d %d %d %d)",
                g_ui.tools.overlay_id, g_ui.tools.overlay_shape, g_ui.tools.overlay_rotation, g_ui.tools.underlay_id);
            activate(w_drop_over);
            CHECK(!Editor_Palette_DropperAt(&g_ui, 99 * 64, 99 * 64, 0) && Editor_Palette_DropperArmed(&g_ui) == 0,
                "palette: a pick from a square that is not open is refused and disarms");
            tile->has_overlay = 0;
            tile->underlay_id = 0;
            g_ui.tools.overlay_none = 1;
        }
    }

    /* The Terrain panel's Mix: shown for Paint underlay only, in the readout
     *  under 100, the hotkey's chord steps it by ten. */
    w_mix = find_widget(terrain, TORIRS_CHROME_W_SLIDER, "Mix (%)");
    CHECK(w_mix >= 0, "terrain: a Mix slider");
    if( w_mix >= 0 )
    {
        Editor_UiSetTool(&g_ui, EDITOR_TOOL_RAISE);
        frame();
        CHECK(g_chrome.widgets[w_mix].hidden, "terrain: Mix is hidden under Raise");
        Editor_UiSetTool(&g_ui, EDITOR_TOOL_PAINT_UNDERLAY);
        frame();
        CHECK(!g_chrome.widgets[w_mix].hidden && g_ui.tools.paint_mix == 100, "terrain: Mix shows under Paint underlay, at 100");
        ToriRSChrome_SliderSet(&g_chrome, w_mix, 50);
        activate(w_mix);
        CHECK(g_ui.tools.paint_mix == 50 && label_with(terrain, "mix 50%") >= 0,
            "terrain: the slider sets the mix and the readout says so (%d)", g_ui.tools.paint_mix);
        CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_BRUSH_STRENGTH_UP) && g_ui.tools.paint_mix == 60,
            "terrain: Shift+] steps the mix by ten under Paint underlay (%d)", g_ui.tools.paint_mix);
        frame();
        CHECK(ToriRSChrome_SliderValue(&g_chrome, w_mix) == 60, "terrain: the slider follows the hotkey");
        g_ui.tools.paint_mix = 100;
    }
    Editor_UiFree(&g_ui);
}

static void
test_statusbar(void)
{
    struct EditorUiPanel* module;
    struct ToriRSChromeRect bar;
    struct EditorUiRect world;
    struct Editor_Chord chord;
    int runs[STATUSBAR_RUNS_MAX];
    int count;
    int panel;
    int run;
    char long_status[160];

    layout_boot(1530, 1006, 1);
    module = Editor_UiFindPanel(&g_ui, "statusbar");
    CHECK(module && module->chrome_panel >= 0, "statusbar: the module registers");
    if( !module )
    {
        Editor_UiFree(&g_ui);
        return;
    }
    panel = module->chrome_panel;
    CHECK(!module->title && Editor_HotkeyForPanel("statusbar") < 0,
        "statusbar: no View-menu row and no hotkey: it is always on");
    CHECK(g_chrome.panels[panel].visible && g_chrome.panels[panel].title[0] == '\0'
              && !g_chrome.panels[panel].framed && g_chrome.panels[panel].style == TORIRS_CHROME_PANEL_MENUBAR,
        "statusbar: shown with the editor, no title bar, no frame, one row");

    /* The band at the foot: 20 px at 1x, the surface's width; the world, the
     * column and the console stop above it. */
    bar = g_chrome.panels[panel].last_rect;
    world = Editor_UiWorldRect(&g_ui);
    CHECK(bar.x == 0 && bar.w == 1530 && bar.h == 20 && bar.y + bar.h == 1006,
        "statusbar: a 20 px band across the foot (%d,%d %dx%d)", bar.x, bar.y, bar.w, bar.h);
    CHECK(world.y + world.h <= bar.y && g_ui.world.y + g_ui.world.h <= bar.y && g_ui.dock_bottom <= bar.y,
        "statusbar: the world (%d..%d) and the column (..%d) stop above it (%d)", world.y, world.y + world.h,
        g_ui.dock_bottom, bar.y);
    for( int i = 0; i < g_chrome.panel_count; i++ )
    {
        struct ToriRSChromeRect const r = g_chrome.panels[i].last_rect;
        if( i == panel || !layout_panel(i) )
            continue;
        CHECK(r.y + r.h <= bar.y, "statusbar: '%s' (%d..%d) ends above the bar", g_chrome.panels[i].title, r.y,
            r.y + r.h);
    }
    layout_check("with the status bar");

    /* The hint: Select armed, nothing selected, the pointer off the world. */
    CHECK(g_ui.tools.tool == EDITOR_TOOL_SELECT, "statusbar: Select is armed at boot");
    /* Five hint rows, then the info run (what the editor holds). */
    count = statusbar_runs(panel, runs);
    CHECK(count == 6 && strcmp(g_chrome.widgets[runs[0]].text, "[click] tile") == 0
              && strcmp(g_chrome.widgets[runs[1]].text, "[Shift+click] add loc") == 0
              && strcmp(g_chrome.widgets[runs[2]].text, "[drag] marquee") == 0
              && strcmp(g_chrome.widgets[runs[3]].text, "[Q] rotate") == 0
              && strcmp(g_chrome.widgets[runs[4]].text, "[Shift+Q] reshape") == 0,
        "statusbar: Select's live chords (%d runs, first '%s')", count,
        count > 0 ? g_chrome.widgets[runs[0]].text : "");
    CHECK(count == 6 && strncmp(g_chrome.widgets[runs[5]].text, "sel: none | Select | level Auto", 31) == 0,
        "statusbar: the info run names the selection, the tool and the level ('%s')",
        count == 6 ? g_chrome.widgets[runs[5]].text : "");
    run = statusbar_run(panel, "[Q] rotate");
    CHECK(run >= 0 && strcmp(g_chrome.widgets[run].tooltip, "Rotate  [Q]") == 0,
        "statusbar: a hint's tooltip names the action and its chord ('%s')",
        run >= 0 ? g_chrome.widgets[run].tooltip : "");
    statusbar_check_fits(panel, "statusbar at boot");

    /* The hover readout, and the pointer over the world brings Ctrl+A. */
    g_ui.app_services.hover_tile = statusbar_fake_hover;
    g_statusbar_hover.on = 1;
    g_statusbar_hover.x = 3222;
    g_statusbar_hover.z = 3218;
    g_statusbar_hover.level = 1;
    frame();
    CHECK(statusbar_run(panel, "3222,3218 L1 | m50_50") >= 0, "statusbar: the hover reads tile, level and square");
    CHECK(statusbar_run(panel, "[" "Ctrl" "+A] square") >= 0 || statusbar_run(panel, "[Cmd+A] square") >= 0,
        "statusbar: over the world Select square appears");
    g_statusbar_hover.on = 0;
    frame();
    count = statusbar_runs(panel, runs);
    CHECK(count == 6, "statusbar: off the world the hover is gone (%d runs)", count);

    /* The armed tool drives the strip; a pick brings Place's key. */
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_LOC_PLACE);
    g_ui.tools.pick_kind = -1;
    g_ui.tools.pick_id = -1;
    frame();
    CHECK(statusbar_run(panel, "[click] place") >= 0 && statusbar_run(panel, "[Q] turn") >= 0
              && statusbar_run(panel, "[Esc] drop") >= 0,
        "statusbar: Place's rows replace Select's");
    g_ui.tools.pick_kind = 0;
    g_ui.tools.pick_id = 1902;
    frame();
    CHECK(statusbar_run(panel, "[click/Space, Enter] place") >= 0, "statusbar: with a loc pick, Space places");
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_RAISE);
    frame();
    CHECK(statusbar_run(panel, "[drag] paint") >= 0 && statusbar_run(panel, "[[ ]] radius") >= 0
              && statusbar_run(panel, "[Shift+[ ]] strength") >= 0,
        "statusbar: a brush's rows");

    /* A rebind relabels the strip on the next frame. */
    CHECK(Editor_ChordParse("minus", &chord), "statusbar: minus parses");
    Editor_HotkeysSet(&g_ui.hotkeys, EDITOR_HK_BRUSH_RADIUS_DOWN, &chord, 1);
    frame();
    CHECK(statusbar_run(panel, "[- ]] radius") >= 0, "statusbar: the rebind shows on the next frame");
    Editor_HotkeysResetDefaults(&g_ui.hotkeys);
    frame();
    CHECK(statusbar_run(panel, "[[ ]] radius") >= 0, "statusbar: Reset all shows the default again");
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_SELECT);

    /* The status sentence: logged, shown for 6 s of the frame clock, gone. */
    ToriRSChrome_Tick(&g_chrome, 100000);
    Editor_UiStatus(&g_ui, "Place refused: tile %d,%d holds a wall of that shape; press E to swap", 3222, 3218);
    frame();
    CHECK(strcmp(Editor_UiConsoleLine(&g_ui, 0),
              "Place refused: tile 3222,3218 holds a wall of that shape; press E to swap")
              == 0,
        "statusbar: the console keeps the sentence");
    CHECK(statusbar_run(panel, "Place refused: tile 3222,3218 holds a wall of that shape; press E to swap") >= 0,
        "statusbar: the bar shows it");
    ToriRSChrome_Tick(&g_chrome, 100000 + EDITOR_UI_STATUS_MS - 1);
    frame();
    CHECK(Editor_UiStatusText(&g_ui)[0] && statusbar_run(panel, Editor_UiStatusText(&g_ui)) >= 0,
        "statusbar: still shown a moment before 6 s");
    ToriRSChrome_Tick(&g_chrome, 100000 + EDITOR_UI_STATUS_MS);
    frame();
    CHECK(!Editor_UiStatusText(&g_ui)[0] && statusbar_runs(panel, runs) == 6,
        "statusbar: gone at 6 s, the hint and the info alone again");
    CHECK(strstr(Editor_UiConsoleLine(&g_ui, 0), "Place refused") != NULL, "statusbar: the console still has it");

    /* Measured, never clipped: a long sentence on a narrow window ends in
     * "...", the full text its tooltip; hint rows that do not fit are left
     * out whole. */
    ToriRSChrome_SetSurface(&g_chrome, 640, 480);
    frame();
    frame();
    bar = g_chrome.panels[panel].last_rect;
    CHECK(bar.w == 640 && bar.y + bar.h == 480, "statusbar: follows a resize (%d,%d %dx%d)", bar.x, bar.y, bar.w,
        bar.h);
    memset(long_status, 0, sizeof(long_status));
    for( int i = 0; i < 25; i++ )
        strcat(long_status, "word ");
    Editor_UiStatus(&g_ui, "%s", long_status);
    frame();
    count = statusbar_runs(panel, runs);
    run = count > 0 ? runs[count - 1] : -1;
    CHECK(run >= 0 && strlen(g_chrome.widgets[run].text) > 3
              && strcmp(g_chrome.widgets[run].text + strlen(g_chrome.widgets[run].text) - 3, "...") == 0
              && strncmp(g_chrome.widgets[run].text, "word word", 9) == 0,
        "statusbar: a long sentence ends in an ellipsis ('%s')", run >= 0 ? g_chrome.widgets[run].text : "");
    CHECK(run >= 0 && strncmp(g_chrome.widgets[run].tooltip, "word word word", 14) == 0,
        "statusbar: and its tooltip is the sentence");
    statusbar_check_fits(panel, "statusbar at 640 px");
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_LOC_PLACE);
    frame();
    statusbar_check_fits(panel, "statusbar at 640 px, Place");

    /* At 2x the band is 40 px and still ends at the foot. */
    Editor_UiFree(&g_ui);
    layout_boot(1530, 1006, 2);
    module = Editor_UiFindPanel(&g_ui, "statusbar");
    if( module )
    {
        bar = g_chrome.panels[module->chrome_panel].last_rect;
        world = Editor_UiWorldRect(&g_ui);
        CHECK(bar.h == 40 && bar.y + bar.h == 1006 && world.y + world.h <= bar.y,
            "statusbar: 40 px at 2x, the world above it (%d,%d %dx%d)", bar.x, bar.y, bar.w, bar.h);
        statusbar_check_fits(module->chrome_panel, "statusbar at 2x");
        /* The server's write state, from FACT_HELLO. */
        g_editor.writable = 0;
        frame();
        CHECK(statusbar_run(module->chrome_panel, "read-only") >= 0, "statusbar: a read-only server says so");
        g_editor.writable = 1;
        frame();
        CHECK(statusbar_run(module->chrome_panel, "read-only") < 0, "statusbar: and stops when it can save");
    }
    Editor_UiFree(&g_ui);
}

static void
test_triggers(void)
{
    static const char* const k_test_rel = "areas/test/scripts/test.rs2";
    static const char* const k_edited = "rs2:edited/test_crate/scripts/test_crate.rs2";
    struct Editor_ScriptRef rows[16];
    char opened_log[2048];
    char open_cmd[2300];
    char got[2048];
    int panel;
    int list;
    int n;

    /* Open in editor writes to a file here, never starts an editor. */
    snprintf(opened_log, sizeof(opened_log), "%s/opened.txt", g_tmp_root);
    remove(opened_log);
#ifdef _WIN32
    /* cmd.exe runs it there (Editor_HostLocalOpenInEditor): echo writes the
     * path as the command line quoted it, "<path>":<line>. */
    snprintf(open_cmd, sizeof(open_cmd), "echo {path}:{line}>>\"%s\"", opened_log);
#else
    snprintf(open_cmd, sizeof(open_cmd), "printf '%%s\\n' {path}:{line} >> '%s'", opened_log);
#endif
    setenv("TORIRS_EDITOR_OPEN_CMD", open_cmd, 1);

    cat_ui_open();
    Editor_Triggers_Register(&g_cat_ui);
    CHECK(Editor_Triggers_SetPaths(&g_cat_ui, g_tmp_root, NULL, NULL) == 0, "triggers: no compiler, no Check");
    cat_frame();
    panel = cat_panel("Triggers");
    CHECK(panel >= 0 && !g_cat_chrome.panels[panel].visible, "triggers: registered, hidden until asked for");
    if( panel < 0 )
    {
        Editor_UiFree(&g_cat_ui);
        return;
    }
    list = cat_widget(panel, TORIRS_CHROME_W_VLIST, "");

    /* A subject by name: the catalog's rows, the type found. */
    n = Editor_Triggers_ShowSubject(&g_cat_ui, NULL, "test_crate");
    cat_frame();
    {
        const char* type = NULL;
        const char* subject = Editor_Triggers_Subject(&g_cat_ui, &type);
        CHECK(n == 1 && g_cat_chrome.panels[panel].visible && strcmp(subject, "test_crate") == 0
                  && strcmp(type, "loc") == 0,
            "triggers: test_crate is a loc with one trigger (%d, %s)", n, type ? type : "-");
    }
    n = Editor_Triggers_Rows(&g_cat_ui, rows, 16);
    CHECK(n == 1 && strcmp(rows[0].kind, "oploc1") == 0 && strcmp(rows[0].rel, k_test_rel) == 0 && rows[0].line == 2,
        "triggers: oploc1 at test.rs2:2");
    CHECK(list >= 0 && cat_vrow(list, 0) && strstr(cat_vrow(list, 0), "oploc1")
              && strstr(cat_vrow(list, 0), "test.rs2:2") && strstr(cat_vrow(list, 0), "areas/test/scripts"),
        "triggers: the row reads kind, file:line, directory (%s)", list >= 0 && cat_vrow(list, 0) ? cat_vrow(list, 0) : "-");
    {
        int const kinds = cat_widget(panel, TORIRS_CHROME_W_DROPDOWN, "Trigger");
        CHECK(kinds >= 0 && ToriRSChrome_DropdownOptionValue(&g_cat_chrome, kinds, 2)
                  && strcmp(ToriRSChrome_DropdownOptionValue(&g_cat_chrome, kinds, 2), "oploc3") == 0,
            "triggers: Add offers the loc kinds");
    }

    /* Open in editor: the command, the path absolute, the line. */
    ToriRSChrome_VListSetSelected(&g_cat_chrome, list, 0);
    cat_activate(list);
    cat_activate(cat_widget(panel, TORIRS_CHROME_W_BUTTON, "Open in editor"));
    {
        char expect[PATH_MAX + 64];
#ifdef _WIN32
        /* The absolute path, quoted by the template, then the line. */
        CHECK(wait_file_has(opened_log, "test.rs2\":2", got, sizeof(got)) && got[0] == '"'
                  && strstr(got, "/server/scripts/areas/test/scripts/test.rs2\":2") != NULL,
            "triggers: Open in editor ran TORIRS_EDITOR_OPEN_CMD with the path and line (%s)", got);
        (void)expect;
#else
        /* realpath writes up to PATH_MAX (glibc's fortify aborts a smaller
         * buffer). */
        char root_abs[PATH_MAX];
        if( !realpath(g_tmp_root, root_abs) )
            snprintf(root_abs, sizeof(root_abs), "%s", g_tmp_root);
        snprintf(expect, sizeof(expect), "%s/server/scripts/areas/test/scripts/test.rs2:2\n", root_abs);
        CHECK(wait_file_has(opened_log, "test.rs2:2", got, sizeof(got)) && strcmp(got, expect) == 0,
            "triggers: Open in editor ran TORIRS_EDITOR_OPEN_CMD with the path and line (%s)", got);
#endif
#ifdef _WIN32
        CHECK(cat_console_has("open in editor: echo"), "triggers: the command line is in the Console");
#else
        CHECK(cat_console_has("open in editor: printf"), "triggers: the command line is in the Console");
#endif
    }

    /* File mode: every trigger of one file. */
    n = Editor_Triggers_ShowFile(&g_cat_ui, k_test_rel);
    cat_frame();
    n = Editor_Triggers_Rows(&g_cat_ui, rows, 16);
    CHECK(n == 2 && strcmp(rows[1].kind, "opnpc1") == 0 && strcmp(rows[1].subject, "test_guard") == 0
              && rows[1].line == 5,
        "triggers: file mode lists both headers of test.rs2 (%d)", n);
    CHECK(cat_vrow(list, 1) && strstr(cat_vrow(list, 1), "[opnpc1,test_guard]"),
        "triggers: a file row names its subject");
    CHECK(Editor_Triggers_ShowFile(&g_cat_ui, "../x.rs2") == 0, "triggers: a path out of server/scripts is refused");

    /* Follow: the Record panel's record. */
    CHECK(Editor_Record_Open(&g_cat_ui, TORIRSMAPED_CATALOG_LOC, "test_crate") == 1, "triggers: record test_crate");
    Editor_Triggers_Follow(&g_cat_ui);
    cat_frame();
    CHECK(strcmp(Editor_Triggers_Subject(&g_cat_ui, NULL), "test_crate") == 0, "triggers: follows the Record panel");
    {
        int const record = cat_panel("Record");
        int const button = cat_widget(record, TORIRS_CHROME_W_BUTTON, "Triggers...");
        CHECK(button >= 0, "triggers: the Record panel has Triggers...");
    }

    /* Add trigger oploc3 + Op text Search: op3 and the template, one step. */
    {
        int const kinds = cat_widget(panel, TORIRS_CHROME_W_DROPDOWN, "Trigger");
        int const op = cat_widget(panel, TORIRS_CHROME_W_TEXTINPUT, "Op text");
        int const add = cat_widget(panel, TORIRS_CHROME_W_BUTTON, "Add trigger...");
        const struct Editor_CfgRecord* rec;
        int row;

        ToriRSChrome_DropdownSetSelected(&g_cat_chrome, kinds, 2);
        ToriRSChrome_SetText(&g_cat_chrome, op, "");
        cat_activate(add);
        CHECK(cat_console_has("type its text") && (!text_of(k_edited) || !text_of(k_edited)[0]),
            "triggers: no op3 and no Op text: refused, nothing written");
        ToriRSChrome_SetText(&g_cat_chrome, op, "Search");
        cat_activate(add);
        cat_frame();
        rec = Editor_Record_Current(&g_cat_ui);
        row = rec ? Editor_CfgRecordFindRow(rec, "op3", "", 0) : -1;
        CHECK(row >= 0 && rec->rows[row].layer >= 0 && strcmp(rec->rows[row].value, "Search") == 0,
            "triggers: op3=Search written through the Record panel");
        if( row < 0 || rec->rows[row].layer < 0 )
            dump_console(8);
        CHECK(text_of(k_edited)
                  && strcmp(text_of(k_edited),
                         "// test_crate: triggers written by hand, started from the ToriRSEd Record panel.\n"
                         "\n[oploc3,test_crate]\nmes(\"Nothing interesting happens.\");\n")
                         == 0,
            "triggers: the template appended to the subject's new edited script");
        CHECK(newest_group_named("Add [oploc3,test_crate] (op3=Search)")
                  && newest_group()->kinds[EDITOR_CMD_CFG_FIELD] == 1 && newest_group()->kinds[EDITOR_CMD_TEXT_SPLICE] == 1,
            "triggers: ONE History row holding the field and the trigger");
        n = Editor_Triggers_Rows(&g_cat_ui, rows, 16);
        CHECK(n == 2 && strcmp(rows[1].kind, "oploc3") == 0
                  && strcmp(rows[1].rel, "edited/test_crate/scripts/test_crate.rs2") == 0 && rows[1].line == 3
                  && rows[1].unsaved && ToriRSChrome_VListSelected(&g_cat_chrome, list) == 1,
            "triggers: the new trigger is a row (unsaved), selected (%d)", n);

        /* A trigger the tree already declares is refused. */
        ToriRSChrome_DropdownSetSelected(&g_cat_chrome, kinds, 0);
        cat_activate(add);
        CHECK(cat_console_has("already declared in areas/test/scripts/test.rs2:2"),
            "triggers: oploc1 exists in test.rs2: refused");

        /* Open in editor on the unsaved row saves it first. */
        ToriRSChrome_VListSetSelected(&g_cat_chrome, list, 1);
        cat_activate(list);
        cat_activate(cat_widget(panel, TORIRS_CHROME_W_BUTTON, "Open in editor"));
        {
            char disk[2048];
            snprintf(disk, sizeof(disk), "%s/server/scripts/edited/test_crate/scripts/test_crate.rs2", g_tmp_root);
            CHECK(read_file_text(disk, got, sizeof(got)) && strcmp(got, text_of(k_edited)) == 0
                      && cat_console_has("saved edited/test_crate/scripts/test_crate.rs2 first"),
                "triggers: Open in editor saved the edited script before handing it over");
            CHECK(wait_file_has(opened_log, OPENED_AT("test_crate.rs2", "3"), got, sizeof(got)),
                "triggers: and handed over line 3");

            /* Edited outside: the disk changes under the open document. */
            {
                FILE* file = fopen(disk, "ab");
                if( file )
                {
                    fputs("\n[oploc4,test_crate]\nmes(\"Kick.\");\n", file);
                    fclose(file);
                }
            }
            CHECK(Editor_Triggers_Refresh(&g_cat_ui) == 1 && !g_cat_chrome.widgets[cat_widget(panel,
                                                                  TORIRS_CHROME_W_BUTTON, "Reload from disk")].hidden,
                "triggers: Refresh finds the document edited outside, offers Reload from disk");
            CHECK(Editor_Triggers_AddTrigger(&g_cat_ui, "oploc5", "Climb") == 0 && cat_console_has("edited outside"),
                "triggers: Add refuses to write over a copy edited outside");
            CHECK(Editor_Triggers_ReloadFromDisk(&g_cat_ui) == 1 && read_file_text(disk, got, sizeof(got))
                      && strcmp(got, text_of(k_edited)) == 0 && newest_group_named("Reload edited/test_crate"),
                "triggers: Reload from disk makes the document the disk's text, one undo step");
            n = Editor_Triggers_Rows(&g_cat_ui, rows, 16);
            CHECK(n == 3 && strcmp(rows[2].kind, "oploc4") == 0 && rows[2].line == 6,
                "triggers: the trigger written outside is a row (%d)", n);
        }
        /* Back: the reload, then the add. */
        CHECK(Editor_Undo(&g_editor) >= 1 && Editor_Undo(&g_editor) == 2, "triggers: undo the reload, then the add");
        rec = Editor_Record_Current(&g_cat_ui);
        row = rec ? Editor_CfgRecordFindRow(rec, "op3", "", 0) : -1;
        CHECK((row < 0 || rec->rows[row].layer < 0) && text_of(k_edited) && text_of(k_edited)[0] == '\0',
            "triggers: op3 unstated and the edited script empty again");
    }

    /* An npc with its op1 stated: the trigger only. */
    Editor_Triggers_ShowSubject(&g_cat_ui, "npc", "test_guard");
    CHECK(Editor_Triggers_AddTrigger(&g_cat_ui, "apnpc1", NULL) == 1 && newest_group_named("Add [apnpc1,test_guard]")
              && newest_group()->kinds[EDITOR_CMD_CFG_FIELD] == 0,
        "triggers: apnpc1 on test_guard (op1 Talk-to stated): the trigger only");
    CHECK(Editor_Triggers_AddTrigger(&g_cat_ui, "oploc1", NULL) == 0 && cat_console_has("not one this record type"),
        "triggers: a loc kind on an npc is refused");
    CHECK(Editor_Undo(&g_editor) == 1, "triggers: undo");

    /* Check: a result's rows; no compiler here, and the hotkeys reach it. */
    {
        static const char k_out[] = "/stage/areas/x/scripts/y.rs2:7: warning: unused local $a\n"
                                    "/stage/areas/test/scripts/test.rs2:3: unknown command 'mess'\n"
                                    "sscompile: 1 compiled, 0 reused, 1 failed\n";
        struct Builder_SscResult* result = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*result));
        int diags;
        assert(result);
        result->status = BUILDER_SSC_FAILED;
        Builder_SscParseOutput("/stage", k_out, strlen(k_out), result);
        Editor_Triggers_ShowResult(&g_cat_ui, result);
        ToriMem_Free(ToriMem_Editor(), result);
        cat_frame();
        diags = cat_widget(panel, TORIRS_CHROME_W_VLIST, "Problems");
        CHECK(diags >= 0 && cat_vrow(diags, 0)
                  && strcmp(cat_vrow(diags, 0), "test.rs2:3: unknown command 'mess'   areas/test/scripts") == 0,
            "triggers: the Problems row is file:line: message, directory (%s)",
            diags >= 0 && cat_vrow(diags, 0) ? cat_vrow(diags, 0) : "-");
        ToriRSChrome_VListSetSelected(&g_cat_chrome, diags, 0);
        cat_activate(diags);
        cat_activate(cat_widget(panel, TORIRS_CHROME_W_BUTTON, "Open problem"));
        CHECK(wait_file_has(opened_log, OPENED_AT("areas/test/scripts/test.rs2", "3"), got, sizeof(got)),
            "triggers: Open problem hands over its file and line (errors listed before warnings)");
        /* Check is the editor server's BUILD job (workbench 6.6), heard
         * through the Live panel; this window has no such link, and the
         * panel says why. */
        CHECK(Editor_HotkeyRun(&g_cat_ui, EDITOR_HK_FILE_CHECK_SCRIPTS) == 1
                  && strncmp(Editor_UiStatusText(&g_cat_ui), "Check refused: ", 15) == 0,
            "triggers: the Check hotkey reaches the panel, which says why it cannot (%s)",
            Editor_UiStatusText(&g_cat_ui));
        CHECK(Editor_HotkeyRun(&g_cat_ui, EDITOR_HK_FILE_RUN) == 1 && cat_console_has("no Live panel"),
            "triggers: the Run hotkey says why it cannot");
        CHECK(ToriRSChrome_Disabled(&g_cat_chrome, cat_widget(panel, TORIRS_CHROME_W_BUTTON, "Run")),
            "triggers: Run disabled without a Live panel");
    }
    {
        int const shown = g_cat_chrome.panels[panel].visible;
        CHECK(Editor_HotkeyRun(&g_cat_ui, EDITOR_HK_VIEW_TRIGGERS) == 1, "triggers: the View action runs");
        cat_frame();
        CHECK(g_cat_chrome.panels[panel].visible == !shown, "triggers: Ctrl+E's action toggles the panel");
    }
    unsetenv("TORIRS_EDITOR_OPEN_CMD");
    Editor_UiFree(&g_cat_ui);
}

/* ---- the command palette (docs/MAPED_HOTKEYS.md "The command palette") -------- */

/** The selected row's item of the command palette, NULL when none. */
static const struct Editor_CmdPalItem*
palette_selected_item(void)
{
    const struct Editor_CmdPalList* list = Editor_CmdPalette_List(&g_ui);
    int const row = Editor_CmdPalette_Selected(&g_ui);
    if( !list || row < 0 || row >= list->row_count || list->row_kind[row] != EDITOR_CMDPAL_ROW_ITEM )
        return NULL;
    return &list->items[list->row_item[row]];
}

/* The palette's additions from the ergonomics work (2026-11-03) on the
 * workbench palette (Ctrl+K): Go to tile, absolute tiles that land the
 * camera, the Recent group, an App-bound action handed to the App. */
static void
test_command_palette(void)
{
    const struct Editor_CmdPalList* list;
    const struct Editor_CmdPalItem* item;
    int opens;

    layout_boot(1280, 800, 1);
    CHECK(Editor_UiFindPanel(&g_ui, "cmdpalette") != NULL, "palette: the module registers (\"cmdpalette\")");

    /* Go to tile...: the palette prefilled with the latched tile, its row
     * selected; Enter brings the camera there. */
    g_ui.tools.tile_valid = 1;
    g_ui.tools.tile_x = 3222;
    g_ui.tools.tile_z = 3218;
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_GOTO) == 1 && Editor_CmdPalette_IsOpen(&g_ui),
        "goto: Go to tile opens the palette");
    g_ui.tools.tile_valid = 0;
    frame();
    item = palette_selected_item();
    CHECK(item && item->kind == EDITOR_CMDPAL_KIND_TILE && item->x == 3222 && item->z == 3218 && item->level == 0
              && item->group == EDITOR_CMDPAL_SQUARES,
        "goto: the tile row is selected (%s)", item ? item->label : "-");
    CHECK(Editor_CmdPalette_RunRow(&g_ui, Editor_CmdPalette_Selected(&g_ui)) == 1, "palette: Enter goes there");
    CHECK(!Editor_CmdPalette_IsOpen(&g_ui), "palette: closed after the run");
    CHECK(g_ui.open_goto_valid && g_ui.open_goto_x == 3222 && g_ui.open_goto_z == 3218,
        "palette: no camera in this window: the open carries the tile for the camera's landing");
    g_ui.open_goto_valid = 0;

    /* A level after the tile; both numbers under 256 are a square. */
    (void)Editor_CmdPalette_OpenWith(&g_ui, "3222 3218 1");
    item = palette_selected_item();
    CHECK(item && item->kind == EDITOR_CMDPAL_KIND_TILE && item->level == 1 && strstr(item->dim, "L1"),
        "palette: '3222 3218 1' is level 1 (%s)", item ? item->dim : "-");
    Editor_CmdPalette_SetQuery(&g_ui, "50 50");
    list = Editor_CmdPalette_List(&g_ui);
    {
        int square = 0;
        for( int r = 0; list && r < list->row_count; r++ )
            if( list->row_kind[r] == EDITOR_CMDPAL_ROW_ITEM && list->items[list->row_item[r]].kind == EDITOR_CMDPAL_KIND_SQUARE
                && strcmp(list->items[list->row_item[r]].name, "50 50") == 0 )
                square = 1;
        CHECK(square, "palette: '50 50' is a square for the picker's Find");
    }

    /* An action only the App performs (Frame the selection) is handed to
     * it, once. */
    Editor_CmdPalette_SetQuery(&g_ui, "frame the selection");
    list = Editor_CmdPalette_List(&g_ui);
    {
        int row = -1;
        for( int r = 0; list && r < list->row_count; r++ )
            if( list->row_kind[r] == EDITOR_CMDPAL_ROW_ITEM && list->items[list->row_item[r]].kind == EDITOR_CMDPAL_KIND_ACTION
                && list->items[list->row_item[r]].target == EDITOR_HK_CAM_FRAME )
                row = r;
        CHECK(row >= 0, "palette: 'frame the selection' finds the camera action");
        if( row >= 0 )
        {
            CHECK(Editor_CmdPalette_RunRow(&g_ui, row) == 1, "palette: the row runs");
            CHECK(Editor_CmdPalette_TakePendingAction(&g_ui) == EDITOR_HK_CAM_FRAME
                      && Editor_CmdPalette_TakePendingAction(&g_ui) == -1,
                "palette: the App takes Frame the selection, once");
        }
    }
    if( Editor_CmdPalette_IsOpen(&g_ui) )
        Editor_CmdPalette_Close(&g_ui);

    /* Recent squares: an open notes the square; the File menu lists it and
     * the palette's empty box offers it first. */
    Editor_UiOpenSquare(&g_ui, 50, 51, "", -1, 0);
    Editor_UiOpenSquare(&g_ui, 50, 50, "ported/testlane", 1, 0);
    frame();
    frame();
    CHECK(g_ui.recent.count == 2 && strcmp(g_ui.recent.keys[0], "ported/testlane/m50_50") == 0
              && strcmp(g_ui.recent.keys[1], "m50_51") == 0,
        "recent: opens are noted newest first (%d: %s)", g_ui.recent.count, g_ui.recent.keys[0]);
    {
        int const file = menu_drop("File");
        CHECK(menu_row(file, "Recent squares") != NULL && menu_row(file, "  ported/testlane/m50_50") != NULL
                  && menu_row(file, "  m50_51") != NULL,
            "recent: File lists Recent squares and both rows");
        opens = g_window.open_calls;
        if( file >= 0 )
        {
            int row_at = -1;
            for( int r = 0; r < g_chrome.widgets[file].option_count; r++ )
                if( menu_row_is(g_chrome.widgets[file].options[r], "  m50_51") )
                    row_at = r;
            CHECK(row_at >= 0, "recent: the m50_51 row is there");
            if( row_at >= 0 )
            {
                ToriRSChrome_DropdownSetSelected(&g_chrome, file, row_at);
                activate(file);
                CHECK((g_window.open_calls == opens + 1 && g_window.open_x == 50 && g_window.open_z == 51)
                          || (g_ui.guard.action == EDITOR_UI_GUARD_OPEN_SQUARE && g_ui.guard.map_x == 50
                              && g_ui.guard.map_z == 51),
                    "recent: the row opens m50_51 (or the save guard asks)");
            }
        }
    }
    (void)Editor_CmdPalette_Open(&g_ui);
    list = Editor_CmdPalette_List(&g_ui);
    CHECK(list && list->row_count > 2 && list->row_group[0] == EDITOR_CMDPAL_RECENT
              && list->row_kind[1] == EDITOR_CMDPAL_ROW_ITEM
              && strcmp(list->items[list->row_item[1]].label, "ported/testlane/m50_50") == 0
              && list->items[list->row_item[1]].kind == EDITOR_CMDPAL_KIND_RECENT
              && strcmp(list->items[list->row_item[2]].label, "m50_51") == 0,
        "palette: an empty box lists the recents first (%s)",
        list && list->row_count > 1 ? list->items[list->row_item[1]].label : "-");
    opens = g_window.open_calls;
    CHECK(Editor_CmdPalette_RunRow(&g_ui, 2) == 1, "palette: a recent row runs");
    frame();
    CHECK((g_window.open_calls == opens + 1 && g_window.open_x == 50 && g_window.open_z == 51)
              || (g_ui.guard.action == EDITOR_UI_GUARD_OPEN_SQUARE && g_ui.guard.map_x == 50 && g_ui.guard.map_z == 51),
        "palette: the recent m50_51 is opened (or the save guard asks)");

    /* The camera scheme toggle is remembered in the same ring's file. */
    CHECK(g_ui.recent.camera_scheme == EDITOR_CAMERA_SCHEME_DEFAULT, "camera: the default scheme at boot");
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_CAM_SCHEME) == 1 && g_ui.recent.camera_scheme == EDITOR_CAMERA_SCHEME_UNITY,
        "camera: the scheme action switches to Unity");
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_CAM_SCHEME) == 1 && g_ui.recent.camera_scheme == EDITOR_CAMERA_SCHEME_DEFAULT,
        "camera: ...and back");

    /* Select all of the picked kind / Invert over the open squares. */
    {
        struct Editor_Square* square = square_of(50, 50);
        int kind_count = 0;
        int total = 0;
        int loc_id = -1;
        if( square )
            for( int i = 0; i < square->loc_count; i++ )
                if( square->locs[i].level == 0 )
                {
                    if( loc_id < 0 )
                        loc_id = square->locs[i].loc_id;
                    total++;
                }
        if( square )
            for( int i = 0; i < square->loc_count; i++ )
                if( square->locs[i].level == 0 && square->locs[i].loc_id == loc_id )
                    kind_count++;
        CHECK(square && loc_id >= 0 && total > kind_count, "select: m50_50 has locs of more than one id on L0");
        g_ui.tools.pick_kind = 0;
        g_ui.tools.pick_id = loc_id;
        g_ui.tools.edit_level = 0;
        g_ui.tools.loc_count = 0;
        CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_EDIT_SELECT_KIND) == 1, "select: the kind action runs");
        CHECK(g_ui.tools.loc_count > 0 && g_ui.tools.loc_count <= EDITOR_LOC_SELECTION_MAX,
            "select: %d loc(s) of id %d selected over the open squares", g_ui.tools.loc_count, loc_id);
        {
            int all_kind = g_ui.tools.loc_count > 0;
            for( int i = 0; i < g_ui.tools.loc_count; i++ )
                all_kind &= g_ui.tools.locs[i].loc_id == loc_id && g_ui.tools.locs[i].level == 0;
            CHECK(all_kind, "select: every selected loc is that id on L0");
        }
        {
            int const before = g_ui.tools.loc_count;
            CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_EDIT_INVERT) == 1, "select: Invert runs");
            CHECK(g_ui.tools.loc_count > 0, "select: the inverse is %d loc(s) (was %d)", g_ui.tools.loc_count, before);
            {
                int none_kind = 1;
                for( int i = 0; i < g_ui.tools.loc_count; i++ )
                    none_kind &= g_ui.tools.locs[i].loc_id != loc_id;
                CHECK(none_kind, "select: none of the inverse is the picked id");
            }
        }
        g_ui.tools.loc_count = 0;
        g_ui.tools.edit_level = -1;
        g_ui.tools.pick_id = -1;
    }
    Editor_UiFree(&g_ui);

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
    /* A crash must not eat the FAIL lines before it. */
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    if( !RSCache_ProfileByName("osrs242", &g_profile) )
    {
        printf("FAIL: no osrs242 profile\n");
        return 1;
    }
    write_catalog_fixture(root);
    if( !Editor_HostOpenMapEdEmbed(&host, root, NULL) )
    {
        printf("FAIL: the embedded ToriRSMapEd did not open %s\n", root);
        return 1;
    }
    Editor_OpenHost(&g_editor, &host, root, &g_profile);
    CHECK(g_editor.writable, "the tmp tree is writable");
    CHECK(Editor_OpenDoc(&g_editor, EDITOR_DOC_SQUARE, "m50_50"), "open m50_50");
    CHECK(Editor_OpenDoc(&g_editor, EDITOR_DOC_SQUARE, "m50_51"), "open m50_51");

    test_shapes();
    test_brushes();
    test_rect();
    test_locs();
    test_spawns();
    test_catalog_model();
    test_picker();
    test_newform();
    test_chrome();
    test_worldmap();
    test_layout();
    g_tmp_root = root;
    test_world_panel();
    test_popout();
    test_shared_tools();
    test_open_square_relay();
    test_layout_codec();
    test_saved_layout();
    test_maximise();

    test_catalog_panel();
    test_preview();
    test_publish();
    test_spawn_duplicate();
    test_spawn_edit();
    test_record_editor();
    test_editing_feedback();
    test_areas();
    test_active_level();
    test_hotkeys();
    test_statusbar();
    test_palette_colour();
    test_triggers();
    test_command_palette();
    Editor_PickerFree(&g_picker);
    Editor_Close(&g_editor);
    printf("editor_panels_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}

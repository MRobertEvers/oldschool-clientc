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
#include "editor/editor_cfg.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanel_catalog_model.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_newform.h"
#include "editor/panels/edpanel_ops.h"
#include "editor/panels/edpanel_picker.h"
#include "editor/panels/edpanel_shapes.h"
#include "editor/panels/edpanels.h"
#include "editor/editor_spawn.h"
#include "editor/map_editor_ghost.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/uitree_debug_overlay.h"

#include <rscache.h>

#include <assert.h>
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
    *out = is_overlay ? 0x8040c0u + (uint32_t)id : 0x40a040u + (uint32_t)id;
    return 1;
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
    Editor_CatalogModelCountText(model, text, sizeof(text));
    CHECK(strcmp(text, "2 of 2 loc") == 0 && cat_widget(panel, TORIRS_CHROME_W_LABEL, text) >= 0,
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
    duplicate = spawns >= 0 ? cat_widget(spawns, TORIRS_CHROME_W_BUTTON, "Duplicate  [D]") : -1;
    CHECK(list >= 0 && duplicate >= 0, "spawns: the panel has Duplicate [D]");
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
    g_picker.cell = 16;
    Editor_PickerCentre(&g_picker, 50, 50);
    x = EDITOR_PICKER_LEFT + (360 - EDITOR_PICKER_LEFT - 2) / 2;
    y = EDITOR_PICKER_TOP + (300 - EDITOR_PICKER_TOP - 2) / 2;
    CHECK(Editor_PickerCellAt(&g_picker, x, y, &map_x, &map_z) && map_x == 50 && map_z == 50,
        "the centre of the well is m50_50 (%d,%d)", map_x, map_z);
    CHECK(Editor_PickerCellAt(&g_picker, x, y - 16, &map_x, &map_z) && map_z == 51, "one cell up is north (z %d)",
        map_z);
    CHECK(Editor_PickerSquareAt(&g_picker, x, y) == index, "the square at the centre is m50_50");
    Editor_PickerZoom(&g_picker, -1, x + 5, y + 5);
    CHECK(g_picker.cell == 24, "wheel in steps the zoom (%d)", g_picker.cell);
    CHECK(Editor_PickerCellAt(&g_picker, x + 5, y + 5, &map_x, &map_z) && map_x == 50 && map_z == 50,
        "the square under the pointer stays under it");
    Editor_PickerPan(&g_picker, -24, 0);
    CHECK(Editor_PickerCellAt(&g_picker, x + 5, y + 5, &map_x, &map_z) && map_x == 51,
        "dragging left one cell shows the square east (%d)", map_x);
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
    *out_x = EDITOR_PICKER_LEFT + (int)((map_x + 0.5 - picker->origin_x) * picker->cell);
    *out_y = EDITOR_PICKER_TOP + (int)((picker->origin_z - (map_z + 0.5)) * picker->cell);
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
    g_ui.hooks.view_window = fake_view_window;
    g_ui.hooks.selection_changed = fake_selection_changed;
    Editor_UiRegisterToolsPanels(&g_ui);

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
    graph = find_widget(squares_panel, TORIRS_CHROME_W_GRAPH, NULL);
    CHECK(graph >= 0, "the picker has its GRAPH well");
    if( !picker || graph < 0 )
        return;

    /* The well shows the surface around m50_50. */
    {
        struct Editor_PickerSearch search;
        int const area = find_widget(squares_panel, TORIRS_CHROME_W_DROPDOWN, "Area");
        CHECK(find_widget(squares_panel, TORIRS_CHROME_W_TABSTRIP, NULL) < 0,
            "no tab strip: ~50 area labels in one row cannot be read");
        CHECK(area >= 0 && g_chrome.widgets[area].option_count == picker->tab_count,
            "the Area dropdown lists every area plus Unmapped (%d)", picker->tab_count);
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
        int const readout = find_widget(squares_panel, TORIRS_CHROME_W_LABEL, NULL);
        int found = 0;
        for( int i = readout; i >= 0 && i < g_chrome.widget_count; i++ )
            if( g_chrome.widgets[i].panel == squares_panel && strstr(g_chrome.widgets[i].text, "m50_50") )
                found = 1;
        CHECK(found, "hover names m50_50 in the readout");
    }
    memset(&g_window, 0, sizeof(g_window));
    push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
    push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x, y, 0);
    frame();
    CHECK(g_window.open_calls == 1 && g_window.open_x == 50 && g_window.open_z == 50 && !g_window.open_add,
        "a click opens m50_50 in place (%d calls)", g_window.open_calls);
    g_ui.shift_held = 1;
    push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y - picker->cell, 0);
    push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x, y - picker->cell, 0);
    frame();
    g_ui.shift_held = 0;
    CHECK(g_window.open_calls == 2 && g_window.open_z == 51 && g_window.open_add,
        "shift-click adds m50_51 to the open set");

    /* A drag pans, and does not open. */
    {
        double const origin = picker->origin_x;
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, x, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_DRAG, x + 40, y, 0);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x + 40, y, 0);
        frame();
        CHECK(picker->origin_x < origin && g_window.open_calls == 2, "a drag pans the map and opens nothing");
    }
    {
        int const cell = picker->cell;
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_WHEEL, x, y, 1);
        frame();
        CHECK(picker->cell < cell, "the wheel zooms out (%d -> %d)", cell, picker->cell);
        push_graph(graph, TORIRS_CHROME_EVENT_GRAPH_WHEEL, x, y, -1);
        frame();
        CHECK(picker->cell == cell, "...and back in (%d)", picker->cell);
    }

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
        ToriRSChrome_DropdownSetSelected(&g_chrome, area, 1);
        activate(area);
        CHECK(picker->active_tab == 1, "the Area dropdown picks the area (%s)", picker->tabs[picker->active_tab].title);
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
    g_ui.hooks.view_window = fake_view_window;
    g_ui.hooks.selection_changed = fake_selection_changed;
    Editor_UiRegisterToolsPanels(&g_ui);
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
    static const int sizes[][2] = { { 1280, 800 }, { 1530, 1006 }, { 1920, 1080 }, { 2560, 1440 } };
    char what[64];

    for( int scale = 1; scale <= 2; scale++ )
        for( int s = 0; s < (int)(sizeof(sizes) / sizeof(sizes[0])); s++ )
        {
            layout_boot(sizes[s][0], sizes[s][1], scale);
            snprintf(what, sizeof(what), "%dx%d at %dx", sizes[s][0], sizes[s][1], scale);
            layout_check(what);
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
            CHECK(g_chrome.panels[palette].x == r.x + r.w && g_chrome.panels[palette].y == r.y,
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
        if( g_chrome.widgets[view].options && strcmp(g_chrome.widgets[view].options[r], label) == 0 )
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

/** Every saved field of two files agrees. */
static int
layout_files_equal(
    const struct EditorLayoutFile* a,
    const struct EditorLayoutFile* b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
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
    EditorLayout_Encode(&a, &text, &size);
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
    /* TORIRS_EDITOR_LAYOUT: unset = default, empty = off, else the path. */
    {
        const char* was = getenv("TORIRS_EDITOR_LAYOUT");
        char saved[1024];
        snprintf(saved, sizeof(saved), "%s", was ? was : "");
        unsetenv("TORIRS_EDITOR_LAYOUT");
        CHECK(strcmp(EditorLayout_Path(), EDITOR_LAYOUT_DEFAULT_PATH) == 0, "unset: the default name");
        setenv("TORIRS_EDITOR_LAYOUT", "", 1);
        CHECK(EditorLayout_Path() == NULL, "empty: persistence off");
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
    g_ui.hooks.view_window = fake_view_window;
    g_ui.hooks.selection_changed = fake_selection_changed;
    Editor_UiRegisterToolsPanels(&g_ui);
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

static void
test_editing_feedback(void)
{
    fb_test_footprint();
    fb_test_spawn_ghost();
    fb_test_history_model();
    fb_test_history_panel();
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
    if( !RSCache_ProfileByName("osrs241", &g_profile) )
    {
        printf("FAIL: no osrs241 profile\n");
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
    test_layout();
    g_tmp_root = root;
    test_world_panel();
    test_layout_codec();
    test_saved_layout();

    test_catalog_panel();
    test_spawn_duplicate();
    test_spawn_edit();
    test_record_editor();
    test_editing_feedback();
    Editor_PickerFree(&g_picker);
    Editor_Close(&g_editor);
    printf("editor_panels_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}

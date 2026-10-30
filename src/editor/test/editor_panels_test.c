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
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_newform.h"
#include "editor/panels/edpanel_ops.h"
#include "editor/panels/edpanel_picker.h"
#include "editor/panels/edpanel_shapes.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/uitree_debug_overlay.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        int const tabs = find_widget(squares_panel, TORIRS_CHROME_W_TABSTRIP, NULL);
        CHECK(tabs >= 0 && g_chrome.widgets[tabs].option_count == picker->tab_count,
            "one tab per area plus Unmapped (%d)", picker->tab_count);
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

    /* The tab strip switches the area the well draws. */
    {
        int const tabs = find_widget(squares_panel, TORIRS_CHROME_W_TABSTRIP, NULL);
        ToriRSChrome_PanelSetActiveTab(&g_chrome, squares_panel, 1);
        activate(tabs);
        CHECK(picker->active_tab == 1, "the tab strip picks the area (%s)", picker->tabs[picker->active_tab].title);
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
    ToriMem_Init();
    if( !RSCache_ProfileByName("osrs241", &g_profile) )
    {
        printf("FAIL: no osrs241 profile\n");
        return 1;
    }
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
    test_picker();
    test_newform();
    test_chrome();

    Editor_PickerFree(&g_picker);
    Editor_Close(&g_editor);
    printf("editor_panels_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}

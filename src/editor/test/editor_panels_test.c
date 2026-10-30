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
#include "editor/panels/edpanel_catalog_model.h"
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

/** A panel that is part of the layout: visible, built, not a popup menu. */
static int
layout_panel(int i)
{
    const struct ToriRSChromePanel* p = &g_chrome.panels[i];
    return p->visible && p->last_rect.w > 0 && p->last_rect.h > 0 && p->style != TORIRS_CHROME_PANEL_MENU
           && !p->modal;
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

    test_catalog_panel();
    test_spawn_duplicate();
    test_editing_feedback();
    Editor_PickerFree(&g_picker);
    Editor_Close(&g_editor);
    printf("editor_panels_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}

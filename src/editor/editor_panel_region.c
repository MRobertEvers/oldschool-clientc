/*
 * The Region tools in the world: the stroke, the lasso, Tidy, the preview
 * (editor/editor_panel_region.h).
 */

#include "editor_panel_region.h"

#include "app.h"
#include "app/app_internal.h"
#include "editor_panel.h"
#include "mem/torirs_mem.h"
#include "panels/edpanel_common.h"

#include <assert.h>
#include <math.h>
#include <string.h>

/* A drag adds a point once the pointer is this far (tiles) from the last. */
#define REGION_STEP 0.25
/* A lasso press becomes a freehand drag past this far from where it went down. */
#define REGION_LASSO_DRAG 0.5
/* A polygon click this near its first corner closes it. */
#define REGION_LASSO_CLOSE 0.6
/* Two clicks this quick, this near, are a double-click (closes the polygon). */
#define REGION_DOUBLE_MS 350
#define REGION_DOUBLE_NEAR 0.35
/* The most points a smoothed loop may grow to. */
#define REGION_LOOP_MAX (EDITOR_REGION_STROKE_POINTS * 8)
/* The brush outline's segments. */
#define REGION_DISC_SEGMENTS 40

static struct Editor_RegionTile* g_preview_tiles;

static int
region_is_tool(int tool)
{
    return tool == EDITOR_TOOL_REGION || tool == EDITOR_TOOL_LASSO;
}

static double
region_snap(
    const struct EditorUi* ui,
    double v)
{
    return ui->tools.region_snap ? floor(v * 2.0 + 0.5) / 2.0 : v;
}

static double
region_dist(
    double ax,
    double az,
    double bx,
    double bz)
{
    return sqrt((ax - bx) * (ax - bx) + (az - bz) * (az - bz));
}

static double
region_radius(const struct EditorUi* ui)
{
    return ui->tools.region_radius_halves / 2.0;
}

/* ---- the point under the pointer -------------------------------------------- */

/* Barycentric weights of screen point (px, py) in triangle a b c; 1 when it is
 * inside (or on) it. */
static int
region_barycentric(
    double px,
    double py,
    const double* a,
    const double* b,
    const double* c,
    double* w)
{
    double const det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
    double const eps = -1e-6;

    if( fabs(det) < 1e-9 )
        return 0;
    w[0] = ((b[1] - c[1]) * (px - c[0]) + (c[0] - b[0]) * (py - c[1])) / det;
    w[1] = ((c[1] - a[1]) * (px - c[0]) + (a[0] - c[0]) * (py - c[1])) / det;
    w[2] = 1.0 - w[0] - w[1];
    return w[0] >= eps && w[1] >= eps && w[2] >= eps;
}

int
Editor_PanelRegionHoverPoint(
    struct Editor_Panel* panel,
    struct App* app,
    double* out_x,
    double* out_z,
    int* out_level)
{
    static const int k_corner[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    static const int k_tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
    int scene_x;
    int scene_z;
    int level;
    double mx;
    double my;

    assert(panel);
    assert(app);
    assert(out_x);
    assert(out_z);
    assert(out_level);
    if( !app->world || !app->frame_view || !Editor_PanelHoverTile(panel, app, &scene_x, &scene_z) )
        return 0;
    level = Editor_PanelEditLevel(panel, app);
    level = level < 0 ? 0 : level > 3 ? 3 : level;
    mx = app->frame_view->world_mouse_x;
    my = app->frame_view->world_mouse_y;
    *out_level = level;
    /* The pick's tile first, then its neighbours (a pick at a tile's edge
     * can name the one beside the triangle the pointer is in). */
    for( int ring = 0; ring < 9; ring++ )
    {
        int const tx = scene_x + (ring == 0 ? 0 : (ring - 1) % 3 - 1);
        int const tz = scene_z + (ring == 0 ? 0 : (ring - 1) / 3 - 1);
        double screen[4][2];
        int projected = 1;

        if( ring == 5 || tx < 0 || tz < 0 || tx >= app->world->_scene_size || tz >= app->world->_scene_size )
            continue;
        for( int k = 0; k < 4 && projected; k++ )
        {
            int const fx = (tx + k_corner[k][0]) * 128;
            int const fz = (tz + k_corner[k][1]) * 128;
            int sx;
            int sy;
            projected = app_world_project_at(app, fx, fz, app_world_height(app, fx, fz, level), &sx, &sy);
            screen[k][0] = sx;
            screen[k][1] = sy;
        }
        if( !projected )
            continue;
        for( int t = 0; t < 2; t++ )
        {
            double w[3];
            double u = 0.0;
            double v = 0.0;
            if( !region_barycentric(mx, my, screen[k_tri[t][0]], screen[k_tri[t][1]], screen[k_tri[t][2]], w) )
                continue;
            for( int k = 0; k < 3; k++ )
            {
                u += w[k] * k_corner[k_tri[t][k]][0];
                v += w[k] * k_corner[k_tri[t][k]][1];
            }
            *out_x = app->world->_base_tile_x + tx + u;
            *out_z = app->world->_base_tile_z + tz + v;
            return 1;
        }
    }
    /* The pick says this tile and no triangle holds the pointer (it is
     * edge-on): its middle. */
    *out_x = app->world->_base_tile_x + scene_x + 0.5;
    *out_z = app->world->_base_tile_z + scene_z + 0.5;
    return 1;
}

/* ---- the stroke ---------------------------------------------------------------- */

static void
region_push(
    struct Editor_RegionStroke* stroke,
    double x,
    double z)
{
    if( stroke->count >= EDITOR_REGION_STROKE_POINTS )
        return;
    stroke->xs[stroke->count] = x;
    stroke->zs[stroke->count] = z;
    stroke->count++;
}

int
Editor_PanelRegionCancel(
    struct Editor_Panel* panel,
    const char* why)
{
    struct Editor_RegionStroke* stroke;

    assert(panel);
    stroke = &panel->region;
    if( !stroke->active )
        return 0;
    stroke->active = 0;
    stroke->held = 0;
    stroke->count = 0;
    stroke->preview_key = 0;
    if( why && panel->ui )
        Editor_UiStatus(panel->ui, "%s", why);
    return 1;
}

int
Editor_PanelRegionTrack(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct Editor_RegionStroke* stroke;
    struct EditorUi* ui;
    double x;
    double z;
    int level;
    int moved = 0;

    assert(panel);
    assert(app);
    stroke = &panel->region;
    ui = panel->ui;
    if( !ui )
        return 0;
    /* Another tool armed mid-stroke drops it: it laid nothing yet. */
    if( stroke->active && (int)ui->tools.tool != stroke->tool )
        (void)Editor_PanelRegionCancel(panel, "Region dropped: another tool was armed");
    if( !Editor_PanelRegionHoverPoint(panel, app, &x, &z, &level) )
    {
        moved = stroke->hover_valid;
        stroke->hover_valid = 0;
        return moved;
    }
    x = region_snap(ui, x);
    z = region_snap(ui, z);
    if( !stroke->hover_valid || stroke->hover_x != x || stroke->hover_z != z )
        moved = 1;
    stroke->hover_valid = 1;
    stroke->hover_x = x;
    stroke->hover_z = z;
    stroke->hover_level = level;
    if( !stroke->active || !stroke->held || stroke->count == 0 )
        return moved;
    if( stroke->tool == EDITOR_TOOL_LASSO && stroke->lasso_mode == 0 )
    {
        if( region_dist(x, z, stroke->xs[0], stroke->zs[0]) >= REGION_LASSO_DRAG )
            stroke->lasso_mode = 1;
        else
            return moved;
    }
    if( stroke->tool == EDITOR_TOOL_LASSO && stroke->lasso_mode == 2 )
        return moved;
    if( region_dist(x, z, stroke->xs[stroke->count - 1], stroke->zs[stroke->count - 1]) >= REGION_STEP )
    {
        if( stroke->count == EDITOR_REGION_STROKE_POINTS )
            Editor_UiStatus(ui, "Region: %d points is as long as one stroke goes; release, then go on",
                EDITOR_REGION_STROKE_POINTS);
        region_push(stroke, x, z);
        moved = 1;
    }
    return moved;
}

int
Editor_PanelRegionPress(
    struct Editor_Panel* panel,
    struct App* app,
    int alt)
{
    struct Editor_RegionStroke* stroke;
    struct EditorUi* ui;

    assert(panel);
    assert(app);
    stroke = &panel->region;
    ui = panel->ui;
    if( !ui || !region_is_tool(ui->tools.tool) )
        return 0;
    (void)Editor_PanelRegionTrack(panel, app);
    if( !stroke->hover_valid )
        return 0;
    /* A polygon in progress: the click drops a corner on release. */
    if( stroke->active && stroke->tool == EDITOR_TOOL_LASSO && stroke->lasso_mode == 2 )
    {
        stroke->held = 1;
        return 1;
    }
    if( !ui->tools.region_underlay && ui->tools.overlay_none && !alt )
    {
        Editor_UiStatus(ui, "Region refused: pick the overlay in the Palette first (Alt-drag takes one off)");
        return 0;
    }
    if( ui->tools.region_underlay && alt )
    {
        Editor_UiStatus(ui, "Region refused: Alt takes an overlay off; an underlay is painted over instead");
        return 0;
    }
    if( alt && ui->tools.overlay_none )
    {
        Editor_UiStatus(ui, "Region refused: pick the overlay to take off in the Palette first");
        return 0;
    }
    stroke->active = 1;
    stroke->tool = ui->tools.tool;
    stroke->level = stroke->hover_level;
    stroke->subtract = alt ? 1 : 0;
    stroke->held = 1;
    stroke->lasso_mode = 0;
    stroke->count = 0;
    stroke->preview_key = 0;
    region_push(stroke, stroke->hover_x, stroke->hover_z);
    return 1;
}

/* The grid the open stroke (or, with nothing open, the brush at the
 * pointer) covers. 1 with `grid` allocated; 0 nothing to plan; -1 too wide. */
static int
region_grid(
    struct Editor_Panel* panel,
    struct Editor_RegionGrid* grid)
{
    struct Editor_RegionStroke* stroke = &panel->region;
    struct EditorUi* ui = panel->ui;
    int const margin = 1 + (ui->tools.region_underlay ? ui->tools.region_feather : 0);
    double min_x = 1e18;
    double min_z = 1e18;
    double max_x = -1e18;
    double max_z = -1e18;
    double reach;
    int brush;
    int count;
    double* xs;
    double* zs;
    int ok;

    memset(grid, 0, sizeof(*grid));
    brush = stroke->active ? stroke->tool == EDITOR_TOOL_REGION : ui->tools.tool == EDITOR_TOOL_REGION;
    if( stroke->active )
        count = stroke->count;
    else if( brush && stroke->hover_valid )
        count = 0;
    else
        return 0;
    xs = ToriMem_Alloc(ToriMem_Editor(), sizeof(*xs) * REGION_LOOP_MAX);
    zs = ToriMem_Alloc(ToriMem_Editor(), sizeof(*zs) * REGION_LOOP_MAX);
    assert(xs);
    assert(zs);
    memcpy(xs, stroke->xs, sizeof(*xs) * (size_t)count);
    memcpy(zs, stroke->zs, sizeof(*zs) * (size_t)count);
    /* The brush at rest previews one disc at the pointer; an open polygon
     * previews its loop closed through the pointer. */
    if( (!stroke->active && brush) || (stroke->active && !brush && stroke->lasso_mode == 2 && stroke->hover_valid) )
    {
        xs[count] = stroke->hover_x;
        zs[count] = stroke->hover_z;
        count++;
    }
    if( !brush )
    {
        if( count < 3 )
        {
            ToriMem_Free(ToriMem_Editor(), xs);
            ToriMem_Free(ToriMem_Editor(), zs);
            return 0;
        }
        Editor_RegionLoopSmooth(xs, zs, &count, REGION_LOOP_MAX, ui->tools.region_lasso_smooth);
    }
    reach = brush ? region_radius(ui) : 0.0;
    for( int i = 0; i < count; i++ )
    {
        min_x = xs[i] < min_x ? xs[i] : min_x;
        min_z = zs[i] < min_z ? zs[i] : min_z;
        max_x = xs[i] > max_x ? xs[i] : max_x;
        max_z = zs[i] > max_z ? zs[i] : max_z;
    }
    {
        int const x0 = (int)floor(min_x - reach) - margin;
        int const z0 = (int)floor(min_z - reach) - margin;
        int const x1 = (int)ceil(max_x + reach) + margin;
        int const z1 = (int)ceil(max_z + reach) + margin;
        ok = Editor_RegionGridInit(grid, x0 < 0 ? 0 : x0, z0 < 0 ? 0 : z0, x1 - (x0 < 0 ? 0 : x0) + 1,
            z1 - (z0 < 0 ? 0 : z0) + 1);
    }
    if( ok && brush )
    {
        if( count == 1 )
            Editor_RegionCapsule(grid, xs[0], zs[0], xs[0], zs[0], reach);
        for( int i = 0; i + 1 < count; i++ )
            Editor_RegionCapsule(grid, xs[i], zs[i], xs[i + 1], zs[i + 1], reach);
    }
    else if( ok )
        Editor_RegionPolygon(grid, xs, zs, count);
    ToriMem_Free(ToriMem_Editor(), xs);
    ToriMem_Free(ToriMem_Editor(), zs);
    return ok ? 1 : -1;
}

static void
region_params(
    const struct EditorUi* ui,
    int subtract,
    struct Editor_RegionParams* out)
{
    out->set = ui->tools.region_set >= 0 && ui->tools.region_set < EDITOR_REGION_SET_COUNT
                   ? (enum Editor_RegionSet)ui->tools.region_set
                   : EDITOR_REGION_NATURAL;
    out->op = subtract ? EDITOR_REGION_SUBTRACT : EDITOR_REGION_ADD;
    out->hardness = ui->tools.region_hardness;
    out->misfit_cap = EDITOR_REGION_POOR_FIT;
}

/* Lay the open stroke: one undo step. */
static int
region_lay(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct Editor_RegionStroke* stroke = &panel->region;
    struct EditorUi* ui = panel->ui;
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params;
    struct Editor_RegionStats stats;
    struct Editor_Cmd* commands;
    const char* what = Editor_ToolName((enum Editor_Tool)stroke->tool);
    int capacity;
    int count;
    int applied;
    int built;

    built = region_grid(panel, &grid);
    if( built <= 0 )
    {
        (void)Editor_PanelRegionCancel(panel, built < 0 ? "Region: wider than 256 tiles; lay it in parts"
                                                        : "Region: the loop needs three corners");
        return 0;
    }
    commands = edp_scratch(&capacity);
    if( ui->tools.region_underlay )
        count = Editor_RegionUnderlayCommands(&app->editor->doc, stroke->level, ui->tools.underlay_id, &grid,
            ui->tools.region_feather, ui->tools.region_seed, commands, capacity, &stats);
    else
    {
        region_params(ui, stroke->subtract, &params);
        count = Editor_RegionOverlayCommands(&app->editor->doc, stroke->level, &params, ui->tools.overlay_id, &grid,
            commands, capacity, &stats);
    }
    Editor_RegionGridFree(&grid);
    applied = edp_apply(ui, commands, count, what);
    stroke->active = 0;
    stroke->held = 0;
    stroke->count = 0;
    stroke->preview_key = 0;
    if( ui->tools.region_underlay )
        ui->tools.region_seed = ui->tools.region_seed * 1664525u + 1013904223u;
    if( stats.dropped > 0 )
        Editor_UiStatus(ui, "%s: %d tiles laid; %d more than one edit holds were left out (lay it in parts)", what,
            applied, stats.dropped);
    else if( stats.closed > 0 )
        Editor_UiStatus(ui, "%s: %d tiles laid; %d in squares not open here were left out", what, applied,
            stats.closed);
    else if( count == 0 )
        Editor_UiStatus(ui, "%s: nothing to change", what);
    else if( ui->tools.region_underlay )
        Editor_UiStatus(ui, "%s: underlay on %d tiles, the rim dithered over %d", what, applied,
            ui->tools.region_feather);
    else
        Editor_UiStatus(ui, "%s: %d tiles %s, %d edge shapes%s", what, applied,
            stroke->subtract ? "changed" : "laid", stats.edges, stats.poor ? ", some steps the shapes cannot follow" : "");
    return applied;
}

int
Editor_PanelRegionRelease(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct Editor_RegionStroke* stroke;
    struct EditorUi* ui;
    uint32_t now;

    assert(panel);
    assert(app);
    stroke = &panel->region;
    ui = panel->ui;
    if( !stroke->active || !ui || !app->editor )
        return 0;
    stroke->held = 0;
    if( stroke->tool == EDITOR_TOOL_REGION )
        return region_lay(panel, app);
    now = (uint32_t)ui->chrome->now_ms;
    if( stroke->lasso_mode == 1 )
        return region_lay(panel, app);
    if( stroke->lasso_mode == 0 )
    {
        /* A click without a drag: the polygon's first corner. */
        stroke->lasso_mode = 2;
        stroke->last_click_ms = now;
        Editor_UiStatus(ui, "Lasso: click corners; the first corner or a double-click closes it, Escape drops it");
        return 0;
    }
    if( stroke->hover_valid && stroke->count >= 3
        && (region_dist(stroke->hover_x, stroke->hover_z, stroke->xs[0], stroke->zs[0]) <= REGION_LASSO_CLOSE
            || (now - stroke->last_click_ms <= REGION_DOUBLE_MS
                && region_dist(stroke->hover_x, stroke->hover_z, stroke->xs[stroke->count - 1],
                       stroke->zs[stroke->count - 1])
                       <= REGION_DOUBLE_NEAR)) )
        return region_lay(panel, app);
    if( stroke->hover_valid )
        region_push(stroke, stroke->hover_x, stroke->hover_z);
    stroke->last_click_ms = now;
    return 0;
}

/* ---- Tidy -------------------------------------------------------------------- */

int
Editor_PanelTidyAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    struct EditorUi* ui;
    struct Editor_RegionTidy tidy;
    struct Editor_RegionStats stats;
    struct Editor_Cmd* commands;
    int capacity;
    int begun;
    int count;
    int applied;
    int set;

    assert(panel);
    assert(app);
    ui = panel->ui;
    if( !ui || !app->editor || !app->world )
        return 0;
    level = level < 0 ? 0 : level > 3 ? 3 : level;
    begun = Editor_RegionTidyBegin(&app->editor->doc, level, app->world->_base_tile_x + scene_x,
        app->world->_base_tile_z + scene_z, ui->tools.region_tidy_halves, ui->tools.region_hardness, &tidy);
    if( begun == 0 )
    {
        Editor_UiStatus(ui, "Tidy: click a tile that holds an overlay");
        return 0;
    }
    if( begun < 0 )
    {
        Editor_RegionTidyFree(&tidy);
        Editor_UiStatus(ui, "Tidy: that run is wider than 256 tiles; Tidy takes one that fits");
        return 0;
    }
    set = ui->tools.region_set >= 0 && ui->tools.region_set < EDITOR_REGION_SET_COUNT ? ui->tools.region_set : 0;
    commands = edp_scratch(&capacity);
    count = Editor_RegionTidyCommands(&app->editor->doc, &tidy, (enum Editor_RegionSet)set, ui->tools.region_hardness,
        commands, capacity, &stats);
    applied = edp_apply(ui, commands, count, Editor_ToolName(EDITOR_TOOL_TIDY));
    if( count == 0 )
        Editor_UiStatus(ui, "Tidy: the run of %d tiles is already as smooth as %d.%d tiles makes it", tidy.run,
            ui->tools.region_tidy_halves / 2, ui->tools.region_tidy_halves % 2 ? 5 : 0);
    else
        Editor_UiStatus(ui, "Tidy: %d of the run's %d tiles reshaped (%d taken off)%s", applied, tidy.run,
            stats.cleared, stats.dropped ? "; the rest did not fit one edit" : "");
    Editor_RegionTidyFree(&tidy);
    panel->region.preview_key = 0;
    return applied;
}

/* ---- the preview ------------------------------------------------------------- */

static uint32_t
region_mix(
    uint32_t h,
    const void* data,
    size_t size)
{
    const unsigned char* p = data;
    for( size_t i = 0; i < size; i++ )
        h = (h ^ p[i]) * 16777619u;
    return h;
}

int
Editor_PanelRegionPreview(
    struct Editor_Panel* panel,
    struct App* app,
    const struct Editor_RegionTile** out_tiles,
    struct Editor_RegionStats* out_stats)
{
    struct Editor_RegionStroke* stroke;
    struct EditorUi* ui;
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params;
    int const room = EDITOR_REGION_SPAN_MAX * EDITOR_REGION_SPAN_MAX;
    uint32_t key = 2166136261u;
    int tool;
    int count = 0;

    assert(panel);
    assert(app);
    assert(out_tiles);
    assert(out_stats);
    stroke = &panel->region;
    ui = panel->ui;
    *out_tiles = NULL;
    memset(out_stats, 0, sizeof(*out_stats));
    if( !ui || !app->editor || !app->world )
        return 0;
    tool = stroke->active ? stroke->tool : ui->tools.tool;
    if( !region_is_tool(tool) && tool != EDITOR_TOOL_TIDY )
        return 0;
    if( !stroke->hover_valid && !stroke->active )
        return 0;
    if( !g_preview_tiles )
    {
        g_preview_tiles = ToriMem_Alloc(ToriMem_Editor(), sizeof(*g_preview_tiles) * (size_t)room);
        assert(g_preview_tiles);
    }
    /* Everything the plan reads. The document is not in the key: a plan
     * redrawn while another window edits the same tiles is one frame stale
     * until the pointer moves. */
    key = region_mix(key, &tool, sizeof(tool));
    key = region_mix(key, &stroke->active, sizeof(stroke->active));
    key = region_mix(key, &stroke->count, sizeof(stroke->count));
    key = region_mix(key, &stroke->subtract, sizeof(stroke->subtract));
    key = region_mix(key, &stroke->lasso_mode, sizeof(stroke->lasso_mode));
    if( stroke->count > 0 )
    {
        key = region_mix(key, &stroke->xs[stroke->count - 1], sizeof(double));
        key = region_mix(key, &stroke->zs[stroke->count - 1], sizeof(double));
    }
    key = region_mix(key, &stroke->hover_x, sizeof(double));
    key = region_mix(key, &stroke->hover_z, sizeof(double));
    key = region_mix(key, &stroke->hover_level, sizeof(int));
    key = region_mix(key, &ui->tools.region_radius_halves, sizeof(int));
    key = region_mix(key, &ui->tools.region_hardness, sizeof(int));
    key = region_mix(key, &ui->tools.region_set, sizeof(int));
    key = region_mix(key, &ui->tools.region_underlay, sizeof(int));
    key = region_mix(key, &ui->tools.region_lasso_smooth, sizeof(int));
    key = region_mix(key, &ui->tools.region_tidy_halves, sizeof(int));
    key = region_mix(key, &ui->tools.region_feather, sizeof(int));
    key = region_mix(key, &ui->tools.region_seed, sizeof(uint32_t));
    key = region_mix(key, &ui->tools.overlay_id, sizeof(int));
    key = region_mix(key, &ui->tools.overlay_none, sizeof(int));
    key |= 1u;
    if( key == stroke->preview_key )
    {
        *out_tiles = g_preview_tiles;
        *out_stats = stroke->preview_stats;
        return stroke->preview_count;
    }

    if( tool == EDITOR_TOOL_TIDY )
    {
        struct Editor_RegionTidy tidy;
        int const set =
            ui->tools.region_set >= 0 && ui->tools.region_set < EDITOR_REGION_SET_COUNT ? ui->tools.region_set : 0;
        if( Editor_RegionTidyBegin(&app->editor->doc, stroke->hover_level, (int)floor(stroke->hover_x),
                (int)floor(stroke->hover_z), ui->tools.region_tidy_halves, ui->tools.region_hardness, &tidy)
            > 0 )
            count = Editor_RegionTidyPlan(&app->editor->doc, &tidy, (enum Editor_RegionSet)set,
                ui->tools.region_hardness, g_preview_tiles, room, out_stats);
        Editor_RegionTidyFree(&tidy);
    }
    else if( region_grid(panel, &grid) > 0 )
    {
        int const level = stroke->active ? stroke->level : stroke->hover_level;
        if( ui->tools.region_underlay )
        {
            static int xs[EDITOR_REGION_SPAN_MAX * EDITOR_REGION_SPAN_MAX];
            static int zs[EDITOR_REGION_SPAN_MAX * EDITOR_REGION_SPAN_MAX];
            count = Editor_RegionDither(&grid, ui->tools.region_feather, ui->tools.region_seed, xs, zs, room);
            for( int i = 0; i < count; i++ )
            {
                g_preview_tiles[i].world_x = xs[i];
                g_preview_tiles[i].world_z = zs[i];
                g_preview_tiles[i].shape = 0;
                g_preview_tiles[i].rotation = 0;
                g_preview_tiles[i].coverage = 64;
                g_preview_tiles[i].misfit = 0;
            }
            out_stats->planned = count;
        }
        else if( !ui->tools.overlay_none )
        {
            region_params(ui, stroke->subtract, &params);
            count = Editor_RegionOverlayPlan(&app->editor->doc, level, &params, ui->tools.overlay_id, &grid,
                g_preview_tiles, room, out_stats);
        }
        Editor_RegionGridFree(&grid);
    }
    stroke->preview_key = key;
    stroke->preview_count = count;
    stroke->preview_stats = *out_stats;
    *out_tiles = g_preview_tiles;
    return count;
}

int
Editor_PanelRegionOutline(
    struct Editor_Panel* panel,
    double* xs,
    double* zs,
    int max,
    int* out_closed)
{
    struct Editor_RegionStroke* stroke;
    struct EditorUi* ui;
    int tool;
    int n = 0;

    assert(panel);
    assert(xs);
    assert(zs);
    assert(out_closed);
    stroke = &panel->region;
    ui = panel->ui;
    *out_closed = 0;
    if( !ui )
        return 0;
    tool = stroke->active ? stroke->tool : ui->tools.tool;
    if( tool == EDITOR_TOOL_REGION )
    {
        /* The disc at the pointer. */
        double const r = region_radius(ui);
        if( !stroke->hover_valid || max < REGION_DISC_SEGMENTS )
            return 0;
        for( int i = 0; i < REGION_DISC_SEGMENTS; i++ )
        {
            double const a = 2.0 * 3.14159265358979323846 * i / REGION_DISC_SEGMENTS;
            xs[n] = stroke->hover_x + r * cos(a);
            zs[n] = stroke->hover_z + r * sin(a);
            n++;
        }
        *out_closed = 1;
        return n;
    }
    if( tool != EDITOR_TOOL_LASSO || !stroke->active )
        return 0;
    for( int i = 0; i < stroke->count && n < max; i++ )
    {
        xs[n] = stroke->xs[i];
        zs[n] = stroke->zs[i];
        n++;
    }
    if( stroke->lasso_mode == 2 && stroke->hover_valid && n < max )
    {
        xs[n] = stroke->hover_x;
        zs[n] = stroke->hover_z;
        n++;
    }
    *out_closed = 1;
    return n;
}

/*
 * The map editor's world overlay: what the next click will touch (the brush
 * footprint, the Place and spawn ghosts, a Place drag's run, a Path drag),
 * the squares' void and tile grids, the region and areas, the camera path,
 * the move gizmo and the selection.
 *
 * Drawn through the map editor side-app's world_overlay hook
 * (app_mapeditor_app.c), at the point of the overlay pass the side-apps' marks
 * go (app_build_entity_overlays): after the hover footprint's, so the two can
 * be on at once without one drawing over the other's meaning. The primitives
 * and the outline helpers it shares with the game's overlays stay in
 * app_overlay.c (app/app_internal.h).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "app/app_editor_campath.h"
#include "editor/editor_doc.h"
#include "editor/editor_view.h"
#include "editor/loc_editor_selection.h"
#include "editor/builder/builder_panels.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "engine/world_builder/edit/world_edit.h"

/* ---- the editor's previews: what the next click will touch ----------------- */

/* The brush footprint's colour (a cyan apart from the select green and the
 * footprint red), the spawn ghost's and the move destinations'. */
#define APP_EDITOR_BRUSH_COLOR 0xFF3CC8FFu
#define APP_EDITOR_GHOST_COLOR 0xFFFFC040u
/* The Height brush's grid: a pale cyan apart from the footprint's, so the
 * plane the ground is pulled to reads apart from the ground it marks. */
#define APP_EDITOR_HEIGHT_GRID_COLOR 0xFFC8F4FFu
/* The Place player tool's marker: the tile the play account would land on
 * (a magenta no other editor overlay uses). */
#define APP_EDITOR_PLAYER_COLOR 0xFFE060E0u
/* The wander preview (Editor_Spawns_WanderRects): an npc's wanderrange square
 * filled and outlined, its maxrange leash a fainter outline only. */
#define APP_EDITOR_WANDER_COLOR 0xFF50E070u
#define APP_EDITOR_LEASH_COLOR 0xFF2C8C46u
/* A patrol route (docs/NPC_EDITOR_DESIGN.md 3.4): waypoints and the walked
 * legs in amber, the closing leg back to waypoint 1 dimmer, a leg the
 * server would teleport (another level, no path) in the footprint red; a
 * nomove npc's pin in orange. */
#define APP_EDITOR_ROUTE_COLOR 0xFFFFB040u
#define APP_EDITOR_ROUTE_CLOSING_COLOR 0xFFA87A30u
#define APP_EDITOR_ROUTE_TELEPORT_COLOR 0xFFE05050u
#define APP_EDITOR_PIN_COLOR 0xFFFF8040u
/* The NPC editor's own route (Builder_RouteWorldRects): the waypoint a click
 * would lay and the legs to it in a pale amber, Play's walker in magenta,
 * washed: a cyan walker was the scripted spawns' marker colour and read as
 * one more spawn standing on the route. */
#define APP_EDITOR_ROUTE_PREVIEW_COLOR 0xFFFFE0A0u
#define APP_EDITOR_ROUTE_WALKER_COLOR 0xFFFF40E0u
#define APP_EDITOR_ROUTE_WALKER_FILL 120
/* The spawn preview's tile markers, by which file the row is in. */
#define APP_EDITOR_SPAWN_EDITED_COLOR 0xFF50E070u
#define APP_EDITOR_SPAWN_GENERATED_COLOR 0xFFFFE070u
#define APP_EDITOR_SPAWN_SUPPRESSED_COLOR 0xFFE05050u
#define APP_EDITOR_SPAWN_SCRIPTED_COLOR 0xFF50D8F0u
/* Brushes up to this many tiles draw every tile (radius 5); larger ones
 * draw nested washes, so a radius-16 brush stays inside the overlay budget
 * (OVERLAY_STAGE_WORLD_MAX). Up to the smaller count each tile is outlined
 * too (radius 3). */
#define APP_EDITOR_BRUSH_TILE_FILLS 121
#define APP_EDITOR_BRUSH_TILE_EDGES 49

/** One tile at its own four corner heights (it is the terrain being edited,
 *  so it bends with it), filled at `trans` (-1 none) and outlined at
 *  `line_width` (0 none). */
static void
app_overlay_editor_tile(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    uint32_t color,
    int trans,
    int line_width)
{
    static const int corner[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int px[4];
    int py[4];
    int hx[4];
    int hy[4];
    int n = 0;
    int hull;

    for( int c = 0; c < 4; c++ )
    {
        int const fx = (scene_x + corner[c][0]) * 128;
        int const fz = (scene_z + corner[c][1]) * 128;
        if( app_world_project_at(app, fx, fz, app_world_height(app, fx, fz, level), &px[n], &py[n]) )
            n++;
    }
    if( n == 0 )
        return;
    hull = ToriDraw_ConvexHull(px, py, n, hx, hy);
    if( trans >= 0 )
        app_overlay_push_polygon_filled(app, hx, hy, hull, color, trans);
    if( line_width > 0 )
        app_overlay_push_polygon(app, hx, hy, hull, color, line_width);
}

/**
 * The hull of a set of whole tiles -- the outer corners of each row's first
 * and last tile, which for a disc or a rectangle is every hull vertex -- at
 * each corner's own height, filled at `trans` (-1 none) and outlined at
 * `line_width` (0 none). `row_x0/row_x1` per row (z0 + i), -1 for an empty
 * row.
 */
static void
app_overlay_editor_rows_hull(
    struct App* app,
    int z0,
    int rows,
    const int* row_x0,
    const int* row_x1,
    int level,
    uint32_t color,
    int trans,
    int line_width)
{
    int px[TORIDRAW_CONVEX_HULL_MAX_POINTS];
    int py[TORIDRAW_CONVEX_HULL_MAX_POINTS];
    int hx[TORIDRAW_CONVEX_HULL_MAX_POINTS];
    int hy[TORIDRAW_CONVEX_HULL_MAX_POINTS];
    int n = 0;
    int hull;

    for( int i = 0; i < rows; i++ )
    {
        int const xs[2] = { row_x0[i], row_x1[i] + 1 };
        if( row_x0[i] < 0 )
            continue;
        for( int e = 0; e < 2; e++ )
            for( int dz = 0; dz < 2; dz++ )
            {
                int const fx = xs[e] * 128;
                int const fz = (z0 + i + dz) * 128;
                if( n < TORIDRAW_CONVEX_HULL_MAX_POINTS
                    && app_world_project_at(app, fx, fz, app_world_height(app, fx, fz, level), &px[n], &py[n]) )
                    n++;
            }
    }
    if( n == 0 )
        return;
    hull = ToriDraw_ConvexHull(px, py, n, hx, hy);
    if( trans >= 0 )
        app_overlay_push_polygon_filled(app, hx, hy, hull, color, trans);
    if( line_width > 0 )
        app_overlay_push_polygon(app, hx, hy, hull, color, line_width);
}

/** A size_x x size_z block of tiles from its SW corner (a spawn's footprint,
 *  a move destination). */
static void
app_overlay_editor_block(
    struct App* app,
    int scene_x,
    int scene_z,
    int size_x,
    int size_z,
    int level,
    uint32_t color,
    int trans)
{
    int x0[EDITOR_BRUSH_RADIUS_MAX * 2 + 1];
    int x1[EDITOR_BRUSH_RADIUS_MAX * 2 + 1];

    if( size_z > EDITOR_BRUSH_RADIUS_MAX * 2 + 1 )
        size_z = EDITOR_BRUSH_RADIUS_MAX * 2 + 1;
    for( int i = 0; i < size_z; i++ )
    {
        x0[i] = scene_x;
        x1[i] = scene_x + size_x - 1;
    }
    app_overlay_editor_rows_hull(
        app, scene_z, size_z, x0, x1, level, color, trans, APP_OVERLAY_SEGMENT_WIDTH);
}

/**
 * The Height brush's grid: a FLAT plane at the brush's target height (the
 * hovered ground's while none is set) over the tiles the footprint covers --
 * every tile edge as a line at that one height, the plane washed at the
 * Terrain panel's fill, and a post from the ground at the centre up (or
 * down) to it, so the gap between the ground and where the brush will take
 * it reads at a glance. `row_x0/row_x1` are the footprint's rows (z0 + i),
 * -1 for an empty row, exactly as the rim was drawn.
 */
static void
app_overlay_build_height_grid(
    struct App* app,
    struct EditorUi const* ui,
    struct MapEditorHover const* hover,
    int z0,
    int rows,
    const int* row_x0,
    const int* row_x1)
{
    int px[TORIDRAW_CONVEX_HULL_MAX_POINTS];
    int py[TORIDRAW_CONVEX_HULL_MAX_POINTS];
    int hx[TORIDRAW_CONVEX_HULL_MAX_POINTS];
    int hy[TORIDRAW_CONVEX_HULL_MAX_POINTS];
    int const cx = hover->scene_x * 128 + 64;
    int const cz = hover->scene_z * 128 + 64;
    int const ground = app_world_height(app, cx, cz, hover->level);
    int const y = ui->tools.height_valid ? ui->tools.height_target : ground;
    int x_min = -1;
    int x_max = -1;
    int n = 0;

    for( int i = 0; i < rows; i++ )
    {
        if( row_x0[i] < 0 )
            continue;
        if( x_min < 0 || row_x0[i] < x_min )
            x_min = row_x0[i];
        if( row_x1[i] > x_max )
            x_max = row_x1[i];
        /* The plane's hull: each row's outer corners, all at one height. */
        for( int e = 0; e < 2; e++ )
            for( int dz = 0; dz < 2; dz++ )
                if( n < TORIDRAW_CONVEX_HULL_MAX_POINTS
                    && app_world_project_at(app, (e ? row_x1[i] + 1 : row_x0[i]) * 128, (z0 + i + dz) * 128, y,
                        &px[n], &py[n]) )
                    n++;
    }
    if( x_min < 0 )
        return;
    if( n > 0 && ui->tools.height_grid_opacity > 0 )
    {
        int const hull = ToriDraw_ConvexHull(px, py, n, hx, hy);
        app_overlay_push_polygon_filled(
            app, hx, hy, hull, APP_EDITOR_HEIGHT_GRID_COLOR, 255 - ui->tools.height_grid_opacity * 255 / 100);
    }
    /* Lines along z = const: the boundary under row i spans both rows it
     * separates. */
    for( int i = 0; i <= rows; i++ )
    {
        int a = -1;
        int b = -1;
        int sx0;
        int sy0;
        int sx1;
        int sy1;
        for( int r = i - 1; r <= i; r++ )
        {
            if( r < 0 || r >= rows || row_x0[r] < 0 )
                continue;
            if( a < 0 || row_x0[r] < a )
                a = row_x0[r];
            if( row_x1[r] + 1 > b )
                b = row_x1[r] + 1;
        }
        if( a >= 0 && app_world_project_at(app, a * 128, (z0 + i) * 128, y, &sx0, &sy0)
            && app_world_project_at(app, b * 128, (z0 + i) * 128, y, &sx1, &sy1) )
            app_overlay_push_segment(app, sx0, sy0, sx1, sy1, APP_EDITOR_HEIGHT_GRID_COLOR, 1);
    }
    /* Lines along x = const: the column of tiles either side of it. */
    for( int x = x_min; x <= x_max + 1; x++ )
    {
        int first = -1;
        int last = -1;
        int sx0;
        int sy0;
        int sx1;
        int sy1;
        for( int i = 0; i < rows; i++ )
        {
            if( row_x0[i] < 0 || row_x0[i] > x || row_x1[i] + 1 < x )
                continue;
            if( first < 0 )
                first = i;
            last = i;
        }
        if( first >= 0 && app_world_project_at(app, x * 128, (z0 + first) * 128, y, &sx0, &sy0)
            && app_world_project_at(app, x * 128, (z0 + last + 1) * 128, y, &sx1, &sy1) )
            app_overlay_push_segment(app, sx0, sy0, sx1, sy1, APP_EDITOR_HEIGHT_GRID_COLOR, 1);
    }
    /* The post: the hovered ground to the plane, while they differ. */
    if( y != ground )
    {
        int sx0;
        int sy0;
        int sx1;
        int sy1;
        if( app_world_project_at(app, cx, cz, ground, &sx0, &sy0) && app_world_project_at(app, cx, cz, y, &sx1, &sy1) )
            app_overlay_push_segment(app, sx0, sy0, sx1, sy1, APP_EDITOR_HEIGHT_GRID_COLOR, APP_OVERLAY_SEGMENT_WIDTH);
    }
}

/**
 * The tiles the next brush application touches, around the hovered tile on
 * the edit level -- the same list the stroke walks
 * (Editor_Terrain_BrushFootprint, Editor_BrushWeight), so the preview and
 * the edit cannot disagree. The falloff is the wash's strength: the centre
 * strongest, the rim faintest; flat, flatten and the paint brushes alike.
 * A tile whose square is not open is left out: the stroke skips it too.
 * During a drag it follows the pointer like the stroke does.
 */
static void
app_overlay_build_brush_footprint(struct App* app)
{
    static struct Editor_BrushTile tiles[EDITOR_BRUSH_TILES_MAX];
    struct EditorUi* ui = app_map_editor_of(app)->editor_panel.ui;
    struct MapEditorHover const* hover = &app_map_editor_of(app)->map_ghost.hover;
    int row_x0[EDITOR_BRUSH_RADIUS_MAX * 2 + 1];
    int row_x1[EDITOR_BRUSH_RADIUS_MAX * 2 + 1];
    int count;
    int falloff;
    int radius;

    if( !ui || !hover->valid || !app->world || !App_MapEditorOpen(app) || !Editor_ToolIsBrush(ui->tools.tool) )
        return;
    radius = ui->tools.brush.radius;
    count = Editor_Terrain_BrushFootprint(&ui->tools.brush, tiles, EDITOR_BRUSH_TILES_MAX);
    falloff = Editor_Terrain_ToolUsesFalloff((int)ui->tools.tool)
              && ui->tools.brush.falloff != EDITOR_BRUSH_FALLOFF_FLAT;
    for( int i = 0; i < 2 * radius + 1; i++ )
        row_x0[i] = row_x1[i] = -1;

    for( int i = 0; i < count; i++ )
    {
        int const sx = hover->scene_x + tiles[i].dx;
        int const sz = hover->scene_z + tiles[i].dz;
        int const wx = app->world->_base_tile_x + sx;
        int const wz = app->world->_base_tile_z + sz;
        struct Editor_Square* square;
        double const weight = falloff ? tiles[i].weight : 1.0;
        int const row = tiles[i].dz + radius;

        if( sx < 0 || sz < 0 || sx >= app->world->_scene_size || sz >= app->world->_scene_size )
            continue;
        square = Editor_DocFindSquare(&app_map_editor_of(app)->session->doc, wx / EDITOR_SQUARE_X, wz / EDITOR_SQUARE_Z);
        if( !square || !square->loaded )
            continue;
        if( row_x0[row] < 0 || sx < row_x0[row] )
            row_x0[row] = sx;
        if( row_x1[row] < 0 || sx > row_x1[row] )
            row_x1[row] = sx;
        if( count <= APP_EDITOR_BRUSH_TILE_FILLS )
            /* 0 opaque .. 255 clear: the centre at 95, a weight-0.1 rim
             * near 230. */
            app_overlay_editor_tile(app, sx, sz, hover->level, APP_EDITOR_BRUSH_COLOR,
                245 - (int)(weight * 150.0), count <= APP_EDITOR_BRUSH_TILE_EDGES && tiles[i].dx == 0 && tiles[i].dz == 0
                                                ? APP_OVERLAY_SEGMENT_WIDTH
                                                : count <= APP_EDITOR_BRUSH_TILE_EDGES ? 1 : 0);
    }
    if( count > APP_EDITOR_BRUSH_TILE_FILLS )
    {
        /* Nested discs, each a light wash: where they overlap -- the centre
         * -- reads strongest. One band for an even brush. */
        static const double k_bands[4] = { 0.0, 0.25, 0.5, 0.75 };
        int const bands = falloff ? 4 : 1;
        for( int b = 0; b < bands; b++ )
        {
            int bx0[EDITOR_BRUSH_RADIUS_MAX * 2 + 1];
            int bx1[EDITOR_BRUSH_RADIUS_MAX * 2 + 1];
            for( int i = 0; i < 2 * radius + 1; i++ )
                bx0[i] = bx1[i] = -1;
            for( int i = 0; i < count; i++ )
            {
                int const sx = hover->scene_x + tiles[i].dx;
                int const row = tiles[i].dz + radius;
                if( tiles[i].weight <= k_bands[b] || row_x0[row] < 0 || sx < row_x0[row] || sx > row_x1[row] )
                    continue;
                if( bx0[row] < 0 || sx < bx0[row] )
                    bx0[row] = sx;
                if( bx1[row] < 0 || sx > bx1[row] )
                    bx1[row] = sx;
            }
            app_overlay_editor_rows_hull(app, hover->scene_z - radius, 2 * radius + 1, bx0, bx1, hover->level,
                APP_EDITOR_BRUSH_COLOR, falloff ? 215 : 160, 0);
        }
    }
    /* The rim: one outline round everything the stroke will touch. */
    app_overlay_editor_rows_hull(app, hover->scene_z - radius, 2 * radius + 1, row_x0, row_x1, hover->level,
        APP_EDITOR_BRUSH_COLOR, -1, APP_OVERLAY_SEGMENT_WIDTH);
    if( ui->tools.tool == EDITOR_TOOL_HEIGHT && ui->tools.height_grid )
        app_overlay_build_height_grid(app, ui, hover, hover->scene_z - radius, 2 * radius + 1, row_x0, row_x1);

    /* A headless shot (TORIRS_EDITOR_HOVER) says once what it drew, so a
     * photograph with no footprint in it is explained by its log. */
    {
        static int reported = -1;
        int drawn = 0;
        int sx = -1;
        int sy = -1;
        if( reported < 0 )
            reported = getenv("TORIRS_EDITOR_HOVER") ? 0 : 1;
        for( int i = 0; i < 2 * radius + 1; i++ )
            drawn += row_x0[i] >= 0 ? row_x1[i] - row_x0[i] + 1 : 0;
        if( !reported )
        {
            reported = 1;
            (void)app_world_project_at(app, hover->scene_x * 128 + 64, hover->scene_z * 128 + 64,
                app_world_height(app, hover->scene_x * 128 + 64, hover->scene_z * 128 + 64, hover->level), &sx, &sy);
            TORIRS_REPORT("editor: brush footprint %d of %d tiles at scene %d,%d level %d (screen %d,%d)\n", drawn,
                count, hover->scene_x, hover->scene_z, hover->level, sx, sy);
        }
    }
}

/**
 * The spawn ghost's footprint (the npc's size x size tiles, an obj's one),
 * and, while the Move tool aims a multi-selection, where every other
 * selected loc would land -- the primary's own ghost and footprint are the
 * loc ghost's. Nothing here is in the document yet.
 */
static void
app_overlay_editor_ground_segment(
    struct App* app,
    int fx0,
    int fz0,
    int fx1,
    int fz1,
    int level,
    uint32_t color,
    int width);

/** A patrol leg as a ground line between two tile centres, one piece per
 *  tile so it follows the terrain. */
static void
app_overlay_editor_route_leg(
    struct App* app,
    int sx0,
    int sz0,
    int sx1,
    int sz1,
    int level,
    uint32_t color,
    int width)
{
    int const dx = sx1 - sx0;
    int const dz = sz1 - sz0;
    int const steps = (dx < 0 ? -dx : dx) > (dz < 0 ? -dz : dz) ? (dx < 0 ? -dx : dx) : (dz < 0 ? -dz : dz);
    int const fx0 = sx0 * 128 + 64;
    int const fz0 = sz0 * 128 + 64;
    int const fx1 = sx1 * 128 + 64;
    int const fz1 = sz1 * 128 + 64;

    if( steps <= 0 )
        return;
    for( int k = 0; k < steps; k++ )
    {
        int const ax = fx0 + (fx1 - fx0) * k / steps;
        int const az = fz0 + (fz1 - fz0) * k / steps;
        int const bx = fx0 + (fx1 - fx0) * (k + 1) / steps;
        int const bz = fz0 + (fz1 - fz0) * (k + 1) / steps;
        app_overlay_editor_ground_segment(app, ax, az, bx, bz, level, color, width);
    }
}

/** A waypoint's number above its tile. */
static void
app_overlay_editor_route_number(
    struct App* app,
    int sx,
    int sz,
    int level,
    int index,
    uint32_t color)
{
    int const font_id = app_minimenu_font_scene_id(app);
    int const fx = sx * 128 + 64;
    int const fz = sz * 128 + 64;
    int px;
    int py;
    struct UITreeEntityOverlay text;

    if( font_id < 0 || !app_world_project_at(app, fx, fz, app_world_height(app, fx, fz, level), &px, &py) )
        return;
    memset(&text, 0, sizeof(text));
    text.kind = UITREE_ENTITY_OVERLAY_TEXT;
    text.font_id = font_id;
    text.x = px + 1;
    text.y = py - 6 + 1;
    text.color = 0xFF000000u;
    snprintf(text.text, sizeof(text.text), "%d", index);
    app_overlay_push(app, &text);
    text.x -= 1;
    text.y -= 1;
    text.color = color;
    app_overlay_push(app, &text);
}

static void
app_overlay_build_editor_ghosts(struct App* app)
{
    struct Editor_Panel const* panel = &app_map_editor_of(app)->editor_panel;
    struct MapEditorSpawnGhost const* spawn = &app_map_editor_of(app)->map_ghost.spawn;
    struct MapEditorHover const* hover = &app_map_editor_of(app)->map_ghost.hover;

    if( !app->world )
        return;
    /* Place player: the hovered tile, filled and outlined thicker, so it
     * reads as a standing spot rather than a brush. */
    if( panel->ui && hover->valid && panel->ui->tools.tool == EDITOR_TOOL_PLACE_PLAYER )
    {
        app_overlay_editor_block(app, hover->scene_x, hover->scene_z, 1, 1, hover->level, APP_EDITOR_PLAYER_COLOR,
            APP_OUTLINE_FILL_TRANS);
        app_overlay_editor_tile(app, hover->scene_x, hover->scene_z, hover->level, APP_EDITOR_PLAYER_COLOR, -1,
            APP_OVERLAY_SEGMENT_WIDTH + 1);
    }
    if( spawn->active && spawn->entity >= 0 && spawn->load_seq == app->world->load_seq )
    {
        if( spawn->spec.kind == MAP_EDITOR_SPAWN_NPC )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, spawn->entity);
            if( npc && npc->element_id >= 0 )
                app_overlay_outline_element_model(app, npc->element_id, APP_EDITOR_GHOST_COLOR);
        }
        app_overlay_editor_block(app, spawn->spec.scene_x, spawn->spec.scene_z, spawn->size, spawn->size,
            spawn->spec.level, APP_EDITOR_GHOST_COLOR, APP_OUTLINE_FILL_TRANS);
    }
    /* The spawn preview: the active level's tile markers, and the selected
     * spawns picked out on any level. */
    {
        struct MapEditorSpawnPreview const* preview = app_map_editor_spawn_preview(app);
        struct Editor_SpawnRow selected[64];
        int const markers = Editor_Spawns_PreviewMarkers(panel->ui);
        int const marker_level = Editor_PanelEditLevel(panel, app);
        int picked = Editor_Spawns_SelectionCount(panel->ui) > 0 ? Editor_Spawns_Selection(panel->ui, selected, 64) : 0;

        if( picked > 64 )
            picked = 64;
        for( int i = 0; i < preview->count; i++ )
        {
            struct MapEditorSpawnPreviewItem const* item = &preview->items[i];
            struct Editor_SpawnRow const* row = &item->want.row;
            int const x = row->abs_x - app->world->_base_tile_x;
            int const z = row->abs_z - app->world->_base_tile_z;
            int chosen = 0;

            if( x < 0 || z < 0 || x >= app->world->_scene_size || z >= app->world->_scene_size )
                continue;
            for( int s = 0; s < picked && !chosen; s++ )
                chosen = selected[s].is_obj == row->is_obj && selected[s].abs_x == row->abs_x
                         && selected[s].abs_z == row->abs_z && selected[s].level == row->level
                         && strcmp(selected[s].name, row->name) == 0;
            if( chosen )
            {
                if( item->element_id >= 0 )
                    app_overlay_outline_element_model(app, item->element_id, APP_OUTLINE_COLOR_EDITOR_SELECT);
                app_overlay_editor_block(app, x, z, item->size, item->size, row->level, APP_OUTLINE_COLOR_EDITOR_SELECT,
                    APP_OUTLINE_FILL_TRANS);
            }
            else if( markers && row->level == marker_level )
                app_overlay_editor_block(app, x, z, item->size, item->size, row->level,
                    item->want.suppressed ? APP_EDITOR_SPAWN_SUPPRESSED_COLOR
                    : item->want.edited   ? APP_EDITOR_SPAWN_EDITED_COLOR
                    : item->want.scripted ? APP_EDITOR_SPAWN_SCRIPTED_COLOR
                                          : APP_EDITOR_SPAWN_GENERATED_COLOR,
                    -1);
        }
    }
    /* The active player, in the Place player colour. */
    {
        int scene_x;
        int scene_z;
        int level;
        int element_id;
        int selected;
        if( app_map_editor_player_preview_view(app, &scene_x, &scene_z, &level, &element_id, &selected) )
        {
            if( selected )
                app_overlay_outline_element_model(app, element_id, APP_OUTLINE_COLOR_EDITOR_SELECT);
            if( selected || (Editor_Spawns_PreviewMarkers(panel->ui) && level == Editor_PanelEditLevel(panel, app)) )
                app_overlay_editor_block(app, scene_x, scene_z, 1, 1, level,
                    selected ? APP_OUTLINE_COLOR_EDITOR_SELECT : APP_EDITOR_PLAYER_COLOR, selected ? APP_OUTLINE_FILL_TRANS : -1);
        }
    }
    /* The wander preview: every selected npc spawn's wanderrange square and
     * maxrange leash, the spawn ghost's, a pending spawn move's destination.
     * Absolute tiles from the Spawns panel, drawn in scene tiles; a rectangle's
     * hull is its first and last rows' corners, so only those rows are set. */
    if( panel->ui )
    {
        static struct MapEditorWanderRect rects[MAP_EDITOR_WANDER_RECTS_MAX];
        static int row_x0[2 * MAP_EDITOR_WANDER_RANGE_MAX + 2];
        static int row_x1[2 * MAP_EDITOR_WANDER_RANGE_MAX + 2];
        int const bx = app->world->_base_tile_x;
        int const bz = app->world->_base_tile_z;
        int const ghost_npc = spawn->active && spawn->spec.kind == MAP_EDITOR_SPAWN_NPC ? spawn->spec.id : -1;
        /* Two passes over one drawing: the Spawns panel's preview, then the
         * route the NPC editor has open (over it). While the editor draws
         * its route -- each leg along the tiles the walk steps on -- the
         * selection's straight-leg copy of a route is left out: two lines
         * for one route read as two routes. */
        int const route_n = Builder_RouteWorldRects(panel->ui, rects, MAP_EDITOR_WANDER_RECTS_MAX);

        for( int pass = 0; pass < 2; pass++ )
        {
        int const n = pass == 0 ? Editor_Spawns_WanderRects(panel->ui, hover->valid, hover->scene_x + bx,
                                      hover->scene_z + bz, hover->level, ghost_npc, rects, MAP_EDITOR_WANDER_RECTS_MAX)
                                : Builder_RouteWorldRects(panel->ui, rects, MAP_EDITOR_WANDER_RECTS_MAX);

        for( int i = 0; i < n; i++ )
        {
            const struct MapEditorWanderRect* r = &rects[i];
            int const x0 = r->x0 - bx < 0 ? 0 : r->x0 - bx;
            int const z0 = r->z0 - bz < 0 ? 0 : r->z0 - bz;
            int const x1 = r->x1 - bx;
            int rows = r->z1 - bz - z0 + 1;

            if( pass == 0 && route_n > 0
                && (r->kind == MAP_EDITOR_WANDER_LEG || r->kind == MAP_EDITOR_WANDER_WAYPOINT) )
                continue;
            if( r->kind == MAP_EDITOR_WANDER_LEG )
            {
                /* A route leg: the walked line, amber; the closing leg dim;
                 * a teleport (another level, no path) red; one that is not
                 * in the document yet pale. */
                uint32_t const color = (r->flags & (MAP_EDITOR_WANDER_FLAG_TELEPORT | MAP_EDITOR_WANDER_FLAG_UNREACHABLE))
                                           ? APP_EDITOR_ROUTE_TELEPORT_COLOR
                                       : (r->flags & MAP_EDITOR_WANDER_FLAG_PREVIEW) ? APP_EDITOR_ROUTE_PREVIEW_COLOR
                                       : (r->flags & MAP_EDITOR_WANDER_FLAG_CLOSING) ? APP_EDITOR_ROUTE_CLOSING_COLOR
                                                                                     : APP_EDITOR_ROUTE_COLOR;
                app_overlay_editor_route_leg(app, r->x0 - bx, r->z0 - bz, r->x1 - bx, r->z1 - bz, r->level, color,
                    APP_OVERLAY_SEGMENT_WIDTH + 1);
                continue;
            }
            if( r->kind == MAP_EDITOR_WANDER_WAYPOINT )
            {
                int const extent = app->world->_scene_size;
                int const sx = r->x0 - bx;
                int const sz = r->z0 - bz;
                if( sx < 0 || sz < 0 || sx >= extent || sz >= extent )
                    continue;
                if( r->flags & MAP_EDITOR_WANDER_FLAG_PREVIEW )
                {
                    /* Not laid yet: an outline, no wash. */
                    app_overlay_editor_tile(app, sx, sz, r->level, APP_EDITOR_ROUTE_PREVIEW_COLOR, -1,
                        APP_OVERLAY_SEGMENT_WIDTH + 1);
                    app_overlay_editor_route_number(app, sx, sz, r->level, r->index, APP_EDITOR_ROUTE_PREVIEW_COLOR);
                }
                else if( r->flags & MAP_EDITOR_WANDER_FLAG_SELECTED )
                {
                    app_overlay_editor_tile(app, sx, sz, r->level, APP_OUTLINE_COLOR_EDITOR_SELECT, APP_OUTLINE_FILL_TRANS,
                        APP_OVERLAY_SEGMENT_WIDTH + 2);
                    app_overlay_editor_route_number(app, sx, sz, r->level, r->index, APP_OUTLINE_COLOR_EDITOR_SELECT);
                }
                else
                {
                    app_overlay_editor_tile(app, sx, sz, r->level, APP_EDITOR_ROUTE_COLOR, APP_OUTLINE_FILL_TRANS,
                        APP_OVERLAY_SEGMENT_WIDTH);
                    app_overlay_editor_route_number(app, sx, sz, r->level, r->index, APP_EDITOR_ROUTE_COLOR);
                }
                continue;
            }
            if( r->kind == MAP_EDITOR_WANDER_STALL || r->kind == MAP_EDITOR_WANDER_WALKER )
            {
                /* Where a leg stops dead (red, thick), and Play's walker. */
                int const extent = app->world->_scene_size;
                uint32_t const color = r->kind == MAP_EDITOR_WANDER_STALL
                                               || (r->flags & MAP_EDITOR_WANDER_FLAG_UNREACHABLE)
                                           ? APP_EDITOR_ROUTE_TELEPORT_COLOR
                                           : APP_EDITOR_ROUTE_WALKER_COLOR;
                int const fill = r->kind == MAP_EDITOR_WANDER_WALKER ? APP_EDITOR_ROUTE_WALKER_FILL
                                                                     : APP_OUTLINE_FILL_TRANS;
                for( int tz = r->z0; tz <= r->z1; tz++ )
                    for( int tx = r->x0; tx <= r->x1; tx++ )
                    {
                        int const sx = tx - bx;
                        int const sz = tz - bz;
                        if( sx < 0 || sz < 0 || sx >= extent || sz >= extent )
                            continue;
                        app_overlay_editor_tile(app, sx, sz, r->level, color, fill, APP_OVERLAY_SEGMENT_WIDTH + 2);
                    }
                continue;
            }
            if( x1 < x0 || rows <= 0 )
                continue;
            if( rows > (int)(sizeof(row_x0) / sizeof(row_x0[0])) )
                rows = (int)(sizeof(row_x0) / sizeof(row_x0[0]));
            for( int k = 0; k < rows; k++ )
                row_x0[k] = row_x1[k] = -1;
            row_x0[0] = row_x0[rows - 1] = x0;
            row_x1[0] = row_x1[rows - 1] = x1;
            if( r->kind == MAP_EDITOR_WANDER_PIN )
                app_overlay_editor_rows_hull(app, z0, rows, row_x0, row_x1, r->level, APP_EDITOR_PIN_COLOR,
                    APP_OUTLINE_FILL_TRANS, APP_OVERLAY_SEGMENT_WIDTH + 1);
            else if( r->kind == MAP_EDITOR_WANDER_FOOTPRINT )
                app_overlay_editor_rows_hull(app, z0, rows, row_x0, row_x1, r->level, APP_EDITOR_GHOST_COLOR,
                    APP_OUTLINE_FILL_TRANS, APP_OVERLAY_SEGMENT_WIDTH);
            else if( r->kind == MAP_EDITOR_WANDER_LEASH )
                app_overlay_editor_rows_hull(app, z0, rows, row_x0, row_x1, r->level, APP_EDITOR_LEASH_COLOR, -1,
                    APP_OVERLAY_SEGMENT_WIDTH);
            else
                app_overlay_editor_rows_hull(app, z0, rows, row_x0, row_x1, r->level, APP_EDITOR_WANDER_COLOR,
                    APP_OUTLINE_FILL_TRANS, APP_OVERLAY_SEGMENT_WIDTH);
        }
        }
    }
    /* The encounter builder's rings (Editor_Encounter_Rings, workbench 6.7):
     * each set's wander squares washed and its hunt squares outlined in the
     * set's colour, the shown set's brighter; drawn as the wander preview is. */
    if( panel->ui )
    {
        static struct Editor_EncounterRing rings[EDITOR_ENCOUNTER_RINGS_MAX];
        static int row_x0[2 * MAP_EDITOR_WANDER_RANGE_MAX + 2];
        static int row_x1[2 * MAP_EDITOR_WANDER_RANGE_MAX + 2];
        int const bx = app->world->_base_tile_x;
        int const bz = app->world->_base_tile_z;
        int const n = Editor_Encounter_Rings(panel->ui, rings, EDITOR_ENCOUNTER_RINGS_MAX);

        for( int i = 0; i < n; i++ )
        {
            const struct Editor_EncounterRing* r = &rings[i];
            uint32_t const color = 0xFF000000u | (r->current ? r->rgb : ((r->rgb >> 1) & 0x7F7F7Fu) + 0x404040u);
            int const x0 = r->x0 - bx < 0 ? 0 : r->x0 - bx;
            int const z0 = r->z0 - bz < 0 ? 0 : r->z0 - bz;
            int const x1 = r->x1 - bx;
            int rows = r->z1 - bz - z0 + 1;

            if( x1 < x0 || rows <= 0 )
                continue;
            if( rows > (int)(sizeof(row_x0) / sizeof(row_x0[0])) )
                rows = (int)(sizeof(row_x0) / sizeof(row_x0[0]));
            for( int k = 0; k < rows; k++ )
                row_x0[k] = row_x1[k] = -1;
            row_x0[0] = row_x0[rows - 1] = x0;
            row_x1[0] = row_x1[rows - 1] = x1;
            app_overlay_editor_rows_hull(app, z0, rows, row_x0, row_x1, r->level, color,
                r->hunt ? -1 : APP_OUTLINE_FILL_TRANS, APP_OVERLAY_SEGMENT_WIDTH + (r->hunt && r->current ? 1 : 0));
        }
    }
    if( panel->ui && hover->valid && panel->ui->tools.tool == EDITOR_TOOL_LOC_MOVE
        && panel->sel_kind == EDITOR_SELECTION_LOC && panel->ui->tools.loc_count > 1 )
    {
        const struct Editor_LocRef* first = &panel->ui->tools.locs[0];
        int const dx = hover->scene_x - (first->map_x * 64 + first->x - app->world->_base_tile_x);
        int const dz = hover->scene_z - (first->map_z * 64 + first->z - app->world->_base_tile_z);

        if( dx == 0 && dz == 0 )
            return;
        for( int i = 1; i < panel->ui->tools.loc_count; i++ )
        {
            const struct Editor_LocRef* ref = &panel->ui->tools.locs[i];
            int const sx = ref->map_x * 64 + ref->x - app->world->_base_tile_x;
            int const sz = ref->map_z * 64 + ref->z - app->world->_base_tile_z;
            int const idx = World_SceneryFindAt(app->world, sx, sz, ref->level, ref->shape);
            struct WorldEntity_Scenery* other =
                idx >= 0 ? World_EntityPoolGet(&app->world->entities.scenery, idx) : NULL;
            int const size_x = other && other->debug.draw_size_x > 0 ? other->debug.draw_size_x : 1;
            int const size_z = other && other->debug.draw_size_z > 0 ? other->debug.draw_size_z : 1;
            app_overlay_editor_block(
                app, sx + dx, sz + dz, size_x, size_z, ref->level, APP_EDITOR_GHOST_COLOR, APP_OUTLINE_FILL_TRANS);
        }
    }
}

/* A run tile the release would not fill (its square closed, its key another
 * loc's): drawn in the footprint red. */
#define APP_EDITOR_REFUSED_COLOR APP_OUTLINE_COLOR_FOOTPRINT
/* A run's wall line, thick enough to read as the wall it will be. */
#define APP_EDITOR_RUN_LINE_WIDTH (APP_OVERLAY_SEGMENT_WIDTH + 1)
/* A corner post's half-width, fine units (a tile is 128). */
#define APP_EDITOR_RUN_POST_HALF 20

/** A segment between two fine points on the ground of `level`. */
static void
app_overlay_editor_ground_segment(
    struct App* app,
    int fx0,
    int fz0,
    int fx1,
    int fz1,
    int level,
    uint32_t color,
    int width)
{
    int sx0;
    int sy0;
    int sx1;
    int sy1;

    if( !app_world_project_at(app, fx0, fz0, app_world_height(app, fx0, fz0, level), &sx0, &sy0) )
        return;
    if( !app_world_project_at(app, fx1, fz1, app_world_height(app, fx1, fz1, level), &sx1, &sy1) )
        return;
    app_overlay_push_segment(app, sx0, sy0, sx1, sy1, color, width);
}

/** A wall edge of scene tile (sx, sz): 0 west, 1 north, 2 east, 3 south. */
static void
app_overlay_editor_wall_edge(
    struct App* app,
    int sx,
    int sz,
    int edge,
    int level,
    uint32_t color)
{
    /* Each edge's two corners, in tile units from the SW corner. */
    static const int k_edge[4][4] = { { 0, 0, 0, 1 }, { 0, 1, 1, 1 }, { 1, 0, 1, 1 }, { 0, 0, 1, 0 } };
    int const* e = k_edge[edge & 3];

    app_overlay_editor_ground_segment(app, (sx + e[0]) * 128, (sz + e[1]) * 128, (sx + e[2]) * 128,
        (sz + e[3]) * 128, level, color, APP_EDITOR_RUN_LINE_WIDTH);
}

/**
 * The Place run in progress (Editor_Panel::run): every footprint of the plan
 * at the pose the release will place -- a wall run's straight pieces as the
 * edge they stand on, its L as both edges, its corner post as a square on
 * the corner; any other run's pose as the loc's footprint block -- and a
 * tile the release would not fill in red. The same plan the release commits
 * (edp_run_end re-plans it against the document), so they cannot disagree.
 */
/* The Path drag's plan (Editor_PanelPathPreview): the tiles the release will
 * pave, edge-shaped ones stronger so the edge the planner chose reads. At
 * most this many washes; a longer path shows its edge tiles first. */
#define APP_EDITOR_PATH_PREVIEW_TILES 240

static void
app_overlay_build_editor_path(struct App* app)
{
    static struct Editor_PathTile tiles[EDITOR_PATH_SPAN_MAX * 8];
    struct Editor_Panel const* panel = &app_map_editor_of(app)->editor_panel;
    int count;
    int drawn = 0;

    if( !app->world || !panel->path.active )
        return;
    count = Editor_PanelPathPreview(panel, tiles, (int)(sizeof(tiles) / sizeof(tiles[0])));
    for( int pass = 0; pass < 2; pass++ )
        for( int i = 0; i < count && drawn < APP_EDITOR_PATH_PREVIEW_TILES; i++ )
        {
            int const edge = tiles[i].shape != 0;
            int const sx = tiles[i].world_x - app->world->_base_tile_x;
            int const sz = tiles[i].world_z - app->world->_base_tile_z;
            /* Edges first, then the whole tiles while the budget lasts. */
            if( edge != (pass == 0) )
                continue;
            if( sx < 0 || sz < 0 || sx >= app->world->_scene_size || sz >= app->world->_scene_size )
                continue;
            app_overlay_editor_tile(app, sx, sz, panel->path.level, APP_EDITOR_BRUSH_COLOR, edge ? 150 : 200, 0);
            drawn++;
        }
}

/* The void grid: lines this many tiles apart, bent to the height field in
 * runs of APP_EDITOR_VOID_GRID_RUN tiles (at most ~290 segments a square). */
#define APP_EDITOR_VOID_GRID_TILES 8
#define APP_EDITOR_VOID_GRID_RUN 4
#define APP_EDITOR_VOID_GRID_COLOR 0xFF55606Au
#define APP_EDITOR_VOID_GRID_EDGE_COLOR 0xFFA0B4C4u

/** Whether square-local tile (x, z) of level 0 has no floor; a line on the
 *  square's far edge asks about the last tile. */
static int
app_overlay_void_tile(
    struct Editor_Square const* square,
    int x,
    int z)
{
    struct Editor_Tile const* tile = &square->tiles[Editor_TileIndex(
        x < EDITOR_SQUARE_X ? x : EDITOR_SQUARE_X - 1, z < EDITOR_SQUARE_Z ? z : EDITOR_SQUARE_Z - 1, 0)];

    return !tile->has_overlay && !tile->underlay_id;
}

/**
 * One line of the void grid along an axis of `square`: `across` tiles in on
 * the other axis, `along_x` for a line running in x. Interior lines are drawn
 * only over tiles with no floor; the square's edge always.
 */
static void
app_overlay_void_grid_line(
    struct App* app,
    struct Editor_Square const* square,
    int across,
    int along_x)
{
    int const base_x = square->map_x * EDITOR_SQUARE_X - app->world->_base_tile_x;
    int const base_z = square->map_z * EDITOR_SQUARE_Z - app->world->_base_tile_z;
    int const size = app->world->_scene_size;
    int const edge = across == 0 || across == EDITOR_SQUARE_X;

    for( int a = 0; a < EDITOR_SQUARE_X; a += APP_EDITOR_VOID_GRID_RUN )
    {
        int const x0 = along_x ? a : across;
        int const z0 = along_x ? across : a;
        int const x1 = along_x ? a + APP_EDITOR_VOID_GRID_RUN : across;
        int const z1 = along_x ? across : a + APP_EDITOR_VOID_GRID_RUN;

        if( base_x + x0 < 0 || base_z + z0 < 0 || base_x + x1 >= size || base_z + z1 >= size )
            continue;
        if( !edge && !app_overlay_void_tile(square, x0, z0) )
            continue;
        app_overlay_editor_ground_segment(app, (base_x + x0) * 128, (base_z + z0) * 128, (base_x + x1) * 128,
            (base_z + z1) * 128, 0, edge ? APP_EDITOR_VOID_GRID_EDGE_COLOR : APP_EDITOR_VOID_GRID_COLOR,
            edge ? APP_OVERLAY_SEGMENT_WIDTH : 1);
    }
}

/**
 * A scratch square, or any square still blank, gets a grid where it has no
 * floor: without one an Empty square is a black view with no way to tell
 * where it is, and the tools act on it (app_map_editor_void_hover) at tiles
 * nothing shows. A placed square's void is left alone: it is the map's.
 */
static void
app_overlay_build_editor_void_grid(struct App* app)
{
    struct Editor_Doc const* doc;

    if( !app->world || !App_MapEditorOpen(app) )
        return;
    doc = &app_map_editor_of(app)->session->doc;
    for( int i = 0; i < doc->square_count; i++ )
    {
        struct Editor_Square const* square = &doc->squares[i];

        if( !square->loaded )
            continue;
        if( !Editor_DocStorageIsScratch(Editor_DocSquareStorage(doc, square)) && !Editor_SquareIsBlank(square) )
            continue;
        for( int across = 0; across <= EDITOR_SQUARE_X; across += APP_EDITOR_VOID_GRID_TILES )
        {
            app_overlay_void_grid_line(app, square, across, 1);
            app_overlay_void_grid_line(app, square, across, 0);
        }
    }
}

/* The tile grid (EditorUi::tile_grid, edp_tile_grid_shows): the tiles within
 * this many of the pointer's, one segment a tile edge so the lines bend with
 * the ground like the Height brush's footprint does (13 x 13 tiles is 364
 * segments, well inside OVERLAY_STAGE_WORLD_MAX); a line on a multiple of 8
 * is a zone's edge and drawn brighter. A square's border in runs of
 * APP_EDITOR_VOID_GRID_RUN tiles, 64 segments a square. */
#define APP_EDITOR_TILE_GRID_RADIUS 6
#define APP_EDITOR_TILE_GRID_COLOR 0xFF6C7A86u
#define APP_EDITOR_TILE_GRID_ZONE_COLOR 0xFFA0B4C4u
#define APP_EDITOR_TILE_GRID_BORDER_COLOR 0xFFE8F0F8u
#define APP_EDITOR_TILE_GRID_NORTH_COLOR 0xFFFFD060u

/** Whether the grid is drawn on `square`: its mode says so, and the square
 *  is in this window's world. The mirror keeps every square it ever opened,
 *  and one the world no longer shows can still fall inside the scene's box,
 *  where a border would stand on nothing. */
static int
app_overlay_tile_grid_on(
    struct App* app,
    struct EditorUi const* ui,
    struct Editor_Square const* square)
{
    if( !square->loaded || !edp_tile_grid_shows(ui, square->loc_count) )
        return 0;
    if( app->world_builder && WorldBuilder_GetLayout(app->world_builder) == WORLD_BUILDER_LAYOUT_EDIT )
        return WorldEdit_SquareInWindow(app->world_builder, square->map_x, square->map_z);
    return 1;
}

/** The open square holding scene tile (sx, sz) when the grid shows on it,
 *  else NULL. */
static struct Editor_Square const*
app_overlay_tile_grid_square(
    struct App* app,
    struct EditorUi const* ui,
    int sx,
    int sz)
{
    struct Editor_Square const* square;
    int const wx = app->world->_base_tile_x + sx;
    int const wz = app->world->_base_tile_z + sz;

    if( sx < 0 || sz < 0 || sx >= app->world->_scene_size || sz >= app->world->_scene_size || wx < 0 || wz < 0 )
        return NULL;
    square = Editor_DocFindSquare(&app_map_editor_of(app)->session->doc, wx / EDITOR_SQUARE_X, wz / EDITOR_SQUARE_Z);
    if( !square || !app_overlay_tile_grid_on(app, ui, square) )
        return NULL;
    return square;
}

/** A ground segment between two scene tile corners, the far ones pulled a
 *  fine unit inside the scene: the last tile's far corner is the scene's
 *  edge, where there is no height to read. */
static void
app_overlay_tile_grid_segment(
    struct App* app,
    int cx0,
    int cz0,
    int cx1,
    int cz1,
    int level,
    uint32_t color,
    int width)
{
    int const limit = app->world->_scene_size * 128 - 1;
    int fx0 = cx0 * 128;
    int fz0 = cz0 * 128;
    int fx1 = cx1 * 128;
    int fz1 = cz1 * 128;

    if( fx0 < 0 || fz0 < 0 || fx1 < 0 || fz1 < 0 || fx0 > limit + 1 || fz0 > limit + 1 || fx1 > limit + 1
        || fz1 > limit + 1 )
        return;
    if( fx0 > limit )
        fx0 = limit;
    if( fz0 > limit )
        fz0 = limit;
    if( fx1 > limit )
        fx1 = limit;
    if( fz1 > limit )
        fz1 = limit;
    app_overlay_editor_ground_segment(app, fx0, fz0, fx1, fz1, level, color, width);
}

/**
 * The tile grid and the open squares' borders (design: N1, 2026-11-09). A
 * new flat square is one green plane to the horizon; this is what says where
 * a tile is, where the square ends and which way north is:
 *
 *   the grid     every edge of the tiles within APP_EDITOR_TILE_GRID_RADIUS
 *                of the hovered tile -- of the tile the camera looks at
 *                while the pointer is off the world -- on the edit level,
 *                each edge at its own ground height (the Height brush's
 *                grid is the same lines on one flat plane); zone edges
 *                (every 8 tiles) brighter
 *   the border   each open square's four edges, and a chevron on its north
 *                edge pointing north
 *
 * Drawn for a square while edp_tile_grid_shows says so: by default one that
 * holds no loc yet.
 */
static void
app_overlay_build_editor_tile_grid(struct App* app)
{
    struct EditorUi const* ui = app_map_editor_of(app)->editor_panel.ui;
    struct MapEditorHover const* hover = &app_map_editor_of(app)->map_ghost.hover;
    struct Editor_Doc const* doc;
    int const r = APP_EDITOR_TILE_GRID_RADIUS;
    int centre_x;
    int centre_z;
    int level;
    int centred = 0;

    if( !ui || !app->world || !App_MapEditorOpen(app) || ui->tile_grid == EDITOR_TILE_GRID_OFF )
        return;
    doc = &app_map_editor_of(app)->session->doc;

    /* The borders, and north. */
    for( int i = 0; i < doc->square_count; i++ )
    {
        struct Editor_Square const* square = &doc->squares[i];
        int base_x;
        int base_z;

        if( !app_overlay_tile_grid_on(app, ui, square) )
            continue;
        base_x = square->map_x * EDITOR_SQUARE_X - app->world->_base_tile_x;
        base_z = square->map_z * EDITOR_SQUARE_Z - app->world->_base_tile_z;
        for( int a = 0; a < EDITOR_SQUARE_X; a += APP_EDITOR_VOID_GRID_RUN )
        {
            int const b = a + APP_EDITOR_VOID_GRID_RUN;
            app_overlay_tile_grid_segment(app, base_x + a, base_z, base_x + b, base_z, 0,
                APP_EDITOR_TILE_GRID_BORDER_COLOR, APP_OVERLAY_SEGMENT_WIDTH);
            app_overlay_tile_grid_segment(app, base_x + a, base_z + EDITOR_SQUARE_Z, base_x + b,
                base_z + EDITOR_SQUARE_Z, 0, APP_EDITOR_TILE_GRID_BORDER_COLOR, APP_OVERLAY_SEGMENT_WIDTH);
            app_overlay_tile_grid_segment(app, base_x, base_z + a, base_x, base_z + b, 0,
                APP_EDITOR_TILE_GRID_BORDER_COLOR, APP_OVERLAY_SEGMENT_WIDTH);
            app_overlay_tile_grid_segment(app, base_x + EDITOR_SQUARE_X, base_z + a, base_x + EDITOR_SQUARE_X,
                base_z + b, 0, APP_EDITOR_TILE_GRID_BORDER_COLOR, APP_OVERLAY_SEGMENT_WIDTH);
        }
        /* North is +z: an arrow standing on the middle of the north edge,
         * its head on the edge. */
        {
            int const mid = base_x + EDITOR_SQUARE_X / 2;
            int const top = base_z + EDITOR_SQUARE_Z;
            app_overlay_tile_grid_segment(app, mid, top - 6, mid, top, 0, APP_EDITOR_TILE_GRID_NORTH_COLOR,
                APP_OVERLAY_SEGMENT_WIDTH + 1);
            app_overlay_tile_grid_segment(app, mid - 2, top - 2, mid, top, 0, APP_EDITOR_TILE_GRID_NORTH_COLOR,
                APP_OVERLAY_SEGMENT_WIDTH + 1);
            app_overlay_tile_grid_segment(app, mid + 2, top - 2, mid, top, 0, APP_EDITOR_TILE_GRID_NORTH_COLOR,
                APP_OVERLAY_SEGMENT_WIDTH + 1);
        }
    }

    /* The grid's centre: the hovered tile, else the tile the camera looks
     * at (the pointer is over a panel). */
    if( hover->valid )
    {
        centre_x = hover->scene_x;
        centre_z = hover->scene_z;
        level = hover->level;
        centred = 1;
    }
    else
    {
        struct EditorViewCamera pose;
        level = ui->tools.edit_level < 0 ? 0 : ui->tools.edit_level;
        centre_x = 0;
        centre_z = 0;
        if( EditorView_AppWorldCamera(app, level, &pose) )
        {
            centre_x = pose.x / 128 - app->world->_base_tile_x;
            centre_z = pose.z / 128 - app->world->_base_tile_z;
            centred = 1;
        }
    }
    if( !centred || level < 0 || level >= EDITOR_SQUARE_LEVELS )
        return;

    /* Each tile's west and south edge, and the east / north edge of a tile
     * whose neighbour that way draws none: every edge once. */
    for( int dz = -r; dz <= r; dz++ )
        for( int dx = -r; dx <= r; dx++ )
        {
            int const sx = centre_x + dx;
            int const sz = centre_z + dz;
            int const wx = app->world->_base_tile_x + sx;
            int const wz = app->world->_base_tile_z + sz;

            if( !app_overlay_tile_grid_square(app, ui, sx, sz) )
                continue;
            app_overlay_tile_grid_segment(app, sx, sz, sx, sz + 1, level,
                wx % 8 == 0 ? APP_EDITOR_TILE_GRID_ZONE_COLOR : APP_EDITOR_TILE_GRID_COLOR, 1);
            app_overlay_tile_grid_segment(app, sx, sz, sx + 1, sz, level,
                wz % 8 == 0 ? APP_EDITOR_TILE_GRID_ZONE_COLOR : APP_EDITOR_TILE_GRID_COLOR, 1);
            if( dx == r || !app_overlay_tile_grid_square(app, ui, sx + 1, sz) )
                app_overlay_tile_grid_segment(app, sx + 1, sz, sx + 1, sz + 1, level,
                    (wx + 1) % 8 == 0 ? APP_EDITOR_TILE_GRID_ZONE_COLOR : APP_EDITOR_TILE_GRID_COLOR, 1);
            if( dz == r || !app_overlay_tile_grid_square(app, ui, sx, sz + 1) )
                app_overlay_tile_grid_segment(app, sx, sz + 1, sx + 1, sz + 1, level,
                    (wz + 1) % 8 == 0 ? APP_EDITOR_TILE_GRID_ZONE_COLOR : APP_EDITOR_TILE_GRID_COLOR, 1);
        }
}

/* The Region tools' preview (Editor_PanelRegionPreview): every tile the
 * release (or Tidy's click) would write, in the shape the fitter chose --
 * the client's own overlay faces, so the preview cannot disagree with the
 * ground it makes -- edge shapes first while the budget lasts; a tile whose
 * best shape still misses the region is outlined red, a tile losing its
 * overlay is outlined in the take-off colour. Then the outline: the brush's
 * disc round the pointer, the lasso's loop with its rubber band. */
#define APP_EDITOR_REGION_TRIANGLES 1800
#define APP_EDITOR_REGION_OUTLINE 256
#define APP_EDITOR_REGION_TAKE_OFF 0xFFFF7A3Cu

/** One triangle of a tile in tile units (0..1), on `level`'s ground. */
static int
app_overlay_editor_region_triangle(
    struct App* app,
    int scene_x,
    int scene_z,
    const double tri[3][2],
    int level,
    uint32_t color,
    int trans)
{
    int px[3];
    int py[3];

    for( int k = 0; k < 3; k++ )
    {
        int const fx = scene_x * 128 + (int)(tri[k][0] * 128.0);
        int const fz = scene_z * 128 + (int)(tri[k][1] * 128.0);
        if( !app_world_project_at(app, fx, fz, app_world_height(app, fx, fz, level), &px[k], &py[k]) )
            return 0;
    }
    app_overlay_push_polygon_filled(app, px, py, 3, color, trans);
    return 1;
}

static void
app_overlay_build_editor_region(struct App* app)
{
    struct Editor_Panel* panel = &app_map_editor_of(app)->editor_panel;
    struct EditorUi* ui = panel->ui;
    const struct Editor_RegionTile* tiles = NULL;
    struct Editor_RegionStats stats;
    double xs[APP_EDITOR_REGION_OUTLINE];
    double zs[APP_EDITOR_REGION_OUTLINE];
    int const level = panel->region.active ? panel->region.level : panel->region.hover_level;
    int const take_off = panel->region.active && panel->region.subtract;
    int closed = 0;
    int count;
    int drawn = 0;
    int n;

    if( !app->world || !ui )
        return;
    if( ui->tools.tool != EDITOR_TOOL_REGION && ui->tools.tool != EDITOR_TOOL_LASSO && ui->tools.tool != EDITOR_TOOL_TIDY
        && !panel->region.active )
        return;
    count = Editor_PanelRegionPreview(panel, app, &tiles, &stats);
    for( int pass = 0; pass < 2 && tiles; pass++ )
        for( int i = 0; i < count && drawn < APP_EDITOR_REGION_TRIANGLES; i++ )
        {
            int const sx = tiles[i].world_x - app->world->_base_tile_x;
            int const sz = tiles[i].world_z - app->world->_base_tile_z;
            int const edge = tiles[i].shape != 0;
            double tri[EDITOR_REGION_SHAPE_TRIANGLES][3][2];
            int t;
            if( edge != (pass == 0) )
                continue;
            if( sx < 0 || sz < 0 || sx >= app->world->_scene_size || sz >= app->world->_scene_size )
                continue;
            if( tiles[i].shape < 0 )
            {
                app_overlay_editor_tile(app, sx, sz, level, APP_EDITOR_REGION_TAKE_OFF, 200, 1);
                drawn += 2;
                continue;
            }
            t = Editor_RegionShapeTriangles(tiles[i].shape, tiles[i].rotation, tri);
            for( int k = 0; k < t; k++ )
                drawn += app_overlay_editor_region_triangle(
                    app, sx, sz, (const double(*)[2])tri[k], level, take_off ? APP_EDITOR_REGION_TAKE_OFF : APP_EDITOR_BRUSH_COLOR,
                    edge ? 120 : 170);
            if( tiles[i].misfit > EDITOR_REGION_POOR_FIT )
                app_overlay_editor_tile(app, sx, sz, level, APP_OUTLINE_COLOR_FOOTPRINT, -1, 1);
        }

    n = Editor_PanelRegionOutline(panel, xs, zs, APP_EDITOR_REGION_OUTLINE, &closed);
    for( int i = 0; i + 1 < n + (closed ? 1 : 0); i++ )
    {
        int const j = (i + 1) % n;
        app_overlay_editor_ground_segment(app, (int)((xs[i] - app->world->_base_tile_x) * 128.0),
            (int)((zs[i] - app->world->_base_tile_z) * 128.0), (int)((xs[j] - app->world->_base_tile_x) * 128.0),
            (int)((zs[j] - app->world->_base_tile_z) * 128.0), level,
            take_off ? APP_EDITOR_REGION_TAKE_OFF : APP_EDITOR_GHOST_COLOR, APP_OVERLAY_SEGMENT_WIDTH);
    }
}

static void
app_overlay_build_editor_run(struct App* app)
{
    struct Editor_Panel const* panel = &app_map_editor_of(app)->editor_panel;
    struct Editor_LocRunPlan const* plan = &panel->run.plan;
    int size_x = 1;
    int size_z = 1;

    if( !app->world || !panel->run.active || panel->run.path.count < 2 || plan->count < 2 )
        return;
    if( !plan->wall && app->provider && (plan->pieces[0].shape == 10 || plan->pieces[0].shape == 11) )
    {
        struct ToriRS_Location* cfg = CacheProvider_LocationGet(app->provider, plan->loc_id);
        if( cfg )
        {
            size_x = cfg->size_x;
            size_z = cfg->size_z;
        }
    }
    for( int i = 0; i < plan->count; i++ )
    {
        struct Editor_LocRunPiece const* piece = &plan->pieces[i];
        int const sx = piece->world_x - app->world->_base_tile_x;
        int const sz = piece->world_z - app->world->_base_tile_z;
        int const refused = piece->state == EDITOR_LOC_RUN_CLOSED || piece->state == EDITOR_LOC_RUN_TAKEN;
        uint32_t const color = refused ? APP_EDITOR_REFUSED_COLOR : APP_EDITOR_GHOST_COLOR;

        if( piece->state == EDITOR_LOC_RUN_NONE )
            continue;
        if( sx < 0 || sz < 0 || sx >= app->world->_scene_size || sz >= app->world->_scene_size )
            continue;
        if( refused )
            app_overlay_editor_tile(app, sx, sz, plan->level, color, APP_OUTLINE_FILL_TRANS, APP_OVERLAY_SEGMENT_WIDTH);
        if( !plan->wall )
        {
            int w;
            int h;
            Editor_LocFootprint(piece->shape, piece->rotation, size_x, size_z, &w, &h);
            if( !refused )
                app_overlay_editor_block(app, sx, sz, w, h, plan->level, color, APP_OUTLINE_FILL_TRANS);
            continue;
        }
        if( piece->shape == 0 )
            app_overlay_editor_wall_edge(app, sx, sz, piece->rotation, plan->level, color);
        else if( piece->shape == 2 )
        {
            app_overlay_editor_wall_edge(app, sx, sz, piece->rotation, plan->level, color);
            app_overlay_editor_wall_edge(app, sx, sz, piece->rotation + 1, plan->level, color);
        }
        else
        {
            /* A corner post: a small square on its corner (rotation 0 NW,
             * 1 NE, 2 SE, 3 SW). */
            static const int k_corner[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
            int const cx = (sx + k_corner[piece->rotation & 3][0]) * 128;
            int const cz = (sz + k_corner[piece->rotation & 3][1]) * 128;
            int const r = APP_EDITOR_RUN_POST_HALF;
            app_overlay_editor_ground_segment(app, cx - r, cz - r, cx + r, cz - r, plan->level, color,
                APP_EDITOR_RUN_LINE_WIDTH);
            app_overlay_editor_ground_segment(app, cx + r, cz - r, cx + r, cz + r, plan->level, color,
                APP_EDITOR_RUN_LINE_WIDTH);
            app_overlay_editor_ground_segment(app, cx + r, cz + r, cx - r, cz + r, plan->level, color,
                APP_EDITOR_RUN_LINE_WIDTH);
            app_overlay_editor_ground_segment(app, cx - r, cz + r, cx - r, cz - r, plan->level, color,
                APP_EDITOR_RUN_LINE_WIDTH);
        }
    }
}

/* Named areas (Editor_Areas_Overlay): a wash this translucent over the
 * ground, the selected area's stronger and its outline thicker. */
#define APP_EDITOR_AREA_FILL_TRANS 214
#define APP_EDITOR_AREA_FILL_TRANS_SELECTED 176
/* Samples per rectangle edge at most: the outline follows the ground between
 * them without spending the overlay budget on a big area. */
#define APP_EDITOR_AREA_EDGE_SAMPLES 8
#define APP_EDITOR_AREAS_MAX 96

/** One edge of an area's rectangle from fine (fx0, fz0) to (fx1, fz1), at the
 *  ground's height on `level`, in up to APP_EDITOR_AREA_EDGE_SAMPLES
 *  segments. */
static void
app_overlay_area_edge(
    struct App* app,
    int fx0,
    int fz0,
    int fx1,
    int fz1,
    int level,
    uint32_t color,
    int line_width)
{
    int const tiles = (abs(fx1 - fx0) + abs(fz1 - fz0)) / 128;
    int const steps = tiles < 1 ? 1 : tiles < APP_EDITOR_AREA_EDGE_SAMPLES ? tiles : APP_EDITOR_AREA_EDGE_SAMPLES;
    int px = 0;
    int py = 0;
    int have = 0;

    for( int i = 0; i <= steps; i++ )
    {
        int const fx = fx0 + (fx1 - fx0) * i / steps;
        int const fz = fz0 + (fz1 - fz0) * i / steps;
        int sx;
        int sy;
        if( !app_world_project_at(app, fx, fz, app_world_height(app, fx, fz, level), &sx, &sy) )
        {
            have = 0;
            continue;
        }
        if( have )
            app_overlay_push_segment(app, px, py, sx, sy, color, line_width);
        px = sx;
        py = sy;
        have = 1;
    }
}

/** A rectangle of scene tiles [x0..x1] x [z0..z1] washed at `trans`: the
 *  hull of its four outer corners, each at its own ground height. */
static void
app_overlay_area_fill(
    struct App* app,
    int x0,
    int z0,
    int x1,
    int z1,
    int level,
    uint32_t color,
    int trans)
{
    int const fx[4] = { x0 * 128, (x1 + 1) * 128, (x1 + 1) * 128, x0 * 128 };
    int const fz[4] = { z0 * 128, z0 * 128, (z1 + 1) * 128, (z1 + 1) * 128 };
    int px[4];
    int py[4];
    int hx[4];
    int hy[4];
    int n = 0;
    int hull;

    for( int c = 0; c < 4; c++ )
        if( app_world_project_at(app, fx[c], fz[c], app_world_height(app, fx[c], fz[c], level), &px[n], &py[n]) )
            n++;
    if( n < 3 )
        return;
    hull = ToriDraw_ConvexHull(px, py, n, hx, hy);
    app_overlay_push_polygon_filled(app, hx, hy, hull, color, trans);
}

/**
 * Every named area the Areas panel shows (Editor_Areas_Overlay): each
 * rectangle washed in its colour on its level and outlined along the ground,
 * the area's name at the top-left (north-west) corner of its first
 * rectangle; the selected area brighter. Absolute tiles from the panel,
 * clipped to the loaded scene and projected through app->world_views.emit_desc
 * like the ghosts, so it follows the World panel. An area on every level
 * (-1) is drawn on the edit level (0 when it follows the pick).
 */
static void
app_overlay_build_editor_areas(struct App* app)
{
    struct Editor_Panel const* panel = &app_map_editor_of(app)->editor_panel;
    static struct Editor_AreaOverlay areas[APP_EDITOR_AREAS_MAX];
    int font_id;
    int n;
    int extent;
    int bx;
    int bz;

    if( !app->world || !panel->ui )
        return;
    n = Editor_Areas_Overlay(panel->ui, areas, APP_EDITOR_AREAS_MAX);
    if( n <= 0 )
        return;
    bx = app->world->_base_tile_x;
    bz = app->world->_base_tile_z;
    extent = app->world->_scene_size;
    font_id = app_minimenu_font_scene_id(app);
    for( int i = 0; i < n; i++ )
    {
        const struct Editor_AreaOverlay* a = &areas[i];
        int const level = a->level >= 0 ? a->level
                          : panel->ui->tools.edit_level >= 0 ? panel->ui->tools.edit_level : 0;
        int const x0 = a->x0 - bx < 0 ? 0 : a->x0 - bx;
        int const z0 = a->z0 - bz < 0 ? 0 : a->z0 - bz;
        int const x1 = a->x1 - bx >= extent ? extent - 1 : a->x1 - bx;
        int const z1 = a->z1 - bz >= extent ? extent - 1 : a->z1 - bz;
        uint32_t const color = 0xFF000000u | (a->selected ? a->rgb : ((a->rgb >> 1) & 0x7F7F7Fu) + 0x404040u);
        int const width = a->selected ? APP_OVERLAY_SEGMENT_WIDTH + 1 : APP_OVERLAY_SEGMENT_WIDTH;

        if( x1 < x0 || z1 < z0 )
            continue;
        /* The wash: the rectangle's hull at its corners' heights. */
        app_overlay_area_fill(app, x0, z0, x1, z1, level, color,
            a->selected ? APP_EDITOR_AREA_FILL_TRANS_SELECTED : APP_EDITOR_AREA_FILL_TRANS);
        /* The outline, along the ground. */
        app_overlay_area_edge(app, x0 * 128, z0 * 128, (x1 + 1) * 128, z0 * 128, level, color, width);
        app_overlay_area_edge(app, (x1 + 1) * 128, z0 * 128, (x1 + 1) * 128, (z1 + 1) * 128, level, color, width);
        app_overlay_area_edge(app, (x1 + 1) * 128, (z1 + 1) * 128, x0 * 128, (z1 + 1) * 128, level, color, width);
        app_overlay_area_edge(app, x0 * 128, (z1 + 1) * 128, x0 * 128, z0 * 128, level, color, width);
        /* The name at the north-west corner, when that corner is loaded. */
        if( a->first && a->label[0] && font_id >= 0 && a->x0 - bx >= 0 && a->z1 - bz < extent )
        {
            int const fx = x0 * 128;
            int const fz = (z1 + 1) * 128;
            int sx;
            int sy;
            if( app_world_project_at(app, fx, fz, app_world_height(app, fx, fz, level), &sx, &sy) )
            {
                int text_w = app_measure_text_cb(app, font_id, a->label);
                struct UITreeEntityOverlay text = {
                    .kind = UITREE_ENTITY_OVERLAY_TEXT,
                    .font_id = font_id,
                };
                if( text_w <= 0 )
                    text_w = (int)strlen(a->label) * 7;
                /* The overlay centres text on x: put its left edge just
                 * inside the corner, above it. */
                text.x = sx + 4 + text_w / 2 + 1;
                text.y = sy - 4 + 1;
                text.color = 0xFF000000u;
                snprintf(text.text, sizeof(text.text), "%s", a->label);
                app_overlay_push(app, &text);
                text.x -= 1;
                text.y -= 1;
                text.color = 0xFF000000u | a->rgb;
                app_overlay_push(app, &text);
            }
        }
    }
}

/**
 * The map editor SELECT tool's latch (editor_panel.sel_kind) -- distinct from
 * the hover footprint above: hover follows the mouse every frame, this stays
 * on what was latched even after the cursor moves off it, matching what
 * panel_refresh (editor_panel.c) is reading for the readout at the same time.
 */
void
app_overlay_build_editor_selection(struct App* app);

/* ---- the move gizmo and the multi-piece ghost (workbench 6.3) ---------------
 *
 * What app_map_editor.c computed this frame, drawn: the multi-piece ghost's
 * footprints (an armed stamp at the hover, a gizmo drag's destinations),
 * yellow, orange where the key is taken; and the gizmo on the primary
 * selected loc -- the N/S arrows green, W/E red, the ring and the centre
 * yellow, the handle under the pointer (or being dragged) white and wider.
 * The points are App_MapEditorGizmoScreen's, the same the click tests, so
 * what is drawn is what a press hits.
 */
#define APP_GIZMO_COLOR_NS 0xFF40D040u
#define APP_GIZMO_COLOR_WE 0xFFE04040u
#define APP_GIZMO_COLOR_RING 0xFFFFD040u
#define APP_GIZMO_COLOR_HOT 0xFFFFFFFFu
#define APP_GIZMO_COLOR_TAKEN 0xFFFF8020u

static void
app_overlay_build_editor_gizmo(struct App* app)
{
    struct MapEditorPieceGhost const* ghost = &app_map_editor_of(app)->map_ghost.pieces;
    struct LocEditorGizmoScreen screen;
    int hot;

    for( int i = 0; i < ghost->count; i++ )
    {
        struct MapEditorPieceGhostPiece const* p = &ghost->pieces[i];
        app_overlay_editor_block(app, p->scene_x, p->scene_z, p->size_x, p->size_z, p->level,
            p->taken ? APP_GIZMO_COLOR_TAKEN : APP_OUTLINE_COLOR_EDITOR_SELECT, APP_OUTLINE_FILL_TRANS);
    }
    if( !App_MapEditorGizmoScreen(app, &screen, &hot) )
        return;
    for( int a = 0; a < 4; a++ )
    {
        int const handle = LOC_EDITOR_GIZMO_ARROW_N + a;
        uint32_t const color = hot == handle ? APP_GIZMO_COLOR_HOT : a < 2 ? APP_GIZMO_COLOR_NS : APP_GIZMO_COLOR_WE;
        int const width = hot == handle ? APP_OVERLAY_SEGMENT_WIDTH + 2 : APP_OVERLAY_SEGMENT_WIDTH + 1;
        int const dx = screen.arrow_x1[a] - screen.arrow_x0[a];
        int const dy = screen.arrow_y1[a] - screen.arrow_y0[a];
        if( !screen.arrow_ok[a] )
            continue;
        app_overlay_push_segment(
            app, screen.arrow_x0[a], screen.arrow_y0[a], screen.arrow_x1[a], screen.arrow_y1[a], color, width);
        /* The head: two strokes back from the tip, a third of the shaft,
         * turned out either side. */
        app_overlay_push_segment(app, screen.arrow_x1[a], screen.arrow_y1[a],
            screen.arrow_x1[a] - dx / 3 - dy / 4, screen.arrow_y1[a] - dy / 3 + dx / 4, color, width);
        app_overlay_push_segment(app, screen.arrow_x1[a], screen.arrow_y1[a],
            screen.arrow_x1[a] - dx / 3 + dy / 4, screen.arrow_y1[a] - dy / 3 - dx / 4, color, width);
    }
    app_overlay_push_polygon(app, screen.ring_x, screen.ring_y, screen.ring_count,
        hot == LOC_EDITOR_GIZMO_RING ? APP_GIZMO_COLOR_HOT : APP_GIZMO_COLOR_RING,
        hot == LOC_EDITOR_GIZMO_RING ? APP_OVERLAY_SEGMENT_WIDTH + 1 : APP_OVERLAY_SEGMENT_WIDTH);
    {
        int const r = hot == LOC_EDITOR_GIZMO_CENTRE ? 6 : 4;
        int const cx[4] = { screen.centre_x - r, screen.centre_x + r, screen.centre_x + r, screen.centre_x - r };
        int const cy[4] = { screen.centre_y - r, screen.centre_y - r, screen.centre_y + r, screen.centre_y + r };
        uint32_t const color = hot == LOC_EDITOR_GIZMO_CENTRE ? APP_GIZMO_COLOR_HOT : APP_GIZMO_COLOR_RING;
        app_overlay_push_polygon_filled(app, cx, cy, 4, color, 64);
        app_overlay_push_polygon(app, cx, cy, 4, color, APP_OVERLAY_SEGMENT_WIDTH);
    }
}

void
app_overlay_build_editor_selection(struct App* app)
{
    struct Editor_Panel const* panel;

    /* Whatever tool is active: the selection is tool-independent (pick with
     * Select, then rotate/move/reshape with the others), so its highlight
     * must not vanish the moment the tool that will act on it is chosen --
     * the Move tool with an invisible subject is aiming blind. The old
     * SELECT-only gate predates select-then-operate. */
    if( !app_map_editor_panel_shown(app) )
        return;
    panel = &app_map_editor_of(app)->editor_panel;

    /* Under everything else: where a floorless square's tiles are. */
    app_overlay_build_editor_void_grid(app);
    /* What the next click touches: the brush's tiles, the spawn ghost, the
     * move destinations. */
    app_overlay_build_brush_footprint(app);
    app_overlay_build_editor_ghosts(app);
    /* A Place drag's run: every footprint it will place, before release. */
    app_overlay_build_editor_run(app);
    /* A Path drag: the tiles the release will pave. */
    app_overlay_build_editor_path(app);
    app_overlay_build_editor_region(app);
    /* The named areas (Areas panel). */
    app_overlay_build_editor_areas(app);
    /* The shown camera path (Camera path panel; app_editor_campath.c). */
    app_overlay_build_editor_campath(app);
    /* The move gizmo and the multi-piece ghost (workbench 6.3). */
    app_overlay_build_editor_gizmo(app);
    /* The tile grid and the squares' borders. After the previews, not under
     * them: the overlay stage drops what is pushed past its budget, and it
     * is the grid's thin lines that may go, never a tile the next click
     * would touch. */
    app_overlay_build_editor_tile_grid(app);

    /* The Place-loc ghost's FOOTPRINT: the translucent model says what it
     * looks like, this says which tiles it will own -- the question that
     * decides whether it fits beside the wall. Same silhouette the hover
     * footprint draws, fed by the ghost's own scenery entity, so a 3x2 loc
     * shows 3x2 here without anything re-deriving sizes. Before the
     * selection early-out: a ghost exists with or without a selection. */
    if( app_map_editor_of(app)->map_ghost.active && app->world )
    {
        int const idx = World_SceneryFindAt(
            app->world,
            app_map_editor_of(app)->map_ghost.spec.scene_x,
            app_map_editor_of(app)->map_ghost.spec.scene_z,
            app_map_editor_of(app)->map_ghost.spec.level,
            app_map_editor_of(app)->map_ghost.spec.shape);
        if( idx >= 0 )
        {
            struct WorldEntity_Scenery* ghost =
                World_EntityPoolGet(&app->world->entities.scenery, idx);
            if( ghost )
                app_overlay_outline_scenery(app, ghost);
        }
    }

    /* The rest of the loc multi-selection (the first is the primary latch
     * below) and the rectangle tool's tiles. Both are held by document
     * address (absolute tiles), turned into scene tiles here. */
    if( app->world && panel->ui )
    {
        for( int i = 1; i < panel->ui->tools.loc_count; i++ )
        {
            const struct Editor_LocRef* ref = &panel->ui->tools.locs[i];
            int const sx = ref->map_x * 64 + ref->x - app->world->_base_tile_x;
            int const sz = ref->map_z * 64 + ref->z - app->world->_base_tile_z;
            int const idx = World_SceneryFindAt(app->world, sx, sz, ref->level, ref->shape);
            struct WorldEntity_Scenery* other =
                idx >= 0 ? World_EntityPoolGet(&app->world->entities.scenery, idx) : NULL;
            if( other )
            {
                app_overlay_outline_element_model(
                    app, other->element_id, APP_OUTLINE_COLOR_EDITOR_SELECT);
                app_overlay_outline_scenery(app, other);
            }
        }
        if( panel->ui->tools.rect.valid )
        {
            /* The same project-and-hull as the terrain latch below, over the
             * rectangle's four outer corners (absolute -> scene). */
            const struct Editor_TileRect* r = &panel->ui->tools.rect;
            int const x0 = (r->x0 < r->x1 ? r->x0 : r->x1) - app->world->_base_tile_x;
            int const x1 = (r->x0 < r->x1 ? r->x1 : r->x0) - app->world->_base_tile_x + 1;
            int const z0 = (r->z0 < r->z1 ? r->z0 : r->z1) - app->world->_base_tile_z;
            int const z1 = (r->z0 < r->z1 ? r->z1 : r->z0) - app->world->_base_tile_z + 1;
            int const cx[4] = { x0, x1, x1, x0 };
            int const cz[4] = { z0, z0, z1, z1 };
            int px[4];
            int py[4];
            int hx[4];
            int hy[4];
            int n = 0;
            int const plane_y = app_world_height(app, x0 * 128, z0 * 128, r->level);
            for( int c = 0; c < 4; c++ )
                if( app_world_project_at(app, cx[c] * 128, cz[c] * 128, plane_y, &px[n], &py[n]) )
                    n++;
            if( n > 0 )
            {
                int const hull = ToriDraw_ConvexHull(px, py, n, hx, hy);
                app_overlay_push_polygon(
                    app, hx, hy, hull, APP_OUTLINE_COLOR_EDITOR_SELECT, APP_OVERLAY_SEGMENT_WIDTH);
            }
        }
    }

    if( panel->sel_kind == EDITOR_SELECTION_NONE || !app->world )
        return;

    if( panel->sel_kind == EDITOR_SELECTION_LOC )
    {
        struct WorldEntity_Scenery* scenery =
            World_SceneryGetByElementId(app->world, panel->sel_element_id);

        /* A reshape/swap deleted the element this selection pointed at (a loc
         * change is delete + add, and the add is async). Re-find the NEW
         * element by tile and shape, and heal the selection -- transiently
         * absent while the add is still in flight, which draws no highlight
         * for a frame or two rather than the wrong one forever. */
        if( !scenery )
        {
            int const idx = World_SceneryFindAt(
                app->world,
                panel->sel_scene_x,
                panel->sel_scene_z,
                panel->sel_level,
                panel->sel_shape);
            if( idx >= 0 )
                scenery = World_EntityPoolGet(&app->world->entities.scenery, idx);
            if( scenery )
                app_map_editor_of(app)->editor_panel.sel_element_id = scenery->element_id;
        }
        if( !scenery )
            return;
        app_overlay_outline_element_model(
            app, panel->sel_element_id, APP_OUTLINE_COLOR_EDITOR_SELECT);
        app_overlay_outline_scenery(app, scenery);
        return;
    }

    /* Terrain: the same single-plane, hulled-corners outline
     * app_overlay_outline_scenery draws for a loc's footprint, for the one
     * latched tile -- there is no WorldEntity_Scenery here to read a size
     * from, so the four corners are built directly instead of looped per
     * tile. */
    {
        int const base_x = panel->sel_scene_x;
        int const base_z = panel->sel_scene_z;
        static int const corner[4][2] = {
            { 0, 0 },
            { 1, 0 },
            { 1, 1 },
            { 0, 1 }
        };
        int px[4];
        int py[4];
        int hull_x[4];
        int hull_y[4];
        int count = 0;
        int hull_size;
        int const plane_y = app_world_height(app, base_x * 128, base_z * 128, panel->sel_level);

        for( int c = 0; c < 4; c++ )
        {
            int screen_x;
            int screen_y;

            if( !app_world_project_at(
                    app,
                    (base_x + corner[c][0]) * 128,
                    (base_z + corner[c][1]) * 128,
                    plane_y,
                    &screen_x,
                    &screen_y) )
                continue;
            px[count] = screen_x;
            py[count] = screen_y;
            count++;
        }
        if( count == 0 )
            return;

        hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
        app_overlay_push_polygon_filled(
            app,
            hull_x,
            hull_y,
            hull_size,
            APP_OUTLINE_COLOR_EDITOR_SELECT,
            APP_OUTLINE_FILL_TRANS);
        app_overlay_push_polygon(
            app,
            hull_x,
            hull_y,
            hull_size,
            APP_OUTLINE_COLOR_EDITOR_SELECT,
            APP_OVERLAY_SEGMENT_WIDTH);
    }
}

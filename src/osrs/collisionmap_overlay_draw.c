#include "collisionmap_overlay_draw.h"

#include "graphics/dash.h"
#include "osrs/collision_map.h"
#include "osrs/game.h"
#include "osrs/heightmap.h"
#include "osrs/painters_i.h"
#include "osrs/world.h"
#include "tori_rs_render.h"

#include <math.h>

#define COLL_OVERLAY_COLOR_RGB 0xFF0000
#define COLL_OVERLAY_DASH_LEN  6

/** Project a scene-relative point and apply the viewport offset.
 * Returns true on success; out_x/out_y are set to framebuffer coordinates. */
static bool
coll_overlay_project(
    struct GGame* game,
    int scene_x,
    int scene_y,
    int scene_z,
    int* out_x,
    int* out_y)
{
    int sx = 0, sy = 0;
    if( !dash3d_project_point(
            game->sys_dash, scene_x, scene_y, scene_z, game->view_port, game->camera, &sx, &sy) )
        return false;
    *out_x = sx + game->viewport_offset_x;
    *out_y = sy + game->viewport_offset_y;
    return true;
}

/** Emit a single DRAW_LINE command. */
static void
coll_overlay_emit_line(
    struct ToriRSRenderCommandBuffer* cmds,
    int x0,
    int y0,
    int x1,
    int y1)
{
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(cmds);
    c->kind = TORIRS_GFX_DRAW_LINE;
    c->_line_draw.x0 = x0;
    c->_line_draw.y0 = y0;
    c->_line_draw.x1 = x1;
    c->_line_draw.y1 = y1;
    c->_line_draw.color_rgb = COLL_OVERLAY_COLOR_RGB;
}

/** Draw a short dash from (mx, my) toward the tile center (cx, cy). */
static void
coll_overlay_emit_corner_dash(
    struct ToriRSRenderCommandBuffer* cmds,
    int mx,
    int my,
    int cx,
    int cy)
{
    int dx = cx - mx;
    int dy = cy - my;
    double len = sqrt((double)(dx * dx + dy * dy));
    if( len < 0.5 )
        return;
    int ex = mx + (int)(dx * COLL_OVERLAY_DASH_LEN / len);
    int ey = my + (int)(dy * COLL_OVERLAY_DASH_LEN / len);
    coll_overlay_emit_line(cmds, mx, my, ex, ey);
}

void
collisionmap_overlay_draw_emit(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmds)
{
    if( !game || !cmds )
        return;
    if( !game->debug_collisionmap_overlay )
        return;
    if( !game->world || !game->sys_dash || !game->view_port || !game->camera )
        return;

    struct World* world = game->world;
    struct CollisionMap* cm = world->collision_map;
    struct Heightmap* hm = world->heightmap;
    if( !cm || !hm )
        return;

    int base_level = (world->painter) ? (int)world->painter->min_level : 0;

    for( int x = 0; x < cm->size_x; x++ )
    {
        for( int z = 0; z < cm->size_z; z++ )
        {
            bool link_below = world->painter && x >= 0 && z >= 0 &&
                              x < world->_scene_size && z < world->_scene_size &&
                              painter_column_has_link_below(world->painter, x, z);
            int hm_level = entity_cache_level_for_column(base_level, link_below);
            if( hm_level >= hm->levels )
                hm_level = hm->levels - 1;
            struct HeightmapHeights heights = { 0 };
            heightmap_get_heights(hm, x, z, hm_level, &heights);

            /* Scene-relative coordinates for the four corners (tile size = 128 units). */
            int sx0 = x * 128 - game->camera_world_x;
            int sx1 = (x + 1) * 128 - game->camera_world_x;
            int sz0 = z * 128 - game->camera_world_z;
            int sz1 = (z + 1) * 128 - game->camera_world_z;

            int sy_sw = heights.sw_height - game->camera_world_y;
            int sy_se = heights.se_height - game->camera_world_y;
            int sy_ne = heights.ne_height - game->camera_world_y;
            int sy_nw = heights.nw_height - game->camera_world_y;

            int px_sw, py_sw, px_se, py_se, px_ne, py_ne, px_nw, py_nw;
            bool ok_sw = coll_overlay_project(game, sx0, sy_sw, sz0, &px_sw, &py_sw);
            bool ok_se = coll_overlay_project(game, sx1, sy_se, sz0, &px_se, &py_se);
            bool ok_ne = coll_overlay_project(game, sx1, sy_ne, sz1, &px_ne, &py_ne);
            bool ok_nw = coll_overlay_project(game, sx0, sy_nw, sz1, &px_nw, &py_nw);

            /* Tile center in screen space (used for corner dash direction). */
            int cx = (px_sw + px_se + px_ne + px_nw) / 4;
            int cy = (py_sw + py_se + py_ne + py_nw) / 4;

            /* Draw tile edge lines where a cardinal step is blocked.
             * BFS checks the destination tile, so "can step south from (x,z)" tests
             * (x, z-1). Draw the south edge when that step is blocked. */
            if( ok_sw && ok_se && !collision_map_can_step_south(cm, x, z) )
                coll_overlay_emit_line(cmds, px_sw, py_sw, px_se, py_se);

            if( ok_se && ok_ne && !collision_map_can_step_east(cm, x, z) )
                coll_overlay_emit_line(cmds, px_se, py_se, px_ne, py_ne);

            if( ok_ne && ok_nw && !collision_map_can_step_north(cm, x, z) )
                coll_overlay_emit_line(cmds, px_ne, py_ne, px_nw, py_nw);

            if( ok_nw && ok_sw && !collision_map_can_step_west(cm, x, z) )
                coll_overlay_emit_line(cmds, px_nw, py_nw, px_sw, py_sw);

            /* Draw short dashes from corners toward tile center for blocked diagonals. */
            if( ok_sw && !collision_map_can_step_diagonal_south_west(cm, x, z) )
                coll_overlay_emit_corner_dash(cmds, px_sw, py_sw, cx, cy);

            if( ok_se && !collision_map_can_step_diagonal_south_east(cm, x, z) )
                coll_overlay_emit_corner_dash(cmds, px_se, py_se, cx, cy);

            if( ok_ne && !collision_map_can_step_diagonal_north_east(cm, x, z) )
                coll_overlay_emit_corner_dash(cmds, px_ne, py_ne, cx, cy);

            if( ok_nw && !collision_map_can_step_diagonal_north_west(cm, x, z) )
                coll_overlay_emit_corner_dash(cmds, px_nw, py_nw, cx, cy);
        }
    }
}

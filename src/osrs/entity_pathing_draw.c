#include "entity_pathing_draw.h"

#include "graphics/dash.h"
#include "osrs/game.h"
#include "osrs/heightmap.h"
#include "osrs/painters.h"
#include "osrs/world.h"
#include "tori_rs_render.h"

#define PATHING_LINE_COLOR_PLAYER 0xFF0000
#define PATHING_LINE_COLOR_NPC 0xFF0000
#define PATHING_LINE_COLOR_SENT 0x0000FF

/** Base ground slice for path Y; bumped to base+1 per-endpoint when the column has LinkBelow,
 * matching world_cycle_push_dynamic_scenery / entity cache level (Client-TS getAvH groundh[level+1]). */
#define PATHING_DRAW_BASE_LEVEL 0

static bool
entity_pathing_heightmap_sample_ok(
    struct Heightmap* hm,
    int draw_x,
    int draw_z,
    int slevel)
{
    if( !hm || slevel < 0 || slevel >= hm->levels )
        return false;
    int tile_x = draw_x >> 7;
    int tile_z = draw_z >> 7;
    return tile_x >= 0 && tile_x < hm->size_x && tile_z >= 0 && tile_z < hm->size_z;
}

/** Resolve the heightmap slice for a path endpoint, bumping by 1 on LinkBelow bridge columns
 * so the overlay rides the bridge surface instead of the under-bridge ground. */
static int
entity_pathing_endpoint_slevel(
    struct World* world,
    int draw_x,
    int draw_z,
    int base_level)
{
    if( !world->painter )
        return base_level;
    int sx = draw_x >> 7;
    int sz = draw_z >> 7;
    if( sx < 0 || sz < 0 || sx >= world->_scene_size || sz >= world->_scene_size )
        return base_level;
    return entity_cache_level_for_column(
        base_level, painter_column_has_link_below(world->painter, sx, sz));
}

static void
entity_pathing_draw_emit_segments(
    struct GGame* game,
    struct World* world,
    struct ToriRSRenderCommandBuffer* cmds,
    struct EntityPathing* pathing,
    int size_x,
    int size_z,
    int color_rgb)
{
    int n = (int)pathing->route_length;
    struct Heightmap* hm = world->heightmap;
    if( n < 2 || !hm )
        return;

    for( int i = n - 1; i >= 1; i-- )
    {
        int tx0 = (int)pathing->route_x[i];
        int tz0 = (int)pathing->route_z[i];
        int tx1 = (int)pathing->route_x[i - 1];
        int tz1 = (int)pathing->route_z[i - 1];

        int wx0 = tx0 * 128 + size_x * 64;
        int wz0 = tz0 * 128 + size_z * 64;
        int wx1 = tx1 * 128 + size_x * 64;
        int wz1 = tz1 * 128 + size_z * 64;

        int slevel0 = entity_pathing_endpoint_slevel(world, wx0, wz0, PATHING_DRAW_BASE_LEVEL);
        int slevel1 = entity_pathing_endpoint_slevel(world, wx1, wz1, PATHING_DRAW_BASE_LEVEL);

        if( !entity_pathing_heightmap_sample_ok(hm, wx0, wz0, slevel0) ||
            !entity_pathing_heightmap_sample_ok(hm, wx1, wz1, slevel1) )
            continue;

        int wy0 = heightmap_get_interpolated(hm, wx0, wz0, slevel0);
        int wy1 = heightmap_get_interpolated(hm, wx1, wz1, slevel1);

        int sx0 = wx0 - game->camera_world_x;
        int sy0 = wy0 - game->camera_world_y;
        int sz0 = wz0 - game->camera_world_z;
        int sx1 = wx1 - game->camera_world_x;
        int sy1 = wy1 - game->camera_world_y;
        int sz1 = wz1 - game->camera_world_z;

        int scr0x = 0;
        int scr0y = 0;
        int scr1x = 0;
        int scr1y = 0;
        if( !dash3d_project_point(
                game->sys_dash, sx0, sy0, sz0, game->view_port, game->camera, &scr0x, &scr0y) )
            continue;
        if( !dash3d_project_point(
                game->sys_dash, sx1, sy1, sz1, game->view_port, game->camera, &scr1x, &scr1y) )
            continue;

        int ax0 = scr0x + game->viewport_offset_x;
        int ay0 = scr0y + game->viewport_offset_y;
        int ax1 = scr1x + game->viewport_offset_x;
        int ay1 = scr1y + game->viewport_offset_y;

        struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(cmds);
        c->kind = TORIRS_GFX_DRAW_LINE;
        c->_line_draw.x0 = ax0;
        c->_line_draw.y0 = ay0;
        c->_line_draw.x1 = ax1;
        c->_line_draw.y1 = ay1;
        c->_line_draw.color_rgb = color_rgb;
    }
}

static void
entity_pathing_draw_sent_path(
    struct GGame* game,
    struct World* world,
    struct ToriRSRenderCommandBuffer* cmds)
{
    int n = game->sent_path.length;
    struct Heightmap* hm = world->heightmap;
    if( n < 2 || !hm )
        return;

    for( int i = 0; i < n - 1; i++ )
    {
        int tx0 = (int)game->sent_path.tile_x[i];
        int tz0 = (int)game->sent_path.tile_z[i];
        int tx1 = (int)game->sent_path.tile_x[i + 1];
        int tz1 = (int)game->sent_path.tile_z[i + 1];

        int wx0 = tx0 * 128 + 64;
        int wz0 = tz0 * 128 + 64;
        int wx1 = tx1 * 128 + 64;
        int wz1 = tz1 * 128 + 64;

        int slevel0 = entity_pathing_endpoint_slevel(world, wx0, wz0, PATHING_DRAW_BASE_LEVEL);
        int slevel1 = entity_pathing_endpoint_slevel(world, wx1, wz1, PATHING_DRAW_BASE_LEVEL);

        if( !entity_pathing_heightmap_sample_ok(hm, wx0, wz0, slevel0) ||
            !entity_pathing_heightmap_sample_ok(hm, wx1, wz1, slevel1) )
            continue;

        int wy0 = heightmap_get_interpolated(hm, wx0, wz0, slevel0);
        int wy1 = heightmap_get_interpolated(hm, wx1, wz1, slevel1);

        int sx0 = wx0 - game->camera_world_x;
        int sy0 = wy0 - game->camera_world_y;
        int sz0 = wz0 - game->camera_world_z;
        int sx1 = wx1 - game->camera_world_x;
        int sy1 = wy1 - game->camera_world_y;
        int sz1 = wz1 - game->camera_world_z;

        int scr0x = 0, scr0y = 0, scr1x = 0, scr1y = 0;
        if( !dash3d_project_point(
                game->sys_dash, sx0, sy0, sz0, game->view_port, game->camera, &scr0x, &scr0y) )
            continue;
        if( !dash3d_project_point(
                game->sys_dash, sx1, sy1, sz1, game->view_port, game->camera, &scr1x, &scr1y) )
            continue;

        struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(cmds);
        c->kind = TORIRS_GFX_DRAW_LINE;
        c->_line_draw.x0 = scr0x + game->viewport_offset_x;
        c->_line_draw.y0 = scr0y + game->viewport_offset_y;
        c->_line_draw.x1 = scr1x + game->viewport_offset_x;
        c->_line_draw.y1 = scr1y + game->viewport_offset_y;
        c->_line_draw.color_rgb = PATHING_LINE_COLOR_SENT;
    }
}

void
entity_pathing_draw_emit(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmds)
{
    if( !game || !cmds || !game->world || !game->sys_dash || !game->view_port || !game->camera )
        return;

    struct World* world = game->world;

    entity_pathing_draw_sent_path(game, world, cmds);

    struct PlayerEntity* local = world_player(world, ACTIVE_PLAYER_SLOT);
    if( local && local->alive && local->pathing.route_length >= 2 )
        entity_pathing_draw_emit_segments(
            game, world, cmds, &local->pathing, 1, 1, PATHING_LINE_COLOR_PLAYER);

    for( int pi = 0; pi < world->active_player_count; pi++ )
    {
        int pid = world->active_players[pi];
        if( pid < 0 || pid == ACTIVE_PLAYER_SLOT )
            continue;
        struct PlayerEntity* player = world_player(world, pid);
        if( !player->alive )
            continue;
        if( player->pathing.route_length < 2 )
            continue;
        entity_pathing_draw_emit_segments(
            game, world, cmds, &player->pathing, 1, 1, PATHING_LINE_COLOR_PLAYER);
    }

    for( int ni = 0; ni < world->active_npc_count; ni++ )
    {
        int nid = world->active_npcs[ni];
        if( nid < 0 )
            continue;
        struct NPCEntity* npc = world_npc(world, nid);
        if( !npc->alive )
            continue;
        if( npc->pathing.route_length < 2 )
            continue;
        entity_pathing_draw_emit_segments(
            game,
            world,
            cmds,
            &npc->pathing,
            (int)npc->size.x,
            (int)npc->size.z,
            PATHING_LINE_COLOR_NPC);
    }
}

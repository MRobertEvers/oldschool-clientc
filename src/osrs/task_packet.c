#include "task_packet.h"

#include "osrs/_light_model_default.u.c"
#include "rscache/bitbuffer.h"
#include "rscache/rsbuf.h"
#include "scenebuilder.h"

#include <stdlib.h>
#include <string.h>

struct TaskPacket
{
    struct GGame* game;
    struct GIOQueue* io;
};

struct TaskPacket*
task_packet_new(
    struct GGame* game,
    struct GIOQueue* io)
{
    struct TaskPacket* task = malloc(sizeof(struct TaskPacket));
    memset(task, 0, sizeof(struct TaskPacket));
    task->game = game;
    task->io = io;
    return task;
}

void
task_packet_free(struct TaskPacket* task)
{
    free(task);
}

#include "dashlib.h"
#include "rscache/tables/model.h"

static int npc_sizes[8096] = { 0 };

static struct DashModel*
npc_model(
    struct TaskPacket* task,
    int npc_id,
    int npc_idx)
{
    struct CacheDatConfigNpc* npc = buildcachedat_get_npc(task->game->buildcachedat, npc_id);
    assert(npc && "Npc must be found");

    assert(npc->size > 0 && "Npc size must be greater than 0");
    npc_sizes[npc_idx] = npc->size;

    struct DashModel* dash_model = NULL;
    struct CacheModel* model = buildcachedat_get_npc_model(task->game->buildcachedat, npc_id);
    if( model )
    {
        model = model_new_copy(model);
        dash_model = dashmodel_new_from_cache_model(model);
        _light_model_default(dash_model, 0, 0);
        return dash_model;
    }

    struct CacheModel* models[20];
    int model_count = 0;
    for( int i = 0; i < npc->models_count; i++ )
    {
        struct CacheModel* got_model =
            buildcachedat_get_model(task->game->buildcachedat, npc->models[i]);
        assert(got_model && "Model must be found");

        models[model_count] = got_model;
        model_count++;
    }

    struct CacheModel* merged = model_new_merge(models, model_count);

    assert(merged->vertices_x && "Merged model must have vertices");
    assert(merged->vertices_y && "Merged model must have vertices");
    assert(merged->vertices_z && "Merged model must have vertices");

    buildcachedat_add_npc_model(task->game->buildcachedat, npc_id, merged);

    struct CacheModel* copy = model_new_copy(merged);

    dash_model = dashmodel_new_from_cache_model(copy);
    _light_model_default(dash_model, 0, 0);

    return dash_model;
}

static struct DashModel* models[8096] = { 0 };

static int sxs[8096] = { 0 };
static int szs[8096] = { 0 };

static void
add_npc_info(
    struct TaskPacket* task,
    struct RevPacket_LC245_2_Item* item)
{
    struct BitBuffer buf;
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, item->packet._npc_info.data, item->packet._npc_info.length);
    bitbuffer_init_from_rsbuf(&buf, &rsbuf);
    bits(&buf);

    int count = gbits(&buf, 8);
    for( int i = 0; i < count; i++ )
    {
        //
    }

    for( int i = 0; i < count; i++ )
    {
        // int index = npc_ids[i];

        int info = gbits(&buf, 1);
        if( info == 0 )
        {
            //
        }
        else
        {
            int op = gbits(&buf, 2);
            switch( op )
            {
            case 0:
                //
                break;
            case 1:
                // walkdir
                gbits(&buf, 3);
                // has extended info
                gbits(&buf, 1);
                break;
            case 2:
                // walkdir
                gbits(&buf, 3);
                // rundir
                gbits(&buf, 3);
                // has extended info
                gbits(&buf, 1);
                break;
            case 3:
                //
                break;
            }
            //
        }
    }

    int npcs[2048] = { 0 };
    int npc_count = 0;

    while( ((buf.byte_position * 8) + buf.bit_offset + 21) < item->packet._npc_info.length * 8 )
    {
        int npc_idx = gbits(&buf, 13);
        if( npc_idx == 8191 )
        {
            break;
        }
        npcs[npc_count] = npc_idx;

        int npc_id = gbits(&buf, 11);

        models[npc_idx] = npc_model(task, npc_id, npc_idx);
        assert(npc_sizes[npc_idx] > 0 && "Npc size must be greater than 0");

        int dx = gbits(&buf, 5);
        int dy = gbits(&buf, 5);

        sxs[npc_idx] += dx;
        szs[npc_idx] += dy;

        int has_extended_info = gbits(&buf, 1);
        npc_count++;
    }

    for( int i = 0; i < npc_count; i++ )
    {
        int idx = npcs[i];
        int x = sxs[idx];
        int z = szs[idx];
        int size_x = npc_sizes[idx];
        int size_z = npc_sizes[idx];
        assert(size_x > 0 && "Npc size must be greater than 0");
        assert(size_z > 0 && "Npc size must be greater than 0");
        scenebuilder_push_element(
            task->game->scenebuilder, task->game->scene, x, z, 0, size_x, size_z, models[idx]);
    }
}

enum GameTaskStatus
task_packet_step(struct TaskPacket* task)
{
    struct RevPacket_LC245_2_Item* item = task->game->packets_lc245_2_inflight;
    assert(item);

    task->game->packets_lc245_2_inflight = item->next_nullable;

    switch( item->packet.packet_type )
    {
    case PKTIN_LC245_2_REBUILD_NORMAL:
    {
        int zone_padding = 104 / (2 * 8);
        int map_sw_x = (item->packet._map_rebuild.zonex - zone_padding) / 8;
        int map_sw_z = (item->packet._map_rebuild.zonez - zone_padding) / 8;
        int map_ne_x = (item->packet._map_rebuild.zonex + zone_padding) / 8;
        int map_ne_z = (item->packet._map_rebuild.zonez + zone_padding) / 8;

        int width = map_ne_x - map_sw_x + 1;
        int height = map_ne_z - map_sw_z + 1;
        int levels = MAP_TERRAIN_LEVELS;

        task->game->sys_painter = painter_new(width * 64, height * 64, levels);
        task->game->sys_painter_buffer = painter_buffer_new();
        task->game->sys_minimap = minimap_new(map_sw_x, map_sw_z, map_ne_x, map_ne_z, levels);
        task->game->scenebuilder =
            scenebuilder_new_painter(task->game->sys_painter, task->game->sys_minimap);

        task->game->scene = scenebuilder_load_from_buildcachedat(
            task->game->scenebuilder,
            map_sw_x,
            map_sw_z,
            map_ne_x,
            map_ne_z,
            task->game->buildcachedat);
    }
    break;
    case PKTIN_LC245_2_NPC_INFO:
    {
        add_npc_info(task, item);
    }
    break;
    default:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}
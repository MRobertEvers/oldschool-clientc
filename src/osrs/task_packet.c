#include "task_packet.h"

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
    default:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}
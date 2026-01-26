#include "task_proto_ld.h"

#include "osrs/game.h"
#include "osrs/gametask_status.h"
#include "osrs/gio.h"
#include "packetin.h"

static void
lc245_2_process(struct GGame* game)
{
    struct RevPacket_LC245_2_Item* item = game->packet_queue_lc245_2_nullable;
    while( item )
    {
        switch( item->packet.packet_type )
        {
        case PKTIN_LC245_2_REBUILD_NORMAL:
            // Load all the chunks asked for in rebuild normal.
            {
                int zonex = item->packet._map_rebuild.zonex;
                int zonez = item->packet._map_rebuild.zonez;
                int zone_padding = SCENE_TILE_WIDTH / (2 * 8);
                int map_sw_x = (zonex - zone_padding) / 8;
                int map_sw_z = (zonez - zone_padding) / 8;
                int map_ne_x = (zonex + zone_padding) / 8;
                int map_ne_z = (zonez + zone_padding) / 8;

                query_engine_push(Q_MAPS_SCENERY, map_sw_x, map_sw_z, map_ne_x, map_ne_z);
                query_engine_push(V_LOCIDS, SET_0);
                query_engine_push(Q_MODELS, FROM_SET, SET_0, SET_1);
                query_engine_push(Q_TEXTURES, FROM_SET, SET_1, SET_3);
                query_engine_push(Q_FLOTYPE, FROM_SET, SET_0, SET_2);
                query_engine_push(Q_TEXTURES, FROM_SET, SET_2, SET_3);

                query_engine_push(MAPS_TERRAIN);
                query_engine_push(ARG, map_sw_x);
                query_engine_push(ARG, map_sw_z);
                query_engine_push(ARG, map_ne_x);
                query_engine_push(ARG, map_ne_z);
                query_engine_push(QUERY_SET, FLOTYPE, SET_2);

                query_engine_load_from(MODELS, SET_0, SET_1);
                query_engine_load_from(TEXTURES, SET_0);
                query_engine_execute();
            }
            break;
        }
    }
}

struct TaskProtoLD
{
    struct GGame* game;
    struct GIOQueue* io;
};

struct TaskProtoLD*
task_proto_ld_new(
    struct GGame* game,
    struct GIOQueue* io)
{
    struct TaskProtoLD* task = malloc(sizeof(struct TaskProtoLD));
    task->game = game;
    task->io = io;
    return task;
}

void
task_proto_ld_free(struct TaskProtoLD* task)
{
    free(task);
}

enum GameTaskStatus
task_proto_ld_step(struct TaskProtoLD* task)
{
    return GAMETASK_STATUS_COMPLETED;
}

#endif
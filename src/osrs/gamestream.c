#include "gamestream.h"

#include "osrs/gametask.h"
#include "packetin.h"
#include "rscache/rsbuf.h"

#define SCENE_TILE_WIDTH 104

static void
pktin_rebuild_region(
    struct GGame* game,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);

    int zonex;
    int zonez;

    zonex = g2(&buffer);
    zonez = g2(&buffer);
    assert(buffer.position == data_size);

    // Rebuild.

    int zone_padding = SCENE_TILE_WIDTH / (2 * 8);
    int map_sw_x = (zonex - zone_padding) / 8;
    int map_sw_z = (zonez - zone_padding) / 8;
    int map_ne_x = (zonex + zone_padding) / 8;
    int map_ne_z = (zonez + zone_padding) / 8;

    gametask_new_init_scene(game, map_sw_x, map_sw_z, map_ne_x, map_ne_z);
}

void
gamestream_process(
    struct GGame* game,
    int packet_type,
    uint8_t* data,
    int data_size)
{
    assert(data_size == packetin_size(packet_type));
    switch( packet_type )
    {
    case PKTIN_REBUILD_REGION:
        pktin_rebuild_region(game, data, data_size);
        break;
    default:
        break;
    }
}
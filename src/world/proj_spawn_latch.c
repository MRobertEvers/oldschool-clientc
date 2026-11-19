#include "world/proj_spawn_latch.h"

#include <assert.h>

void
ProjSpawnLatch_Reset(struct ProjSpawnLatch* latch)
{
    assert(latch);
    latch->src_x = -1;
    latch->src_z = -1;
    latch->src_level = 0;
}

enum ProjSpawnLatchAction
ProjSpawnLatch_Press(
    struct ProjSpawnLatch* latch,
    int tile_x,
    int tile_z,
    int level,
    int* out_src_x,
    int* out_src_z,
    int* out_src_level)
{
    assert(latch);
    assert(out_src_x);
    assert(out_src_z);
    assert(out_src_level);

    if( latch->src_x < 0 )
    {
        latch->src_x = tile_x;
        latch->src_z = tile_z;
        latch->src_level = level;
        return PROJ_SPAWN_LATCH_MARKED;
    }
    if( latch->src_x == tile_x && latch->src_z == tile_z )
    {
        latch->src_x = -1;
        latch->src_z = -1;
        return PROJ_SPAWN_LATCH_CLEARED;
    }
    *out_src_x = latch->src_x;
    *out_src_z = latch->src_z;
    *out_src_level = latch->src_level;
    latch->src_x = -1;
    latch->src_z = -1;
    return PROJ_SPAWN_LATCH_LAUNCH;
}

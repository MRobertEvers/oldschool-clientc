#ifndef GAMECACHE_TERRAIN_H
#define GAMECACHE_TERRAIN_H

#include <stdint.h>

#define GAMECACHE_TERRAIN_X 64
#define GAMECACHE_TERRAIN_Z 64
#define GAMECACHE_TERRAIN_LEVELS 4

/** Trimmed per-tile type: drops attr_opcode (only set during decode, not read by world build). */
struct GameCacheFloorTile
{
    uint16_t overlay_id;
    uint8_t underlay_id;
    int16_t height;
    /* attr_opcode dropped */
    uint8_t settings;
    uint8_t shape;
    uint8_t rotation;
};

/** Standalone GameCache terrain chunk: chunk coords are the DashMap key, so not stored here. */
struct GameCacheTerrainChunk
{
    struct GameCacheFloorTile tiles[GAMECACHE_TERRAIN_X * GAMECACHE_TERRAIN_Z * GAMECACHE_TERRAIN_LEVELS];
};

#endif

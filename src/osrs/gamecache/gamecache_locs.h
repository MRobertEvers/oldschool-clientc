#ifndef GAMECACHE_LOCS_H
#define GAMECACHE_LOCS_H

/** Standalone GameCache map-loc type -- all 6 consumer-read fields kept. */
struct GameCacheMapLoc
{
    int loc_id;
    int shape_select;
    int orientation;
    int chunk_pos_x;
    int chunk_pos_z;
    int chunk_pos_level;
};

/** Standalone GameCache map-locs chunk: chunk coords are the DashMap key, so not stored here. */
struct GameCacheMapLocs
{
    struct GameCacheMapLoc* locs;
    int locs_count;
};

#endif

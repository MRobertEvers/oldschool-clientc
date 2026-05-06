#ifndef GAMECACHE_SPOTANIM_H
#define GAMECACHE_SPOTANIM_H

/** Standalone GameCache spotanim type -- all fields kept (small struct, rendering imminent). */
struct GameCacheSpotAnim
{
    int model;
    int seq_id;
    int anim_gfx_height;
    int resizeh;
    int resizev;
    int orientation;
    int ambient;
    int contrast;
};

#endif

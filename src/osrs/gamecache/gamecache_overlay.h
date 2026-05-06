#ifndef GAMECACHE_OVERLAY_H
#define GAMECACHE_OVERLAY_H

/** Standalone GameCache overlay type -- only fields read by world terrain build. */
struct GameCacheOverlay
{
    int rgb_color;
    int texture;
    int secondary_rgb_color;
};

#endif

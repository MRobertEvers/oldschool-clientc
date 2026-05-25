#ifndef GAMECACHE_FLOTYPE_H
#define GAMECACHE_FLOTYPE_H

#include <stdbool.h>

struct CacheConfigOverlay;

struct GameCache_Flotype
{
    int rgb_color;
    int texture;
    int secondary_rgb_color;
    bool hide_underlay;
    bool flotype_overlay;
    char* flotype_name;
    int _id;
};

struct GameCache_Flotype*
gamecache_flotype_new_from_cache_config_overlay(struct CacheConfigOverlay* overlay);

void
gamecache_flotype_free(struct GameCache_Flotype* flotype);

#endif

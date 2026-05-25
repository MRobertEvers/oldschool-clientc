#include "gamecache_flotype.h"

#include "osrs/rscache/tables/config_floortype.h"

#include <stdlib.h>
#include <string.h>

struct GameCache_Flotype*
gamecache_flotype_new_from_cache_config_overlay(struct CacheConfigOverlay* overlay)
{
    if( !overlay )
        return NULL;

    struct GameCache_Flotype* flotype = malloc(sizeof(struct GameCache_Flotype));
    if( !flotype )
        return NULL;

    memset(flotype, 0, sizeof(struct GameCache_Flotype));
    flotype->rgb_color = overlay->rgb_color;
    flotype->texture = overlay->texture;
    flotype->secondary_rgb_color = overlay->secondary_rgb_color;
    flotype->hide_underlay = overlay->hide_underlay;
    flotype->flotype_overlay = overlay->flotype_overlay;
    flotype->flotype_name = overlay->flotype_name;
    flotype->_id = overlay->_id;

    overlay->flotype_name = NULL;

    free(overlay);
    return flotype;
}

void
gamecache_flotype_free(struct GameCache_Flotype* flotype)
{
    if( !flotype )
        return;

    free(flotype->flotype_name);
    free(flotype);
}

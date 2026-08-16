#include "engine/torirs_flotype_from_rscache.h"

#include <assert.h>
#include <stdlib.h>

struct ToriRS_Flotype*
ToriRS_FlotypeFromRSCacheOverlay(
    int flo_id,
    const struct RSCache_Dat2ConfigOverlay* src)
{
    struct ToriRS_Flotype* flotype;

    assert(src);

    flotype = calloc(1, sizeof(*flotype));
    assert(flotype);

    flotype->id = flo_id;
    flotype->rgb_color = src->rgb_color;
    flotype->texture = src->texture;
    flotype->secondary_rgb_color = src->secondary_rgb_color;
    flotype->hide_underlay = src->hide_underlay;

    return flotype;
}

struct ToriRS_Flotype*
ToriRS_FlotypeFromRSCacheUnderlay(
    int underlay_id,
    const struct RSCache_Dat2ConfigUnderlay* src)
{
    struct ToriRS_Flotype* flotype;

    assert(src);

    flotype = calloc(1, sizeof(*flotype));
    assert(flotype);

    flotype->id = underlay_id;
    flotype->rgb_color = src->rgb_color;
    flotype->texture = -1;
    flotype->secondary_rgb_color = -1;
    flotype->hide_underlay = false;

    return flotype;
}

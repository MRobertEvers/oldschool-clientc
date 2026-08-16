#ifndef TORIRS_FLOTYPE_FROM_RSCACHE_H
#define TORIRS_FLOTYPE_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

struct ToriRS_Flotype*
ToriRS_FlotypeFromRSCacheOverlay(
    int flo_id,
    const struct RSCache_Dat2ConfigOverlay* src);

struct ToriRS_Flotype*
ToriRS_FlotypeFromRSCacheUnderlay(
    int underlay_id,
    const struct RSCache_Dat2ConfigUnderlay* src);

#endif

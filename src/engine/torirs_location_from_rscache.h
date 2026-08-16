#ifndef TORIRS_LOCATION_FROM_RSCACHE_H
#define TORIRS_LOCATION_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

struct ToriRS_Location*
ToriRS_LocationFromRSCacheDat2(
    int loc_id,
    const struct RSCache_Dat2ConfigLoc* src);

#endif

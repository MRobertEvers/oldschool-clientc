#ifndef TORIRS_SPOTANIMTYPE_FROM_RSCACHE_H
#define TORIRS_SPOTANIMTYPE_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

struct ToriRS_Spotanimtype*
ToriRS_SpotanimtypeFromRSCacheDat1(
    int spotanim_id,
    const struct RSCache_Dat1ConfigSpotanim* src);

struct ToriRS_Spotanimtype*
ToriRS_SpotanimtypeFromRSCacheDat2(
    int spotanim_id,
    const struct RSCache_Dat2ConfigSpotanim* src);

#endif

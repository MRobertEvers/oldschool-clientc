#ifndef TORIRS_OBJTYPE_FROM_RSCACHE_H
#define TORIRS_OBJTYPE_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

struct ToriRS_Objtype*
ToriRS_ObjtypeFromRSCacheDat1(
    int obj_id,
    const struct RSCache_Dat1ConfigObj* src);

struct ToriRS_Objtype*
ToriRS_ObjtypeFromRSCacheDat2(
    int obj_id,
    const struct RSCache_Dat2ConfigObj* src);

#endif

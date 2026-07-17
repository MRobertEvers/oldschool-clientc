#ifndef TORIRS_COMPONENT_FROM_RSCACHE_H
#define TORIRS_COMPONENT_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

struct ToriRS_Component*
ToriRS_ComponentFromRSCacheDat2(const struct RSCache_Dat2Component* src);

struct ToriRS_ComponentPack*
ToriRS_ComponentPackFromRSCacheDat2(const struct RSCache_Dat2ComponentPack* src);

#endif

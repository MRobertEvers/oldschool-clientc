#ifndef TORIRS_PARAMTYPE_FROM_RSCACHE_H
#define TORIRS_PARAMTYPE_FROM_RSCACHE_H

struct RSCache_Dat2ConfigParam;
struct ToriRS_ParamType;

struct ToriRS_ParamType*
ToriRS_ParamTypeFromRSCacheDat2(
    int id,
    struct RSCache_Dat2ConfigParam* entry);

#endif

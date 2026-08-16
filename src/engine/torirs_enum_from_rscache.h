#ifndef TORIRS_ENUM_FROM_RSCACHE_H
#define TORIRS_ENUM_FROM_RSCACHE_H

struct RSCache_Dat2ConfigEnum;
struct ToriRS_Enum;

/** Steals owned pointers from entry; entry must not be freed afterward. */
struct ToriRS_Enum*
ToriRS_EnumFromRSCacheDat2(
    int enum_id,
    struct RSCache_Dat2ConfigEnum* entry);

#endif

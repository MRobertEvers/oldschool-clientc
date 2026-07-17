#ifndef TORIRS_STRUCT_FROM_RSCACHE_H
#define TORIRS_STRUCT_FROM_RSCACHE_H

struct RSCache_Dat2ConfigStruct;
struct ToriRS_Struct;

struct ToriRS_Struct*
ToriRS_StructFromRSCacheDat2(
    int struct_id,
    struct RSCache_Dat2ConfigStruct* entry);

#endif

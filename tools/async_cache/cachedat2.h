#ifndef ASYNC_CACHE_CACHEDAT2_H
#define ASYNC_CACHE_CACHEDAT2_H

struct HMap;
struct RSCacheDat2A_Model;
struct RSCacheDat2Disk_ReferenceTable;

struct CacheDat2
{
    struct HMap* reference_tables;
    struct HMap* models;
    struct HMap* object_models;
};

void
CacheDat2_Init(struct CacheDat2* cache);

void
CacheDat2_Model_Add(
    struct CacheDat2* cache,
    int id,
    struct RSCacheDat2A_Model* model);

void
CacheDat2_ObjectModel_Add(
    struct CacheDat2* cache,
    int object_id,
    struct RSCacheDat2A_Model* model);

void
CacheDat2_ReferenceTable_Add(
    struct CacheDat2* cache,
    int table_id,
    struct RSCacheDat2Disk_ReferenceTable* reference_table);

struct RSCacheDat2Disk_ReferenceTable*
CacheDat2_ReferenceTable_Get(
    struct CacheDat2* cache,
    int table_id);

struct RSCacheDat2A_Model*
CacheDat2_ObjectModel_Get(
    struct CacheDat2* cache,
    int object_id);

#endif

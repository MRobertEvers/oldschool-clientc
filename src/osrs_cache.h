#ifndef OSRS_CACHE_H
#define OSRS_CACHE_H

#include "osrs/tables/config_npctype.h"
#include "osrs/tables/model.h"

struct Model* load_models();

struct Model* cache_load_model(int model_id);

struct CacheConfigNPCType* cache_load_config_npctype(int npc_type_id);

/**
 * Describes a file within an archive.
 */
struct ReferenceTableEntryFileMetadata
{
    int name_hash;
    int id;
};

/**
 * Describes an archive in a table.
 */
struct ReferenceTableEntry
{
    int index;
    int identifier;
    int crc;
    int hash;
    unsigned char whirlpool[64];
    int compressed;
    int uncompressed;
    int version;
    struct
    {
        struct ReferenceTableEntryFileMetadata* entries;
        int count;
    } children;
};

struct ReferenceTable
{
    int format;
    int version;
    int flags;
    int id_count;
    int* ids; // entries is a sparse array. ids is the list of indices that are valid.
    // Each entry refers to an archive.
    // The 'children' of the archives are "files" in the archive.
    struct ReferenceTableEntry* entries;
    int entry_count;
};

struct ReferenceTable* cache_load_reference_table(int reference_table_id);

#endif

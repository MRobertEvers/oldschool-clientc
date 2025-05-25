#ifndef OSRS_CACHE_H
#define OSRS_CACHE_H

#include "osrs/config_npctype.h"
#include "osrs/model.h"

struct Model* load_models();

struct Model* cache_load_model(int model_id);

struct NPCType* cache_load_config_npctype(int npc_type_id);

struct ReferenceTable
{
    int format;
    int version;
    int flags;
    int id_count;
    int* ids; // entries is a sparse array. ids is the list of indices that are valid.
    struct ReferenceTableEntry* entries;
    int entry_count;
};

struct ReferenceTable* cache_load_reference_table(int reference_table_id);

#endif

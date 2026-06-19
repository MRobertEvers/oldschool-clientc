#ifndef RSCACHE_RSCACHEDAT1A_CONFIGNPC_H
#define RSCACHE_RSCACHEDAT1A_CONFIGNPC_H

#include "../shared/rscache_shared_rs_buffer.h"

#include <stdbool.h>

struct RSCacheDat1A_ConfigNpc
{
    char* name;
    char* desc;
    int size;
    int* models;
    int models_count;
    int* heads;
    int heads_count;
    int readyanim;
    int walkanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
    bool animHasAlpha;
    int* recol_s;
    int* recol_d;
    int recol_count;
    char* op[5]; // Options 30-34
    int resizex;
    int resizey;
    int resizez;
    bool minimap;
    int vislevel;
    int resizeh;
    int resizev;
    bool alwaysontop;
    int headicon;
    int ambient;
    int contrast;
};

struct RSCacheDat1A_ConfigNpcList
{
    struct RSCacheDat1A_ConfigNpc** npcs;
    int npcs_count;
};

struct RSCacheDat1A_ConfigNpcList*
RSCacheDat1A_ConfigNpcListNewDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size);

/** Decode a single NPC from a buffer positioned at the start of an entry. Ownership is
 * transferred to the caller. */
struct RSCacheDat1A_ConfigNpc*
RSCacheDat1A_ConfigNpcDecodeOne(struct RSCacheShared_RSBuffer* buffer);

void
RSCacheDat1A_ConfigNpcFree(struct RSCacheDat1A_ConfigNpc* npc);

#endif

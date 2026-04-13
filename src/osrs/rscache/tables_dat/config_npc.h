#ifndef CONFIG_NPC_H
#define CONFIG_NPC_H

#include "../rsbuf.h"

#include <stdbool.h>

struct CacheDatConfigNpc
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

struct CacheDatConfigNpcList
{
    struct CacheDatConfigNpc** npcs;
    int npcs_count;
};

struct CacheDatConfigNpcList*
cache_dat_config_npc_list_new_decode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size);

/** Decode a single NPC from a buffer positioned at the start of an entry. Ownership is
 * transferred to the caller. */
struct CacheDatConfigNpc*
cache_dat_config_npc_decode_one(struct RSBuffer* buffer);

void
cache_dat_config_npc_free(struct CacheDatConfigNpc* npc);

#endif

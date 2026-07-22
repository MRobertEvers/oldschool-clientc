#ifndef RSCACHE_DATATYPES_DAT1_CONFIG_NPC_H
#define RSCACHE_DATATYPES_DAT1_CONFIG_NPC_H

#include "../rsbuffer.h"

#include <stdbool.h>

struct RSCache_Dat1ConfigNpc
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
    /** NpcType.turnspeed (opcode 103), default 32; 0 = never turns. */
    int turnspeed;
};

struct RSCache_Dat1ConfigNpcList
{
    struct RSCache_Dat1ConfigNpc** npcs;
    int npcs_count;
};

struct RSCache_Dat1ConfigNpcList*
RSCache_Dat1ConfigNpcListNewDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size);

/** Decode a single NPC from a buffer positioned at the start of an entry. Ownership is
 * transferred to the caller. */
struct RSCache_Dat1ConfigNpc*
RSCache_Dat1ConfigNpcDecodeOne(struct RSCache_Buffer* buffer);

void
RSCache_Dat1ConfigNpcFree(struct RSCache_Dat1ConfigNpc* npc);

#endif

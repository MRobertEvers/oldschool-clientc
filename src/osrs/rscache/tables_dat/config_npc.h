#ifndef CONFIG_NPC_H
#define CONFIG_NPC_H

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
    struct CacheDatConfigNpc* npcs;
    int npcs_count;
};

struct CacheDatConfigNpcList*
cache_dat_config_npc_list_new_decode(
    void* jagfile_npcdat_data,
    int jagfile_npcdat_data_size);

#endif

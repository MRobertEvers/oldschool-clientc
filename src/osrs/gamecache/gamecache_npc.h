#ifndef GAMECACHE_NPC_H
#define GAMECACHE_NPC_H

/** Standalone GameCache NPC-config type -- only fields consumed by world/entity build. */
struct GameCacheNpc
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

    int* recol_s;
    int* recol_d;
    int recol_count;

    char* op[5];

    int minimap;
    int vislevel;
};

#endif

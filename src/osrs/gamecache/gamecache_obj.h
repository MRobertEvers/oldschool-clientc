#ifndef GAMECACHE_OBJ_H
#define GAMECACHE_OBJ_H

/** Standalone GameCache obj-config type -- only fields consumed by entity build / inventory. */
struct GameCacheObj
{
    int model;
    char* name;

    int* recol_s;
    int* recol_d;
    int recol_count;

    int zoom2d;
    int xan2d;
    int yan2d;
    int zan2d;
    int xof2d;
    int yof2d;

    int stackable;
    int cost;

    int manwear;
    int manwear2;
    int manwear3;
    int manhead;
    int manhead2;
    int womanhead;
    int womanhead2;

    int* countobj;
    int* countco;
    int countobj_count;

    int ambient;
    int contrast;

    char* iop[5];
};

#endif

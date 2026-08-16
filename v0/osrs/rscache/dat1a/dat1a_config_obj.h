#ifndef RSCACHE_RSCACHEDAT1A_CONFIGOBJ_H
#define RSCACHE_RSCACHEDAT1A_CONFIGOBJ_H

#include <stdbool.h>

struct RSCacheDat1A_ConfigObj
{
    int model;
    char* name;
    char* desc;
    int* recol_s;
    int* recol_d;
    int recol_count;
    int zoom2d;
    int xan2d;
    int yan2d;
    int zan2d;
    int xof2d;
    int yof2d;
    bool code9;
    int code10;
    bool stackable;
    int cost;
    bool members;
    int manwearOffsetY;
    int womanwearOffsetY;
    int manwear;
    int manwear2;
    int womanwear;
    int womanwear2;
    int manwear3;
    int womanwear3;
    int manhead;
    int manhead2;
    int womanhead;
    int womanhead2;
    int certlink;
    int certtemplate;
    int resizex;
    int resizey;
    int resizez;
    int ambient;
    int contrast;
    int* countobj;
    int* countco;
    int countobj_count;
    char* op[5];  // Options 30-34
    char* iop[5]; // Inventory options 35-39
};

struct RSCacheDat1A_ConfigObjList
{
    struct RSCacheDat1A_ConfigObj** objs;
    int objs_count;
};

struct RSCacheDat1A_ConfigObjList*
RSCacheDat1A_ConfigObjListNewDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size);

/** Decode a single obj from a raw data buffer. Ownership is transferred to the caller. */
struct RSCacheDat1A_ConfigObj*
RSCacheDat1A_ConfigObjDecodeOne(void* data, int size);

void
RSCacheDat1A_ConfigObjFree(struct RSCacheDat1A_ConfigObj* obj);

#endif

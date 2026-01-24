#ifndef CONFIG_OBJ_H
#define CONFIG_OBJ_H

#include <stdbool.h>

struct CacheDatConfigObj
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

struct CacheDatConfigObjList
{
    struct CacheDatConfigObj* objs;
    int objs_count;
};

struct CacheDatConfigObjList*
cache_dat_config_obj_list_new_decode(
    void* jagfile_objdat_data,
    int jagfile_objdat_data_size);

#endif

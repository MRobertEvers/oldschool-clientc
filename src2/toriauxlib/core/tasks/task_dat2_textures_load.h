#ifndef TORIAUXLIB_TASK_DAT2_TEXTURES_LOAD_H
#define TORIAUXLIB_TASK_DAT2_TEXTURES_LOAD_H

#include "3rd/minipt.h"
#include "ioqueue/libtorirs_io.h"

struct ToriAuxLibCache;
struct RSCacheDat2A_Texture;
struct RSCacheDat2A_SpritePack;

struct Task_Dat2TexturesLoad
{
    struct pt thread;
    struct ToriAuxLibCache* cache;
    struct RSCacheShared_FileList* filelist;
    int texture_count;
    int texture_index;
    int sprite_index;
    struct RSCacheDat2A_Texture* cur_def;
    int cur_texture_id;
    struct RSCacheDat2A_SpritePack** cur_packs;
};

struct Task_Dat2TexturesLoad*
Task_Dat2TexturesLoad_New(struct ToriAuxLibCache* cache);

void
Task_Dat2TexturesLoad_Free(struct Task_Dat2TexturesLoad* task);

int
Task_Dat2TexturesLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif

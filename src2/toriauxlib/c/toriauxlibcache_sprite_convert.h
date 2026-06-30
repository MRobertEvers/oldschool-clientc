#ifndef TORIAUXLIBCACHE_SPRITE_CONVERT_H
#define TORIAUXLIBCACHE_SPRITE_CONVERT_H

#include "revconfig/revconfig.h"
#include "toriauxlib/core/toriauxlibcore_types.h"

struct RSCacheDat2A_SpritePack;
struct RSCacheDat2Disk_Archive;
struct RSCacheShared_FileListDat;
struct ToriDraw_Sprite;

struct ToriAuxLibCore_SpriteFrame*
ToriAuxLibCache_SpriteFrameNewFromToriDrawByMove(struct ToriDraw_Sprite* sprite);

struct ToriAuxLibCore_SpriteFrame*
ToriAuxLibCache_SpriteFrameNewFromArgbByMove(
    uint32_t* pixels_argb,
    int width,
    int height);

struct ToriAuxLibCore_Sprite*
ToriAuxLibCache_SpriteNewFromDat1RevConfigItem(
    struct RSCacheShared_FileListDat* filelist,
    struct RevConfigCacheItem const* item);

struct ToriAuxLibCore_Sprite*
ToriAuxLibCache_SpriteNewFromDat1Ref(
    struct RSCacheShared_FileListDat* filelist,
    char const* sprite_ref);

struct ToriAuxLibCore_Sprite*
ToriAuxLibCache_SpriteNewFromDat2Archive(
    struct RSCacheDat2Disk_Archive* archive,
    struct RevConfigCacheItem const* item);

struct ToriAuxLibCore_Sprite*
ToriAuxLibCache_SpriteNewFromDat2ArchiveId(
    struct RSCacheDat2Disk_Archive* archive,
    int sprite_id);

void
ToriAuxLibCache_SpriteApplyRevConfigItem(
    struct ToriAuxLibCore_Sprite* sprite,
    struct RevConfigCacheItem const* item);

#endif

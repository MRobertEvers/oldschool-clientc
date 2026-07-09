#ifndef TORIAUXLIBCACHE_FONT_CONVERT_H
#define TORIAUXLIBCACHE_FONT_CONVERT_H

#include "buildcache/dat2_buildcache.h"
#include "toriauxlib/core/toriauxlibcore_types.h"

struct RSCacheDat2Disk;
struct RSCacheDat2Disk_Archive;
struct RSCacheShared_FileListDat;
struct ToriDraw_Font;

struct ToriAuxLibCore_Font*
ToriAuxLibCache_FontNewFromToriDrawByMove(struct ToriDraw_Font* font);

struct ToriAuxLibCore_Font*
ToriAuxLibCache_FontNewFromRSBytesByMove(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size);

struct ToriAuxLibCore_Font*
ToriAuxLibCache_FontNewFromDat1Jagfile(
    struct RSCacheShared_FileListDat* filelist,
    char const* font_name);

struct ToriAuxLibCore_Font*
ToriAuxLibCache_FontNewFromDat2FontAsset(
    struct Dat2BuildCache_FontAsset const* asset,
    int font_id);

struct ToriAuxLibCore_Font*
ToriAuxLibCache_FontNewFromDat2Archives(
    struct RSCacheDat2Disk_Archive* font_archive,
    struct RSCacheDat2Disk_Archive* sprite_archive,
    int font_id);

#endif

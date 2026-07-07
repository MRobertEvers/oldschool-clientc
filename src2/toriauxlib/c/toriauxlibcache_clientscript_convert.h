#ifndef TORIAUXLIBCACHE_CLIENTSCRIPT_CONVERT_H
#define TORIAUXLIBCACHE_CLIENTSCRIPT_CONVERT_H

#include "osrs/rscache/dat2a/dat2a_clientscript.h"
#include "toriauxlib/core/toriauxlibcore_types.h"

struct RSCacheDat2Disk;
struct RSCacheDat2Disk_Archive;

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptNewFromDat2Decode(
    int script_id,
    struct RSCacheDat2A_ClientScript* src);

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptNewFromDat2Archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int script_id,
    int clientscript_decode_flags);

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
    struct RSCacheDat2Disk_Archive* archive,
    int script_id,
    int clientscript_decode_flags);

#endif

#ifndef TORIAUXLIBCACHE_CLIENTSCRIPT_CONVERT_H
#define TORIAUXLIBCACHE_CLIENTSCRIPT_CONVERT_H

#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/core/toriauxlibcore_types.h"

struct RSCacheDat2A_ClientScript;

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptNewFromDat2Decode(
    int script_id,
    const struct RSCacheDat2A_ClientScript* src);

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptNewFromDat2Archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int script_id);

void
ToriAuxLibCore_ClientScriptBuildCs2vmOps(struct ToriAuxLibCore_ClientScript* script);

#endif

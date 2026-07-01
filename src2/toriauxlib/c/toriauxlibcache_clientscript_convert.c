#include "toriauxlibcache_clientscript_convert.h"

#include "osrs/rscache/dat2a/dat2a_clientscript.h"
#include "osrs/rscache/dat2disk/dat2disk.h"

#include <stdlib.h>
#include <string.h>

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptNewFromDat2Decode(
    int script_id,
    struct RSCacheDat2A_ClientScript* src)
{
    if( !src || src->script.op_count <= 0 )
        return NULL;

    struct ToriAuxLibCore_ClientScript* dst = calloc(1, sizeof(*dst));
    if( !dst )
        return NULL;

    dst->script = src->script;
    cs2_script_init(&src->script);
    dst->script.script_id = script_id;
    return dst;
}

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptNewFromDat2Archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int script_id)
{
    if( !cache || !archive || script_id < 0 )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    RSCacheDat2Disk_ArchiveInitMetadata(cache, archive);
    struct RSCacheDat2A_ClientScript* decoded = RSCacheDat2A_ClientScriptNewDecode(
        script_id,
        (const uint8_t*)archive->data,
        archive->data_size);
    RSCacheDat2Disk_ArchiveFree(archive);
    if( !decoded )
        return NULL;

    struct ToriAuxLibCore_ClientScript* script =
        ToriAuxLibCache_ClientScriptNewFromDat2Decode(script_id, decoded);
    RSCacheDat2A_ClientScriptFree(decoded);
    return script;
}

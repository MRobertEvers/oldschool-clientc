#include "toriauxlibcache_clientscript_convert.h"

#include "osrs/rscache/dat2a/dat2a_clientscript.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "vm/cs2_script.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
move_rscache_cs2_script_to_vm(
    struct RSCache_CS2_Script* src,
    struct CS2_Script* dst)
{
    cs2_script_init(dst);
    dst->script_id = src->script_id;
    dst->signature = src->signature;
    dst->local_int_count = src->local_int_count;
    dst->local_string_count = src->local_string_count;
    dst->local_long_count = src->local_long_count;
    dst->int_argument_count = src->int_argument_count;
    dst->string_argument_count = src->string_argument_count;
    dst->long_argument_count = src->long_argument_count;
    dst->op_count = src->op_count;
    dst->opcodes = src->opcodes;
    dst->int_operands = src->int_operands;
    dst->string_operands = src->string_operands;
    dst->switch_table_count = src->switch_table_count;
    memcpy(dst->switch_tables, src->switch_tables, sizeof(dst->switch_tables));
    RSCache_CS2_ScriptInit(src);
}

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

    move_rscache_cs2_script_to_vm(&src->script, &dst->script);
    dst->script.script_id = script_id;
    return dst;
}

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
    struct RSCacheDat2Disk_Archive* archive,
    int script_id,
    int clientscript_decode_flags)
{
    assert(archive);
    assert(script_id >= 0);

    struct RSCacheDat2A_ClientScript* decoded = RSCacheDat2A_ClientScriptNewFromDecodeFlags(
        script_id, (const uint8_t*)archive->data, archive->data_size, clientscript_decode_flags);
    RSCacheDat2Disk_ArchiveFree(archive);
    if( !decoded )
        return NULL;

    struct ToriAuxLibCore_ClientScript* script =
        ToriAuxLibCache_ClientScriptNewFromDat2Decode(script_id, decoded);
    RSCacheDat2A_ClientScriptFree(decoded);
    return script;
}

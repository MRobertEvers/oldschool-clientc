#include "toriauxlibcache_clientscript_convert.h"

#include "osrs/rscache/dat2a/dat2a_clientscript.h"

#include <stdlib.h>
#include <string.h>

static void
clientscript_append_cs2vm_op(
    int** ops_out,
    int* count_out,
    int* cap_out,
    int opcode,
    int operand)
{
    if( !ops_out || !count_out || !cap_out )
        return;

    int need = *count_out + (operand >= 0 ? 2 : 1);
    if( need > *cap_out )
    {
        int new_cap = *cap_out > 0 ? *cap_out * 2 : 16;
        while( new_cap < need )
            new_cap *= 2;
        int* grown = realloc(*ops_out, (size_t)new_cap * sizeof(int));
        if( !grown )
            return;
        *ops_out = grown;
        *cap_out = new_cap;
    }

    (*ops_out)[(*count_out)++] = opcode;
    if( operand >= 0 )
        (*ops_out)[(*count_out)++] = operand;
}

static int
clientscript_map_jagex_to_cs2vm(
    int jagex_opcode,
    int jagex_operand)
{
    switch( jagex_opcode )
    {
    case 0:
        return 50;
    case 1:
        return 1;
    case 21:
        return 2;
    case 25:
        return 3;
    case 42:
        return 4;
    case 31:
        return 10;
    case 32:
        return 11;
    case 33:
        return 12;
    case 34:
        return 13;
    case 44:
        return 20;
    case 45:
        return 21;
    case 46:
        return 22;
    case 47:
        return 23;
    default:
        if( jagex_opcode >= 100 || jagex_opcode == 38 || jagex_opcode == 39 )
            return 1;
        return -1;
    }
    (void)jagex_operand;
}

void
ToriAuxLibCore_ClientScriptBuildCs2vmOps(struct ToriAuxLibCore_ClientScript* script)
{
    if( !script || script->cs2vm_ops )
        return;

    int* ops = NULL;
    int count = 0;
    int cap = 0;

    for( int i = 0; i < script->op_count; i++ )
    {
        int jagex_opcode = script->instructions ? script->instructions[i] : 0;
        int jagex_operand = script->int_operands ? script->int_operands[i] : 0;
        if( jagex_opcode == 3 )
            continue;

        int cs2_opcode = clientscript_map_jagex_to_cs2vm(jagex_opcode, jagex_operand);
        if( cs2_opcode < 0 )
            continue;

        int operand = -1;
        if( cs2_opcode == 1 || cs2_opcode == 2 || cs2_opcode == 3 || cs2_opcode == 4 )
            operand = jagex_operand;
        clientscript_append_cs2vm_op(&ops, &count, &cap, cs2_opcode, operand);
    }

    clientscript_append_cs2vm_op(&ops, &count, &cap, 0, -1);
    script->cs2vm_ops = ops;
    script->cs2vm_op_count = count;
}

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptNewFromDat2Decode(
    int script_id,
    const struct RSCacheDat2A_ClientScript* src)
{
    if( !src || src->op_count <= 0 )
        return NULL;

    struct ToriAuxLibCore_ClientScript* dst = calloc(1, sizeof(*dst));
    if( !dst )
        return NULL;

    dst->script_id = script_id;
    dst->op_count = src->op_count;
    dst->instructions = malloc((size_t)src->op_count * sizeof(int));
    dst->int_operands = malloc((size_t)src->op_count * sizeof(int));
    if( !dst->instructions || !dst->int_operands )
    {
        ToriAuxLibCore_ClientScriptFree(dst);
        return NULL;
    }

    memcpy(dst->instructions, src->instructions, (size_t)src->op_count * sizeof(int));
    memcpy(dst->int_operands, src->int_operands, (size_t)src->op_count * sizeof(int));
    ToriAuxLibCore_ClientScriptBuildCs2vmOps(dst);
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

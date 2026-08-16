#include "dat2a_clientscript.h"

#include "../shared/shared_rs_buffer.h"
#include "dat2a_cs2_opcode_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RSCACHE_CS2_OP_RETURN 21
#define RSCACHE_CS2_OP_POP_INT_DISCARD 38
#define RSCACHE_CS2_OP_POP_STRING_DISCARD 39
#define RSCACHE_CS2_OP_PUSH_CONSTANT_LONG 61
#define RSCACHE_CS2_OP_POP_LONG_DISCARD 62
#define RSCACHE_CS2_OP_PUSH_NULL 63

enum CS2ScriptTrailerFooter
{
    CS2_SCRIPT_TRAILER_FOOTER_LEGACY = 14,
    CS2_SCRIPT_TRAILER_FOOTER_MODERN = 18,
};

static int
read_u16_at(
    const uint8_t* data,
    int size,
    int pos)
{
    if( pos < 0 || pos + 1 >= size )
        return 0;
    return ((int)data[pos] << 8) | (int)data[pos + 1];
}

static bool
cs2_operand_uses_int8(int opcode)
{
    return opcode >= 100 || opcode == RSCACHE_CS2_OP_RETURN ||
           opcode == RSCACHE_CS2_OP_POP_INT_DISCARD ||
           opcode == RSCACHE_CS2_OP_POP_STRING_DISCARD ||
           opcode == RSCACHE_CS2_OP_POP_LONG_DISCARD || opcode == RSCACHE_CS2_OP_PUSH_NULL;
}

static bool
cs2_script_read_operand(
    struct RSCacheShared_RSBuffer* body,
    int opcode,
    int op_index,
    struct RSCache_CS2_Script* script)
{
    if( opcode == RSCACHE_CS2_OP_PUSH_CONSTANT_LONG )
    {
        int high = RSCacheShared_RSBufferG4(body);
        int low = RSCacheShared_RSBufferG4(body);
        script->int_operands[op_index] = low;
        (void)high;
        return true;
    }

    if( cs2_operand_uses_int8(opcode) )
    {
        script->int_operands[op_index] = (int)RSCacheShared_RSBufferG1b(body);
        return true;
    }

    switch( rscache_cs2_opcode_operand_kind(opcode) )
    {
    case RSCACHE_CS2_OPERAND_STRING:
        script->string_operands[op_index] = RSCacheShared_RSBufferReadStringNullTerminated(body);
        script->int_operands[op_index] = 0;
        break;
    case RSCACHE_CS2_OPERAND_INT8:
        script->int_operands[op_index] = (int)RSCacheShared_RSBufferG1b(body);
        break;
    case RSCACHE_CS2_OPERAND_NONE:
        script->int_operands[op_index] = 0;
        break;
    case RSCACHE_CS2_OPERAND_INT32:
    default:
        script->int_operands[op_index] = RSCacheShared_RSBufferG4(body);
        break;
    }
    return true;
}

static bool
cs2_script_parse_body(
    const uint8_t* data,
    int data_size,
    int trailer_pos,
    int op_count,
    struct RSCache_CS2_Script* script)
{
    struct RSCacheShared_RSBuffer body;
    RSCacheShared_RSBufferInit(&body, (uint8_t*)data, (uint32_t)data_size);
    script->signature = RSCacheShared_RSBufferReadStringNullTerminated(&body);

    int op = 0;
    while( body.position < (uint32_t)trailer_pos && op < op_count )
    {
        int opcode = (int)RSCacheShared_RSBufferG2(&body);
        script->opcodes[op] = (uint16_t)opcode;
        if( !cs2_script_read_operand(&body, opcode, op, script) )
            return false;
        op++;
    }

    return op == op_count && body.position == (uint32_t)trailer_pos;
}

static struct RSCacheDat2A_ClientScript*
cs2_script_try_decode_footer(
    int script_id,
    const uint8_t* data,
    int data_size,
    int footer_size,
    bool legacy)
{
    int trailer_len = read_u16_at(data, data_size, data_size - 2);
    int trailer_pos = data_size - footer_size - trailer_len;
    if( trailer_pos < 1 || trailer_pos > data_size )
        return NULL;

    struct RSCacheShared_RSBuffer trailer;
    RSCacheShared_RSBufferInit(
        &trailer, (uint8_t*)data + trailer_pos, (uint32_t)(data_size - trailer_pos));

    int op_count = RSCacheShared_RSBufferG4(&trailer);
    int local_int_count = (int)RSCacheShared_RSBufferG2(&trailer);
    int local_string_count = (int)RSCacheShared_RSBufferG2(&trailer);
    int local_long_count = 0;
    int int_argument_count;
    int string_argument_count;
    int long_argument_count = 0;

    if( legacy )
    {
        int_argument_count = (int)RSCacheShared_RSBufferG2(&trailer);
        string_argument_count = (int)RSCacheShared_RSBufferG2(&trailer);
    }
    else
    {
        local_long_count = (int)RSCacheShared_RSBufferG2(&trailer);
        int_argument_count = (int)RSCacheShared_RSBufferG2(&trailer);
        string_argument_count = (int)RSCacheShared_RSBufferG2(&trailer);
        long_argument_count = (int)RSCacheShared_RSBufferG2(&trailer);
    }

    int switch_table_count = (int)RSCacheShared_RSBufferG1(&trailer);
    if( switch_table_count < 0 || switch_table_count > RSCACHE_CS2_SCRIPT_MAX_SWITCHES )
        return NULL;
    if( op_count <= 0 || op_count > 65536 )
        return NULL;

    struct RSCacheDat2A_ClientScript* out = calloc(1, sizeof(*out));
    if( !out )
        return NULL;

    struct RSCache_CS2_Script* script = &out->script;
    RSCache_CS2_ScriptInit(script);
    script->script_id = script_id;
    script->local_int_count = local_int_count;
    script->local_string_count = local_string_count;
    script->local_long_count = local_long_count;
    script->int_argument_count = int_argument_count;
    script->string_argument_count = string_argument_count;
    script->long_argument_count = long_argument_count;
    script->op_count = op_count;
    script->switch_table_count = switch_table_count;

    for( int s = 0; s < switch_table_count; s++ )
    {
        int case_count = (int)RSCacheShared_RSBufferG2(&trailer);
        if( case_count < 0 || case_count > RSCACHE_CS2_SCRIPT_MAX_SWITCH_CASES )
        {
            RSCacheDat2A_ClientScriptFree(out);
            return NULL;
        }
        script->switch_tables[s].case_count = case_count;
        for( int c = 0; c < case_count; c++ )
        {
            script->switch_tables[s].cases[c].key = RSCacheShared_RSBufferG4(&trailer);
            script->switch_tables[s].cases[c].target_pc = RSCacheShared_RSBufferG4(&trailer);
        }
    }

    script->opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script->int_operands = calloc((size_t)op_count, sizeof(int));
    script->string_operands = calloc((size_t)op_count, sizeof(char*));
    if( !script->opcodes || !script->int_operands || !script->string_operands )
    {
        RSCacheDat2A_ClientScriptFree(out);
        return NULL;
    }

    if( !cs2_script_parse_body(data, data_size, trailer_pos, op_count, script) )
    {
        RSCacheDat2A_ClientScriptFree(out);
        return NULL;
    }

    script->op_count = op_count;
    return out;
}

struct RSCacheDat2A_ClientScript*
RSCacheDat2A_ClientScriptNewFromDecodeFlags(
    int script_id,
    const uint8_t* data,
    int data_size,
    int flags)
{
    if( !data || data_size < CS2_SCRIPT_TRAILER_FOOTER_LEGACY )
        return NULL;

    bool legacy = flags == CLIENTSCRIPT_DECODE_TRAILER_LEGACY;
    int footer_size =
        legacy ? CS2_SCRIPT_TRAILER_FOOTER_LEGACY : CS2_SCRIPT_TRAILER_FOOTER_MODERN;

    struct RSCacheDat2A_ClientScript* decoded =
        cs2_script_try_decode_footer(script_id, data, data_size, footer_size, legacy);
    if( decoded )
        return decoded;

    fprintf(
        stderr,
        "RSCacheDat2A_ClientScriptNewFromDecodeFlags: script %d decode failed (trailer=%s)\n",
        script_id,
        legacy ? "legacy" : "modern");
    return NULL;
}

void
RSCacheDat2A_ClientScriptFree(struct RSCacheDat2A_ClientScript* script)
{
    if( !script )
        return;
    RSCache_CS2_ScriptFree(&script->script);
    free(script);
}

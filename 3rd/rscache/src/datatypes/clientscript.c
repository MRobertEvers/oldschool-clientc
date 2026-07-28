#include "clientscript.h"

#include "../rsbuffer.h"
#include "cs2_opcode_decode.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RSCACHE_CS2_OP_RETURN 21
#define RSCACHE_CS2_OP_POP_INT_DISCARD 38
#define RSCACHE_CS2_OP_POP_STRING_DISCARD 39
/* 51/52 (varc long) and 66-73 (long load/store/cmp) are INT32 in the
 * generated operand-kind table — no special-case width handling needed. */
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
    struct RSCache_Buffer* body,
    int opcode,
    int op_index,
    struct RSCache_CS2_Script* script)
{
    if( opcode == RSCACHE_CS2_OP_PUSH_CONSTANT_LONG )
    {
        script->long_operands[op_index] = RSCache_BufferG8(body);
        script->int_operands[op_index] = 0;
        return true;
    }

    if( cs2_operand_uses_int8(opcode) )
    {
        script->int_operands[op_index] = (int)RSCache_BufferG1b(body);
        return true;
    }

    switch( rscache_cs2_opcode_operand_kind(opcode) )
    {
    case RSCACHE_CS2_OPERAND_STRING:
        script->string_operands[op_index] = RSCache_BufferReadStringNullTerminated(body);
        script->int_operands[op_index] = 0;
        break;
    case RSCACHE_CS2_OPERAND_INT8:
        script->int_operands[op_index] = (int)RSCache_BufferG1b(body);
        break;
    case RSCACHE_CS2_OPERAND_NONE:
        script->int_operands[op_index] = 0;
        break;
    case RSCACHE_CS2_OPERAND_INT32:
    default:
        /* Long-stack ops 51/52/66-73 are INT32 in the operand-kind table. */
        script->int_operands[op_index] = RSCache_BufferG4(body);
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
    struct RSCache_Buffer body;
    RSCache_BufferInit(&body, (uint8_t*)data, (uint32_t)data_size);
    script->signature = RSCache_BufferReadStringNullTerminated(&body);

    int op = 0;
    while( body.position < (uint32_t)trailer_pos && op < op_count )
    {
        int opcode = (int)RSCache_BufferG2(&body);
        script->opcodes[op] = (uint16_t)opcode;
        if( !cs2_script_read_operand(&body, opcode, op, script) )
            return false;
        op++;
    }

    return op == op_count && body.position == (uint32_t)trailer_pos;
}

static struct RSCache_ClientScript*
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

    struct RSCache_Buffer trailer;
    RSCache_BufferInit(&trailer, (uint8_t*)data + trailer_pos, (uint32_t)(data_size - trailer_pos));

    int op_count = RSCache_BufferG4(&trailer);
    int local_int_count = (int)RSCache_BufferG2(&trailer);
    int local_string_count = (int)RSCache_BufferG2(&trailer);
    int local_long_count = 0;
    int int_argument_count;
    int string_argument_count;
    int long_argument_count = 0;

    if( legacy )
    {
        int_argument_count = (int)RSCache_BufferG2(&trailer);
        string_argument_count = (int)RSCache_BufferG2(&trailer);
    }
    else
    {
        local_long_count = (int)RSCache_BufferG2(&trailer);
        int_argument_count = (int)RSCache_BufferG2(&trailer);
        string_argument_count = (int)RSCache_BufferG2(&trailer);
        long_argument_count = (int)RSCache_BufferG2(&trailer);
    }

    int switch_table_count = (int)RSCache_BufferG1(&trailer);
    if( switch_table_count < 0 )
        return NULL;
    if( op_count <= 0 || op_count > 65536 )
        return NULL;

    struct RSCache_ClientScript* out = calloc(1, sizeof(*out));
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

    if( !RSCache_CS2_ScriptAllocSwitches(script, switch_table_count) )
    {
        RSCache_ClientScriptFree(out);
        return NULL;
    }

    for( int s = 0; s < switch_table_count; s++ )
    {
        int case_count = (int)RSCache_BufferG2(&trailer);
        /* Bounded by what is actually left in the trailer rather than by a
         * fixed ceiling: a case is 8 bytes, so a count that could not fit is
         * a corrupt header, and any count that fits is legitimate. Real
         * scripts run to hundreds of cases and dozens of tables. */
        int bytes_remaining = (int)(trailer.size - trailer.position);
        if( case_count < 0 || case_count > bytes_remaining / 8 )
        {
            RSCache_ClientScriptFree(out);
            return NULL;
        }
        if( !RSCache_CS2_ScriptAllocSwitchCases(script, s, case_count) )
        {
            RSCache_ClientScriptFree(out);
            return NULL;
        }
        for( int c = 0; c < case_count; c++ )
        {
            script->switch_tables[s].cases[c].key = RSCache_BufferG4(&trailer);
            script->switch_tables[s].cases[c].target_pc = RSCache_BufferG4(&trailer);
        }
    }

    script->opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script->int_operands = calloc((size_t)op_count, sizeof(int));
    script->long_operands = calloc((size_t)op_count, sizeof(int64_t));
    script->string_operands = calloc((size_t)op_count, sizeof(char*));
    if( !script->opcodes || !script->int_operands || !script->long_operands ||
        !script->string_operands )
    {
        RSCache_ClientScriptFree(out);
        return NULL;
    }

    if( !cs2_script_parse_body(data, data_size, trailer_pos, op_count, script) )
    {
        RSCache_ClientScriptFree(out);
        return NULL;
    }

    script->op_count = op_count;
    return out;
}

/* Bytes the switch tables occupy on the wire. */
static int
cs2_switch_bytes(const struct RSCache_CS2_Script* script)
{
    int total = 0;
    for( int i = 0; i < script->switch_table_count; i++ )
        total += 2 + script->switch_tables[i].case_count * 8;
    return total;
}

static void
cs2_script_write_operand(
    struct RSCache_Buffer* body,
    int opcode,
    int op_index,
    const struct RSCache_CS2_Script* script)
{
    if( opcode == RSCACHE_CS2_OP_PUSH_CONSTANT_LONG )
    {
        int64_t value = script->long_operands ? script->long_operands[op_index] : 0;
        p8(body, value);
        return;
    }
    if( cs2_operand_uses_int8(opcode) )
    {
        p1b(body, script->int_operands[op_index]);
        return;
    }
    switch( rscache_cs2_opcode_operand_kind(opcode) )
    {
    case RSCACHE_CS2_OPERAND_STRING:
        pjstr(
            body,
            script->string_operands[op_index] ? script->string_operands[op_index] : "",
            RSCACHE_JSTR_TERMINATOR_NULL);
        break;
    case RSCACHE_CS2_OPERAND_INT8:
        p1b(body, script->int_operands[op_index]);
        break;
    case RSCACHE_CS2_OPERAND_NONE:
        break;
    case RSCACHE_CS2_OPERAND_INT32:
    default:
        p4(body, script->int_operands[op_index]);
        break;
    }
}

uint32_t
RSCache_ClientScriptEncodeBound(const struct RSCache_ClientScript* script)
{
    if( !script )
        return 0;

    const struct RSCache_CS2_Script* cs2 = &script->script;
    uint32_t total = (uint32_t)(cs2->signature ? strlen(cs2->signature) : 0) + 1u;
    /* Worst case per op: u16 opcode plus an 8-byte long operand. Strings are counted
     * separately since they can exceed that. */
    total += (uint32_t)cs2->op_count * 10u;
    for( int i = 0; i < cs2->op_count; i++ )
    {
        if( cs2->string_operands && cs2->string_operands[i] )
            total += (uint32_t)strlen(cs2->string_operands[i]) + 1u;
    }
    total += (uint32_t)CS2_SCRIPT_TRAILER_FOOTER_MODERN;
    total += (uint32_t)cs2_switch_bytes(cs2);
    total += 64u;
    return total;
}

uint32_t
RSCache_ClientScriptEncodeFlags(
    const struct RSCache_ClientScript* script,
    int flags,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !script || !out )
        return 0;

    const struct RSCache_CS2_Script* cs2 = &script->script;
    if( cs2->op_count <= 0 || !cs2->opcodes || !cs2->int_operands )
        return 0;
    if( out_capacity < RSCache_ClientScriptEncodeBound(script) )
        return 0;

    bool legacy = flags == RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    pjstr(&buffer, cs2->signature ? cs2->signature : "", RSCACHE_JSTR_TERMINATOR_NULL);

    for( int i = 0; i < cs2->op_count; i++ )
    {
        int opcode = (int)cs2->opcodes[i];
        p2(&buffer, opcode);
        cs2_script_write_operand(&buffer, opcode, i, cs2);
    }

    /* Fixed trailer fields. The long counts exist only in the modern layout. */
    p4(&buffer, cs2->op_count);
    p2(&buffer, cs2->local_int_count);
    p2(&buffer, cs2->local_string_count);
    if( !legacy )
        p2(&buffer, cs2->local_long_count);
    p2(&buffer, cs2->int_argument_count);
    p2(&buffer, cs2->string_argument_count);
    if( !legacy )
        p2(&buffer, cs2->long_argument_count);
    p1(&buffer, cs2->switch_table_count);

    for( int i = 0; i < cs2->switch_table_count; i++ )
    {
        const struct RSCache_CS2_ScriptSwitch* table = &cs2->switch_tables[i];
        p2(&buffer, table->case_count);
        for( int c = 0; c < table->case_count; c++ )
        {
            p4(&buffer, table->cases[c].key);
            p4(&buffer, table->cases[c].target_pc);
        }
    }

    /*
     * The trailing length. The decoder locates the trailer with
     *   trailer_pos = data_size - footer_size - trailer_len
     * which only works if trailer_len is the switch-table byte count **plus one**.
     * Measured across every script in cache.osrs230, with and without switches:
     * S=0 -> 1, S=50 -> 51, S=92 -> 93, S=162 -> 163, S=192 -> 193, S=418 -> 419.
     * The +1 is the switch-count byte, which the footer size does not cover.
     */
    p2(&buffer, cs2_switch_bytes(cs2) + 1);

    return buffer.position;
}

uint32_t
RSCache_ClientScriptEncode(
    const struct RSCache* cache,
    const struct RSCache_ClientScript* script,
    uint8_t* out,
    uint32_t out_capacity)
{
    return RSCache_ClientScriptEncodeFlags(
        script, RSCache_ClientScriptFlags(cache), out, out_capacity);
}

int
RSCache_ClientScriptFlags(const struct RSCache* cache)
{
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_CLIENTSCRIPT, 237, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        return RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_MODERN;
    return RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY;
}

struct RSCache_ClientScript*
RSCache_ClientScriptNewFromDecodeFlags(
    int script_id,
    const uint8_t* data,
    int data_size,
    int flags)
{
    assert(data);
    assert(data_size > 0);

    bool legacy = flags == RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY;
    int footer_size = legacy ? CS2_SCRIPT_TRAILER_FOOTER_LEGACY : CS2_SCRIPT_TRAILER_FOOTER_MODERN;

    struct RSCache_ClientScript* decoded =
        cs2_script_try_decode_footer(script_id, data, data_size, footer_size, legacy);
    if( decoded )
        return decoded;

    fprintf(
        stderr,
        "RSCache_ClientScriptNewFromDecodeFlags: script %d decode failed (trailer=%s)\n",
        script_id,
        legacy ? "legacy" : "modern");
    return NULL;
}

struct RSCache_ClientScript*
RSCache_ClientScriptNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int script_id,
    int flags)
{
    assert(archive);
    assert(script_id >= 0);

    struct RSCache_ClientScript* script = RSCache_ClientScriptNewFromDecodeFlags(
        script_id, (const uint8_t*)archive->data, archive->data_size, flags);
    return script;
}

void
RSCache_ClientScriptFree(struct RSCache_ClientScript* script)
{
    if( !script )
        return;
    RSCache_CS2_ScriptFree(&script->script);
    free(script);
}

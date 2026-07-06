#include "dat2a_clientscript.h"

#include "../shared/shared_rs_buffer.h"
#include "vm/cs2_opcode.h"

#include <stdlib.h>
#include <string.h>

/* Long/null opcodes not yet in generated cs2_opcode.h (RuneStar Script.kt). */
#ifndef CS2_OP_PUSH_CONSTANT_LONG
#define CS2_OP_PUSH_CONSTANT_LONG 61
#endif
#ifndef CS2_OP_POP_LONG_DISCARD
#define CS2_OP_POP_LONG_DISCARD 62
#endif
#ifndef CS2_OP_PUSH_NULL
#define CS2_OP_PUSH_NULL 63
#endif

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

static int
cs2_operand_is_int8(int opcode)
{
    return opcode >= 100 || opcode == CS2_OP_RETURN || opcode == CS2_OP_POP_INT_DISCARD ||
           opcode == CS2_OP_POP_STRING_DISCARD || opcode == CS2_OP_POP_LONG_DISCARD ||
           opcode == CS2_OP_PUSH_NULL;
}

struct RSCacheDat2A_ClientScript*
RSCacheDat2A_ClientScriptNewDecode(
    int script_id,
    const uint8_t* data,
    int data_size)
{
    if( !data || data_size < 18 || data[0] != 0 )
        return NULL;

    int trailer_len = read_u16_at(data, data_size, data_size - 2);
    int trailer_pos = data_size - 18 - trailer_len;
    if( trailer_pos < 1 || trailer_pos > data_size )
        return NULL;

    struct RSCacheShared_RSBuffer trailer;
    RSCacheShared_RSBufferInit(&trailer, (uint8_t*)data + trailer_pos, (uint32_t)(data_size - trailer_pos));

    int op_count = RSCacheShared_RSBufferG4(&trailer);
    int local_int_count = (int)RSCacheShared_RSBufferG2(&trailer);
    int local_string_count = (int)RSCacheShared_RSBufferG2(&trailer);
    int local_long_count = (int)RSCacheShared_RSBufferG2(&trailer);
    int int_argument_count = (int)RSCacheShared_RSBufferG2(&trailer);
    int string_argument_count = (int)RSCacheShared_RSBufferG2(&trailer);
    int long_argument_count = (int)RSCacheShared_RSBufferG2(&trailer);

    int switch_table_count = (int)RSCacheShared_RSBufferG1(&trailer);
    if( switch_table_count < 0 || switch_table_count > CS2_SCRIPT_MAX_SWITCHES )
        return NULL;
    if( op_count <= 0 || op_count > 65536 )
        return NULL;

    struct RSCacheDat2A_ClientScript* out = calloc(1, sizeof(*out));
    if( !out )
        return NULL;

    struct CS2_Script* script = &out->script;
    cs2_script_init(script);
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
        if( case_count < 0 || case_count > CS2_SCRIPT_MAX_SWITCH_CASES )
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

    struct RSCacheShared_RSBuffer body;
    RSCacheShared_RSBufferInit(&body, (uint8_t*)data, (uint32_t)data_size);
    script->signature = RSCacheShared_RSBufferReadStringNullTerminated(&body);

    int op = 0;
    while( body.position < (uint32_t)trailer_pos && op < op_count )
    {
        int opcode = (int)RSCacheShared_RSBufferG2(&body);
        script->opcodes[op] = (uint16_t)opcode;

        if( opcode == CS2_OP_PUSH_CONSTANT_STRING )
        {
            script->string_operands[op] = RSCacheShared_RSBufferReadStringNullTerminated(&body);
            script->int_operands[op] = 0;
        }
        else if( opcode == CS2_OP_PUSH_CONSTANT_LONG )
        {
            int high = RSCacheShared_RSBufferG4(&body);
            int low = RSCacheShared_RSBufferG4(&body);
            script->int_operands[op] = low;
            (void)high;
        }
        else if( cs2_operand_is_int8(opcode) )
            script->int_operands[op] = (int)RSCacheShared_RSBufferG1b(&body);
        else
            script->int_operands[op] = RSCacheShared_RSBufferG4(&body);

        op++;
    }

    if( op != op_count || body.position != (uint32_t)trailer_pos )
    {
        RSCacheDat2A_ClientScriptFree(out);
        return NULL;
    }

    script->op_count = op;
    return out;
}

void
RSCacheDat2A_ClientScriptFree(struct RSCacheDat2A_ClientScript* script)
{
    if( !script )
        return;
    cs2_script_free(&script->script);
    free(script);
}

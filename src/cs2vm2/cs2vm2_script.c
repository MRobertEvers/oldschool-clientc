#include "cs2vm2_script.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void
CS2VM2_ScriptInit(struct CS2VM2_Script* script)
{
    assert(script);
    memset(script, 0, sizeof(*script));
}

void
CS2VM2_ScriptFree(struct CS2VM2_Script* script)
{
    if( !script )
        return;

    free(script->signature);
    free(script->opcodes);
    free(script->int_operands);
    if( script->string_operands )
    {
        for( int i = 0; i < script->op_count; i++ )
            free(script->string_operands[i]);
        free(script->string_operands);
    }
    memset(script, 0, sizeof(*script));
}

bool
CS2VM2_ScriptCopy(
    const struct CS2VM2_Script* src,
    struct CS2VM2_Script* dst)
{
    int i;

    assert(src);
    assert(dst);

    if( src->op_count <= 0 )
        return false;

    CS2VM2_ScriptInit(dst);
    dst->script_id = src->script_id;
    dst->rs2_dialect = src->rs2_dialect;
    dst->local_int_count = src->local_int_count;
    dst->local_string_count = src->local_string_count;
    dst->local_long_count = src->local_long_count;
    dst->int_argument_count = src->int_argument_count;
    dst->string_argument_count = src->string_argument_count;
    dst->long_argument_count = src->long_argument_count;
    dst->int_stack_depth = src->int_stack_depth;
    dst->str_stack_depth = src->str_stack_depth;
    dst->op_count = src->op_count;
    dst->switch_table_count = src->switch_table_count;
    memcpy(dst->switch_tables, src->switch_tables, sizeof(dst->switch_tables));

    if( src->signature )
    {
        dst->signature = strdup(src->signature);
        assert(dst->signature);
    }

    dst->opcodes = malloc((size_t)src->op_count * sizeof(*dst->opcodes));
    assert(dst->opcodes);
    memcpy(dst->opcodes, src->opcodes, (size_t)src->op_count * sizeof(*dst->opcodes));

    dst->int_operands = malloc((size_t)src->op_count * sizeof(*dst->int_operands));
    assert(dst->int_operands);
    memcpy(dst->int_operands, src->int_operands, (size_t)src->op_count * sizeof(*dst->int_operands));

    dst->string_operands = calloc((size_t)src->op_count, sizeof(*dst->string_operands));
    assert(dst->string_operands);
    for( i = 0; i < src->op_count; i++ )
    {
        if( !src->string_operands[i] )
            continue;
        dst->string_operands[i] = strdup(src->string_operands[i]);
        assert(dst->string_operands[i]);
    }

    return true;
}

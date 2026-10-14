#include "mem/torirs_mem.h"
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

bool
CS2VM2_ScriptAllocSwitches(
    struct CS2VM2_Script* script,
    int count)
{
    assert(script);
    assert(!script->switch_tables);

    script->switch_table_count = 0;
    if( count <= 0 )
        return true;

    script->switch_tables = ToriMem_Calloc(ToriMem_Script(), (size_t)count, sizeof(*script->switch_tables));
    assert(script->switch_tables);
    script->switch_table_count = count;
    return true;
}

bool
CS2VM2_ScriptAllocSwitchCases(
    struct CS2VM2_Script* script,
    int table_index,
    int case_count)
{
    struct CS2VM2_ScriptSwitch* table;

    assert(script);
    assert(table_index >= 0 && table_index < script->switch_table_count);

    table = &script->switch_tables[table_index];
    table->case_count = 0;
    if( case_count <= 0 )
        return true;

    table->cases = ToriMem_Calloc(ToriMem_Script(), (size_t)case_count, sizeof(*table->cases));
    assert(table->cases);
    table->case_count = case_count;
    return true;
}

void
CS2VM2_ScriptFree(struct CS2VM2_Script* script)
{
    if( !script )
        return;

    if( script->switch_tables )
    {
        for( int i = 0; i < script->switch_table_count; i++ )
            ToriMem_Free(ToriMem_Script(), script->switch_tables[i].cases);
        ToriMem_Free(ToriMem_Script(), script->switch_tables);
    }
    ToriMem_Free(ToriMem_Script(), script->signature);
    ToriMem_Free(ToriMem_Script(), script->opcodes);
    ToriMem_Free(ToriMem_Script(), script->int_operands);
    if( script->string_operands )
    {
        for( int i = 0; i < script->op_count; i++ )
            ToriMem_Free(ToriMem_Script(), script->string_operands[i]);
        ToriMem_Free(ToriMem_Script(), script->string_operands);
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
    if( !CS2VM2_ScriptAllocSwitches(dst, src->switch_table_count) )
        return false;
    for( i = 0; i < src->switch_table_count; i++ )
    {
        if( !CS2VM2_ScriptAllocSwitchCases(dst, i, src->switch_tables[i].case_count) )
            return false;
        memcpy(
            dst->switch_tables[i].cases,
            src->switch_tables[i].cases,
            (size_t)src->switch_tables[i].case_count * sizeof(*dst->switch_tables[i].cases));
    }

    if( src->signature )
    {
        dst->signature = ToriMem_Strdup(ToriMem_Script(), src->signature);
        assert(dst->signature);
    }

    dst->opcodes = ToriMem_Alloc(ToriMem_Script(), (size_t)src->op_count * sizeof(*dst->opcodes));
    assert(dst->opcodes);
    memcpy(dst->opcodes, src->opcodes, (size_t)src->op_count * sizeof(*dst->opcodes));

    dst->int_operands = ToriMem_Alloc(ToriMem_Script(), (size_t)src->op_count * sizeof(*dst->int_operands));
    assert(dst->int_operands);
    memcpy(dst->int_operands, src->int_operands, (size_t)src->op_count * sizeof(*dst->int_operands));

    dst->string_operands = ToriMem_Calloc(ToriMem_Script(), (size_t)src->op_count, sizeof(*dst->string_operands));
    assert(dst->string_operands);
    for( i = 0; i < src->op_count; i++ )
    {
        if( !src->string_operands[i] )
            continue;
        dst->string_operands[i] = ToriMem_Strdup(ToriMem_Script(), src->string_operands[i]);
        assert(dst->string_operands[i]);
    }

    return true;
}
